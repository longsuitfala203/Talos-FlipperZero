#include "../talos_i.h"

void talos_scene_about_on_enter(void* context) {
    TalosApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    FuriString* s = furi_string_alloc();
    furi_string_cat_str(s, "\e#Talos " TALOS_VERSION "\n");
    furi_string_cat_str(s, "iButton / Dallas key grader\n\n");
    furi_string_cat_str(
        s,
        "Touch a 1-Wire key to the two\n"
        "pads at the top-left of the\n"
        "Flipper. Talos reads the ROM and\n"
        "hands back a plain-English\n"
        "security grade.\n\n");

    furi_string_cat_str(s, "\e#The name\n");
    furi_string_cat_str(
        s,
        "Talos was the bronze guardian of\n"
        "Crete: unkillable, tireless - and\n"
        "held together by a single vein\n"
        "closed with a single nail. Pull\n"
        "the nail and the giant falls.\n\n"
        "A Dallas key is a single number.\n\n");

    furi_string_cat_str(s, "\e#Almost everything is an F\n");
    furi_string_cat_str(
        s,
        "That is the finding, not a bug.\n"
        "A key answers READ ROM with 64\n"
        "bits - family code, serial, CRC -\n"
        "and that is the entire exchange.\n"
        "No challenge, no nonce, nothing\n"
        "held back. Touch it once and you\n"
        "hold everything the lock will\n"
        "ever ask for.\n\n"
        "Authentication is worth 45 of the\n"
        "100 points, so no part sold as a\n"
        "door key can pass.\n\n");

    furi_string_cat_str(s, "\e#What does pass\n");
    furi_string_cat_str(
        s,
        "Three parts on this bus carry a\n"
        "real engine: DS1961S and DS1963S\n"
        "answer a challenge with a SHA-1\n"
        "MAC, and the DS1957 Java button\n"
        "is a smartcard in a can. They\n"
        "grade B and A.\n\n"
        "The catch: Talos reads the ROM,\n"
        "so it cannot see whether your\n"
        "lock ever issues a challenge.\n"
        "Most installations fitted with\n"
        "these compare the serial anyway.\n"
        "If yours does, the key is an F\n"
        "like all the others.\n\n");

    furi_string_cat_str(s, "\e#The four terms\n");
    furi_string_cat_str(
        s,
        "Auth      0-45  proves a secret?\n"
        "Integrity 0-15  forgery caught?\n"
        "Copy cost 0-25  more than a blank?\n"
        "Key space 0-15  left to guess?\n\n");

    furi_string_cat_str(s, "\e#The bands\n");
    furi_string_cat_str(
        s,
        "REPLAYABLE  this device can be it\n"
        "CLONEABLE   one touch, one blank\n"
        "GATED       a password, at least\n"
        "CHALLENGED  proves a real secret\n"
        "NOT A KEY   a sensor or a switch\n\n");

    furi_string_cat_str(s, "\e#Sequential serials\n");
    furi_string_cat_str(
        s,
        "The trick no single key shows.\n"
        "Dallas issues serials in order,\n"
        "so a site that bought a strip of\n"
        "keys holds a contiguous run. With\n"
        "logging on, Talos compares each\n"
        "key against the ones already on\n"
        "file - and when two sit a few\n"
        "numbers apart, the 48-bit field\n"
        "collapses to a handful of\n"
        "guesses. Grade two keys from the\n"
        "same door and watch the score\n"
        "drop.\n\n");

    furi_string_cat_str(s, "\e#Not a key\n");
    furi_string_cat_str(
        s,
        "1-Wire carries thermometers,\n"
        "battery gauges, switches and\n"
        "clocks on the same two wires.\n"
        "Talos names them and refuses to\n"
        "score them: grading a DS18B20\n"
        "against door criteria would be a\n"
        "lie. Their ROMs are just as\n"
        "readable, though, and it says so.\n\n");

    furi_string_cat_str(s, "\e#Read-only, always\n");
    furi_string_cat_str(
        s,
        "Talos uses the firmware's read\n"
        "path and nothing else. It never\n"
        "writes a blank, never emulates\n"
        "your key, and leaves the part\n"
        "exactly as it found it.\n\n"
        "The stock iButton app on this\n"
        "same device can do all three.\n"
        "That is the attack, and it is\n"
        "why the grade is what it is.\n\n");

    furi_string_cat_str(s, "\e#Ethics\n");
    furi_string_cat_str(
        s,
        "Grade keys you own or are\n"
        "authorised to test. Know your own\n"
        "doors before someone else does.\n\n");

    furi_string_cat_str(s, "\e#Family\n");
    furi_string_cat_str(
        s,
        "Warden grades 13.56 MHz cards.\n"
        "Bastion grades 125 kHz badges.\n"
        "Talos grades the contact.\n"
        "Same scale on all three.\n\n");

    furi_string_cat_printf(s, "Log: %s\n\n", tls_store_log_path());
    furi_string_cat_str(s, "by at0m-b0mb\n");
    furi_string_cat_str(s, "github.com/at0m-b0mb/\nTalos-FlipperZero\n");

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(s));
    furi_string_free(s);

    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewWidget);
}

bool talos_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void talos_scene_about_on_exit(void* context) {
    TalosApp* app = context;
    widget_reset(app->widget);
}
