#include "../talos_i.h"
#include <string.h>

/* Ticks are 100 ms. A grader that sits on "Pulsing the contact..." forever
 * leaves the user guessing whether the key is wrong, the placement is wrong, or
 * the app is broken - so when the hold window runs out Talos answers with the
 * one verdict it can honestly give: nothing answered, and here is why that
 * happens. The window is a setting, because seating a fob one-handed takes
 * longer than touching a bare can to the pads. */

/* The moment after a key answers: the trace clocks out the ROM it just read,
 * with the presence pulse in it, before the result screen appears. Long enough
 * to see the edge that mattered, short enough not to be in the way. */
#define TLS_FLOURISH_TICKS 8u

void talos_scene_scan_on_enter(void* context) {
    TalosApp* app = context;

    scene_manager_set_scene_state(app->scene_manager, TalosSceneScan, 0);
    app->flourish = 0;
    scan_view_reset(app->scan_view);
    scan_view_set_countdown(
        app->scan_view, (uint8_t)(tls_hold_ticks(app->settings.hold) / 10u));
    key_reader_start(app->reader);
    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewScan);
}

bool talos_scene_scan_on_event(void* context, SceneManagerEvent event) {
    TalosApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == TalosCustomEventKeyRead) {
            scene_manager_next_scene(app->scene_manager, TalosSceneResult);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        scan_view_tick(app->scan_view);

        if(app->flourish > 0) {
            /* Already graded; just letting the trace finish its sentence. */
            app->flourish--;
            if(app->flourish == 0) {
                view_dispatcher_send_custom_event(
                    app->view_dispatcher, TalosCustomEventKeyRead);
            }
            return true;
        }

        if(key_reader_take(app->reader, &app->capture, app->decoded_fields)) {
            /* The keyring lookup runs here, on the GUI thread, once the bus is
             * down - and before this key is appended, so it can never be found
             * as its own neighbour. */
            app->capture.reading.neighbour_delta = 0;
            if(app->settings.neighbours) {
                uint64_t delta = 0;
                if(tls_store_nearest_delta(
                       app->capture.reading.data, app->capture.reading.data_len, &delta)) {
                    app->capture.reading.neighbour_delta = delta;
                }
            }

            tls_grade_evaluate(&app->capture.reading, &app->grade);
            /* Only real reads reach the log; a timeout is not a key. */
            if(app->settings.logging) {
                tls_store_log_append(&app->grade, &app->capture.reading);
            }
            talos_notify_graded(app, &app->grade);

            scan_view_set_bits(
                app->scan_view, app->capture.reading.data, app->capture.reading.data_len);
            scan_view_set_stage(app->scan_view, KeyStageDecoded);
            app->flourish = TLS_FLOURISH_TICKS;
        } else {
            const uint32_t limit = tls_hold_ticks(app->settings.hold);
            uint32_t ticks = scene_manager_get_scene_state(app->scene_manager, TalosSceneScan) + 1;
            scene_manager_set_scene_state(app->scene_manager, TalosSceneScan, ticks);

            scan_view_set_stage(app->scan_view, key_reader_stage(app->reader));
            const uint32_t left = (ticks >= limit) ? 0 : (limit - ticks);
            scan_view_set_countdown(app->scan_view, (uint8_t)((left + 9u) / 10u));

            if(ticks >= limit) {
                key_reader_stop(app->reader);
                memset(&app->capture, 0, sizeof(app->capture));
                app->capture.reading.proto = TlsProtoUnread;
                furi_string_reset(app->decoded_fields);
                tls_grade_evaluate(&app->capture.reading, &app->grade);
                talos_notify_graded(app, &app->grade);
                view_dispatcher_send_custom_event(app->view_dispatcher, TalosCustomEventKeyRead);
            }
        }
        consumed = true;
    }
    return consumed;
}

void talos_scene_scan_on_exit(void* context) {
    TalosApp* app = context;
    app->flourish = 0;
    key_reader_stop(app->reader);
}
