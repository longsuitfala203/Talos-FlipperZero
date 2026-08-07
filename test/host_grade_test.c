/**
 * Host tests for Talos's grading engine.
 *
 * The grade is the whole product. It is also the one part of the app a
 * screenshot cannot vouch for, so every family code, every band boundary, every
 * string bound and the whole neighbour ladder are pinned here and checked on
 * every push. helpers/tls_grade.c includes no Flipper header precisely so this
 * file can exist.
 *
 *   make -C test
 *
 * The coverage check at the bottom is the important one: adding a family code to
 * the table without deciding what it scores fails the build.
 */
#include "../helpers/tls_grade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks = 0;
static unsigned failures = 0;

#define CHECK(cond, ...)                                     \
    do {                                                     \
        checks++;                                            \
        if(!(cond)) {                                        \
            failures++;                                      \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                             \
            printf("\n");                                    \
        }                                                    \
    } while(0)

#define CHECK_EQ_INT(got, want, ...)                         \
    do {                                                     \
        checks++;                                            \
        if((long)(got) != (long)(want)) {                     \
            failures++;                                      \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                             \
            printf(" (got %ld, want %ld)\n", (long)(got), (long)(want)); \
        }                                                    \
    } while(0)

#define CHECK_EQ_STR(got, want, ...)                         \
    do {                                                     \
        checks++;                                            \
        if(strcmp((got), (want)) != 0) {                     \
            failures++;                                      \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                             \
            printf(" (got \"%s\", want \"%s\")\n", (got), (want)); \
        }                                                    \
    } while(0)

/* ---------------------------------------------------------------- helpers -- */

/* Build a Dallas reading with a correct CRC in byte 7. `serial` is the 48-bit
 * field, laid out least-significant byte first the way the silicon does. */
static TlsReading dallas(uint8_t family, uint64_t serial) {
    TlsReading r;
    memset(&r, 0, sizeof(r));
    r.proto = TlsProtoDallas;
    r.data_len = 8;
    r.data[0] = family;
    for(int i = 1; i <= 6; i++) {
        r.data[i] = (uint8_t)(serial & 0xFFu);
        serial >>= 8;
    }
    r.data[7] = tls_crc8(r.data, 7);
    r.crc_calc = r.data[7];
    r.crc_ok = true;
    return r;
}

/* Evaluate into a deliberately dirty struct, so any field the engine forgets to
 * write shows up as garbage rather than as a convenient zero. */
static void evaluate_dirty(const TlsReading* r, TlsGrade* g) {
    memset(g, 0xAA, sizeof(*g));
    tls_grade_evaluate(r, g);
}

static void check_strings_sane(const TlsGrade* g, const char* who) {
    CHECK(g->name[0] != '\0', "%s: name is empty", who);
    CHECK(g->what[0] != '\0', "%s: what is empty", who);
    CHECK(g->headline[0] != '\0', "%s: headline is empty", who);
    CHECK(g->verdict[0] != '\0', "%s: verdict is empty", who);
    CHECK(g->id_line[0] != '\0', "%s: id_line is empty", who);
    CHECK(g->letter[0] != '\0', "%s: letter is empty", who);

    /* Every buffer must be NUL-terminated inside its own bounds. */
    CHECK(memchr(g->name, '\0', sizeof(g->name)) != NULL, "%s: name unterminated", who);
    CHECK(memchr(g->what, '\0', sizeof(g->what)) != NULL, "%s: what unterminated", who);
    CHECK(
        memchr(g->headline, '\0', sizeof(g->headline)) != NULL, "%s: headline unterminated", who);
    CHECK(memchr(g->verdict, '\0', sizeof(g->verdict)) != NULL, "%s: verdict unterminated", who);
    CHECK(memchr(g->id_line, '\0', sizeof(g->id_line)) != NULL, "%s: id_line unterminated", who);
    CHECK(
        memchr(g->serial_line, '\0', sizeof(g->serial_line)) != NULL,
        "%s: serial_line unterminated",
        who);
    CHECK(memchr(g->letter, '\0', sizeof(g->letter)) != NULL, "%s: letter unterminated", who);

    /* The result screen prints the part name in FontPrimary across 124 px. It
     * truncates rather than overflows, but a name that never fits is a copy bug,
     * so keep them inside what the screen can show. */
    CHECK(strlen(g->name) <= 26, "%s: name too long to read on screen: %s", who, g->name);
    CHECK(strlen(g->headline) < sizeof(g->headline), "%s: headline overran", who);

    CHECK(g->finding_num <= TLS_MAX_FINDINGS, "%s: too many findings", who);
    for(uint8_t i = 0; i < g->finding_num; i++) {
        CHECK(g->findings[i].text[0] != '\0', "%s: finding %u is empty", who, i);
        CHECK(
            memchr(g->findings[i].text, '\0', sizeof(g->findings[i].text)) != NULL,
            "%s: finding %u unterminated",
            who,
            i);
        /* The report renders "[x] " plus the text on a 128 px line; the widget
         * wraps, but a finding longer than the buffer means copy was silently
         * cut mid-word. */
        CHECK(
            strlen(g->findings[i].text) < sizeof(g->findings[i].text) - 1,
            "%s: finding %u was truncated: %s",
            who,
            i,
            g->findings[i].text);
    }

    /* The letter and the score must never disagree. */
    if(g->scored) {
        CHECK_EQ_STR(g->letter, tls_score_letter(g->score), "%s: letter/score disagree", who);
        CHECK(g->score >= 0 && g->score <= 100, "%s: score out of range", who);
    } else {
        CHECK_EQ_STR(g->letter, "-", "%s: unscored key must show a dash", who);
        CHECK_EQ_INT(g->score, 0, "%s: unscored key must score 0", who);
    }

    /* The four terms must add up to the total, every time. */
    if(g->scored) {
        const int sum = (int)g->parts.auth + (int)g->parts.integrity + (int)g->parts.obscurity +
                        (int)g->parts.keyspace;
        CHECK_EQ_INT(sum, g->score, "%s: the four terms do not add up", who);
        CHECK(g->parts.auth <= 45, "%s: auth over its cap", who);
        CHECK(g->parts.integrity <= 15, "%s: integrity over its cap", who);
        CHECK(g->parts.obscurity <= 25, "%s: obscurity over its cap", who);
        CHECK(g->parts.keyspace <= 15, "%s: keyspace over its cap", who);
    }
}

/* ------------------------------------------------------------------- CRC --- */

static void test_crc(void) {
    printf("CRC-8\n");

    const uint8_t zero = 0x00;
    const uint8_t one = 0x01;
    CHECK_EQ_INT(tls_crc8(&zero, 1), 0x00, "crc8({00})");
    /* Hand-computed from the 0x8C reflected polynomial. */
    CHECK_EQ_INT(tls_crc8(&one, 1), 0x5E, "crc8({01})");
    CHECK_EQ_INT(tls_crc8(&zero, 0), 0x00, "crc8 of nothing");

    /* The defining property of the Dallas ROM checksum: run the CRC over all
     * eight bytes, checksum included, and it comes out zero. Any change to the
     * polynomial or the bit order breaks this. */
    const uint64_t serials[] = {0, 1, 0x0000094FA3ull, 0xFFFFFFFFFFFFull, 0x123456789Aull};
    for(size_t i = 0; i < sizeof(serials) / sizeof(serials[0]); i++) {
        TlsReading r = dallas(0x01, serials[i]);
        CHECK_EQ_INT(tls_crc8(r.data, 8), 0x00, "whole-ROM CRC must be zero");
        CHECK_EQ_INT(tls_crc8(r.data, 7), r.data[7], "byte 7 must be the CRC of 0..6");
    }

    /* Deterministic, and sensitive to every byte. */
    TlsReading a = dallas(0x01, 0x0000094FA3ull);
    TlsReading b = dallas(0x01, 0x0000094FA4ull);
    CHECK(a.data[7] != b.data[7] || a.data[1] != b.data[1], "crc must react to the serial");
}

/* ------------------------------------------------------- the letter scale --- */

static void test_letters(void) {
    printf("Letter scale (shared with Warden and Bastion)\n");

    struct {
        int score;
        const char* letter;
    } cases[] = {
        {-10, "F"}, {0, "F"},   {34, "F"},  {35, "D"},  {49, "D"},  {50, "C"},
        {64, "C"},  {65, "B"},  {79, "B"},  {80, "A"},  {89, "A"},  {90, "A+"},
        {100, "A+"}, {1000, "A+"},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK_EQ_STR(
            tls_score_letter(cases[i].score), cases[i].letter, "letter for %d", cases[i].score);
    }
}

/* ------------------------------------------------------- every family code -- */

typedef struct {
    uint8_t family;
    int score;
    const char* letter;
    TlsBand band;
    TlsCloneClass clone;
    TlsClass cls;
    bool scored;
} Expect;

/* One row per family code the engine claims to know. These numbers are the
 * product: if a change moves one, it moves here too, deliberately. */
static const Expect expected[] = {
    /* door keys: the archetype, and the archetype wearing a dongle label */
    {0x01, 22, "F", TlsBandCloneable, TlsCloneInstant, TlsClassCredential, true},
    {0x81, 22, "F", TlsBandCloneable, TlsCloneInstant, TlsClassCredential, true},

    /* memory parts - a copy needs a blank of the same type */
    {0x14, 28, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x89, 28, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x2D, 32, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x23, 32, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x43, 32, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x08, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x06, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x0A, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x0C, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x04, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x1A, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x09, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x0B, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},
    {0x0F, 34, "F", TlsBandCloneable, TlsCloneBlank, TlsClassMemory, true},

    /* password-gated memory - a gate, but the password is on the wire */
    {0x37, 48, "D", TlsBandGated, TlsCloneSniff, TlsClassMemory, true},
    {0x02, 50, "C", TlsBandGated, TlsCloneSniff, TlsClassMemory, true},

    /* the three parts that can actually prove a secret */
    {0x33, 74, "B", TlsBandChallenged, TlsCloneLab, TlsClassCrypto, true},
    {0x18, 74, "B", TlsBandChallenged, TlsCloneLab, TlsClassCrypto, true},
    {0x16, 82, "A", TlsBandChallenged, TlsCloneLab, TlsClassCrypto, true},

    /* sensors: not credentials, so not scored */
    {0x10, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x22, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x28, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x42, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x26, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x30, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x20, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x21, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},
    {0x41, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassSensor, false},

    /* switches, couplers, clocks and counters: likewise */
    {0x05, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x12, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x29, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x3A, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x1F, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x2C, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x24, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x27, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},
    {0x1D, 0, "-", TlsBandNotAKey, TlsCloneNotAKey, TlsClassControl, false},

    /* a bus that answered with nothing at all */
    {0x00, 0, "-", TlsBandUnread, TlsCloneUnknown, TlsClassInvalid, false},
    {0xFF, 0, "-", TlsBandUnread, TlsCloneUnknown, TlsClassInvalid, false},
};

#define EXPECTED_NUM (sizeof(expected) / sizeof(expected[0]))

static void test_families(void) {
    printf("Family codes (%zu pinned)\n", EXPECTED_NUM);

    for(size_t i = 0; i < EXPECTED_NUM; i++) {
        const Expect* e = &expected[i];
        char who[32];
        snprintf(who, sizeof(who), "family %02X", e->family);

        TlsReading r = dallas(e->family, 0x0000094FA3ull);
        TlsGrade g;
        evaluate_dirty(&r, &g);

        CHECK_EQ_INT(g.score, e->score, "%s score", who);
        CHECK_EQ_STR(g.letter, e->letter, "%s letter", who);
        CHECK_EQ_INT(g.band, e->band, "%s band", who);
        CHECK_EQ_INT(g.clone, e->clone, "%s clone class", who);
        CHECK_EQ_INT(g.cls, e->cls, "%s class", who);
        CHECK_EQ_INT(g.scored, e->scored, "%s scored", who);

        /* The name must come from the table, not from a fallback. */
        const char* part = tls_family_part(e->family);
        CHECK(part != NULL, "%s must be in the table", who);
        if(part) CHECK_EQ_STR(g.name, part, "%s name", who);
        CHECK(tls_family_what(e->family) != NULL, "%s must have a description", who);

        /* Family code is reported back, so the report can print it. */
        CHECK_EQ_INT(g.family, e->family, "%s family byte", who);

        /* Every gradeable key must produce at least one finding; a screen with
         * no findings looks like the app failed. */
        CHECK(g.finding_num > 0, "%s produced no findings", who);

        check_strings_sane(&g, who);
    }
}

/* Adding a row to the table without pinning what it scores must fail here. */
static void test_family_coverage(void) {
    printf("Coverage: every table entry is pinned\n");

    const size_t table = tls_family_count();
    CHECK_EQ_INT(table, EXPECTED_NUM, "table size vs pinned expectations");

    for(size_t i = 0; i < table; i++) {
        const uint8_t family = tls_family_code_at(i);
        bool found = false;
        for(size_t j = 0; j < EXPECTED_NUM; j++) {
            if(expected[j].family == family) {
                found = true;
                break;
            }
        }
        CHECK(found, "family %02X is in the table but has no pinned grade", family);
    }

    /* ...and the reverse, so a typo in the expectations does not pass. */
    for(size_t j = 0; j < EXPECTED_NUM; j++) {
        CHECK(
            tls_family_part(expected[j].family) != NULL,
            "family %02X is pinned but not in the table",
            expected[j].family);
    }

    /* No duplicate family codes: the first would shadow the second forever. */
    for(size_t i = 0; i < table; i++) {
        for(size_t j = i + 1; j < table; j++) {
            CHECK(
                tls_family_code_at(i) != tls_family_code_at(j),
                "family %02X appears twice in the table",
                tls_family_code_at(i));
        }
    }
}

/* ------------------------------------------------------- the intercom pair -- */

static void test_intercom(void) {
    printf("Cyfral and Metakom\n");

    TlsReading r;
    TlsGrade g;

    memset(&r, 0, sizeof(r));
    r.proto = TlsProtoCyfral;
    r.data_len = 2;
    r.data[0] = 0x5A;
    r.data[1] = 0xA5;
    evaluate_dirty(&r, &g);
    CHECK_EQ_INT(g.score, 0, "Cyfral scores nothing at all");
    CHECK_EQ_STR(g.letter, "F", "Cyfral letter");
    CHECK_EQ_INT(g.band, TlsBandReplayable, "Cyfral band");
    CHECK_EQ_INT(g.cls, TlsClassIntercom, "Cyfral class");
    CHECK_EQ_INT(g.clone, TlsCloneInstant, "Cyfral clone class");
    CHECK_EQ_INT(g.id_bits, 16, "Cyfral carries 16 bits");
    CHECK_EQ_INT(g.parts.integrity, 0, "Cyfral has no checksum to credit");
    CHECK_EQ_INT(g.family, 0, "Cyfral has no family code");
    CHECK_EQ_STR(g.id_line, "5A A5", "Cyfral id line");
    check_strings_sane(&g, "Cyfral");

    memset(&r, 0, sizeof(r));
    r.proto = TlsProtoMetakom;
    r.data_len = 4;
    r.data[0] = 0xCA;
    r.data[1] = 0xFE;
    r.data[2] = 0xBA;
    r.data[3] = 0xBE;
    evaluate_dirty(&r, &g);
    CHECK_EQ_INT(g.score, 6, "Metakom score");
    CHECK_EQ_STR(g.letter, "F", "Metakom letter");
    CHECK_EQ_INT(g.band, TlsBandReplayable, "Metakom band");
    CHECK_EQ_INT(g.parts.integrity, 4, "Metakom gets parity credit, not more");
    CHECK_EQ_INT(g.id_bits, 32, "Metakom carries 32 bits");
    CHECK_EQ_STR(g.id_line, "CA FE BA BE", "Metakom id line");
    check_strings_sane(&g, "Metakom");

    /* The neighbour check is a Dallas-serial idea; it must not touch these. */
    r.neighbour_delta = 1;
    evaluate_dirty(&r, &g);
    CHECK_EQ_INT(g.score, 6, "Metakom is unaffected by a Dallas neighbour");
    CHECK_EQ_INT(g.guess_bits, 28, "Metakom guess bits unchanged");
}

/* ----------------------------------------------------------- unlisted parts -- */

static void test_unlisted(void) {
    printf("Unlisted family codes\n");

    /* Codes deliberately chosen to be absent from the table. */
    const uint8_t unknowns[] = {0x77, 0x3B, 0x50, 0x99, 0xAB, 0x07, 0x0D, 0x11};
    for(size_t i = 0; i < sizeof(unknowns) / sizeof(unknowns[0]); i++) {
        const uint8_t family = unknowns[i];
        CHECK(tls_family_part(family) == NULL, "family %02X must not be in the table", family);

        TlsReading r = dallas(family, 0x0000094FA3ull);
        TlsGrade g;
        evaluate_dirty(&r, &g);

        char who[32];
        snprintf(who, sizeof(who), "unlisted %02X", family);
        /* Graded as the bare 64-bit ROM it is: the lock can only be comparing
         * a serial, whatever the silicon turns out to be. */
        CHECK_EQ_INT(g.score, 22, "%s score", who);
        CHECK_EQ_INT(g.cls, TlsClassUnlisted, "%s class", who);
        CHECK_EQ_INT(g.band, TlsBandCloneable, "%s band", who);
        CHECK_EQ_INT(g.family, family, "%s family byte", who);
        CHECK(g.scored, "%s must still be graded", who);
        check_strings_sane(&g, who);
    }
}

/* ------------------------------------------------------------ CRC failures -- */

static void test_crc_failure(void) {
    printf("Failed checksums\n");

    TlsReading r = dallas(0x01, 0x0000094FA3ull);
    const uint8_t real_crc = r.data[7];
    r.data[7] = (uint8_t)(real_crc ^ 0xFFu);
    r.crc_ok = false;
    r.crc_calc = real_crc;

    TlsGrade g;
    evaluate_dirty(&r, &g);

    /* Integrity is the term a checksum buys, so a failed checksum takes it. */
    CHECK_EQ_INT(g.parts.integrity, 0, "failed CRC forfeits integrity");
    CHECK_EQ_INT(g.score, 12, "DS1990A with a bad CRC");
    CHECK_EQ_INT(g.band, TlsBandReplayable, "a bad CRC drops the band");
    CHECK(g.suspect, "a failed CRC makes the ROM suspect");
    CHECK(g.scored, "a bad CRC is still graded, loudly");
    check_strings_sane(&g, "bad CRC");

    /* The expected value has to reach the user, or the finding is useless. */
    bool mentions_crc = false;
    for(uint8_t i = 0; i < g.finding_num; i++) {
        if(strstr(g.findings[i].text, "checksum") != NULL) mentions_crc = true;
    }
    CHECK(mentions_crc, "a failed CRC must produce a finding that says so");
    CHECK(strstr(g.verdict, "CRC") != NULL, "the verdict must explain the CRC failure");

    /* A sensor with a bad CRC stays ungraded rather than becoming a key. */
    TlsReading s = dallas(0x28, 0x0000094FA3ull);
    s.crc_ok = false;
    evaluate_dirty(&s, &g);
    CHECK_EQ_INT(g.band, TlsBandNotAKey, "a thermometer with a bad CRC is still not a key");
    CHECK(!g.scored, "still unscored");
}

/* ------------------------------------------------------- suspect ROM tells -- */

static void test_suspect(void) {
    printf("Suspect ROM detection\n");

    TlsGrade g;

    /* All-zero and all-FF serials are not factory silicon. */
    TlsReading zeros = dallas(0x01, 0);
    evaluate_dirty(&zeros, &g);
    CHECK(g.suspect, "an all-zero serial is suspect");

    TlsReading ones = dallas(0x01, 0xFFFFFFFFFFFFull);
    evaluate_dirty(&ones, &g);
    CHECK(g.suspect, "an all-FF serial is suspect");

    /* The deliberate non-tell: a genuinely low serial number ends in zero bytes
     * once it is laid out least-significant-first. Flagging those as fakes would
     * make every other finding on the screen less believable, so it must not
     * happen. */
    const uint64_t low_but_real[] = {1ull, 0x12345ull, 0x0000012345ull, 0xFFull, 0x0100ull};
    for(size_t i = 0; i < sizeof(low_but_real) / sizeof(low_but_real[0]); i++) {
        TlsReading r = dallas(0x01, low_but_real[i]);
        evaluate_dirty(&r, &g);
        CHECK(!g.suspect, "a low serial (%llu) is real, not suspect", (unsigned long long)low_but_real[i]);
        CHECK_EQ_INT(g.score, 22, "a low serial still grades normally");
    }

    /* A truncated ROM cannot have been verified, so it forfeits integrity. */
    TlsReading short_rom = dallas(0x01, 0x0000094FA3ull);
    short_rom.data_len = 5;
    evaluate_dirty(&short_rom, &g);
    CHECK(g.suspect, "a short ROM is suspect");
    CHECK_EQ_INT(g.parts.integrity, 0, "a short ROM forfeits integrity");
}

/* --------------------------------------------------- the neighbour ladder --- */

static void test_neighbour(void) {
    printf("Sequential-neighbour penalty\n");

    struct {
        uint64_t delta;
        int score;
        TlsBand band;
        uint16_t guess_bits;
        const char* why;
    } ladder[] = {
        /* no neighbour: the full 48-bit field stands */
        {0, 22, TlsBandCloneable, 48, "no neighbour"},
        /* both sides of every rung */
        {1, 14, TlsBandReplayable, 3, "adjacent serial"},
        {4, 14, TlsBandReplayable, 3, "top of the -8 rung"},
        {5, 16, TlsBandCloneable, 7, "bottom of the -6 rung"},
        {64, 16, TlsBandCloneable, 7, "top of the -6 rung"},
        {65, 18, TlsBandCloneable, 13, "bottom of the -4 rung"},
        {4096, 18, TlsBandCloneable, 13, "top of the -4 rung"},
        {4097, 20, TlsBandCloneable, 21, "bottom of the -2 rung"},
        {1048576, 20, TlsBandCloneable, 21, "top of the -2 rung"},
        {1048577, 22, TlsBandCloneable, 48, "far enough apart to be unrelated"},
        {0xFFFFFFFFFFFFull, 22, TlsBandCloneable, 48, "opposite ends of the field"},
    };

    for(size_t i = 0; i < sizeof(ladder) / sizeof(ladder[0]); i++) {
        TlsReading r = dallas(0x01, 0x0000094FA3ull);
        r.neighbour_delta = ladder[i].delta;
        TlsGrade g;
        evaluate_dirty(&r, &g);

        CHECK_EQ_INT(g.score, ladder[i].score, "delta %llu (%s) score", (unsigned long long)ladder[i].delta, ladder[i].why);
        CHECK_EQ_INT(g.band, ladder[i].band, "delta %llu band", (unsigned long long)ladder[i].delta);
        CHECK_EQ_INT(
            g.guess_bits, ladder[i].guess_bits, "delta %llu guess bits", (unsigned long long)ladder[i].delta);
        check_strings_sane(&g, ladder[i].why);
    }

    /* A neighbour must be reported, not just quietly subtracted. */
    TlsReading r = dallas(0x01, 0x0000094FA3ull);
    r.neighbour_delta = 3;
    TlsGrade g;
    evaluate_dirty(&r, &g);
    bool mentions = false;
    for(uint8_t i = 0; i < g.finding_num; i++) {
        if(strstr(g.findings[i].text, "serials away") != NULL) mentions = true;
    }
    CHECK(mentions, "a nearby serial must produce a finding");
    CHECK(strstr(g.verdict, "sequence") != NULL, "the verdict must explain sequential stock");

    /* The penalty must never underflow the term or the total. */
    TlsReading cy = dallas(0x01, 0x0000094FA3ull);
    for(uint64_t d = 0; d < 40; d++) {
        cy.neighbour_delta = d;
        evaluate_dirty(&cy, &g);
        CHECK(g.parts.keyspace <= 15, "keyspace stays in range at delta %llu", (unsigned long long)d);
        CHECK(g.score >= 0, "score stays non-negative at delta %llu", (unsigned long long)d);
    }
}

/* ------------------------------------------------------------- no key read -- */

static void test_unread(void) {
    printf("Nothing on the contact\n");

    TlsGrade g;
    TlsReading r;

    memset(&r, 0, sizeof(r));
    r.proto = TlsProtoUnread;
    evaluate_dirty(&r, &g);
    CHECK(!g.scored, "an unread key is not scored");
    CHECK_EQ_INT(g.band, TlsBandUnread, "unread band");
    CHECK_EQ_INT(g.clone, TlsCloneUnknown, "unread clone class");
    CHECK_EQ_STR(g.letter, "-", "unread letter");
    CHECK(g.finding_num > 0, "unread must still explain itself");
    check_strings_sane(&g, "unread");

    /* A protocol with no bytes is the same situation. */
    memset(&r, 0, sizeof(r));
    r.proto = TlsProtoDallas;
    r.data_len = 0;
    evaluate_dirty(&r, &g);
    CHECK_EQ_INT(g.band, TlsBandUnread, "a Dallas read with no bytes is unread");

    /* Talos must never call a failed read safe. */
    CHECK(strstr(g.verdict, "not a grade") != NULL, "a failed read must say it is not a grade");

    /* Garbage in must not crash or produce a grade. */
    memset(&r, 0xFF, sizeof(r));
    r.proto = (TlsProto)999;
    r.data_len = 200;
    evaluate_dirty(&r, &g);
    CHECK(g.finding_num <= TLS_MAX_FINDINGS, "garbage input stays in bounds");
    check_strings_sane(&g, "garbage");

    /* A NULL reading is a programming error, not a crash. */
    evaluate_dirty(NULL, &g);
    CHECK_EQ_INT(g.band, TlsBandUnread, "NULL reading reads as unread");
    tls_grade_evaluate(&r, NULL); /* must simply return */
}

/* ------------------------------------------------------ label bounds & UI --- */

static void test_labels(void) {
    printf("Labels and the space they have to fit\n");

    for(int b = 0; b < TlsBandCount; b++) {
        const char* label = tls_band_label((TlsBand)b);
        CHECK(label[0] != '\0', "band %d has no label", b);
        /* The band bar is 128 px of FontSecondary; "REPLAYABLE" is the longest
         * that fits with the letter beside it. */
        CHECK(strlen(label) <= 10, "band label too wide for the bar: %s", label);
        CHECK(tls_band_blurb((TlsBand)b)[0] != '\0', "band %d has no blurb", b);
    }

    for(int c = 0; c < TlsCloneCount; c++) {
        const char* shortl = tls_clone_short((TlsCloneClass)c);
        CHECK(shortl[0] != '\0', "clone %d has no short label", c);
        /* The result screen's clone column is 74 px: about 13 characters of
         * FontSecondary. Pinned because it is invisible until it overlaps. */
        CHECK(strlen(shortl) <= 13, "clone short label too wide: %s", shortl);
        CHECK(tls_clone_time((TlsCloneClass)c)[0] != '\0', "clone %d has no time", c);
        CHECK(tls_clone_label((TlsCloneClass)c)[0] != '\0', "clone %d has no full label", c);
    }

    for(int c = 0; c < TlsClassCount; c++) {
        CHECK(tls_class_label((TlsClass)c)[0] != '\0', "class %d has no label", c);
    }
    for(int p = 0; p < TlsProtoCount; p++) {
        CHECK(tls_proto_label((TlsProto)p)[0] != '\0', "proto %d has no label", p);
    }

    CHECK_EQ_STR(tls_severity_glyph(TlsFindCritical), "[x]", "critical glyph");
    CHECK_EQ_STR(tls_severity_glyph(TlsFindWarn), "[!]", "warn glyph");
    CHECK_EQ_STR(tls_severity_glyph(TlsFindGood), "[+]", "good glyph");
    CHECK_EQ_STR(tls_severity_glyph(TlsFindInfo), "[i]", "info glyph");

    /* Out-of-range values must fall back, not read past the array. */
    CHECK(tls_band_label((TlsBand)99)[0] != '\0', "band label guards its range");
    CHECK(tls_clone_short((TlsCloneClass)99)[0] != '\0', "clone label guards its range");
    CHECK(tls_class_label((TlsClass)99)[0] != '\0', "class label guards its range");
    CHECK(tls_proto_label((TlsProto)99)[0] != '\0', "proto label guards its range");
    CHECK(tls_family_part(0x3C) == NULL, "an absent family returns NULL");
    CHECK(tls_family_code_at(99999) == 0, "an out-of-range index returns 0");
}

/* ---------------------------------------------------- the thesis, asserted -- */

static void test_thesis(void) {
    printf("The thesis holds across the table\n");

    for(size_t i = 0; i < EXPECTED_NUM; i++) {
        const Expect* e = &expected[i];
        if(!e->scored) continue;

        TlsReading r = dallas(e->family, 0x0000094FA3ull);
        TlsGrade g;
        evaluate_dirty(&r, &g);

        /* Nothing on 1-Wire earns full marks for authentication: even the SHA-1
         * parts only get partial credit, because the ROM read cannot show that
         * the lock ever issues a challenge. */
        CHECK(g.parts.auth < 45, "family %02X must not score full auth", e->family);

        /* Anything a lock keys off the plain serial is an F. That is the whole
         * argument, so it is pinned rather than assumed. */
        if(g.parts.auth == 0) {
            CHECK_EQ_STR(g.letter, "F", "family %02X has no auth, so it must be F", e->family);
        }

        /* And no key sold as a door key passes. */
        if(g.cls == TlsClassCredential) {
            CHECK(g.score < 35, "family %02X is a door key and must fail", e->family);
            CHECK_EQ_INT(g.clone, TlsCloneInstant, "a door key is instantly copyable");
        }

        /* A part that can prove a secret must never be labelled instantly
         * copyable, and vice versa. */
        if(g.parts.auth >= 20) {
            CHECK_EQ_INT(g.clone, TlsCloneLab, "family %02X authenticates, so a copy is lab work", e->family);
            CHECK_EQ_INT(g.band, TlsBandChallenged, "family %02X should be CHALLENGED", e->family);
        }
    }
}

/* -------------------------------------------------------------------- main -- */

int main(void) {
    printf("\nTalos grading engine\n====================\n\n");

    test_crc();
    test_letters();
    test_families();
    test_family_coverage();
    test_intercom();
    test_unlisted();
    test_crc_failure();
    test_suspect();
    test_neighbour();
    test_unread();
    test_labels();
    test_thesis();

    printf("\n%u checks, %u failures\n\n", checks, failures);
    if(failures) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}
