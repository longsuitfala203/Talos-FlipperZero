#include "scan_view.h"
#include <gui/elements.h>
#include <furi.h>
#include <string.h>

/* Layout, 128x64. Kept as named constants because tools_gen_mockups.py mirrors
 * these exact numbers - if a row moves here it moves in the README too. */
#define SV_HEADER_BASE 9 /* FontPrimary baseline, title row */
#define SV_RULE_Y      11 /* hairline under the header */
#define SV_CAN_CX      16 /* the iButton can, face on */
#define SV_CAN_CY      29
#define SV_SCALE_X     31 /* the 1 / 0 rail labels */
#define SV_TRACE_X0    38 /* the logic trace */
#define SV_TRACE_X1    125
#define SV_TRACE_HI    21 /* idle high rail */
#define SV_TRACE_LO    37 /* pulled-low rail */
#define SV_MARK_BASE   46 /* FontSecondary baseline, what the trace is showing */
#define SV_STAGE_BASE  55 /* FontSecondary baseline, stage line */
#define SV_HINT_BASE   63 /* FontSecondary baseline, hint line */

/* ---------------------------------------------------------- the timeline --- *
 * 1-Wire, drawn the way a logic analyser would draw it, because that is the
 * whole conversation and it is short enough to fit on the screen.
 *
 * While Talos is sensing, the worker really is doing this: pulling the line low
 * for a reset, releasing it, and finding nothing there. So the animation is not
 * decoration - an empty trough is exactly what an empty contact looks like.
 * When a key answers, the same trace gains the one edge that matters: the
 * presence pulse, the key pulling the line down to say it exists. Then the ROM
 * clocks out, least significant bit first, a short low for a 1 and a long low
 * for a 0, which is how the bus actually encodes them. */
#define SV_RESET_LEN    28 /* the long low that starts every transaction */
#define SV_IDLE_LEN     44 /* ...and the silence after it, when nothing answers */
#define SV_RECOVER_LEN  5
#define SV_PRESENCE_LEN 8 /* the key's answer */
#define SV_GAP_LEN      5
#define SV_BIT_CELLS    6 /* one bit slot */
#define SV_BIT_LOW_ONE  1 /* a 1 is a brief low */
#define SV_BIT_LOW_ZERO 4 /* a 0 is held down */
#define SV_TAIL_LEN     30

#define SV_SENSE_PERIOD (SV_RESET_LEN + SV_IDLE_LEN)
#define SV_SENSE_STEP   6 /* cells per 100 ms tick while sensing */
#define SV_READ_STEP    8 /* ...and during the closing flourish */

struct ScanView {
    View* view;
};

typedef struct {
    uint32_t phase;
    KeyStage stage;
    uint8_t seconds;
    uint8_t bits[TLS_MAX_DATA];
    uint8_t bit_bytes;
} ScanModel;

static uint32_t sv_period(const ScanModel* m) {
    if(m->stage != KeyStageDecoded) return SV_SENSE_PERIOD;
    return SV_RESET_LEN + SV_RECOVER_LEN + SV_PRESENCE_LEN + SV_GAP_LEN +
           (uint32_t)m->bit_bytes * 8u * SV_BIT_CELLS + SV_TAIL_LEN;
}

/* Is the bus low at timeline position t? */
static bool sv_bus_low(const ScanModel* m, uint32_t t) {
    if(m->stage != KeyStageDecoded) {
        return t < SV_RESET_LEN; /* a reset pulse, and nothing answering it */
    }

    if(t < SV_RESET_LEN) return true;
    t -= SV_RESET_LEN;
    if(t < SV_RECOVER_LEN) return false;
    t -= SV_RECOVER_LEN;
    if(t < SV_PRESENCE_LEN) return true; /* the key says it is there */
    t -= SV_PRESENCE_LEN;
    if(t < SV_GAP_LEN) return false;
    t -= SV_GAP_LEN;

    const uint32_t nbits = (uint32_t)m->bit_bytes * 8u;
    if(t < nbits * SV_BIT_CELLS) {
        const uint32_t i = t / SV_BIT_CELLS;
        const uint32_t off = t % SV_BIT_CELLS;
        /* 1-Wire clocks each byte out least significant bit first. */
        const bool one = (m->bits[i / 8] >> (i % 8)) & 1u;
        return off < (one ? SV_BIT_LOW_ONE : SV_BIT_LOW_ZERO);
    }
    return false;
}

/* Name whatever is under the middle of the window, so the label tracks the
 * trace instead of describing it in general terms. Kept short: the label starts
 * at SV_TRACE_X0 and the screen ends 90 px later. */
static void sv_mark(const ScanModel* m, uint32_t centre, char* out, size_t out_sz) {
    if(m->stage != KeyStageDecoded) {
        snprintf(out, out_sz, "%s", (centre < SV_RESET_LEN) ? "RESET, no answer" : "BUS IDLE");
        return;
    }

    uint32_t t = centre;
    if(t < SV_RESET_LEN + SV_RECOVER_LEN) {
        snprintf(out, out_sz, "RESET PULSE");
        return;
    }
    t -= SV_RESET_LEN + SV_RECOVER_LEN;
    if(t < SV_PRESENCE_LEN + SV_GAP_LEN) {
        snprintf(out, out_sz, "PRESENCE!");
        return;
    }
    t -= SV_PRESENCE_LEN + SV_GAP_LEN;
    if(t < (uint32_t)m->bit_bytes * 8u * SV_BIT_CELLS) {
        /* The real width of this key, not an assumed 64: Cyfral answers with 16
         * and Metakom with 32, and the trace is showing those exact bits. */
        snprintf(out, out_sz, "ROM: %u bits", (unsigned)m->bit_bytes * 8u);
        return;
    }
    snprintf(out, out_sz, "DONE");
}

static const char* sv_stage_text(KeyStage stage) {
    switch(stage) {
    case KeyStageDecoded:
        return "Key answered";
    case KeyStageSensing:
        return "Pulsing the contact";
    default:
        return "Idle";
    }
}

/* The iButton can, face on. Two concentric rings and a lid, which is genuinely
 * what an F5 package looks like from the front: the rim is ground, the disc in
 * the middle is data, and those two contacts are the entire interface. */
static void sv_draw_can(Canvas* canvas, int cx, int cy, bool answered) {
    canvas_draw_circle(canvas, cx, cy, 13);
    canvas_draw_circle(canvas, cx, cy, 10);
    if(answered) {
        canvas_draw_disc(canvas, cx, cy, 7);
    } else {
        canvas_draw_circle(canvas, cx, cy, 7);
    }
}

static void scan_view_draw(Canvas* canvas, void* model) {
    ScanModel* m = model;
    canvas_clear(canvas);

    /* --- header: what we are doing, and how long we will keep at it --- */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, SV_HEADER_BASE, "Touch a Key");
    canvas_set_font(canvas, FontSecondary);
    if(m->stage != KeyStageDecoded) {
        char left[8];
        snprintf(left, sizeof(left), "%us", (unsigned)m->seconds);
        canvas_draw_str_aligned(canvas, 126, SV_HEADER_BASE, AlignRight, AlignBottom, left);
    }
    canvas_draw_line(canvas, 0, SV_RULE_Y, 127, SV_RULE_Y);

    sv_draw_can(canvas, SV_CAN_CX, SV_CAN_CY, m->stage == KeyStageDecoded);

    /* --- the bus, as a logic trace --- */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, SV_SCALE_X, SV_TRACE_HI + 3, "1");
    canvas_draw_str(canvas, SV_SCALE_X, SV_TRACE_LO + 3, "0");

    const uint32_t period = sv_period(m);
    int prev_y = -1;
    for(int x = SV_TRACE_X0; x <= SV_TRACE_X1; x++) {
        const uint32_t t = (m->phase + (uint32_t)(x - SV_TRACE_X0)) % period;
        const int y = sv_bus_low(m, t) ? SV_TRACE_LO : SV_TRACE_HI;
        /* A vertical at every level change, so edges read as edges. */
        if(prev_y >= 0 && prev_y != y) canvas_draw_line(canvas, x, prev_y, x, y);
        canvas_draw_dot(canvas, x, y);
        prev_y = y;
    }

    const uint32_t centre = (m->phase + (uint32_t)((SV_TRACE_X1 - SV_TRACE_X0) / 2)) % period;
    char mark[20];
    sv_mark(m, centre, mark, sizeof(mark));
    canvas_draw_str(canvas, SV_TRACE_X0, SV_MARK_BASE, mark);

    /* --- stage + hint --- */
    char buf[28];
    char dots[4] = {0};
    const int nd = (m->stage == KeyStageDecoded) ? 0 : (int)((m->phase / 12) % 4);
    for(int i = 0; i < nd; i++) dots[i] = '.';
    snprintf(buf, sizeof(buf), "%s%s", sv_stage_text(m->stage), dots);
    canvas_draw_str_aligned(canvas, 64, SV_STAGE_BASE, AlignCenter, AlignBottom, buf);

    canvas_draw_str_aligned(
        canvas, 64, SV_HINT_BASE, AlignCenter, AlignBottom, "Flat face to the two pads");
}

static bool scan_view_input(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return false; // let Back reach the scene manager
}

ScanView* scan_view_alloc(void) {
    ScanView* v = malloc(sizeof(ScanView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ScanModel));
    view_set_draw_callback(v->view, scan_view_draw);
    view_set_input_callback(v->view, scan_view_input);
    return v;
}

void scan_view_free(ScanView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* scan_view_get_view(ScanView* v) {
    furi_assert(v);
    return v->view;
}

void scan_view_reset(ScanView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        ScanModel * m,
        {
            m->phase = 0;
            m->stage = KeyStageSensing;
            m->bit_bytes = 0;
            m->seconds = 0;
            memset(m->bits, 0, sizeof(m->bits));
        },
        true);
}

void scan_view_tick(ScanView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        ScanModel * m,
        {
            const uint32_t step = (m->stage == KeyStageDecoded) ? SV_READ_STEP : SV_SENSE_STEP;
            m->phase += step;
        },
        true);
}

void scan_view_set_stage(ScanView* v, KeyStage stage) {
    furi_assert(v);
    with_view_model(
        v->view,
        ScanModel * m,
        {
            /* Entering the flourish restarts the timeline, so the viewer sees
             * the reset and the presence pulse rather than joining mid-word. */
            if(m->stage != KeyStageDecoded && stage == KeyStageDecoded) m->phase = 0;
            m->stage = stage;
        },
        true);
}

void scan_view_set_countdown(ScanView* v, uint8_t seconds) {
    furi_assert(v);
    with_view_model(v->view, ScanModel * m, { m->seconds = seconds; }, true);
}

void scan_view_set_bits(ScanView* v, const uint8_t* data, uint8_t len) {
    furi_assert(v);
    if(len > TLS_MAX_DATA) len = TLS_MAX_DATA;
    with_view_model(
        v->view,
        ScanModel * m,
        {
            m->bit_bytes = len;
            if(data != NULL && len > 0) memcpy(m->bits, data, len);
        },
        true);
}
