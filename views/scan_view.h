#pragma once

#include <gui/view.h>

#include "../helpers/key_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScanView ScanView;

ScanView* scan_view_alloc(void);
void scan_view_free(ScanView* v);
View* scan_view_get_view(ScanView* v);

void scan_view_reset(ScanView* v);
void scan_view_tick(ScanView* v);
void scan_view_set_stage(ScanView* v, KeyStage stage);

/** Seconds left in the hold window, shown in the header. */
void scan_view_set_countdown(ScanView* v, uint8_t seconds);

/** The ROM just read, so the closing flourish clocks out the real bits. */
void scan_view_set_bits(ScanView* v, const uint8_t* data, uint8_t len);

#ifdef __cplusplus
}
#endif
