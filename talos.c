#include "talos_i.h"
#include <string.h>

/* ---------------------------------------------------------- feedback ----- *
 * Four outcomes, four distinct signals, so the verdict lands before you look at
 * the screen: a flat falling two-tone for a credential this device could simply
 * be, a single blip for one that needs a blank, a rising pair for a key that
 * actually authenticates, and a neutral chirp for anything Talos will not
 * grade. */

static const NotificationSequence seq_replayable = {
    &message_red_255,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_50,
    &message_note_gs4,
    &message_delay_100,
    &message_note_ds4,
    &message_delay_100,
    &message_sound_off,
    &message_red_0,
    NULL,
};

static const NotificationSequence seq_cloneable = {
    &message_red_255,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_sound_off,
    &message_red_0,
    NULL,
};

static const NotificationSequence seq_gated = {
    &message_red_255,
    &message_green_255, // red + green = amber
    &message_delay_250,
    &message_red_0,
    &message_green_0,
    &message_note_c5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_challenged = {
    &message_green_255,
    &message_delay_100,
    &message_note_c5,
    &message_delay_50,
    &message_note_e5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_sound_off,
    &message_green_0,
    NULL,
};

static const NotificationSequence seq_neutral = {
    &message_blue_255,
    &message_delay_100,
    &message_blue_0,
    &message_note_c5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

void talos_notify_graded(TalosApp* app, const TlsGrade* grade) {
    furi_assert(app);
    furi_assert(grade);

    const NotificationSequence* seq;
    switch(grade->band) {
    case TlsBandReplayable:
        seq = &seq_replayable;
        break;
    case TlsBandCloneable:
        seq = &seq_cloneable;
        break;
    case TlsBandGated:
        seq = &seq_gated;
        break;
    case TlsBandChallenged:
        seq = &seq_challenged;
        break;
    default:
        seq = &seq_neutral;
        break;
    }

    /* The sequences mix LED, vibro and tone; if the user turned all three off,
     * stay silent rather than firing a half-muted one. */
    if(app->settings.led || app->settings.sound || app->settings.vibro) {
        notification_message(app->notifications, seq);
    }
}

/* ------------------------------------------------ view dispatcher glue ---- */
static bool talos_custom_event_callback(void* context, uint32_t event) {
    TalosApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool talos_back_event_callback(void* context) {
    TalosApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void talos_tick_event_callback(void* context) {
    TalosApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* --------------------------------------------------------- lifecycle ----- */
static TalosApp* talos_app_alloc(void) {
    TalosApp* app = malloc(sizeof(TalosApp));
    memset(app, 0, sizeof(TalosApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&talos_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, talos_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, talos_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, talos_tick_event_callback, 100);

    app->settings.hold = TlsHold20s;
    app->settings.sound = true;
    app->settings.vibro = true;
    app->settings.led = true;
    app->settings.logging = true;
    app->settings.neighbours = true;
    tls_store_settings_load(&app->settings);

    app->reader = key_reader_alloc();
    app->decoded_fields = furi_string_alloc();

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TalosViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TalosViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, TalosViewWidget, widget_get_view(app->widget));

    app->scan_view = scan_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TalosViewScan, scan_view_get_view(app->scan_view));

    app->result_view = result_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TalosViewResult, result_view_get_view(app->result_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void talos_app_free(TalosApp* app) {
    furi_assert(app);

    key_reader_stop(app->reader);

    view_dispatcher_remove_view(app->view_dispatcher, TalosViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, TalosViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, TalosViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, TalosViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, TalosViewResult);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    scan_view_free(app->scan_view);
    result_view_free(app->result_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    key_reader_free(app->reader);
    furi_string_free(app->decoded_fields);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t talos_app(void* p) {
    UNUSED(p);
    TalosApp* app = talos_app_alloc();
    scene_manager_next_scene(app->scene_manager, TalosSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    talos_app_free(app);
    return 0;
}
