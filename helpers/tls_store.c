#include "tls_store.h"

#include <furi_hal.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>
#include <datetime/datetime.h>
#include <stdio.h>
#include <stdlib.h>

#define TLS_SETTINGS_PATH    APP_DATA_PATH("settings.bin")
#define TLS_LOG_PATH         APP_DATA_PATH("keyring.csv")
#define TLS_SETTINGS_MAGIC   0x7A
#define TLS_SETTINGS_VERSION 1

/* How many entries we hold in memory while scanning the file. The log itself
 * may grow without limit; only the newest slice is ever rendered, and the
 * neighbour search streams the whole file without buffering it. */
#define TLS_LOG_WINDOW 20

/* One CSV row, as written and as parsed. Kept in one place so the two format
 * strings below cannot drift apart. */
#define TLS_CSV_WRITE "%04u-%02u-%02u,%02u:%02u,%s,%s,%u,%d,%s,%s,%s\n"
/* The name field's width matches TlsLogged::name exactly. They have to: a wider
 * scan buffer copied into the narrower struct is a truncation the compiler
 * rightly refuses to build. */
#define TLS_CSV_READ  "%u-%u-%u,%u:%u,%3[^,],%11[^,],%u,%d,%15[^,],%16[^,],%27[^\r\n]"
#define TLS_CSV_FIELDS 12

typedef struct {
    uint8_t month, day, hour, minute;
    char letter[4];
    char band[12];
    int16_t score;
    uint8_t scored;
    char name[28];
    char rom[18];
} TlsLogged;

static void tls_store_ensure_dir(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);
}

/* ------------------------------------------------------------ hold times --- */

static const uint32_t hold_ticks[TlsHoldCount] = {100, 200, 600}; /* 100 ms ticks */
static const char* const hold_labels[TlsHoldCount] = {"10 s", "20 s", "60 s"};

uint32_t tls_hold_ticks(uint8_t hold) {
    if(hold >= TlsHoldCount) hold = TlsHold20s;
    return hold_ticks[hold];
}

const char* tls_hold_label(uint8_t hold) {
    if(hold >= TlsHoldCount) hold = TlsHold20s;
    return hold_labels[hold];
}

/* ----------------------------------------------------------- settings ----- */

void tls_store_settings_save(const TalosSettings* s) {
    furi_assert(s);
    tls_store_ensure_dir();
    saved_struct_save(
        TLS_SETTINGS_PATH, s, sizeof(TalosSettings), TLS_SETTINGS_MAGIC, TLS_SETTINGS_VERSION);
}

void tls_store_settings_load(TalosSettings* s) {
    furi_assert(s);
    TalosSettings loaded;
    if(!saved_struct_load(
           TLS_SETTINGS_PATH,
           &loaded,
           sizeof(TalosSettings),
           TLS_SETTINGS_MAGIC,
           TLS_SETTINGS_VERSION)) {
        return; /* nothing valid on disk - the caller keeps its defaults */
    }
    /* Never let a file on the SD card index an array. */
    if(loaded.hold >= TlsHoldCount) loaded.hold = TlsHold20s;
    *s = loaded;
}

/* -------------------------------------------------------------- log ------- */

/* Commas and newlines would split a CSV field in two; the name comes from the
 * grader's tables, but the file is plain text a user may well edit, so it is
 * sanitised on the way out rather than trusted on the way back in. */
static void csv_safe(char* out, size_t out_sz, const char* in) {
    size_t n = 0;
    for(; in[n] != '\0' && n + 1 < out_sz; n++) {
        const char c = in[n];
        out[n] = (c == ',' || c == '\n' || c == '\r') ? ' ' : c;
    }
    out[n] = '\0';
}

/* The ROM as unbroken hex, so the neighbour search can parse one field. */
static void rom_hex(char* out, size_t out_sz, const uint8_t* data, uint8_t len) {
    if(out_sz == 0) return;
    out[0] = '\0';
    for(uint8_t i = 0; i < len; i++) {
        char cell[3];
        snprintf(cell, sizeof(cell), "%02X", data[i]);
        const size_t used = strlen(out);
        if(used + 2 + 1 > out_sz) break;
        out[used] = cell[0];
        out[used + 1] = cell[1];
        out[used + 2] = '\0';
    }
    if(out[0] == '\0') snprintf(out, out_sz, "-");
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* A 16-character hex field back into 8 ROM bytes. False on anything malformed,
 * which is how a hand-edited line gets skipped instead of misread. */
static bool rom_parse(const char* hex, uint8_t* out) {
    for(uint8_t i = 0; i < TLS_ROM_LEN; i++) {
        const int hi = hex_nibble(hex[i * 2]);
        const int lo = hex_nibble(hex[i * 2 + 1]);
        if(hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return hex[TLS_ROM_LEN * 2] == '\0';
}

/* Bytes 1..6, least significant first, as one 48-bit number. */
static uint64_t rom_serial(const uint8_t* rom) {
    uint64_t sn = 0;
    for(int i = 6; i >= 1; i--) {
        sn = (sn << 8) | rom[i];
    }
    return sn;
}

bool tls_store_log_append(const TlsGrade* grade, const TlsReading* reading) {
    furi_assert(grade);
    furi_assert(reading);
    tls_store_ensure_dir();

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);

    char name[28];
    char rom[18];
    csv_safe(name, sizeof(name), grade->name);
    rom_hex(rom, sizeof(rom), reading->data, reading->data_len);

    char line[192];
    int n = snprintf(
        line,
        sizeof(line),
        TLS_CSV_WRITE,
        (unsigned)dt.year,
        (unsigned)dt.month,
        (unsigned)dt.day,
        (unsigned)dt.hour,
        (unsigned)dt.minute,
        grade->letter,
        tls_band_label(grade->band),
        grade->scored ? 1u : 0u,
        grade->score,
        tls_proto_label(reading->proto),
        rom,
        name);
    if(n <= 0) return false;
    if((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, TLS_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        ok = storage_file_write(file, line, (size_t)n) == (size_t)n;
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

uint8_t tls_store_log_render(FuriString* out, uint8_t max) {
    furi_assert(out);
    if(max == 0) return 0;
    if(max > TLS_LOG_WINDOW) max = TLS_LOG_WINDOW;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    /* On the heap: twenty entries is ~1.4 KB, and this runs on a 4 KB app stack
     * that the widget and FuriString are already drawing from. */
    TlsLogged* ring = malloc(sizeof(TlsLogged) * TLS_LOG_WINDOW);
    uint8_t count = 0;
    uint8_t head = 0;

    if(file_stream_open(stream, TLS_LOG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FuriString* line = furi_string_alloc();
        while(stream_read_line(stream, line)) {
            TlsLogged e;
            unsigned year, month, day, hour, minute, scored;
            int score;
            char letter[4] = {0};
            char band[12] = {0};
            char proto[16] = {0};
            char rom[17] = {0};
            char name[28] = {0};

            /* Anything that does not parse cleanly is skipped rather than shown
             * as garbage. */
            if(sscanf(
                   furi_string_get_cstr(line),
                   TLS_CSV_READ,
                   &year,
                   &month,
                   &day,
                   &hour,
                   &minute,
                   letter,
                   band,
                   &scored,
                   &score,
                   proto,
                   rom,
                   name) != TLS_CSV_FIELDS) {
                continue;
            }

            e.month = (uint8_t)month;
            e.day = (uint8_t)day;
            e.hour = (uint8_t)hour;
            e.minute = (uint8_t)minute;
            e.score = (int16_t)score;
            e.scored = (uint8_t)(scored ? 1 : 0);
            snprintf(e.letter, sizeof(e.letter), "%s", letter);
            snprintf(e.band, sizeof(e.band), "%s", band);
            snprintf(e.rom, sizeof(e.rom), "%s", rom);
            snprintf(e.name, sizeof(e.name), "%s", name);

            ring[head] = e;
            head = (uint8_t)((head + 1) % max);
            if(count < max) count++;
        }
        furi_string_free(line);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);

    /* Newest first: walk the ring backwards from the most recent write. */
    for(uint8_t i = 0; i < count; i++) {
        const uint8_t idx = (uint8_t)((head + max - 1 - i) % max);
        const TlsLogged* e = &ring[idx];
        if(e->scored) {
            furi_string_cat_printf(
                out, "\e#%s  %d/100  %s\e#\n", e->letter, (int)e->score, e->band);
        } else {
            furi_string_cat_printf(out, "\e#--  %s\e#\n", e->band);
        }
        furi_string_cat_printf(
            out,
            "%s\n%s\n%02u-%02u %02u:%02u\n\n",
            e->name,
            e->rom,
            (unsigned)e->day,
            (unsigned)e->month,
            (unsigned)e->hour,
            (unsigned)e->minute);
    }

    free(ring);
    return count;
}

bool tls_store_log_clear(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    /* storage_simply_remove() reports success when the file was never there, so
     * ask first - otherwise "Cleared" would be claimed over an empty log. */
    const bool existed = storage_common_exists(storage, TLS_LOG_PATH);
    const bool removed = storage_simply_remove(storage, TLS_LOG_PATH);
    furi_record_close(RECORD_STORAGE);
    return existed && removed;
}

const char* tls_store_log_path(void) {
    return TLS_LOG_PATH;
}

bool tls_store_nearest_delta(const uint8_t* rom, uint8_t rom_len, uint64_t* out_delta) {
    furi_assert(rom);
    furi_assert(out_delta);
    if(rom_len < TLS_ROM_LEN) return false;

    const uint8_t family = rom[0];
    const uint64_t mine = rom_serial(rom);

    uint64_t best = 0;
    bool found = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);

    if(file_stream_open(stream, TLS_LOG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FuriString* line = furi_string_alloc();
        while(stream_read_line(stream, line)) {
            unsigned year, month, day, hour, minute, scored;
            int score;
            char letter[4] = {0};
            char band[12] = {0};
            char proto[16] = {0};
            char hex[17] = {0};
            char name[28] = {0};

            if(sscanf(
                   furi_string_get_cstr(line),
                   TLS_CSV_READ,
                   &year,
                   &month,
                   &day,
                   &hour,
                   &minute,
                   letter,
                   band,
                   &scored,
                   &score,
                   proto,
                   hex,
                   name) != TLS_CSV_FIELDS) {
                continue;
            }

            uint8_t other[TLS_ROM_LEN];
            if(!rom_parse(hex, other)) continue; /* Cyfral, Metakom, or hand-edited */
            if(other[0] != family) continue; /* different stock entirely */

            const uint64_t theirs = rom_serial(other);
            if(theirs == mine) continue; /* the same key, read again */

            const uint64_t delta = (theirs > mine) ? (theirs - mine) : (mine - theirs);
            if(!found || delta < best) {
                best = delta;
                found = true;
            }
        }
        furi_string_free(line);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);

    if(found) *out_delta = best;
    return found;
}
