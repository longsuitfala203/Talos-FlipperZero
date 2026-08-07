#include "tls_grade.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ CRC ---- *
 * The CRC-8 a Dallas ROM carries in byte 7: polynomial x^8 + x^5 + x^4 + 1,
 * reflected, which is the 0x8C shift-right form. Identical to the firmware's
 * maxim_crc8(); duplicated here only so the host tests can build a valid ROM
 * without linking Flipper code. The test suite pins it against ROMs read off
 * real parts, so the two cannot drift apart unnoticed. */
uint8_t tls_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for(size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for(uint8_t bit = 0; bit < 8; bit++) {
            const uint8_t mix = (uint8_t)((crc ^ byte) & 0x01u);
            crc >>= 1;
            if(mix) crc ^= 0x8Cu;
            byte >>= 1;
        }
    }
    return crc;
}

/* -------------------------------------------------------------- labels ---- */

static const char* const band_labels[TlsBandCount] = {
    "REPLAYABLE",
    "CLONEABLE",
    "GATED",
    "CHALLENGED",
    "NOT A KEY",
    "UNREAD",
};

static const char* const band_blurbs[TlsBandCount] = {
    "Short enough, or plain enough, that the device reading it can simply be it.",
    "One touch copies the whole credential onto a blank that costs about a dollar.",
    "Something beyond the open serial is asked for - though not a secret kept back.",
    "It answers a challenge by proving a secret it never puts on the wire.",
    "This is not an access credential, so a security grade would be meaningless.",
    "Nothing answered on the contact, so there is nothing to grade.",
};

const char* tls_band_label(TlsBand band) {
    if((unsigned)band >= TlsBandCount) return band_labels[TlsBandUnread];
    return band_labels[band];
}

const char* tls_band_blurb(TlsBand band) {
    if((unsigned)band >= TlsBandCount) return band_blurbs[TlsBandUnread];
    return band_blurbs[band];
}

static const char* const clone_times[TlsCloneCount] = {
    "~3 s",
    "~2 min",
    "1 use",
    "lab",
    "n/a",
    "-",
};

static const char* const clone_labels[TlsCloneCount] = {
    "any Flipper, or a one-dollar rewritable blank",
    "a same-type blank and a copier that writes memory",
    "one tap on the contact while the key is used legitimately",
    "recovering a secret that never leaves the die",
    "nothing - this is not a credential",
    "nothing was read",
};

/* <= 13 characters: the result screen's clone column is only 74 px wide. */
static const char* const clone_shorts[TlsCloneCount] = {
    "Any Flipper",
    "Typed blank",
    "Bus tap",
    "Lab attack",
    "Not a key",
    "Nothing read",
};

const char* tls_clone_time(TlsCloneClass c) {
    if((unsigned)c >= TlsCloneCount) return clone_times[TlsCloneUnknown];
    return clone_times[c];
}
const char* tls_clone_label(TlsCloneClass c) {
    if((unsigned)c >= TlsCloneCount) return clone_labels[TlsCloneUnknown];
    return clone_labels[c];
}
const char* tls_clone_short(TlsCloneClass c) {
    if((unsigned)c >= TlsCloneCount) return clone_shorts[TlsCloneUnknown];
    return clone_shorts[c];
}

static const char* const class_labels[TlsClassCount] = {
    "Door key",
    "Memory part",
    "Authenticating part",
    "Sensor",
    "Control device",
    "No device",
    "Unlisted 1-Wire part",
    "Intercom key",
};

const char* tls_class_label(TlsClass c) {
    if((unsigned)c >= TlsClassCount) return class_labels[TlsClassInvalid];
    return class_labels[c];
}

static const char* const proto_labels[TlsProtoCount] = {
    "Dallas 1-Wire",
    "Cyfral",
    "Metakom",
    "nothing",
};

const char* tls_proto_label(TlsProto p) {
    if((unsigned)p >= TlsProtoCount) return proto_labels[TlsProtoUnread];
    return proto_labels[p];
}

const char* tls_severity_glyph(TlsFindSeverity sev) {
    switch(sev) {
    case TlsFindCritical:
        return "[x]";
    case TlsFindWarn:
        return "[!]";
    case TlsFindGood:
        return "[+]";
    default:
        return "[i]";
    }
}

const char* tls_score_letter(int score) {
    /* Warden's thresholds, so a grade means the same thing on all three radios. */
    if(score >= 90) return "A+";
    if(score >= 80) return "A";
    if(score >= 65) return "B";
    if(score >= 50) return "C";
    if(score >= 35) return "D";
    return "F";
}

/* Band boundaries are chosen to land on the letter boundaries: CLONEABLE ends
 * where F ends, and CHALLENGED starts where B starts. So the word and the letter
 * never tell different stories. */
static TlsBand band_for_score(int score) {
    if(score >= 65) return TlsBandChallenged;
    if(score >= 35) return TlsBandGated;
    if(score >= 15) return TlsBandCloneable;
    return TlsBandReplayable;
}

/* ------------------------------------------------------- family code table -- *
 * Family codes are assigned by the silicon vendor and stamped into byte 0 of
 * the ROM, so this table is the one part of the identification that cannot be
 * thrown off by a firmware update reordering its protocol list.
 *
 * The four score terms live here rather than in the code below because every
 * one of them is a claim about a specific part, and a claim belongs next to the
 * thing it is about. test/host_grade_test.c pins each row.                    */

#define TLS_ENTRY_NOTES 3u

typedef struct {
    uint8_t family;
    const char* part; /* what is printed on the can */
    const char* what; /* what it is */
    TlsClass cls;
    uint8_t auth; /* 0..45 */
    uint8_t integ; /* 0..15, before the CRC result is applied */
    uint8_t obsc; /* 0..25 */
    uint8_t keysp; /* 0..15, before the neighbour penalty */
    TlsCloneClass clone;
    const char* headline; /* NULL falls back to the class default */
    TlsFindSeverity nsev[TLS_ENTRY_NOTES];
    const char* note[TLS_ENTRY_NOTES];
} TlsFamilyInfo;

/* Every part that is used as a credential, everything a lock might key off,
 * and the sensors people put on the contact by mistake. */
static const TlsFamilyInfo tls_families[] = {
    /* ---------------------------------------------------- door keys ------- */
    {
        .family = 0x01,
        .part = "DS1990A / DS2401",
        .what = "Serial-number iButton",
        .cls = TlsClassCredential,
        .auth = 0,
        .integ = 10,
        .obsc = 0,
        .keysp = 12,
        .clone = TlsCloneInstant,
        .headline = "64 bits in the clear, to anything that touches it",
        .nsev = {TlsFindInfo, TlsFindCritical, TlsFindInfo},
        .note =
            {"Family 01 is the key most people mean by 'iButton'",
             "RW1990 blanks take any serial - including this one",
             "The part has no memory: the serial is the whole key"},
    },
    {
        .family = 0x81,
        .part = "DS1420",
        .what = "Serial ID / licence dongle",
        .cls = TlsClassCredential,
        .auth = 0,
        .integ = 10,
        .obsc = 0,
        .keysp = 12,
        .clone = TlsCloneInstant,
        .headline = "A unique number, and nothing that keeps it unique",
        .nsev = {TlsFindInfo, TlsFindCritical, TlsFindInfo},
        .note =
            {"Usually fitted as a software licence dongle",
             "Copying it lifts the licence as easily as a door",
             "Same 64-bit ROM, same one-touch read, as a door key"},
    },

    /* ---------------------------------------------------- memory parts ---- */
    {
        .family = 0x14,
        .part = "DS1971 / DS2430A",
        .what = "256-bit EEPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 6,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindWarn, TlsFindInfo, TlsFindInfo},
        .note =
            {"32 bytes of EEPROM a lock may or may not check",
             "Write-protect can be set, but the ROM stays readable",
             NULL},
    },
    {
        .family = 0x2D,
        .part = "DS1972 / DS2431",
        .what = "1 Kbit EEPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 10,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"128 bytes of user EEPROM alongside the ROM", NULL, NULL},
    },
    {
        .family = 0x23,
        .part = "DS1973 / DS2433",
        .what = "4 Kbit EEPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 10,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"512 bytes of user EEPROM alongside the ROM", NULL, NULL},
    },
    {
        .family = 0x43,
        .part = "DS28EC20",
        .what = "20 Kbit EEPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 10,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"2560 bytes of EEPROM, with page write protection", NULL, NULL},
    },
    {
        .family = 0x08,
        .part = "DS1992",
        .what = "1 Kbit NVRAM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"128 bytes of battery-backed RAM beside the ROM", NULL, NULL},
    },
    {
        .family = 0x06,
        .part = "DS1993",
        .what = "4 Kbit NVRAM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"512 bytes of battery-backed RAM beside the ROM", NULL, NULL},
    },
    {
        .family = 0x0A,
        .part = "DS1995",
        .what = "16 Kbit NVRAM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"2 KB of battery-backed RAM beside the ROM", NULL, NULL},
    },
    {
        .family = 0x0C,
        .part = "DS1996",
        .what = "64 Kbit NVRAM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"8 KB of battery-backed RAM beside the ROM", NULL, NULL},
    },
    {
        .family = 0x04,
        .part = "DS1994 / DS2404",
        .what = "4 Kbit NVRAM + clock",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note = {"Carries a real-time clock and a cycle counter", NULL, NULL},
    },
    {
        .family = 0x1A,
        .part = "DS1963L",
        .what = "4 Kbit monetary NVRAM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindWarn, TlsFindInfo, TlsFindInfo},
        .note =
            {"Sold for stored value, but with no SHA engine",
             "The 'S' part (DS1963S) is the one with crypto",
             NULL},
    },
    {
        .family = 0x09,
        .part = "DS1982 / DS2502",
        .what = "1 Kbit EPROM, write-once",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindGood, TlsFindCritical, TlsFindInfo},
        .note =
            {"Add-only memory: bits can be set but never cleared",
             "Unwritable does not mean uncopyable - a fresh blank is",
             NULL},
    },
    {
        .family = 0x0B,
        .part = "DS1985 / DS2505",
        .what = "16 Kbit EPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindGood, TlsFindInfo, TlsFindInfo},
        .note = {"Add-only memory: bits can be set but never cleared", NULL, NULL},
    },
    {
        .family = 0x0F,
        .part = "DS1986 / DS2506",
        .what = "64 Kbit EPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 12,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindGood, TlsFindInfo, TlsFindInfo},
        .note = {"Add-only memory: bits can be set but never cleared", NULL, NULL},
    },
    {
        .family = 0x89,
        .part = "DS2502-E48",
        .what = "48-bit node address EPROM",
        .cls = TlsClassMemory,
        .auth = 0,
        .integ = 10,
        .obsc = 6,
        .keysp = 12,
        .clone = TlsCloneBlank,
        .headline = NULL,
        .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
        .note =
            {"Exists to hand equipment a unique Ethernet address",
             "If this opens a door, the door is using a MAC address",
             NULL},
    },
    {
        .family = 0x02,
        .part = "DS1991",
        .what = "MultiKey secure memory",
        .cls = TlsClassMemory,
        .auth = 10,
        .integ = 10,
        .obsc = 18,
        .keysp = 12,
        .clone = TlsCloneSniff,
        .headline = "Password-gated - and the password crosses the wire",
        .nsev = {TlsFindWarn, TlsFindWarn, TlsFindInfo},
        .note =
            {"Three 48-byte subkeys, each behind its own password",
             "Wrong-password reads return a predictable block",
             "The password itself is sent in the clear to unlock"},
    },
    {
        .family = 0x37,
        .part = "DS1977",
        .what = "32 Kbit password EEPROM",
        .cls = TlsClassMemory,
        .auth = 8,
        .integ = 10,
        .obsc = 18,
        .keysp = 12,
        .clone = TlsCloneSniff,
        .headline = "Password-gated - and the password crosses the wire",
        .nsev = {TlsFindWarn, TlsFindInfo, TlsFindInfo},
        .note =
            {"Read and full-access passwords, both sent in the clear",
             "One tap on the contact during real use captures them",
             NULL},
    },

    /* ------------------------------------------------- authenticating ----- */
    {
        .family = 0x33,
        .part = "DS1961S / DS2432",
        .what = "1 Kbit EEPROM + SHA-1",
        .cls = TlsClassCrypto,
        .auth = 22,
        .integ = 15,
        .obsc = 25,
        .keysp = 12,
        .clone = TlsCloneLab,
        .headline = "Real crypto on the die - if the lock asks for it",
        .nsev = {TlsFindGood, TlsFindWarn, TlsFindWarn},
        .note =
            {"SHA-1 MAC over a reader nonce: the secret stays put",
             "Published side-channel work targets this SHA engine",
             "Most locks fitted with these still only read the ROM"},
    },
    {
        .family = 0x18,
        .part = "DS1963S",
        .what = "SHA-1 monetary iButton",
        .cls = TlsClassCrypto,
        .auth = 22,
        .integ = 15,
        .obsc = 25,
        .keysp = 12,
        .clone = TlsCloneLab,
        .headline = "Real crypto on the die - if the lock asks for it",
        .nsev = {TlsFindGood, TlsFindInfo, TlsFindWarn},
        .note =
            {"Eight secrets and write-cycle counters for stored value",
             "Built for cash systems, so the crypto is usually in use",
             "Talos reads the ROM only - it cannot see a MAC exchange"},
    },
    {
        .family = 0x16,
        .part = "DS1957 / DS1955",
        .what = "Java iButton coprocessor",
        .cls = TlsClassCrypto,
        .auth = 30,
        .integ = 15,
        .obsc = 25,
        .keysp = 12,
        .clone = TlsCloneLab,
        .headline = "A tamper-resistant smartcard in an iButton can",
        .nsev = {TlsFindGood, TlsFindGood, TlsFindInfo},
        .note =
            {"A real crypto coprocessor running Java Card applets",
             "Tamper response zeroes the keys instead of leaking them",
             "The strongest thing that ever shipped in this form factor"},
    },

    /* ------------------------------------------------------- sensors ------ */
    {
        .family = 0x10,
        .part = "DS1920 / DS18S20",
        .what = "Temperature sensor",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x22,
        .part = "DS1822",
        .what = "Temperature sensor",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x28,
        .part = "DS18B20",
        .what = "Temperature sensor",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x42,
        .part = "DS28EA00",
        .what = "Temperature sensor, chained",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x26,
        .part = "DS2438",
        .what = "Battery / humidity monitor",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x30,
        .part = "DS2760",
        .what = "Battery monitor",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x20,
        .part = "DS2450",
        .what = "Quad A/D converter",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x21,
        .part = "DS1921",
        .what = "Thermochron logger",
        .cls = TlsClassSensor,
    },
    {
        .family = 0x41,
        .part = "DS1922 / DS1923",
        .what = "Hygrochron logger",
        .cls = TlsClassSensor,
    },

    /* ------------------------------------------------- control devices ---- */
    {
        .family = 0x05,
        .part = "DS2405",
        .what = "Addressable switch",
        .cls = TlsClassControl,
    },
    {
        .family = 0x12,
        .part = "DS2406 / DS2407",
        .what = "Dual switch + EPROM",
        .cls = TlsClassControl,
    },
    {
        .family = 0x29,
        .part = "DS2408",
        .what = "8-channel switch",
        .cls = TlsClassControl,
    },
    {
        .family = 0x3A,
        .part = "DS2413",
        .what = "Dual-channel switch",
        .cls = TlsClassControl,
    },
    {
        .family = 0x1F,
        .part = "DS2409",
        .what = "MicroLAN coupler",
        .cls = TlsClassControl,
    },
    {
        .family = 0x2C,
        .part = "DS2890",
        .what = "Digital potentiometer",
        .cls = TlsClassControl,
    },
    {
        .family = 0x24,
        .part = "DS1904 / DS2415",
        .what = "Real-time clock",
        .cls = TlsClassControl,
    },
    {
        .family = 0x27,
        .part = "DS2417",
        .what = "Real-time clock with interrupt",
        .cls = TlsClassControl,
    },
    {
        .family = 0x1D,
        .part = "DS2423",
        .what = "4 Kbit NVRAM + counter",
        .cls = TlsClassControl,
    },

    /* ------------------------------------------------------- no device ---- */
    {
        .family = 0x00,
        .part = "No device",
        .what = "Bus held low",
        .cls = TlsClassInvalid,
    },
    {
        .family = 0xFF,
        .part = "No device",
        .what = "Bus idle high",
        .cls = TlsClassInvalid,
    },
};

#define TLS_FAMILY_NUM (sizeof(tls_families) / sizeof(tls_families[0]))

/* Anything with a valid CRC but an unlisted family code. Graded as the bare
 * 64-bit ROM it is: the lock can only be comparing the serial. */
static const TlsFamilyInfo tls_unlisted = {
    .family = 0,
    .part = "Unlisted 1-Wire part",
    .what = "Genuine ROM, family code unknown",
    .cls = TlsClassUnlisted,
    .auth = 0,
    .integ = 10,
    .obsc = 0,
    .keysp = 12,
    .clone = TlsCloneInstant,
    .headline = "A valid ROM from a part Talos does not know",
    .nsev = {TlsFindInfo, TlsFindInfo, TlsFindInfo},
    .note =
        {"The CRC checks out, so this is real 1-Wire silicon",
         "Whatever it is, its 64-bit ROM reads like any key's",
         NULL},
};

/* The two Soviet-era intercom formats. Not Dallas silicon, not 1-Wire, no
 * family code - they self-clock a fixed word onto the same contact. Kept beside
 * the Dallas table because the Flipper reads them from the same pin. */
static const TlsFamilyInfo tls_cyfral = {
    .family = 0,
    .part = "Cyfral",
    .what = "Intercom key, 2 bytes",
    .cls = TlsClassIntercom,
    .auth = 0,
    .integ = 0,
    .obsc = 0,
    .keysp = 0,
    .clone = TlsCloneInstant,
    .headline = "Sixteen bits, no checksum, no secret at all",
    .nsev = {TlsFindCritical, TlsFindCritical, TlsFindInfo},
    .note =
        {"Two bytes is the entire credential - the whole of it",
         "Its nibbles come from a restricted set, so the real",
         "space is smaller still than sixteen bits suggests"},
};

static const TlsFamilyInfo tls_metakom = {
    .family = 0,
    .part = "Metakom",
    .what = "Intercom key, 4 bytes",
    .cls = TlsClassIntercom,
    .auth = 0,
    .integ = 4,
    .obsc = 0,
    .keysp = 2,
    .clone = TlsCloneInstant,
    .headline = "Thirty-two bits, a parity check, and no secret",
    .nsev = {TlsFindCritical, TlsFindInfo, TlsFindInfo},
    .note =
        {"Four bytes, sent in the clear on every touch",
         "Parity catches a bad read; it stops no attacker",
         NULL},
};

static const TlsFamilyInfo* family_lookup(uint8_t family) {
    for(size_t i = 0; i < TLS_FAMILY_NUM; i++) {
        if(tls_families[i].family == family) return &tls_families[i];
    }
    return NULL;
}

const char* tls_family_part(uint8_t family) {
    const TlsFamilyInfo* info = family_lookup(family);
    return info ? info->part : NULL;
}

const char* tls_family_what(uint8_t family) {
    const TlsFamilyInfo* info = family_lookup(family);
    return info ? info->what : NULL;
}

size_t tls_family_count(void) {
    return TLS_FAMILY_NUM;
}

uint8_t tls_family_code_at(size_t index) {
    if(index >= TLS_FAMILY_NUM) return 0;
    return tls_families[index].family;
}

/* ------------------------------------------------------------- helpers ---- */

static void add_finding(TlsGrade* out, TlsFindSeverity sev, const char* text) {
    if(out->finding_num >= TLS_MAX_FINDINGS) return;
    if(text == NULL || text[0] == '\0') return;
    TlsFinding* f = &out->findings[out->finding_num++];
    f->sev = sev;
    snprintf(f->text, sizeof(f->text), "%s", text);
}

static void add_findingf(TlsGrade* out, TlsFindSeverity sev, const char* fmt, ...) {
    if(out->finding_num >= TLS_MAX_FINDINGS) return;
    TlsFinding* f = &out->findings[out->finding_num++];
    f->sev = sev;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->text, sizeof(f->text), fmt, ap);
    va_end(ap);
}

/* Append to the verdict, stopping cleanly at the buffer end. Written as one
 * helper so no call site has to reason about how much room is left. */
static void vcat(char* buf, size_t sz, const char* fmt, ...) {
    const size_t used = strlen(buf);
    if(used + 1 >= sz) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + used, sz - used, fmt, ap);
    va_end(ap);
}

static void hex_bytes(char* out, size_t out_sz, const uint8_t* data, uint8_t len) {
    if(out_sz == 0) return;
    out[0] = '\0';
    for(uint8_t i = 0; i < len; i++) {
        char cell[4];
        snprintf(cell, sizeof(cell), "%s%02X", (i == 0) ? "" : " ", data[i]);
        const size_t used = strlen(out);
        if(used + strlen(cell) + 1 > out_sz) break;
        memcpy(out + used, cell, strlen(cell) + 1);
    }
}

/* Bytes 1..6 of the ROM, least significant first, as one 48-bit number. */
static uint64_t rom_serial(const uint8_t* rom) {
    uint64_t sn = 0;
    for(int i = 6; i >= 1; i--) {
        sn = (sn << 8) | rom[i];
    }
    return sn;
}

/**
 * How much a nearby logged serial costs this key.
 *
 * Dallas serials are issued sequentially, so a site that bought a strip of keys
 * holds a contiguous run. Reading one of them tells an attacker roughly where
 * the others are, and the number of guesses needed to sweep the neighbourhood
 * is what `bits` reports. A deliberately coarse ladder: the point is that the
 * space collapses, and pretending to know it to the bit would be false
 * precision.
 *
 * Returns the keyspace points to subtract, and writes the surviving guess-bits.
 */
static uint8_t neighbour_cost(uint64_t delta, uint16_t* bits) {
    if(delta == 0) return 0; /* no neighbour, or the check is switched off */
    if(delta <= 4) {
        *bits = 3;
        return 8;
    }
    if(delta <= 64) {
        *bits = 7;
        return 6;
    }
    if(delta <= 4096) {
        *bits = 13;
        return 4;
    }
    if(delta <= 1048576) {
        *bits = 21;
        return 2;
    }
    return 0; /* far enough apart to be unrelated stock */
}

static const char* class_headline(TlsClass cls) {
    switch(cls) {
    case TlsClassCredential:
        return "64 bits in the clear, to anything that touches it";
    case TlsClassMemory:
        return "A memory part, keyed on the same open serial";
    case TlsClassCrypto:
        return "Real crypto on the die - if the lock asks for it";
    case TlsClassSensor:
        return "A sensor, not a credential";
    case TlsClassControl:
        return "A control device, not a credential";
    case TlsClassIntercom:
        return "A fixed word, shouted on every touch";
    case TlsClassUnlisted:
        return "A valid ROM from a part Talos does not know";
    default:
        return "Nothing answered on the contact";
    }
}

/* --------------------------------------------------------- the unread case - */

static void grade_unread(TlsGrade* out) {
    out->scored = false;
    out->score = 0;
    snprintf(out->letter, sizeof(out->letter), "-");
    out->band = TlsBandUnread;
    out->cls = TlsClassInvalid;
    out->clone = TlsCloneUnknown;
    snprintf(out->name, sizeof(out->name), "No key read");
    snprintf(out->what, sizeof(out->what), "Nothing answered the reset pulse");
    snprintf(out->headline, sizeof(out->headline), "Nothing answered on the contact");
    snprintf(out->id_line, sizeof(out->id_line), "-");

    add_finding(out, TlsFindInfo, "Nothing answered - so nothing here is a verdict");
    add_finding(out, TlsFindInfo, "Touch the key's flat face to the two pads");
    add_finding(out, TlsFindInfo, "The rim is ground; the centre is data");
    add_finding(out, TlsFindInfo, "Hold it still - a rocking key breaks the bus");
    add_finding(out, TlsFindWarn, "Plastic fobs need firm pressure to seat");

    snprintf(
        out->verdict,
        sizeof(out->verdict),
        "No device answered the reset pulse.\n\n"
        "A 1-Wire key needs metal on metal: the flat face of the can against "
        "the two pads at the top-left of the Flipper, rim on rim, held still. "
        "Unlike a card, there is no field to find - if the contact is not made, "
        "nothing happens at all.\n\n"
        "If the key is a Cyfral or Metakom intercom fob, it may need a firmer "
        "press than a Dallas can, because the contact is buried in plastic.\n\n"
        "This is not a grade. Talos will not report a key as safe because it "
        "failed to read it.");
}

/* ---------------------------------------------------------- the main path - */

void tls_grade_evaluate(const TlsReading* reading, TlsGrade* out) {
    if(out == NULL) return;
    memset(out, 0, sizeof(*out));
    if(reading == NULL) {
        grade_unread(out);
        return;
    }

    const TlsProto proto = ((unsigned)reading->proto < TlsProtoCount) ? reading->proto :
                                                                       TlsProtoUnread;
    uint8_t data_len = reading->data_len;
    if(data_len > TLS_MAX_DATA) data_len = TLS_MAX_DATA;

    if(proto == TlsProtoUnread || data_len == 0) {
        grade_unread(out);
        return;
    }

    /* --- which part is this --------------------------------------------- */
    const TlsFamilyInfo* info = NULL;
    bool is_dallas = false;

    if(proto == TlsProtoCyfral) {
        info = &tls_cyfral;
    } else if(proto == TlsProtoMetakom) {
        info = &tls_metakom;
    } else {
        is_dallas = true;
        out->family = reading->data[0];
        info = family_lookup(out->family);
        if(info == NULL) info = &tls_unlisted;
    }

    out->cls = info->cls;
    out->clone = info->clone;
    snprintf(out->name, sizeof(out->name), "%s", info->part);
    snprintf(out->what, sizeof(out->what), "%s", info->what);
    snprintf(
        out->headline,
        sizeof(out->headline),
        "%s",
        info->headline ? info->headline : class_headline(info->cls));
    hex_bytes(out->id_line, sizeof(out->id_line), reading->data, data_len);

    /* --- does the ROM look like factory silicon ------------------------- *
     * Only tells that cannot be anything else. A run of zero bytes is *not*
     * one of them: a low serial number genuinely ends in zeros once it is laid
     * out least-significant-first, and flagging real keys as fakes would make
     * every other finding on the screen less believable. */
    const bool short_rom = is_dallas && data_len < TLS_ROM_LEN;
    uint64_t serial = 0;

    if(is_dallas && !short_rom) {
        serial = rom_serial(reading->data);
        snprintf(out->serial_line, sizeof(out->serial_line), "Serial 0x%012llX", (unsigned long long)serial);

        bool all_zero = true, all_ones = true;
        for(uint8_t i = 1; i <= 6; i++) {
            if(reading->data[i] != 0x00) all_zero = false;
            if(reading->data[i] != 0xFF) all_ones = false;
        }
        if(all_zero || all_ones) out->suspect = true;
    }
    if(is_dallas && (!reading->crc_ok || short_rom)) out->suspect = true;

    /* --- sensors, switches and dead buses are not graded ----------------- */
    if(info->cls == TlsClassSensor || info->cls == TlsClassControl) {
        out->scored = false;
        out->score = 0;
        snprintf(out->letter, sizeof(out->letter), "-");
        out->band = TlsBandNotAKey;
        out->clone = TlsCloneNotAKey;

        add_findingf(out, TlsFindInfo, "This is a %s, not a credential", info->what);
        add_finding(out, TlsFindInfo, "Grading it against door criteria would be a lie");
        add_finding(out, TlsFindWarn, "Its 64-bit ROM still reads exactly like a key's");
        add_finding(out, TlsFindInfo, "A lock that accepts it is no better off");
        if(!reading->crc_ok) {
            add_finding(out, TlsFindWarn, "ROM checksum failed - the read may be bad");
        }

        snprintf(
            out->verdict,
            sizeof(out->verdict),
            "This is a %s (family %02X), not an access credential, so Talos "
            "will not put a security grade on it.\n\n"
            "Worth knowing anyway: its 64-bit ROM is read by exactly the same "
            "command as a door key's, with exactly as little ceremony. Some "
            "installations do key off whatever 1-Wire part happens to be on the "
            "fob. If yours does, it inherits every weakness a DS1990A has - the "
            "part being a sensor buys nothing.\n\n"
            "If you meant to grade a key, the one on the contact is not one.",
            info->what,
            out->family);
        return;
    }

    if(info->cls == TlsClassInvalid) {
        grade_unread(out);
        /* Keep the more specific cause, and the bytes that produced it, rather
         * than the generic "nothing answered" wording. */
        snprintf(out->name, sizeof(out->name), "%s", info->part);
        snprintf(out->what, sizeof(out->what), "%s", info->what);
        hex_bytes(out->id_line, sizeof(out->id_line), reading->data, data_len);
        out->family = reading->data[0];
        out->suspect = true;
        return;
    }

    /* --- the four terms -------------------------------------------------- */
    out->parts.auth = info->auth;
    out->parts.obscurity = info->obsc;
    /* A checksum is error detection, never authentication - but a ROM whose CRC
     * fails has not even proved it was read correctly, so the term goes to
     * zero rather than being quietly credited. */
    out->parts.integrity = (is_dallas && (!reading->crc_ok || short_rom)) ? 0 : info->integ;

    uint16_t guess_bits = 0;
    if(is_dallas) {
        out->id_bits = 64;
        guess_bits = 48; /* the serial field; family and CRC are not secrets */
    } else if(proto == TlsProtoCyfral) {
        out->id_bits = 16;
        guess_bits = 16;
    } else {
        out->id_bits = 32;
        guess_bits = 28; /* parity and a fixed prefix eat into the 32 */
    }

    uint8_t keyspace = info->keysp;
    const uint8_t penalty = is_dallas ? neighbour_cost(reading->neighbour_delta, &guess_bits) : 0;
    keyspace = (penalty >= keyspace) ? 0 : (uint8_t)(keyspace - penalty);
    out->parts.keyspace = keyspace;
    out->guess_bits = guess_bits;

    int score = (int)out->parts.auth + (int)out->parts.integrity + (int)out->parts.obscurity +
                (int)out->parts.keyspace;
    if(score < 0) score = 0;
    if(score > 100) score = 100;
    out->score = score;
    out->scored = true;
    snprintf(out->letter, sizeof(out->letter), "%s", tls_score_letter(score));
    out->band = band_for_score(score);

    /* --- findings -------------------------------------------------------- *
     * Engine-derived truths first: they are the same for every key of this
     * shape and they are the most important thing on the screen. Then what is
     * specific to the key in hand. Then the part's own notes, filling whatever
     * room is left. */

    if(!reading->crc_ok && is_dallas) {
        add_findingf(
            out,
            TlsFindCritical,
            "ROM checksum failed (expected %02X)",
            reading->crc_calc);
        add_finding(out, TlsFindWarn, "Bad contact, or a blank a copier wrote carelessly");
    }

    if(out->parts.auth == 0) {
        add_finding(out, TlsFindCritical, "No challenge: one read hands over the whole key");
    } else if(out->parts.auth < 20) {
        add_finding(out, TlsFindWarn, "Gated by a password sent in the clear on the wire");
    } else {
        add_finding(out, TlsFindGood, "Challenge-response: it proves a secret it never sends");
    }

    switch(out->clone) {
    case TlsCloneInstant:
        add_finding(out, TlsFindCritical, "A Flipper, or a one-dollar blank, is a working copy");
        break;
    case TlsCloneBlank:
        add_finding(out, TlsFindWarn, "A copy needs a same-type blank, not a bargain fob");
        break;
    case TlsCloneSniff:
        add_finding(out, TlsFindWarn, "One tap on the contact during real use gets the password");
        break;
    case TlsCloneLab:
        add_finding(out, TlsFindGood, "A copy needs a secret that never leaves the die");
        break;
    default:
        break;
    }

    if(is_dallas && reading->neighbour_delta > 0 && penalty > 0) {
        add_findingf(
            out,
            TlsFindCritical,
            "A logged key sits %llu serials away",
            (unsigned long long)reading->neighbour_delta);
        add_findingf(
            out, TlsFindCritical, "Sequential stock: ~%u guesses covers both", 1u << guess_bits);
    }

    if(out->suspect && reading->crc_ok) {
        add_finding(out, TlsFindWarn, "Serial is all 00 or all FF - not factory silicon");
    }

    if(proto == TlsProtoCyfral) {
        add_finding(out, TlsFindCritical, "No checksum at all: a misread is indistinguishable");
    }

    for(unsigned i = 0; i < TLS_ENTRY_NOTES; i++) {
        if(info->note[i] == NULL) break;
        add_finding(out, info->nsev[i], info->note[i]);
    }

    /* --- verdict --------------------------------------------------------- */
    char* v = out->verdict;
    const size_t vs = sizeof(out->verdict);
    v[0] = '\0';

    if(is_dallas) {
        vcat(v, vs, "%s, family code %02X.\n\n", info->part, out->family);
    } else {
        vcat(v, vs, "%s intercom key.\n\n", info->part);
    }

    if(out->parts.auth == 0) {
        vcat(
            v,
            vs,
            "The whole conversation is one command and %u bits of answer. There "
            "is no challenge, no nonce, nothing held back - so whoever touches "
            "this key once holds everything the lock will ever ask for. That is "
            "the 45 points for authentication, and this key scores none of "
            "them.\n\n",
            (unsigned)out->id_bits);
    } else if(out->parts.auth < 20) {
        vcat(
            v,
            vs,
            "This part does ask for a password before it gives up its memory, "
            "which is more than a plain key does. But the password travels the "
            "same two wires in the clear, so anyone who can bridge the contact "
            "while the key is used legitimately walks away with it. A gate is "
            "not a secret.\n\n");
    } else {
        vcat(
            v,
            vs,
            "This part carries a real authentication engine: the reader sends a "
            "challenge, the key answers with a MAC computed from a secret that "
            "is never transmitted and cannot be read back. Copying it means "
            "recovering that secret, which is lab work, not keyring work.\n\n"
            "The caveat is the important part. Talos reads the ROM, which is all "
            "any 1-Wire device offers without being asked - it cannot see "
            "whether your lock ever issues a challenge. Plenty of installations "
            "fit crypto-capable buttons and then compare the serial anyway. If "
            "yours does, this key is an F like any other.\n\n");
    }

    if(is_dallas && !reading->crc_ok) {
        vcat(
            v,
            vs,
            "The CRC in byte 7 does not match the first seven bytes (it should "
            "have been %02X). Either the contact broke mid-read, or this is a "
            "blank that a copier programmed without fixing the checksum - which "
            "is to say, a key that is already a copy.\n\n",
            reading->crc_calc);
    }

    if(is_dallas && reading->neighbour_delta > 0 && penalty > 0) {
        vcat(
            v,
            vs,
            "One more thing, and it is the one people do not expect. A key "
            "already in your log sits %llu serial numbers from this one. Dallas "
            "hands serials out in sequence, so a site that bought a strip of "
            "keys holds a contiguous run of them. Read one, and the rest are a "
            "short walk away - about %u tries, not the 281 trillion the 48-bit "
            "field suggests.\n\n",
            (unsigned long long)reading->neighbour_delta,
            1u << guess_bits);
    }

    if(out->clone == TlsCloneInstant) {
        vcat(
            v,
            vs,
            "What to do about it: nothing at this layer will help. A longer "
            "serial, a different family code, a fob in a different colour - all "
            "of it is still a number read by anyone who touches it. The fix is "
            "to move the credential to something that authenticates: a DESFire "
            "or Seos card at 13.56 MHz, or, staying on 1-Wire, a DS1961S with a "
            "reader that actually issues the challenge.\n\n");
    } else if(out->clone == TlsCloneBlank || out->clone == TlsCloneSniff) {
        vcat(
            v,
            vs,
            "What to do about it: check whether your reader actually looks at "
            "the memory in this part, or only at the ROM. If it is the ROM, this "
            "key is a DS1990A wearing a bigger die, and it grades like one.\n\n");
    }

    vcat(
        v,
        vs,
        "Talos read this key and put it back exactly as it found it. It does "
        "not write blanks and does not emulate. Everything above is what any "
        "reader learns for free.");
}
