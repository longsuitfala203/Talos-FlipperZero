#include "../talos_i.h"

void talos_scene_report_on_enter(void* context) {
    TalosApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    const TlsGrade* g = &app->grade;
    const KeyCapture* c = &app->capture;

    FuriString* s = furi_string_alloc();

    furi_string_cat_printf(s, "\e#%s\n", g->name);
    if(g->scored) {
        furi_string_cat_printf(
            s, "Grade %s   %d/100   %s\n", g->letter, g->score, tls_band_label(g->band));
    } else {
        furi_string_cat_printf(s, "Not graded   %s\n", tls_band_label(g->band));
    }
    furi_string_cat_printf(s, "%s\n\n", g->headline);
    furi_string_cat_printf(s, "%s\n\n", tls_band_blurb(g->band));

    /* --- findings --- */
    furi_string_cat_str(s, "\e#Findings\n");
    for(uint8_t i = 0; i < g->finding_num; i++) {
        furi_string_cat_printf(
            s, "%s %s\n", tls_severity_glyph(g->findings[i].sev), g->findings[i].text);
    }

    /* --- how the number was reached --- *
     * Shown in full because a grade nobody can check is just an opinion. */
    if(g->scored) {
        furi_string_cat_str(s, "\n\e#Score\n");
        furi_string_cat_printf(s, "Authentication  %u/45\n", (unsigned)g->parts.auth);
        furi_string_cat_printf(s, "Integrity       %u/15\n", (unsigned)g->parts.integrity);
        furi_string_cat_printf(s, "Copy cost       %u/25\n", (unsigned)g->parts.obscurity);
        furi_string_cat_printf(s, "Key space       %u/15\n", (unsigned)g->parts.keyspace);
        furi_string_cat_printf(s, "Total           %d/100\n", g->score);
        if(g->parts.auth == 0) {
            furi_string_cat_str(
                s,
                "\nA key that answers READ ROM and\n"
                "nothing else scores none of the\n"
                "45 points for authentication.\n");
        }
    }

    /* --- the part itself --- */
    furi_string_cat_str(s, "\n\e#This key\n");
    furi_string_cat_printf(s, "Wire: %s\n", tls_proto_label(c->reading.proto));
    if(c->fw_name[0] != '\0') furi_string_cat_printf(s, "Protocol: %s\n", c->fw_name);
    if(c->manufacturer[0] != '\0') furi_string_cat_printf(s, "Vendor: %s\n", c->manufacturer);
    furi_string_cat_printf(s, "Kind: %s\n", tls_class_label(g->cls));
    furi_string_cat_printf(s, "Part: %s\n", g->what);

    if(c->reading.proto == TlsProtoDallas && c->reading.data_len >= TLS_ROM_LEN) {
        furi_string_cat_printf(s, "Family code: %02X\n", g->family);
        furi_string_cat_printf(s, "%s\n", g->serial_line);
        if(c->reading.crc_ok) {
            furi_string_cat_printf(
                s, "CRC: %02X, verified\n", c->reading.data[TLS_ROM_LEN - 1]);
        } else {
            furi_string_cat_printf(
                s,
                "CRC: %02X, FAILED (want %02X)\n",
                c->reading.data[TLS_ROM_LEN - 1],
                c->reading.crc_calc);
        }
    }
    if(g->id_line[0] != '\0') furi_string_cat_printf(s, "ROM: %s\n", g->id_line);
    if(g->id_bits > 0) furi_string_cat_printf(s, "Carries: %u bits\n", (unsigned)g->id_bits);
    if(g->guess_bits > 0) {
        furi_string_cat_printf(
            s, "Left to guess on-site: %u bits\n", (unsigned)g->guess_bits);
    }
    if(!c->fw_valid && c->fw_error[0] != '\0') {
        furi_string_cat_printf(s, "Firmware says: %s\n", c->fw_error);
    }

    /* The firmware's own field breakdown for this protocol. */
    if(app->decoded_fields && !furi_string_empty(app->decoded_fields)) {
        furi_string_cat_str(s, "\n\e#Decoded fields\n");
        furi_string_cat(s, app->decoded_fields);
        furi_string_cat_str(s, "\n");
    }

    /* --- what the keyring log knows that this key does not --- */
    if(c->reading.neighbour_delta > 0) {
        furi_string_cat_str(s, "\n\e#Sequential stock\n");
        furi_string_cat_printf(
            s,
            "Nearest logged serial: %llu away\n",
            (unsigned long long)c->reading.neighbour_delta);
        furi_string_cat_str(
            s,
            "Dallas issues serials in order,\n"
            "so keys bought together sit in a\n"
            "run. One read points at the rest.\n");
    }

    /* --- attacker cost --- */
    if(g->clone != TlsCloneNotAKey && g->clone != TlsCloneUnknown) {
        furi_string_cat_str(s, "\n\e#Cost to copy\n");
        furi_string_cat_printf(s, "%s with %s\n", tls_clone_time(g->clone), tls_clone_label(g->clone));
        furi_string_cat_str(
            s,
            "Talos copies nothing. It reads,\n"
            "and tells you what a reader\n"
            "already learns for free.\n");
    }

    furi_string_cat_printf(s, "\n\e#Verdict\n%s\n", g->verdict);

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(s));
    furi_string_free(s);

    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewWidget);
}

bool talos_scene_report_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void talos_scene_report_on_exit(void* context) {
    TalosApp* app = context;
    widget_reset(app->widget);
}
