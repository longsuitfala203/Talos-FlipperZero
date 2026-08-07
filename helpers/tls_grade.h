/**
 * Talos's brain: turn a 1-Wire ROM into a security grade.
 *
 * Deliberately free of every Flipper header. The grade is the whole product, so
 * it is compiled for the host and pinned by tests (see test/), and that only
 * works if this file depends on nothing but the C standard library.
 *
 * The thesis, stated up front so the scores make sense:
 *
 * A Dallas key answers READ ROM with 64 bits - one family code, six serial
 * bytes, one CRC - and that is the entire conversation. There is no challenge,
 * no nonce, no secret withheld. Whoever touches the key once holds everything
 * the lock will ever ask for. So the authentication term, worth 45 of the 100
 * points, is zero for every part sold as a door key, and no such key can pass.
 *
 * What varies - and what Talos measures - is how much *worse* than that a given
 * part is (Cyfral answers with sixteen bits and no checksum at all), and which
 * 1-Wire silicon does better (DS1961S and DS1963S carry a real SHA-1 engine, so
 * they can prove a secret they never send). Those exist. Almost nobody deploys
 * them. That gap is the finding.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS_MAX_FINDINGS 8u
#define TLS_MAX_DATA     8u /* a 1-Wire ROM is 8 bytes; nothing here is longer */
#define TLS_ROM_LEN      8u

/**
 * Which wire protocol answered. Talos does not mirror the firmware's protocol
 * enum: those ids are ordered by the firmware's own table and shift when a
 * protocol is added. The reader classifies by name instead, and everything
 * below the Dallas branch is decided by the family code in the ROM, which is
 * assigned by the silicon vendor and cannot drift.
 */
typedef enum {
    TlsProtoDallas = 0, /* 8-byte 1-Wire ROM: family + serial + CRC */
    TlsProtoCyfral, /* 2-byte intercom key, no checksum */
    TlsProtoMetakom, /* 4-byte intercom key */
    TlsProtoUnread, /* nothing answered */
    TlsProtoCount,
} TlsProto;

/** What kind of part this family code belongs to. */
typedef enum {
    TlsClassCredential, /* sold and fitted as a door key */
    TlsClassMemory, /* a memory part; locks still key off its ROM */
    TlsClassCrypto, /* carries an engine that can prove a secret */
    TlsClassSensor, /* thermometer, logger, A/D, battery gauge */
    TlsClassControl, /* switch, coupler, clock, counter */
    TlsClassInvalid, /* 0x00 or 0xFF: no device, or a stuck bus */
    TlsClassUnlisted, /* genuine ROM, family code not in the table */
    TlsClassIntercom, /* Cyfral / Metakom - not Dallas silicon at all */
    TlsClassCount,
} TlsClass;

/** What it costs an attacker to walk away with a working copy. */
typedef enum {
    TlsCloneInstant, /* the Flipper in your pocket already emulates it */
    TlsCloneBlank, /* needs a same-type blank and a copier that writes memory */
    TlsCloneSniff, /* password-gated: tap the contact during one real use */
    TlsCloneLab, /* needs the secret, and the secret never leaves the die */
    TlsCloneNotAKey, /* not a credential */
    TlsCloneUnknown, /* nothing read */
    TlsCloneCount,
} TlsCloneClass;

/** The verdict band. Boundaries line up with the letter grades on purpose. */
typedef enum {
    TlsBandReplayable, /*   < 15  this device can be your key right now */
    TlsBandCloneable, /*  15-34  one touch and a one-dollar blank */
    TlsBandGated, /*  35-64  something beyond the serial is asked for */
    TlsBandChallenged, /*  >= 65  proves a secret it never sends */
    TlsBandNotAKey, /* a sensor or a switch: scoring it would be a lie */
    TlsBandUnread, /* nothing on the contact */
    TlsBandCount,
} TlsBand;

typedef enum {
    TlsFindCritical, /* [x] a break, not a nitpick */
    TlsFindWarn, /* [!] a real weakness */
    TlsFindGood, /* [+] a genuine strength */
    TlsFindInfo, /* [i] a neutral fact about this part */
} TlsFindSeverity;

typedef struct {
    TlsFindSeverity sev;
    char text[62];
} TlsFinding;

/** The four terms the score is built from, exposed so the tests can pin each
 *  one instead of only checking the total, and so the report can show the
 *  arithmetic. A grade nobody can check is just an opinion. */
typedef struct {
    uint8_t auth; /*  0..45  proves a secret it never sends?   0 for every key */
    uint8_t integrity; /*  0..15  can a mangled or forged ROM be detected?          */
    uint8_t obscurity; /*  0..25  does a copy need more than a blank?               */
    uint8_t keyspace; /*  0..15  how much is left to guess inside one site?        */
} TlsScoreParts;

/** What the reader hands the grader. Pure data - no Flipper types. */
typedef struct {
    TlsProto proto;
    uint8_t data[TLS_MAX_DATA];
    uint8_t data_len;
    bool crc_ok; /* Dallas only: byte 7 checked against maxim_crc8 of 0..6 */
    uint8_t crc_calc; /* what the CRC should have been, for the report */

    /**
     * Distance to the nearest serial already in the keyring log, or 0 when
     * there is no neighbour (or the check is off). Dallas serials are handed
     * out sequentially at the factory, so two keys from one order sit next to
     * each other - and an attacker who reads one can walk to the others. This
     * arrives as plain data so the penalty stays a pure function.
     */
    uint64_t neighbour_delta;
} TlsReading;

typedef struct {
    int score; /* 0..100 */
    bool scored; /* false for sensors and failed reads: a number would lie */
    char letter[4]; /* "A+".."F", or "-" when !scored */
    TlsBand band;
    TlsClass cls;
    TlsCloneClass clone;
    TlsScoreParts parts;

    char name[30]; /* "DS1990A / DS2401" */
    char what[38]; /* "Serial-number iButton" */
    char headline[56]; /* one line, sits under the grade on the report */
    char id_line[34]; /* "01 A3 4F 09 00 00 00 7B" */
    char serial_line[26]; /* "Serial 0x0000094FA3" */

    uint8_t family; /* the family code byte, 0 when there is no ROM */
    uint16_t id_bits; /* bits the credential carries */
    uint16_t guess_bits; /* bits an attacker still has to guess on-site */
    bool suspect; /* the ROM does not look like factory silicon */

    char verdict[640];
    TlsFinding findings[TLS_MAX_FINDINGS];
    uint8_t finding_num;
} TlsGrade;

/** The one entry point. `out` is fully written; nothing is left stale. */
void tls_grade_evaluate(const TlsReading* reading, TlsGrade* out);

/**
 * The CRC-8 a Dallas ROM carries in byte 7, over bytes 0..6. Same polynomial
 * the firmware's maxim_crc8() uses (x^8 + x^5 + x^4 + 1, reflected); kept here
 * so the host tests can build a valid ROM without linking Flipper code.
 */
uint8_t tls_crc8(const uint8_t* data, size_t len);

const char* tls_band_label(TlsBand band); /* "REPLAYABLE" .. "UNREAD" */
const char* tls_band_blurb(TlsBand band); /* one sentence for the report */
const char* tls_clone_time(TlsCloneClass c); /* "~3 s" */
const char* tls_clone_label(TlsCloneClass c); /* full phrase, for the report */
const char* tls_clone_short(TlsCloneClass c); /* <=13 chars, for the 74 px column */
const char* tls_class_label(TlsClass c); /* "Door key" / "Sensor" / ... */
const char* tls_proto_label(TlsProto p); /* "Dallas 1-Wire" / "Cyfral" / ... */
const char* tls_severity_glyph(TlsFindSeverity sev); /* "[x]" "[!]" "[+]" "[i]" */

/** Letter for a raw score, on the scale Warden and Bastion use, so an F here
 *  and an F there mean the same thing. */
const char* tls_score_letter(int score);

/** Look up a family code without grading. Returns NULL when it is not in the
 *  table. Exposed for the About screen's family browser and for the tests. */
const char* tls_family_part(uint8_t family);
const char* tls_family_what(uint8_t family);

/** Walk the family table. The tests use this to prove that every family code
 *  Talos claims to know has a grade pinned to it, so adding a row without
 *  deciding what it scores fails the build rather than shipping. */
size_t tls_family_count(void);
uint8_t tls_family_code_at(size_t index); /* 0 when index is out of range */

#ifdef __cplusplus
}
#endif
