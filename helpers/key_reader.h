/**
 * Talos's read layer: a thin, strictly read-only wrapper around the firmware's
 * iButton worker.
 *
 * The worker drives the 1-Wire contact, issues a reset, and runs the firmware's
 * own protocol bank against whatever answers - Dallas ROMs, and the two Soviet
 * intercom formats that share the same pin. Talos uses the read path and nothing
 * else: it never calls write_id, write_copy or emulate, so it cannot program a
 * blank, cannot pretend to be your key, and leaves the part exactly as it found
 * it. The stock iButton app can do all three; that Talos deliberately does not
 * is the point of it.
 *
 * Threading: the worker calls back on its own thread. The callback only sets a
 * flag under a mutex. Every read of the key object happens later, on the GUI
 * thread, inside key_reader_take() and only after the worker has been stopped -
 * so nothing inspects the decoder's buffers while it is still filling them.
 */
#pragma once

#include <furi.h>

#include "tls_grade.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the contact is doing right now - drives the scan animation.
 *
 * There is no "key present" stage, and that is honest rather than lazy: 1-Wire
 * has no field to load. Either the part answers the reset pulse and the whole
 * ROM arrives, or nothing happens at all. The scan view animates unanswered
 * reset pulses while sensing, which is exactly what the pin is doing. */
typedef enum {
    KeyStageIdle, /* not started */
    KeyStageSensing, /* reset pulses going out, nothing answering */
    KeyStageDecoded, /* a part answered; ready to take */
} KeyStage;

/** Everything one successful read produced. */
typedef struct {
    TlsReading reading; /* pure payload, handed straight to the grader */
    char fw_name[24]; /* the firmware's own name for the protocol */
    char manufacturer[24]; /* vendor string, when the protocol supplies one */
    bool fw_valid; /* the firmware's own verdict on the data */
    char fw_error[72]; /* why not, when fw_valid is false */
} KeyCapture;

typedef struct KeyReader KeyReader;

KeyReader* key_reader_alloc(void);
void key_reader_free(KeyReader* reader);

/** Start issuing reset pulses. Safe to call when already running. */
void key_reader_start(KeyReader* reader);

/** Stop. Idempotent - scene exits call it unconditionally. */
void key_reader_stop(KeyReader* reader);

/** Current stage, for the scan view. Cheap; poll it from the tick handler. */
KeyStage key_reader_stage(KeyReader* reader);

/**
 * If a key has answered, stop the worker, extract everything and return true -
 * exactly once per read. `rendered` (optional) receives the firmware's own
 * multi-line data dump for the report.
 */
bool key_reader_take(KeyReader* reader, KeyCapture* out, FuriString* rendered);

#ifdef __cplusplus
}
#endif
