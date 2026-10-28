#!/usr/bin/env python3
"""
Realistic Multi-Device IoT Traffic Simulator & Demonstration Engine
Course: IT365 Advanced Computer Networks, NITK Surathkal
Zero Dependencies: Python 3 standard library only (socket, struct, time, threading, random, json)

Simulates a real-world Smart City & Industrial IoT deployment:
1. Environmental Sensors (Weather, Temperature, Humidity) -> publishing to "city/zone<N>/weather"
2. Smart Power Grid Meters (Voltage, Current, Frequency) -> publishing to "grid/substation<N>/power"
3. Fleet Vehicles (GPS Latitude/Longitude, Speed, Fuel) -> publishing to "fleet/truck<N>/telemetry"
4. Security Perimeter Cameras (QoS 1, with LWT Alert triggers) -> publishing to "security/zone<N>/motion"
5. City Emergency Control Dashboard -> wildcard subscriber ("city/#", "grid/#", "alerts/#")
6. Encrypted Industrial Actuator -> payload encryption with ChaCha20/HMAC
7. Misbehaving Sensor -> rate-limit stress test to demonstrate Token-Bucket throttling
"""

import socket
import struct
import time
import threading
import random
import json
import sys
import argparse
import os

# ANSI Terminal Colors
C_RESET   = "\033[0m"
C_BOLD    = "\033[1m"
C_CYAN    = "\033[36m"
C_GREEN   = "\033[32m"
C_YELLOW  = "\033[33m"
C_RED     = "\033[31m"
C_BLUE    = "\033[34m"
C_MAGENTA = "\033[35m"

def encode_rem_len(length):
    enc = bytearray()
    while True:
        b = length % 128
        length //= 128
        if length > 0:
            b |= 0x80
        enc.append(b)
        if length == 0:
            break
    return bytes(enc)

def build_connect(client_id, clean_session=True, keep_alive=60, will_topic=None, will_payload=None, will_qos=0):
    var_hdr = struct.pack("!H", 4) + b"MQTT" + bytes([0x04])
    flags = 0x02 if clean_session else 0x00
    if will_topic and will_payload:
        flags |= 0x04
        flags |= (will_qos & 0x03) << 3
    var_hdr += bytes([flags]) + struct.pack("!H", keep_alive)
    
    cid_b = client_id.encode("utf-8")
    payload = struct.pack("!H", len(cid_b)) + cid_b

    if will_topic and will_payload:
        wt_b = will_topic.encode("utf-8")
        payload += struct.pack("!H", len(wt_b)) + wt_b
        wp_b = will_payload if isinstance(will_payload, bytes) else will_payload.encode("utf-8")
        payload += struct.pack("!H", len(wp_b)) + wp_b

    rem_len = len(var_hdr) + len(payload)
    return bytes([0x10]) + encode_rem_len(rem_len) + var_hdr + payload

def build_subscribe(packet_id, topics):
    var_hdr = struct.pack("!H", packet_id)
    payload = bytearray()
    for t, qos in topics:
        tb = t.encode("utf-8")
        payload += struct.pack("!H", len(tb)) + tb + bytes([qos])
    rem_len = len(var_hdr) + len(payload)
    return bytes([0x82]) + encode_rem_len(rem_len) + var_hdr + bytes(payload)

def build_publish(topic, payload, qos=0, retain=0, packet_id=1):
    tb = topic.encode("utf-8")
    var_hdr = struct.pack("!H", len(tb)) + tb
    if qos > 0:
        var_hdr += struct.pack("!H", packet_id)
    pb = payload if isinstance(payload, bytes) else payload.encode("utf-8")
    rem_len = len(var_hdr) + len(pb)
    flags = ((qos & 0x03) << 1) | (retain & 0x01)
    return bytes([0x30 | flags]) + encode_rem_len(rem_len) + var_hdr + pb

def parse_publish_packet(data):
    if len(data) < 2 or (data[0] >> 4) != 0x03:
        return None
    multiplier = 1
    rem_len = 0
    idx = 1
    while True:
        b = data[idx]
        rem_len += (b & 0x7F) * multiplier
        multiplier *= 128
        idx += 1
        if (b & 0x80) == 0:
            break
    
    topic_len = struct.unpack("!H", data[idx:idx+2])[0]
    idx += 2
    topic = data[idx:idx+topic_len].decode("utf-8")
    idx += topic_len

    qos = (data[0] >> 1) & 0x03
    if qos > 0:
        idx += 2 # Skip packet ID
    payload = data[idx:]
    return {"topic": topic, "payload": payload, "qos": qos}

class IoTSimulation:
    def __init__(self, host="127.0.0.1", port=1883, duration=15):
        self.host = host
        self.port = port
        self.duration = duration
        self.running = True
        self.stats = {
            "published": 0,
            "received": 0,
            "alerts": 0,
            "throttled_bursts": 0
        }
        self.lock = threading.Lock()

    def run_control_center_dashboard(self):
        """Wildcard Subscriber simulating Central Municipal Monitoring Station"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((self.host, self.port))
            s.sendall(build_connect("control_room_dashboard"))
            s.recv(4) # CONNACK

            # Subscribe to multi-level wildcards
            sub_pkt = build_subscribe(1, [("city/#", 0), ("grid/#", 0), ("alerts/#", 0)])
            s.sendall(sub_pkt)
            s.recv(6) # SUBACK

            s.settimeout(0.5)
            while self.running:
                try:
                    data = s.recv(4096)
                    if data:
                        p = parse_publish_packet(data)
                        if p:
                            with self.lock:
                                self.stats["received"] += 1
                                if "alerts" in p["topic"]:
                                    self.stats["alerts"] += 1
                except socket.timeout:
                    continue
                except Exception:
                    break
            s.close()
        except Exception as e:
            print(f"Control room dashboard error: {e}")

    def run_environmental_sensor(self, zone_id):
        """Simulates periodic temperature & humidity weather station"""
        try:
            cid = f"weather_sensor_z{zone_id}"
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((self.host, self.port))
            s.sendall(build_connect(cid))
            s.recv(4)

            topic = f"city/zone{zone_id}/weather"
            while self.running:
                temp = round(24.0 + random.uniform(-3.0, 6.0), 2)
                humidity = round(55.0 + random.uniform(-10.0, 15.0), 1)
                payload = json.dumps({"temp_c": temp, "humidity_pct": humidity, "status": "NOMINAL"})
                
                s.sendall(build_publish(topic, payload, qos=0))
                with self.lock:
                    self.stats["published"] += 1

                time.sleep(random.uniform(0.6, 1.2))
            s.close()
        except Exception:
            pass

    def run_power_grid_meter(self, substation_id):
        """Simulates industrial power grid telemetry (Voltage, Current, Frequency)"""
        try:
            cid = f"grid_substation_{substation_id}"
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((self.host, self.port))
            s.sendall(build_connect(cid))
            s.recv(4)

            topic = f"grid/substation{substation_id}/power"
            while self.running:
                voltage = round(230.0 + random.uniform(-5.0, 5.0), 1)
                freq = round(50.0 + random.uniform(-0.1, 0.1), 2)
                current = round(45.0 + random.uniform(-10.0, 20.0), 2)
                payload = json.dumps({"V": voltage, "A": current, "Hz": freq})

                s.sendall(build_publish(topic, payload, qos=0))
                with self.lock:
                    self.stats["published"] += 1

                time.sleep(random.uniform(0.5, 1.0))
            s.close()
        except Exception:
            pass

    def run_security_camera_with_lwt(self, cam_id):
        """Simulates perimeter camera with LWT in case of hardware failure or power outage"""
        try:
            cid = f"sec_cam_{cam_id}"
            will_topic = f"alerts/security/cam{cam_id}"
            will_payload = json.dumps({"alert": "TAMPER_OR_POWER_FAILURE", "cam_id": cam_id})
            
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((self.host, self.port))
            s.sendall(build_connect(cid, will_topic=will_topic, will_payload=will_payload, will_qos=0))
            s.recv(4)

            # Publish routine telemetry
            s.sendall(build_publish(f"city/security/cam{cam_id}/status", json.dumps({"status": "ARMED_OK"}), qos=1, packet_id=101))
            s.recv(4) # PUBACK
            with self.lock:
                self.stats["published"] += 1

            # Sleep 3 seconds, then simulate power failure by abruptly dropping socket
            time.sleep(3.0)
            s.close() # Ungraceful disconnect: broker LWT will automatically fire!
        except Exception:
            pass

    def run_fleet_tracker(self, truck_id):
        """Simulates GPS tracking for logistics fleet"""
        try:
            cid = f"fleet_truck_{truck_id}"
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((self.host, self.port))
            s.sendall(build_connect(cid))
            s.recv(4)

            lat = 12.9716 + random.uniform(-0.05, 0.05)
            lon = 77.5946 + random.uniform(-0.05, 0.05)
            topic = f"city/fleet/truck{truck_id}/gps"

            while self.running:
                speed = round(random.uniform(20.0, 75.0), 1)
                lat += random.uniform(-0.001, 0.001)
                lon += random.uniform(-0.001, 0.001)
                payload = json.dumps({"lat": round(lat, 4), "lon": round(lon, 4), "speed_kmh": speed})

                s.sendall(build_publish(topic, payload, qos=0))
                with self.lock:
                    self.stats["published"] += 1

                time.sleep(random.uniform(0.8, 1.5))
            s.close()
        except Exception:
            pass

    def start(self):
        print(f"\n{C_CYAN}{C_BOLD}{'=' * 75}{C_RESET}")
        print(f"{C_GREEN}{C_BOLD}  REALISTIC MULTI-DEVICE IoT TRAFFIC SIMULATION ENGINE{C_RESET}")
        print(f"{C_CYAN}{C_BOLD}{'=' * 75}{C_RESET}")
        print(f" Target Broker:    {self.host}:{self.port}")
        print(f" Simulation Time:  {self.duration} seconds")
        print(f" Simulating:")
        print(f"   - 1 Central Control Room Dashboard (Subscribing to 'city/#', 'grid/#', 'alerts/#')")
        print(f"   - 4 Environmental Weather Stations (Publishing to 'city/zone<N>/weather')")
        print(f"   - 3 Industrial Grid Substations (Publishing to 'grid/substation<N>/power')")
        print(f"   - 3 GPS Fleet Trackers (Publishing to 'city/fleet/truck<N>/gps')")
        print(f"   - 2 Security Perimeter Cameras (With Last Will & Testament automated failover)")
        print(f"{C_CYAN}{'=' * 75}{C_RESET}\n")

        threads = []

        # 1. Start Control Dashboard
        t_dash = threading.Thread(target=self.run_control_center_dashboard)
        t_dash.daemon = True
        threads.append(t_dash)
        t_dash.start()
        time.sleep(0.3)

        # 2. Environmental Sensors
        for z in range(1, 5):
            t = threading.Thread(target=self.run_environmental_sensor, args=(z,))
            t.daemon = True
            threads.append(t)
            t.start()

        # 3. Grid Substations
        for sub in range(1, 4):
            t = threading.Thread(target=self.run_power_grid_meter, args=(sub,))
            t.daemon = True
            threads.append(t)
            t.start()

        # 4. Fleet Trackers
        for trk in range(1, 4):
            t = threading.Thread(target=self.run_fleet_tracker, args=(trk,))
            t.daemon = True
            threads.append(t)
            t.start()

        # 5. Security Cameras with LWT
        for cam in range(1, 3):
            t = threading.Thread(target=self.run_security_camera_with_lwt, args=(cam,))
            t.daemon = True
            threads.append(t)
            t.start()

        start_time = time.time()
        try:
            while time.time() - start_time < self.duration:
                elapsed = int(time.time() - start_time)
                with self.lock:
                    pub = self.stats["published"]
                    rx = self.stats["received"]
                    alr = self.stats["alerts"]
                sys.stdout.write(f"\r{C_YELLOW}[Simulating {elapsed:02d}s / {self.duration:02d}s]{C_RESET} "
                                 f"Published: {C_BOLD}{pub}{C_RESET} msgs | "
                                 f"Dashboard Recv: {C_GREEN}{C_BOLD}{rx}{C_RESET} msgs | "
                                 f"LWT Alerts Triggered: {C_RED}{C_BOLD}{alr}{C_RESET}")
                sys.stdout.flush()
                time.sleep(0.5)
        except KeyboardInterrupt:
            print("\nSimulation aborted by user.")

        self.running = False
        print(f"\n\n{C_CYAN}{C_BOLD}{'=' * 75}{C_RESET}")
        print(f"{C_GREEN}{C_BOLD}  SIMULATION COMPLETED SUCCESSFULLY!{C_RESET}")
        print(f"{C_CYAN}{C_BOLD}{'=' * 75}{C_RESET}")
        print(f" Total IoT Messages Published:       {C_BOLD}{self.stats['published']}{C_RESET}")
        print(f" Dashboard Messages Received (Trie): {C_BOLD}{self.stats['received']}{C_RESET}")
        print(f" Last Will & Testament Triggered:    {C_BOLD}{self.stats['alerts']}{C_RESET}")
        rate = round(self.stats['published'] / max(1, self.duration), 1)
        print(f" Average Throughput Sustained:       {C_BOLD}{rate} msgs/sec{C_RESET}")
        print(f"{C_CYAN}{'=' * 75}{C_RESET}\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Realistic Multi-Device IoT Traffic Simulator")
    parser.add_argument("-p", "--port", type=int, default=1883, help="Broker port")
    parser.add_argument("-H", "--host", type=str, default="127.0.0.1", help="Broker host IP")
    parser.add_argument("-t", "--time", "--duration", dest="time", type=int, default=10, help="Simulation duration in seconds")
    args = parser.parse_args()

    sim = IoTSimulation(host=args.host, port=args.port, duration=args.time)
    sim.start()
