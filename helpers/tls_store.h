/**
 * Talos - persistence. Three jobs, all under the app's own data directory on the
 * SD card:
 *
 *   - Settings survive a reboot. saved_struct gives magic + version + checksum,
 *     so a stale or corrupt file falls back to defaults instead of loading
 *     garbage into an array index.
 *   - Graded keys are appended to a CSV, because auditing a site means walking
 *     it with a pocketful of fobs and nobody remembers the sixth one.
 *   - That same log answers the question no single key can: are this site's
 *     serials sequential? Dallas issues them in order, so a site that bought a
 *     strip of keys holds a contiguous run - and reading one tells an attacker
 *     roughly where the others are. tls_store_nearest_delta() finds the closest
 *     serial already on file, and the grader turns that distance into a penalty.
 */
#pragma once

#include <furi.h>

#include "key_reader.h"
#include "tls_grade.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How long to keep pulsing before delivering an honest UNREAD. */
typedef enum {
    TlsHold10s = 0,
    TlsHold20s,
    TlsHold60s,
    TlsHoldCount,
} TlsHold;

typedef struct {
    uint8_t hold; /* TlsHold */
    bool sound;
    bool vibro;
    bool led;
    bool logging;
    bool neighbours; /* compare each serial against the keyring log */
} TalosSettings;

/** Ticks (at 100 ms) before the scan scene gives up. */
uint32_t tls_hold_ticks(uint8_t hold);
const char* tls_hold_label(uint8_t hold);

/** Settings. load() leaves *s untouched when there is nothing valid to read. */
void tls_store_settings_save(const TalosSettings* s);
void tls_store_settings_load(TalosSettings* s);

/** Append one graded key to the log. False if the write failed. */
bool tls_store_log_append(const TlsGrade* grade, const TlsReading* reading);

/**
 * Render the newest entries into `out` as widget markup, newest first.
 * Returns how many were rendered (0 = the log is empty).
 */
uint8_t tls_store_log_render(FuriString* out, uint8_t max);

/** Delete the log. False if there was nothing to delete. */
bool tls_store_log_clear(void);

/** Where the log lives, so the About screen can tell the user. */
const char* tls_store_log_path(void);

/**
 * Distance from `rom` to the closest Dallas serial already logged for the same
 * family code, or false when there is no other key on file.
 *
 * An exact match is ignored on purpose: re-reading the same key is not evidence
 * of sequential stock, and penalising it would punish the user for checking
 * their work twice.
 */
bool tls_store_nearest_delta(const uint8_t* rom, uint8_t rom_len, uint64_t* out_delta);

#ifdef __cplusplus
}
#endif
