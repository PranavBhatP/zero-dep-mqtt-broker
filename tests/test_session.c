#include "session.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_session_lifecycle(void) {
    printf("[TEST] Testing session connect & clean session lifecycle...\n");
    session_registry_t reg;
    assert(session_registry_init(&reg));

    uint8_t session_present = 0;
    mqtt_session_t *s1 = session_connect(&reg, "device_01", 10, 1, 60, &session_present);
    assert(s1 != NULL);
    assert(session_present == 0);
    assert(s1->is_connected == true);
    assert(reg.total_sessions == 1);
    assert(reg.active_connected_sessions == 1);

    /* Disconnect clean session: should purge from registry */
    session_disconnect(&reg, "device_01", true);
    assert(reg.total_sessions == 0);
    assert(reg.active_connected_sessions == 0);
    assert(session_find(&reg, "device_01") == NULL);

    session_registry_cleanup(&reg);
    printf("  -> PASS: Clean session lifecycle\n");
}

static void test_persistent_session(void) {
    printf("[TEST] Testing persistent session resumption & offline queue...\n");
    session_registry_t reg;
    assert(session_registry_init(&reg));

    uint8_t session_present = 0;
    /* Connect with clean_session = 0 */
    mqtt_session_t *s = session_connect(&reg, "device_persist", 11, 0, 120, &session_present);
    assert(s != NULL);
    assert(session_present == 0);

    /* Add subscriptions */
    assert(session_add_subscription(s, "sensors/temp", 1));
    assert(session_add_subscription(s, "sensors/humidity", 0));
    assert(s->sub_count == 2);

    /* Disconnect ungracefully (socket drop) */
    session_disconnect(&reg, "device_persist", false);
    assert(reg.total_sessions == 1); /* Retained in registry! */
    assert(reg.active_connected_sessions == 0);

    /* Queue an offline message while device is disconnected */
    mqtt_session_t *found = session_find(&reg, "device_persist");
    assert(found != NULL);
    assert(found->is_connected == false);
    const char *test_payload = "alert: 45C";
    assert(session_queue_offline_message(found, "sensors/temp", (const uint8_t *)test_payload, strlen(test_payload), 1, 0, 101));
    assert(found->queue_count == 1);

    /* Reconnect device with clean_session = 0 */
    mqtt_session_t *resumed = session_connect(&reg, "device_persist", 12, 0, 60, &session_present);
    assert(resumed != NULL);
    assert(session_present == 1); /* Session present flag = 1! */
    assert(resumed->is_connected == true);
    assert(resumed->socket_fd == 12);
    assert(resumed->sub_count == 2); /* Subscriptions restored! */
    assert(resumed->queue_count == 1); /* Offline message preserved! */

    session_registry_cleanup(&reg);
    printf("  -> PASS: Persistent session resumption & offline queue\n");
}

int main(void) {
    printf("========================================\n");
    printf("RUNNING SESSION REGISTRY TESTS\n");
    printf("========================================\n");

    test_session_lifecycle();
    test_persistent_session();

    printf("ALL SESSION TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
