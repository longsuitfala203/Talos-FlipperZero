#include "../talos_i.h"

/* The last 20 graded keys, newest first. Auditing a site means walking it with a
 * pocketful of fobs, and nobody remembers the sixth one. The log is also what
 * makes the sequential-serial check possible at all. */
#define TLS_LOG_SHOWN 20

void talos_scene_keyring_on_enter(void* context) {
    TalosApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    FuriString* s = furi_string_alloc();
    furi_string_cat_str(s, "\e#Keyring Log\n");

    const uint8_t shown = tls_store_log_render(s, TLS_LOG_SHOWN);
    if(shown == 0) {
        furi_string_cat_str(
            s,
            "\nNothing logged yet.\n\n"
            "Grade a key with logging on and\n"
            "it lands here, newest first.\n\n"
            "Two keys from one site is where\n"
            "this gets interesting: Talos\n"
            "compares serials and tells you\n"
            "when they were issued in a run.\n\n"
            "The full history is a CSV on the\n"
            "SD card, so a site survey can be\n"
            "opened in a spreadsheet:\n");
        furi_string_cat_printf(s, "%s\n", tls_store_log_path());
    } else {
        furi_string_cat_printf(s, "\e#Showing %u\e#\n", (unsigned)shown);
        furi_string_cat_printf(s, "Full history: %s\n", tls_store_log_path());
    }

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(s));
    furi_string_free(s);

    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewWidget);
}

bool talos_scene_keyring_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void talos_scene_keyring_on_exit(void* context) {
    TalosApp* app = context;
    widget_reset(app->widget);
}
