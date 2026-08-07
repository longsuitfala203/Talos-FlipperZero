#include "../talos_i.h"

typedef enum {
    SettingsIndexHold,
    SettingsIndexNeighbours,
    SettingsIndexLogging,
    SettingsIndexSound,
    SettingsIndexVibro,
    SettingsIndexLed,
    SettingsIndexClearLog,
} SettingsIndex;

static const char* const on_off[] = {"OFF", "ON"};

static void settings_hold_cb(VariableItem* item) {
    TalosApp* app = variable_item_get_context(item);
    const uint8_t v = variable_item_get_current_value_index(item);
    app->settings.hold = v;
    variable_item_set_current_value_text(item, tls_hold_label(v));
}
static void settings_neighbours_cb(VariableItem* item) {
    TalosApp* app = variable_item_get_context(item);
    const uint8_t v = variable_item_get_current_value_index(item);
    app->settings.neighbours = v;
    variable_item_set_current_value_text(item, on_off[v]);
}
static void settings_logging_cb(VariableItem* item) {
    TalosApp* app = variable_item_get_context(item);
    const uint8_t v = variable_item_get_current_value_index(item);
    app->settings.logging = v;
    variable_item_set_current_value_text(item, on_off[v]);
}
static void settings_sound_cb(VariableItem* item) {
    TalosApp* app = variable_item_get_context(item);
    const uint8_t v = variable_item_get_current_value_index(item);
    app->settings.sound = v;
    variable_item_set_current_value_text(item, on_off[v]);
}
static void settings_vibro_cb(VariableItem* item) {
    TalosApp* app = variable_item_get_context(item);
    const uint8_t v = variable_item_get_current_value_index(item);
    app->settings.vibro = v;
    variable_item_set_current_value_text(item, on_off[v]);
}
static void settings_led_cb(VariableItem* item) {
    TalosApp* app = variable_item_get_context(item);
    const uint8_t v = variable_item_get_current_value_index(item);
    app->settings.led = v;
    variable_item_set_current_value_text(item, on_off[v]);
}

/* OK on "Clear keyring" wipes the CSV and reports back in the item's value
 * column, so the action confirms itself without a modal. */
static void settings_enter_cb(void* context, uint32_t index) {
    TalosApp* app = context;
    if(index != SettingsIndexClearLog) return;

    const bool removed = tls_store_log_clear();
    if(app->clear_log_item) {
        variable_item_set_current_value_text(app->clear_log_item, removed ? "Done" : "Empty");
    }
    notification_message(
        app->notifications, removed ? &sequence_success : &sequence_blink_blue_100);
}

void talos_scene_settings_on_enter(void* context) {
    TalosApp* app = context;
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);

    VariableItem* item;

    /* Seating a fob one-handed takes longer than touching a bare can to the
     * pads, so how long Talos keeps pulsing is the user's call. */
    item = variable_item_list_add(list, "Hold window", TlsHoldCount, settings_hold_cb, app);
    variable_item_set_current_value_index(item, app->settings.hold);
    variable_item_set_current_value_text(item, tls_hold_label(app->settings.hold));

    item = variable_item_list_add(list, "Serial check", 2, settings_neighbours_cb, app);
    variable_item_set_current_value_index(item, app->settings.neighbours);
    variable_item_set_current_value_text(item, on_off[app->settings.neighbours]);

    item = variable_item_list_add(list, "Log keys", 2, settings_logging_cb, app);
    variable_item_set_current_value_index(item, app->settings.logging);
    variable_item_set_current_value_text(item, on_off[app->settings.logging]);

    item = variable_item_list_add(list, "Sound", 2, settings_sound_cb, app);
    variable_item_set_current_value_index(item, app->settings.sound);
    variable_item_set_current_value_text(item, on_off[app->settings.sound]);

    item = variable_item_list_add(list, "Vibro", 2, settings_vibro_cb, app);
    variable_item_set_current_value_index(item, app->settings.vibro);
    variable_item_set_current_value_text(item, on_off[app->settings.vibro]);

    item = variable_item_list_add(list, "LED", 2, settings_led_cb, app);
    variable_item_set_current_value_index(item, app->settings.led);
    variable_item_set_current_value_text(item, on_off[app->settings.led]);

    item = variable_item_list_add(list, "Clear keyring", 0, NULL, app);
    variable_item_set_current_value_text(item, "OK");
    app->clear_log_item = item;

    variable_item_list_set_enter_callback(list, settings_enter_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewSettings);
}

bool talos_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void talos_scene_settings_on_exit(void* context) {
    TalosApp* app = context;
    variable_item_list_reset(app->var_item_list);
    app->clear_log_item = NULL; /* reset() freed the row it pointed at */
    tls_store_settings_save(&app->settings);
}
