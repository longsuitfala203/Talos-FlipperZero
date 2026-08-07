#include "result_view.h"
#include <gui/elements.h>
#include <furi.h>
#include <string.h>

/* Layout, 128x64. tools_gen_mockups.py mirrors these constants, so the README
 * screenshots stay honest when a row moves. */
#define RV_NAME_BASE  9 /* FontPrimary baseline, part name */
#define RV_RULE_Y     11
#define RV_BAND_Y     13 /* inverted band bar */
#define RV_BAND_H     12
#define RV_SCORE_BASE 44 /* FontBigNumbers baseline */
#define RV_INFO_X     54 /* right-hand column: what a copy costs */
#define RV_INFO_B1    33
#define RV_INFO_B2    43
/* The whole secret, on one line. A boxed plate needs 2 px of frame plus 7 px of
 * glyph plus padding, and the rows above and below leave ten - so the frame goes
 * and a hairline does its job instead, mirroring the rule under the header. That
 * buys the row a clear pixel above the footer, which a frame did not have. */
#define RV_ROM_RULE   45 /* hairline separating the score row from the ROM */
#define RV_ROM_TOP    46 /* top of the cell ink */
#define RV_ROM_BASE   53 /* FontSecondary baseline, also the cell's bottom */
#define RV_ROM_X      4
#define RV_ROM_W      120
#define RV_FOOT_Y     55
#define RV_FOOT_BASE  62

struct ResultView {
    View* view;
    ResultViewCallback cb;
    void* ctx;
};

typedef struct {
    TlsGrade grade;
    uint8_t data[TLS_MAX_DATA];
    uint8_t data_len;
    bool has;
} ResultModel;

/* Truncate a copy of `src` so it fits within `max_w` px in the current font.
 *
 * Shortens in place: each pass moves the ".." one character left, and since it
 * only ever writes at or after the cut, the surviving prefix is never touched.
 * Working inside `out` (rather than a scratch buffer) keeps the result bounded
 * by the caller's buffer no matter how long the source is. */
static void fit_text(Canvas* canvas, const char* src, int max_w, char* out, size_t out_sz) {
    if(out_sz == 0) return;
    snprintf(out, out_sz, "%s", src);
    if(canvas_string_width(canvas, out) <= max_w) return;
    if(out_sz < 4) {
        out[0] = '\0';
        return;
    }

    size_t keep = strlen(out);
    if(keep > out_sz - 3) keep = out_sz - 3;
    while(keep > 0) {
        keep--;
        out[keep] = '.';
        out[keep + 1] = '.';
        out[keep + 2] = '\0';
        if(canvas_string_width(canvas, out) <= max_w) return;
    }
}

/**
 * The ROM, stamped across one line.
 *
 * This is the app's whole argument in one graphic: a Dallas key's entire secret
 * is eight bytes, and eight bytes fit on a 128-pixel screen with room to spare.
 * The family byte is fenced off on the left because it says what the part is
 * rather than which one, and the checksum is fenced off on the right and
 * inverted when it verifies - struck through when it does not, which is how a
 * carelessly written blank announces itself.
 */
static void rv_draw_rom(Canvas* canvas, const ResultModel* m) {
    canvas_draw_line(canvas, 0, RV_ROM_RULE, 127, RV_ROM_RULE);

    const uint8_t len = m->data_len;
    if(len == 0) {
        /* Nothing read: a dashed rail, so the row never looks like a ROM of all
         * zeroes. */
        for(int x = RV_ROM_X; x < RV_ROM_X + RV_ROM_W; x += 4) {
            canvas_draw_line(canvas, x, RV_ROM_TOP + 3, x + 1, RV_ROM_TOP + 3);
        }
        return;
    }

    const int cell = RV_ROM_W / len;
    const bool dallas = (len >= TLS_ROM_LEN);

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < len; i++) {
        const int x0 = RV_ROM_X + i * cell;
        const bool is_crc = dallas && (i == len - 1);

        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", m->data[i]);

        /* Gated on `suspect` alone, not on `scored`: a thermometer's checksum is
         * as genuinely verified as a door key's, and saying so is the point of
         * the mark. `suspect` is already set whenever the CRC failed. */
        if(is_crc && !m->grade.suspect) {
            /* Verified: stamped into the row. */
            canvas_draw_box(canvas, x0, RV_ROM_TOP, cell, RV_ROM_BASE - RV_ROM_TOP + 1);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str_aligned(
                canvas, x0 + cell / 2, RV_ROM_BASE, AlignCenter, AlignBottom, hex);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str_aligned(
                canvas, x0 + cell / 2, RV_ROM_BASE, AlignCenter, AlignBottom, hex);
            if(is_crc) {
                /* Failed, or a ROM that does not look like factory silicon. */
                canvas_draw_line(canvas, x0 + 1, RV_ROM_BASE, x0 + cell - 2, RV_ROM_TOP);
            }
        }
    }

    /* Fences: the family code says what the part is rather than which one, and
     * the checksum is not part of the identity either. What sits between them is
     * the whole of the secret. */
    if(dallas) {
        canvas_draw_line(
            canvas, RV_ROM_X + cell, RV_ROM_TOP, RV_ROM_X + cell, RV_ROM_BASE);
        const int crc_x = RV_ROM_X + (len - 1) * cell;
        canvas_draw_line(canvas, crc_x, RV_ROM_TOP, crc_x, RV_ROM_BASE);
    }
}

static void result_view_draw(Canvas* canvas, void* model) {
    ResultModel* m = model;
    canvas_clear(canvas);
    if(!m->has) return;
    const TlsGrade* g = &m->grade;

    /* --- what it is --- */
    canvas_set_font(canvas, FontPrimary);
    char name[36];
    fit_text(canvas, g->name, 124, name, sizeof(name));
    canvas_draw_str(canvas, 2, RV_NAME_BASE, name);
    canvas_draw_line(canvas, 0, RV_RULE_Y, 127, RV_RULE_Y);

    /* --- band bar: the letter on the left, the verdict word across it --- */
    canvas_draw_rbox(canvas, 0, RV_BAND_Y, 128, RV_BAND_H, 2);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, RV_BAND_Y + 10, g->letter);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 70, RV_BAND_Y + 6, AlignCenter, AlignCenter, tls_band_label(g->band));
    /* A ROM that does not look like factory silicon earns a mark of its own, so
     * the caveat travels with the grade instead of hiding in the report. */
    if(g->suspect) canvas_draw_str(canvas, 121, RV_BAND_Y + 10, "!");
    canvas_set_color(canvas, ColorBlack);

    /* --- the score --- */
    canvas_set_font(canvas, FontBigNumbers);
    if(g->scored) {
        char sc[8];
        snprintf(sc, sizeof(sc), "%d", g->score);
        canvas_draw_str(canvas, 3, RV_SCORE_BASE, sc);
        const int nw = canvas_string_width(canvas, sc);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 3 + nw + 2, RV_SCORE_BASE - 1, "/100");
    } else {
        /* Sensors and failed reads get no number: inventing one would be the
         * only dishonest thing on this screen. */
        canvas_draw_str(canvas, 3, RV_SCORE_BASE, "--");
    }

    /* --- what a copy costs --- */
    canvas_set_font(canvas, FontSecondary);
    char line[24];
    if(g->clone == TlsCloneUnknown) {
        /* Nothing read: a clone cost here would be meaningless, so point at the
         * report, which carries the placement advice. */
        canvas_draw_str(canvas, RV_INFO_X, RV_INFO_B1, "Nothing read");
        canvas_draw_str(canvas, RV_INFO_X, RV_INFO_B2, "OK for help");
    } else if(g->clone == TlsCloneNotAKey) {
        canvas_draw_str(canvas, RV_INFO_X, RV_INFO_B1, "Not a key");
        fit_text(canvas, g->what, 126 - RV_INFO_X, line, sizeof(line));
        canvas_draw_str(canvas, RV_INFO_X, RV_INFO_B2, line);
    } else {
        snprintf(line, sizeof(line), "COPY   %s", tls_clone_time(g->clone));
        canvas_draw_str(canvas, RV_INFO_X, RV_INFO_B1, line);
        fit_text(canvas, tls_clone_short(g->clone), 126 - RV_INFO_X, line, sizeof(line));
        canvas_draw_str(canvas, RV_INFO_X, RV_INFO_B2, line);
    }

    rv_draw_rom(canvas, m);

    /* --- footer --- */
    canvas_draw_box(canvas, 0, RV_FOOT_Y, 128, 64 - RV_FOOT_Y);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, RV_FOOT_BASE, "OK Report");
    canvas_draw_str_aligned(canvas, 125, RV_FOOT_BASE, AlignRight, AlignBottom, "Rescan >");
    canvas_set_color(canvas, ColorBlack);
}

static bool result_view_input(InputEvent* event, void* context) {
    ResultView* v = context;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyOk) {
        if(v->cb) v->cb(v->ctx, ResultEventReport);
        return true;
    }
    if(event->key == InputKeyRight) {
        if(v->cb) v->cb(v->ctx, ResultEventRescan);
        return true;
    }
    return false; // Back falls through to navigation
}

ResultView* result_view_alloc(void) {
    ResultView* v = malloc(sizeof(ResultView));
    v->cb = NULL;
    v->ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ResultModel));
    view_set_draw_callback(v->view, result_view_draw);
    view_set_input_callback(v->view, result_view_input);
    return v;
}

void result_view_free(ResultView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* result_view_get_view(ResultView* v) {
    furi_assert(v);
    return v->view;
}

void result_view_set_callback(ResultView* v, ResultViewCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void result_view_set_result(ResultView* v, const TlsGrade* grade, const TlsReading* reading) {
    furi_assert(v);
    furi_assert(grade);
    with_view_model(
        v->view,
        ResultModel * m,
        {
            m->grade = *grade;
            m->data_len = 0;
            if(reading) {
                m->data_len = (reading->data_len > TLS_MAX_DATA) ? TLS_MAX_DATA :
                                                                   reading->data_len;
                memcpy(m->data, reading->data, m->data_len);
            }
            m->has = true;
        },
        true);
}
