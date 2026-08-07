#include "key_reader.h"

#include <lib/ibutton/ibutton_worker.h>
#include <lib/ibutton/ibutton_protocols.h>
#include <lib/ibutton/ibutton_key.h>
#include <one_wire/maxim_crc.h>
#include <string.h>

struct KeyReader {
    iButtonProtocols* protocols;
    iButtonWorker* worker;
    iButtonKey* key;
    FuriMutex* mutex;

    bool running;
    bool have_read; /* set by the worker thread, cleared by take() */
    KeyStage stage;
};

static void lock(KeyReader* r) {
    furi_mutex_acquire(r->mutex, FuriWaitForever);
}
static void unlock(KeyReader* r) {
    furi_mutex_release(r->mutex);
}

/* Runs on the worker's thread. Records that something answered and nothing
 * else - every inspection of the key happens on the GUI thread in take(). */
static void key_reader_read_cb(void* context) {
    KeyReader* r = context;
    lock(r);
    r->have_read = true;
    r->stage = KeyStageDecoded;
    unlock(r);
}

/* ---------------------------------------------------------- classification -- *
 * Which of Talos's three wire protocols answered.
 *
 * By name first, because the firmware's protocol *ids* are positions in its own
 * table and shift whenever a protocol is added, while the names are stable
 * strings. By payload length second, as a fallback: 2 and 4 bytes are the only
 * sizes the two intercom formats use, so a rename upstream degrades to a
 * correct guess rather than a failed read. Everything else with a full 8-byte
 * ROM is Dallas, and from there the family code in byte 0 - assigned by the
 * silicon vendor - decides what the part actually is. */
static TlsProto key_reader_classify(const char* name, uint8_t data_len) {
    if(name != NULL) {
        if(strstr(name, "Cyfral") != NULL) return TlsProtoCyfral;
        if(strstr(name, "Metakom") != NULL) return TlsProtoMetakom;
    }
    if(data_len >= TLS_ROM_LEN) return TlsProtoDallas;
    if(data_len == 2) return TlsProtoCyfral;
    if(data_len == 4) return TlsProtoMetakom;
    return TlsProtoUnread;
}

/* Pull hex byte pairs out of a rendered string.
 *
 * Only used when the protocol hands back no editable data - the firmware's
 * render_uid() output is then the sole route to the bytes, and a key that reads
 * but shows no ROM would be a worse failure than a tolerant parse. */
static uint8_t hex_scan(const char* src, uint8_t* out, uint8_t max) {
    uint8_t n = 0;
    int hi = -1;

    for(const char* p = src; *p != '\0' && n < max; p++) {
        int v;
        if(*p >= '0' && *p <= '9') {
            v = *p - '0';
        } else if(*p >= 'A' && *p <= 'F') {
            v = *p - 'A' + 10;
        } else if(*p >= 'a' && *p <= 'f') {
            v = *p - 'a' + 10;
        } else {
            hi = -1; /* a separator ends the current pair */
            continue;
        }

        if(hi < 0) {
            hi = v;
        } else {
            out[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    return n;
}

/* ------------------------------------------------------------- lifecycle --- */

KeyReader* key_reader_alloc(void) {
    KeyReader* r = malloc(sizeof(KeyReader));
    memset(r, 0, sizeof(KeyReader));

    r->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    r->protocols = ibutton_protocols_alloc();
    r->key = ibutton_key_alloc(ibutton_protocols_get_max_data_size(r->protocols));
    r->worker = ibutton_worker_alloc(r->protocols);
    ibutton_worker_read_set_callback(r->worker, key_reader_read_cb, r);
    ibutton_worker_start_thread(r->worker);
    r->stage = KeyStageIdle;
    return r;
}

void key_reader_free(KeyReader* r) {
    furi_assert(r);

    key_reader_stop(r);
    ibutton_worker_stop_thread(r->worker);
    ibutton_worker_free(r->worker);
    ibutton_key_free(r->key);
    ibutton_protocols_free(r->protocols);
    furi_mutex_free(r->mutex);
    free(r);
}

void key_reader_start(KeyReader* r) {
    furi_assert(r);
    if(r->running) return;

    ibutton_key_reset(r->key);
    lock(r);
    r->have_read = false;
    r->stage = KeyStageSensing;
    unlock(r);

    ibutton_worker_read_start(r->worker, r->key);
    r->running = true;
}

void key_reader_stop(KeyReader* r) {
    furi_assert(r);
    if(!r->running) return;

    ibutton_worker_stop(r->worker);
    r->running = false;

    lock(r);
    if(r->stage != KeyStageDecoded) r->stage = KeyStageIdle;
    unlock(r);
}

KeyStage key_reader_stage(KeyReader* r) {
    furi_assert(r);
    lock(r);
    const KeyStage s = r->stage;
    unlock(r);
    return s;
}

bool key_reader_take(KeyReader* r, KeyCapture* out, FuriString* rendered) {
    furi_assert(r);
    furi_assert(out);

    lock(r);
    const bool ready = r->have_read;
    if(ready) r->have_read = false; /* fires once per read */
    unlock(r);

    if(!ready) return false;

    /* Drop the bus before touching anything the worker was writing. */
    ibutton_worker_stop(r->worker);
    r->running = false;

    memset(out, 0, sizeof(*out));

    const iButtonProtocolId id = ibutton_key_get_protocol_id(r->key);
    const char* name = ibutton_protocols_get_name(r->protocols, id);
    const char* vendor = ibutton_protocols_get_manufacturer(r->protocols, id);
    if(name) snprintf(out->fw_name, sizeof(out->fw_name), "%s", name);
    if(vendor) snprintf(out->manufacturer, sizeof(out->manufacturer), "%s", vendor);

    out->fw_valid = ibutton_protocols_is_valid(r->protocols, r->key);

    /* The bytes. The editable view is the protocol's own ROM buffer, which is
     * what every Dallas part exposes for editing; the hex fallback below covers
     * anything that declines to offer one. */
    iButtonEditableData editable = {.ptr = NULL, .size = 0};
    ibutton_protocols_get_editable_data(r->protocols, r->key, &editable);

    uint8_t len = 0;
    if(editable.ptr != NULL && editable.size > 0) {
        len = (editable.size > TLS_MAX_DATA) ? TLS_MAX_DATA : (uint8_t)editable.size;
        memcpy(out->reading.data, editable.ptr, len);
    }

    FuriString* uid = furi_string_alloc();
    ibutton_protocols_render_uid(r->protocols, r->key, uid);
    if(len == 0) {
        len = hex_scan(furi_string_get_cstr(uid), out->reading.data, TLS_MAX_DATA);
    }
    furi_string_free(uid);

    out->reading.data_len = len;
    out->reading.proto = key_reader_classify(out->fw_name, len);

    /* The ROM's own checksum, computed here rather than trusted: a key whose
     * CRC does not match has either been misread or been programmed onto a
     * blank by a copier that did not bother, and both are worth saying out
     * loud. */
    if(out->reading.proto == TlsProtoDallas && len >= TLS_ROM_LEN) {
        out->reading.crc_calc = maxim_crc8(out->reading.data, TLS_ROM_LEN - 1, MAXIM_CRC8_INIT);
        out->reading.crc_ok = (out->reading.crc_calc == out->reading.data[TLS_ROM_LEN - 1]);
    } else {
        /* Cyfral and Metakom carry no CRC field; there is nothing to fail. */
        out->reading.crc_ok = true;
    }

    if(!out->fw_valid) {
        FuriString* err = furi_string_alloc();
        ibutton_protocols_render_error(r->protocols, r->key, err);
        snprintf(out->fw_error, sizeof(out->fw_error), "%s", furi_string_get_cstr(err));
        furi_string_free(err);
    }

    if(rendered != NULL) {
        furi_string_reset(rendered);
        ibutton_protocols_render_data(r->protocols, r->key, rendered);
    }

    return true;
}
