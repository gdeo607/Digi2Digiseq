/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DIGISEQ for Digitakt II (digiseq-dt2): sequencer types a track can play in
 * place of its own trigs, for the Digitakt II (OS 1.17). Ported from Robbie's
 * DIGISEQ for Digitakt (Digitakt mk1, OS 1.53/1.54): 0.2 first, then 0.8's
 * ARP type (DT2 0.3); it replaces DIGISNAKE-DT2, whose snake is its SNAKE
 * type. Every track picks its own TYPE, on all 16 tracks (audio: a note is
 * the sample's pitch; MIDI tracks too).
 *
 *   SNAKE  a Cartesian sequencer after MDD Snake (and Make Noise's Rene):
 *          DIGISNAKE, unchanged.
 *   185    a stage sequencer after ML-185 (Nico Starke's free Max for Live
 *          device, itself after RYK's M-185): eight stages, each with a pitch,
 *          a velocity, a length in clock pulses (1-8) and a mode: OFF, ONE,
 *          ALL (every DIV pulses) or HOLD (one note for the whole stage).
 *   ARP    an arpeggiator played by the track's own note trigs (0.3).
 *
 * The page opens after the TRIG page's own pages (press TRIG until they come
 * round). The page is drawn over the screen and takes the keys and knobs
 * while it is open; TRIG, another page key, or a menu over the pages closes
 * it. Nothing is saved with the project; PRINT (0.5, on core 2.6's pattern
 * service) keeps a sequence in the track's trigs. Not on the Digitakt II yet:
 * the 2X clock (the mk1's per-track speed), ARP from the keyboard, live
 * recording and the stock drop-down messages.
 *
 * The Digitakt II side is DIGISNAKE-DT2's: two sites at the sequencer's step
 * fires (audio and MIDI) give a step the type's trig, note, velocity and
 * length; the page, keys and drawing use OS 1.17's screen routines.
 */
#include <stdint.h>

/* ---- the Digitakt II, OS 1.17 (measured in digikit's emulator, 2026-10-25) ----
 * The pattern: 16 tracks of 1701 bytes from the active pattern's start; a
 * track's step words big-endian u16 at +0 (bit 0 the trig, bit 2 off), the
 * step-order table at +1444/+1445, its NOTE at +1157, VEL +1152, LEN +1153.
 * Note, velocity and length locks live in 6-byte records at +27216. The kit's
 * word at +21806 has bit t set for a MIDI track. The sequencer fires a step
 * through 0x400d6982 (audio) and 0x400d6e88 (MIDI): (track, pattern, ?, step,
 * ...) -> the trig event, or 0. Its note is the event's +38, velocity +39,
 * length +40 when the record index at +32 is -1 (MIDI: in the note record at
 * the event's +24, the index at its +24). */
#define PATTERN     (*(uint8_t *const volatile *)0x4316501cu)   /* the active pattern */
#define KIT         (*(uint8_t *const volatile *)0x43165028u)   /* the active kit: +21806 its MIDI tracks */
#define TRANSPORT   (*(volatile uint32_t *)0x43164f94u)           /* 0 stopped, 1 playing */
#define UI_TRACK    (*(volatile uint8_t *)0x42936825u)            /* the selected track, 0-15 */
#define TRK_STRIDE  1701
#define TR_ORDER_N  1444
#define TR_ORDER    1445
#define TR_MODE     1176
#define TR_VEL      1152
#define TR_LEN      1153
#define TR_NOTE     1157
#define PAT_MIDI    21806
#define PAT_LOCKS   27216
#define SQ_TRACKS   16
#define SQ_STEPS    128
#define SQ_TRIG     1u
#define SQ_AUDIO    16          /* the mk1's audio tracks; here every track (ARP's keyboard: not on the DT2 yet) */
#define SQ_NOTES    4           /* a trig's notes: the first, then a MIDI chord's offsets */

static unsigned rd16(const uint8_t *a) { return (unsigned)(a[0] << 8 | a[1]); }
static void wr16(uint8_t *a, unsigned v) { a[0] = (uint8_t)(v >> 8); a[1] = (uint8_t)v; }
static int is_midi(const uint8_t *kit, int t) { return kit && ((rd16(kit + PAT_MIDI) >> t) & 1); }

#define TRACKS    SQ_TRACKS     /* 16 tracks, each audio or MIDI */
#define CELLS     8
#define NOTE_MAX  24

/* Short messages: the stock drop-down (0.6), as the Digitone's DIGISEQ shows
 * them. A small printf of %s, %d and %c into msgbuf, then the firmware's
 * drop-down routine (DSQ_POPUP: f(obj, fmt, ...), the one the pages call with
 * "%s=%s"), whose object is the shown page view's +108 (measured in digiemu on
 * OS 1.53: the TRIG page's and the master view's alike). With no view to ask,
 * the message shows on the page's bottom line instead. UI task only. */
static char msgbuf[40];
static unsigned long frames;               /* UI frames: the random numbers' seed */
static unsigned char msg_ticks, redraw, msg_stock;    /* msg_stock: the drop-down has it */
static void sayf(const char *fmt, ...)
{
    __builtin_va_list ap;
    char *o = msgbuf, *end = msgbuf + sizeof msgbuf - 1;
    __builtin_va_start(ap, fmt);
    for (; *fmt && o < end; fmt++) {
        if (*fmt != '%') {
            *o++ = *fmt;
            continue;
        }
        fmt++;
        if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (s && *s && o < end)
                *o++ = *s++;
        } else if (*fmt == 'c') {
            *o++ = (char)__builtin_va_arg(ap, int);
        } else if (*fmt == 'd') {
            int v = __builtin_va_arg(ap, int);
            char tmp[12];
            int n = 0;
            unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
            if (v < 0)
                *o++ = '-';
            do {
                tmp[n++] = (char)('0' + u % 10);
                u /= 10;
            } while (u && n < 11);
            while (n && o < end)
                *o++ = tmp[--n];
        } else if (*fmt) {
            *o++ = *fmt;
        } else {
            break;
        }
    }
    *o = 0;
    __builtin_va_end(ap);
    msg_stock = 0;
    msg_ticks = 60;
    redraw = 1;
}
#define POPUP(...) sayf(__VA_ARGS__)

enum { E_SNAKE = 1, E_185 = 2, E_ARP = 3 };
#define NTYPES 4
static const char *const type_names[NTYPES] = { "OFF", "SNAKE", "185", "ARP" };

/* ==== the project's data ================================================ */

/* SNAKE (DIGISNAKE's) */
enum { P_ON, P_SHAPE, P_DIR, P_LEN, P_DIV, P_SCALE, P_RANGE, P_SHIFT, NSET };
struct snake {
    signed char note[CELLS];    /* semitones from NOTE */
    unsigned char vel[CELLS];   /* 1-127 */
    unsigned char set[NSET];    /* the PATH page; set[P_ON] mirrors TYPE SNAKE */
    unsigned char gate;         /* bit c: cell c fires */
    unsigned char pad[7];       /* pad[0]: 2X's speed to put back + 1 */
};                              /* 32 bytes */
struct snake digiseq_snake[TRACKS];

/* 185 and the track's TYPE */
enum { M_OFF, M_ONE, M_ALL, M_HOLD, MODES };
static const char *const mode_names[MODES] = { "OFF", "ONE", "ALL", "HOLD" };
enum { S_TYPE, S_DIR, S_STAGES, S_CLOCK, S_SCALE, S_RANGE, S_NLEN, S_SHIFT, NSETS };
struct stages {
    signed char pitch[CELLS];   /* semitones from NOTE */
    unsigned char vel[CELLS];   /* 1-127 */
    unsigned char pulses[CELLS];/* 1-8 clock pulses */
    unsigned char mode[CELLS];  /* M_ */
    unsigned char div[CELLS];   /* ALL: a note every div pulses, 1-4 */
    unsigned char set[NSETS];   /* SETUP; set[S_TYPE] unused (struct track's) */
};                              /* 48 bytes */
struct track {
    unsigned char on;           /* TYPE is not OFF */
    unsigned char eng;          /* E_SNAKE or E_185: the type (kept while OFF) */
    unsigned char spd;          /* 185 2X: the speed to put back + 1 */
    unsigned char pad0;
    struct stages st;
    unsigned char pad[12];
};                              /* 64 bytes */
struct track digiseq_trk[TRACKS];

/* ARP: the ARP page's settings and the offset sequence */
#define OFFS     16
#define OFF_MAX  24
enum { A_TYPE, A_MODE, A_SPD, A_RNG, A_SCALE, A_RPT, A_WALK, A_HOLD, NASETS };   /* SCALE: 0.7, in N.LEN's place */
struct arpset {
    signed char off[OFFS];      /* semitones (slices) added, a step an arp note */
    unsigned char set[NASETS];  /* set[A_TYPE] unused (struct track's); WALK + WALK_MAX */
    unsigned char len;          /* the offsets played, 1-16 */
    unsigned char pad[7];
};                              /* 32 bytes */
struct arpset digiseq_arp[TRACKS];

/* ==== shared tables ===================================================== */

#define SHAPES 7
static const unsigned char shapes[SHAPES][CELLS] = {
    {0, 1, 2, 3, 4, 5, 6, 7},   /* ROWS   left to right, top then bottom */
    {0, 1, 2, 3, 7, 6, 5, 4},   /* SNAKE  along the top, back along the bottom */
    {0, 4, 1, 5, 2, 6, 3, 7},   /* COLS   down each column */
    {0, 4, 5, 1, 2, 6, 7, 3},   /* ZIGZAG down, across, up, across */
    {0, 5, 2, 7, 4, 1, 6, 3},   /* DIAG   diagonal hops */
    {0, 3, 7, 4, 1, 2, 6, 5},   /* CORNER the corners, then the middle */
    {0, 6, 1, 7, 2, 4, 3, 5},   /* HOP    two across and down */
};
static const char *const shape_names[SHAPES] = {
    "ROWS", "SNAKE", "COLS", "ZIGZAG", "DIAG", "CORNER", "HOP"
};

enum { D_FWD, D_BWD, D_FB, D_BF, D_RND, DIRS };
static const char *const dir_names[DIRS] = { "FWD", "BWD", "F/B", "B/F", "RND" };

#define SCALES 12
static const unsigned short scale_mask[SCALES] = {
    0xfff, 0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x9ad, 0x295, 0x4a9, 0x4e9, 0x555,
};
static const char *const scale_names[SCALES] = {
    "CHR", "MAJ", "MIN", "DOR", "PHR", "LYD", "MIX", "HMIN", "PENT", "mPNT", "BLUE", "WHOL"
};

/* SNAKE's PATH page */
static const unsigned char set_min[NSET] = { 0, 0, 0, 1, 1, 0, 1, 0 };
static const unsigned char set_max[NSET] = { 1, SHAPES - 1, DIRS - 1, CELLS, 8, SCALES - 1, NOTE_MAX, CELLS - 1 };
static const char *const set_labels[NSET] = { "TYPE", "SHAPE", "DIR", "LEN", "DIV", "SCALE", "RANGE", "SHIFT" };

/* 185's SETUP page */
#define NLENS 5
static const unsigned char sset_min[NSETS] = { 0, 0, 1, 1, 0, 1, 0, 0 };
static const unsigned char sset_max[NSETS] = { 0, DIRS - 1, CELLS, 8, SCALES - 1, NOTE_MAX, NLENS - 1, CELLS - 1 };
static const char *const sset_labels[NSETS] = { "TYPE", "DIR", "STAGES", "CLOCK", "SCALE", "RANGE", "LEN", "SHIFT" };
static const char *const nlen_names[NLENS] = { "TRK", "1/32", "1/16", "1/8", "1/4" };

/* ARP's page. MODE: UP, DOWN, UP-DN (the ends once), UP+DN (the ends twice),
 * CONV (outside in), DIV (inside out), PINKY (each note, then the top),
 * THUMB (the bottom, then each note), ORDER (as played), RAND, SHUF (each
 * note once a cycle, in a new order). SPD: steps a note (no faster than the
 * sequencer's step: 1/16). */
enum { AM_UP, AM_DOWN, AM_UPDN, AM_UPDN2, AM_CONV, AM_DIV, AM_PINKY, AM_THUMB, AM_ORDER,
       AM_RAND, AM_SHUF, AMODES };
static const char *const amode_names[AMODES] = {
    "UP", "DOWN", "UP-DN", "UP+DN", "CONV", "DIV", "PINKY", "THUMB", "ORDER", "RAND", "SHUF"
};
#define SPDS 6
static const unsigned char spd_steps[SPDS] = { 1, 2, 3, 4, 8, 16 };
static const char *const spd_names[SPDS] = { "1/16", "1/8", "3/16", "1/4", "1/2", "1/1" };
#define WALK_MAX 12
#define WALK_WRAP 48                        /* past +-48 the walk starts again from 0 */
static const unsigned char aset_min[NASETS] = { 0, 0, 0, 1, 0, 1, 0, 0 };
static const unsigned char aset_max[NASETS] = { 0, AMODES - 1, SPDS - 1, 4, SCALES - 1, 4, 2 * WALK_MAX, 1 };
static const char *const aset_labels[NASETS] = { "TYPE", "MODE", "SPD", "RNG", "SCALE", "RPT", "WALK", "HOLD" };

/* DIV (SNAKE) and CLOCK (185): sequencer steps a move (1 = 1X, 2 = 1/2, 4 =
 * 1/4, 8 = 1/8), or 0: 2X, the track itself at twice its speed (per-track
 * SCALE mode) and a move every step. */
#define DIVS 4                      /* 2X (the mk1's per-track speed) is not on the Digitakt II yet */
static const unsigned char div_list[DIVS] = { 8, 4, 2, 1 };
static const char *div_name(int v)
{
    switch (v) {
    case 0: return "2X";
    case 1: return "1X";
    case 2: return "1/2";
    case 4: return "1/4";
    case 8: return "1/8";
    default: return "1/?";
    }
}
static int div_turn(int v, long d)
{
    int i, best = 3;
    for (i = 0; i < DIVS; i++)
        if (div_list[i] == v)
            best = i;
    best += (int)d;
    if (best < 0) best = 0;
    if (best > DIVS - 1) best = DIVS - 1;
    return div_list[best];
}
static int steps_of(int div)                /* sequencer steps a move */
{
    return div < 1 ? 1 : div;
}

/* ---- LEN: the note lengths, as the TRIG page's LEN index ------
 * Measured on the Digitone's TRIG page (emulated OS 1.44; the Digitakt's
 * LEN takes the same values, checked in digiemu on 1.53), in steps: index 14 is 1/16
 * (one step), 30 1/8, 46 1/4, 62 1/2, 78 16 steps, 94 32, 110 64; below each
 * of those the index moves in 1/16, 1/8, 1/4, 1/2, 1 and 2 steps. h: half
 * steps (1-128), the index for that length (or the nearest below). */
static int len_for_halves(int h)
{
    if (h < 1) h = 1;
    if (h > 128) h = 128;
    if (h <= 4)  return 8 * h - 2;          /* to 2 steps: 1/16 of a step an index */
    if (h <= 8)  return 30 + (h - 4) * 4;   /* to 4: 1/8 */
    if (h <= 16) return 46 + (h - 8) * 2;   /* to 8: 1/4 */
    if (h <= 32) return 62 + (h - 16);      /* to 16: 1/2 */
    if (h <= 64) return 78 + (h - 32) / 2;  /* to 32: 1 */
    return 94 + (h - 64) / 4;               /* to 64: 2 */
}

/* ==== defaults and checks ================================================ */

static void snake_defaults(struct snake *s)
{
    static const signed char demo[CELLS] = { 0, 3, 7, 10, 12, 10, 7, 3 };
    int c;
    for (c = 0; c < CELLS; c++) {
        s->note[c] = demo[c];
        s->vel[c] = 100;
    }
    s->gate = 0xff;
    s->set[P_ON] = 0;
    s->set[P_SHAPE] = 1;
    s->set[P_DIR] = D_FWD;
    s->set[P_LEN] = CELLS;
    s->set[P_DIV] = 1;
    s->set[P_SCALE] = 0;
    s->set[P_RANGE] = 12;
    s->set[P_SHIFT] = 0;
    for (c = 0; c < 7; c++)
        s->pad[c] = 0;
}

static void snake_check(struct snake *s)
{
    int c;
    for (c = 0; c < CELLS; c++) {
        if (s->note[c] > NOTE_MAX) s->note[c] = NOTE_MAX;
        else if (s->note[c] < -NOTE_MAX) s->note[c] = -NOTE_MAX;
        if (s->vel[c] < 1) s->vel[c] = 1;
        else if (s->vel[c] > 127) s->vel[c] = 127;
    }
    for (c = 0; c < NSET; c++)
        if (s->set[c] < set_min[c] || s->set[c] > set_max[c])
            s->set[c] = c == P_DIV ? 1 : c == P_RANGE ? 12 : set_min[c];
    if (s->set[P_LEN] < 1)
        s->set[P_LEN] = CELLS;
}

/* A demo line: a rising figure with a held note and a ratchet */
static void stages_defaults(struct stages *g)
{
    static const signed char demo_p[CELLS] = { 0, 7, 12, 10, 3, 5, 7, 0 };
    static const unsigned char demo_l[CELLS] = { 1, 1, 2, 1, 4, 1, 2, 1 };
    static const unsigned char demo_m[CELLS] = { M_ONE, M_ONE, M_ALL, M_ONE, M_HOLD, M_OFF, M_ONE, M_ONE };
    int c;
    for (c = 0; c < CELLS; c++) {
        g->pitch[c] = demo_p[c];
        g->vel[c] = 100;
        g->pulses[c] = demo_l[c];
        g->mode[c] = demo_m[c];
        g->div[c] = 1;
    }
    g->set[S_TYPE] = 0;
    g->set[S_DIR] = D_FWD;
    g->set[S_STAGES] = CELLS;
    g->set[S_CLOCK] = 1;
    g->set[S_SCALE] = 0;
    g->set[S_RANGE] = 12;
    g->set[S_NLEN] = 0;
    g->set[S_SHIFT] = 0;
}

static void stages_check(struct stages *g)
{
    int c;
    for (c = 0; c < CELLS; c++) {
        if (g->pitch[c] > NOTE_MAX) g->pitch[c] = NOTE_MAX;
        else if (g->pitch[c] < -NOTE_MAX) g->pitch[c] = -NOTE_MAX;
        if (g->vel[c] < 1) g->vel[c] = 1;
        else if (g->vel[c] > 127) g->vel[c] = 127;
        if (g->pulses[c] < 1 || g->pulses[c] > 8) g->pulses[c] = 1;
        if (g->mode[c] >= MODES) g->mode[c] = M_ONE;
        if (g->div[c] < 1 || g->div[c] > 4) g->div[c] = 1;
    }
    for (c = 0; c < NSETS; c++)
        if (g->set[c] < sset_min[c] || g->set[c] > sset_max[c])
            g->set[c] = c == S_CLOCK ? 1 : c == S_RANGE ? 12 : c == S_STAGES ? CELLS : sset_min[c];
}

static void arp_defaults(struct arpset *a)
{
    int c;
    for (c = 0; c < OFFS; c++)
        a->off[c] = 0;
    a->set[A_TYPE] = 0;
    a->set[A_MODE] = AM_UP;
    a->set[A_SPD] = 0;                      /* 1/16 */
    a->set[A_RNG] = 2;
    a->set[A_SCALE] = 0;                    /* CHR: no scale */
    a->set[A_RPT] = 1;
    a->set[A_WALK] = WALK_MAX;              /* 0 */
    a->set[A_HOLD] = 0;
    a->len = OFFS;
    for (c = 0; c < 7; c++)
        a->pad[c] = 0;
}

static void track_defaults(struct track *k)
{
    int c;
    k->on = 0;
    k->eng = E_SNAKE;
    k->spd = 0;
    k->pad0 = 0;
    stages_defaults(&k->st);
    for (c = 0; c < 12; c++)
        k->pad[c] = 0;
}

/* ==== state shared by the UI and the fires ================================ */

static unsigned char data_ready;
static volatile unsigned char reset_req[TRACKS];
/* Starting from STOP (DIGISNAKE 1.2): the sequencer prepares step 1 while
 * stopped (a fire with the transport at 0): that fire starts the sequence
 * (prep), and the first fire after PLAY is then step 2. Without a prepare
 * (rewind), the first fire after PLAY starts it. */
static volatile unsigned char prep[TRACKS], rewind[TRACKS];

/* DIRECT JUMP's count of pattern starts on the mk1; none on the Digitakt II yet */
static const uint32_t dj_starts = 0;
static uint32_t dj_seen[TRACKS];

static void changed(void)
{
}

/* A track's whole state, for TRIG + NO, FUNC + NO and PRINT's undo */
struct whole {
    struct snake s;
    struct track k;
    struct arpset a;
};
static struct whole hold_copy[TRACKS], run_copy[TRACKS];
static unsigned char hold_valid[TRACKS], run_valid[TRACKS], run_active[TRACKS];

/* a byte copy: struct assignments would call memcpy, which a mod has not */
static void copy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
}

static void get_whole(int t, struct whole *w)
{
    copy(&w->s, &digiseq_snake[t], sizeof w->s);
    copy(&w->k, &digiseq_trk[t], sizeof w->k);
    copy(&w->a, &digiseq_arp[t], sizeof w->a);
}
static void put_whole(int t, const struct whole *w)
{
    copy(&digiseq_snake[t], &w->s, sizeof w->s);
    copy(&digiseq_trk[t], &w->k, sizeof w->k);
    copy(&digiseq_arp[t], &w->a, sizeof w->a);
    digiseq_snake[t].set[P_ON] = (unsigned char)(w->k.on && w->k.eng == E_SNAKE);
}

static void put_together(void)              /* the first UI frame: every track's defaults */
{
    int t;
    for (t = 0; t < TRACKS; t++) {
        snake_defaults(&digiseq_snake[t]);
        track_defaults(&digiseq_trk[t]);
        arp_defaults(&digiseq_arp[t]);
        reset_req[t] = 1;
    }
    data_ready = 1;
}

/* ==== playback (interrupt level, in the sequencer's step fire) ============ */

static unsigned long irq_rand = 0x2545f491UL;
static unsigned long xorshift(unsigned long *s)
{
    unsigned long x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x2545f491UL;
    return *s;
}

static int eng_on(int32_t track, int eng)
{
    return data_ready && (uint32_t)track < TRACKS && digiseq_trk[track].on && digiseq_trk[track].eng == eng;
}

/* A walk over len places, by a direction: SNAKE's path, 185's stages */
struct walk {
    unsigned char k;            /* the place, 0..len-1 */
    signed char pp;             /* ping-pong's way */
};

static void walk_on(struct walk *w, int dir, int len, unsigned long *rnd)
{
    int k = w->k;
    switch (dir) {
    case D_BWD:
        k = k > 0 ? k - 1 : len - 1;
        break;
    case D_FB:
    case D_BF:
        if (len < 2) {
            k = 0;
            break;
        }
        if (w->pp >= 0) {
            if (k + 1 >= len) { w->pp = -1; k = len - 2; } else k++;
        } else {
            if (k <= 0) { w->pp = 1; k = 1; } else k--;
        }
        break;
    case D_RND:
        k = (int)(xorshift(rnd) % (unsigned long)len);
        break;
    default:
        k = k + 1 < len ? k + 1 : 0;
        break;
    }
    if (k >= len)
        k = 0;
    w->k = (unsigned char)k;
}

static void walk_start(struct walk *w, int dir, int len)
{
    if (dir == D_BWD || dir == D_BF) {
        w->k = (unsigned char)(len - 1);
        w->pp = -1;
    } else {
        w->k = 0;
        w->pp = 1;
    }
}

/* ---- SNAKE ---- */
struct play {
    struct walk w;
    unsigned char count;        /* sequencer steps since the last move */
    unsigned char cell;         /* the cell the step plays */
    unsigned char moved;        /* this step is a snake step (DIV: the others rest) */
};
static struct play play[TRACKS];
volatile unsigned char digiseq_cell[TRACKS];        /* for the screen */

static int snake_len(const struct snake *s)
{
    int len = s->set[P_LEN];
    return len < 1 || len > CELLS ? CELLS : len;
}

static int cell_of(const struct snake *s, int k, int len)
{
    int i = (k + s->set[P_SHIFT]) % len;
    return shapes[s->set[P_SHAPE] % SHAPES][i];
}

static void snake_start(struct play *p, const struct snake *s, int len)
{
    p->count = 0;
    p->moved = 1;
    walk_start(&p->w, s->set[P_DIR], len);
}

static void snake_advance(struct play *p, const struct snake *s, int len, unsigned long *rnd)
{
    if (++p->count >= s->set[P_DIV]) {
        p->count = 0;
        p->moved = 1;
        walk_on(&p->w, s->set[P_DIR], len, rnd);
    } else {
        p->moved = 0;
    }
}

/* ---- 185 ---- */
struct play185 {
    struct walk w;              /* the stage, as the k-th of STAGES */
    unsigned char pulse;        /* the pulse within the stage */
    unsigned char count;        /* sequencer steps since the last pulse */
    unsigned char moved;        /* this step is a clock pulse */
    unsigned char stage;        /* the stage the step plays, 0-7 */
    unsigned char fire;         /* the step fires */
    unsigned char hold;         /* a HOLD's first pulse: its length in steps, else 0 */
};
static struct play185 p185[TRACKS];
volatile unsigned char digiseq_stage[TRACKS], digiseq_pulse[TRACKS];

static int stages_n(const struct stages *g)
{
    int n = g->set[S_STAGES];
    return n < 1 || n > CELLS ? CELLS : n;
}

static int stage_of(const struct stages *g, int k)
{
    return (k + g->set[S_SHIFT]) % CELLS;
}

static void st_decide(struct play185 *p, const struct stages *g)
{
    int s = stage_of(g, p->w.k), m = g->mode[s], d = g->div[s];
    p->stage = (unsigned char)s;
    p->hold = 0;
    if (!p->moved || m == M_OFF)
        p->fire = 0;
    else if (m == M_ALL)
        p->fire = (unsigned char)(p->pulse % (d < 1 ? 1 : d) == 0);
    else
        p->fire = (unsigned char)(p->pulse == 0);
    if (p->fire && m == M_HOLD)
        p->hold = (unsigned char)(g->pulses[s] * steps_of(g->set[S_CLOCK]));
}

static void st_start(struct play185 *p, const struct stages *g)
{
    p->count = 0;
    p->pulse = 0;
    p->moved = 1;
    walk_start(&p->w, g->set[S_DIR], stages_n(g));
    st_decide(p, g);
}

static void st_advance(struct play185 *p, const struct stages *g, unsigned long *rnd)
{
    int n = stages_n(g);
    if (++p->count >= steps_of(g->set[S_CLOCK])) {
        p->count = 0;
        p->moved = 1;
        if (p->w.k >= n)
            p->w.k = 0;
        if (++p->pulse >= g->pulses[stage_of(g, p->w.k)]) {
            p->pulse = 0;
            walk_on(&p->w, g->set[S_DIR], n, rnd);
        }
    } else {
        p->moved = 0;
    }
    if (p->w.k >= n)
        p->w.k = 0;
    st_decide(p, g);
}

/* ---- the step clock: start, catch up, advance (both types) ---- */
static void eng_start(int t)
{
    if (digiseq_trk[t].eng == E_185)
        st_start(&p185[t], &digiseq_trk[t].st);
    else
        snake_start(&play[t], &digiseq_snake[t], snake_len(&digiseq_snake[t]));
}

static void eng_advance(int t, unsigned long *rnd)
{
    if (digiseq_trk[t].eng == E_185)
        st_advance(&p185[t], &digiseq_trk[t].st, rnd);
    else
        snake_advance(&play[t], &digiseq_snake[t], snake_len(&digiseq_snake[t]), rnd);
}

/* ---- ARP ----
 * The track's own note trigs play it. A note trig is a key going down: its
 * note is held for the trig's LEN (the step's LEN lock, else the track's;
 * INF, or HOLD on: until the next note trig) and the arp plays a note every
 * SPD steps from the trig's step. The trig itself plays the first. Whether a
 * trig starts the arp is the trig's own business: the arp starts only when
 * the stock fire makes the trig's event (its condition passes). Every arp
 * note, the trig's included, is the track's LEN long.
 * On the Digitakt II for now: the trig's note is the track's NOTE unless the
 * step has no locks (then the stock fire's note); a MIDI track's chord plays
 * its first note; the keyboard (0.4) and live recording (0.5, 0.8) are the
 * mk1's only. */
#define A_INF       0xffffu
#define MAXHELD     8                       /* notes held at once (keys) */
#define MAXARP      (MAXHELD * 4)           /* times 4 octaves */

/* The keyboard (0.4): the keys held on an audio track in keyboard mode (FUNC +
 * TRK), as kbsites.s hands them over (UI task). A key's note is held while it
 * is (HOLD: until a key goes down with none held, which starts a new set).
 * While they hold notes they play the arp, in place of the track's trigs. */
struct live {
    signed char note[MAXHELD];  /* as played */
    unsigned char key[MAXHELD]; /* the trig key (0-15) each came from */
    volatile unsigned char n;   /* notes held */
    unsigned char vel;
    unsigned short down;        /* the keys down now, bit k: key k */
};
static struct live live[SQ_AUDIO];

struct parp {
    unsigned char active;       /* a note is held */
    unsigned char live;         /* the keyboard plays it (struct live) */
    unsigned char thru;         /* live recording, no key held: the step plays as stored */
    unsigned char srcp;         /* this step has a note trig (it may start the arp) */
    unsigned char capvel;       /* the trig's velocity is the arp's: take it */
    unsigned char fire;         /* this step fires */
    unsigned char nheld;        /* the notes held, 1-SQ_NOTES */
    unsigned char vel;
    unsigned char p;            /* where the arp is in MODE's cycle */
    unsigned char rep;          /* RPT: the plays of this note so far */
    unsigned char oidx;         /* the offset step */
    signed char walk;           /* WALK so far */
    unsigned short elapsed;     /* steps since the trig */
    unsigned short units;       /* how long it is held, in 1/16 steps (A_INF: on) */
    signed char held[MAXHELD];  /* the notes, low to high */
    signed char order[MAXHELD]; /* as played */
    unsigned char perm[MAXARP]; /* SHUF's order this cycle */
};
static struct parp parp[TRACKS];
volatile unsigned char digiseq_aoff[TRACKS];        /* for the screen: the offset step */

static int aclamp(int n)
{
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

/* LEN, as the TRIG page's index, in 1/16 steps (see len_for_halves); 127 is INF */
static unsigned units_for_len(int l)
{
    if (l < 0 || l >= 127) return A_INF;
    if (l <= 30)  return (unsigned)(l + 2);
    if (l <= 46)  return 32u + (unsigned)(l - 30) * 2u;
    if (l <= 62)  return 64u + (unsigned)(l - 46) * 4u;
    if (l <= 78)  return 128u + (unsigned)(l - 62) * 8u;
    if (l <= 94)  return 256u + (unsigned)(l - 78) * 16u;
    if (l <= 110) return 512u + (unsigned)(l - 94) * 32u;
    return 1024u + (unsigned)(l - 110) * 64u;
}


static void arp_reset(struct parp *a)
{
    a->active = a->live = a->thru = a->srcp = a->capvel = a->fire = 0;
}

/* ---- live recording (0.5, 0.8): the mk1's only (rec_live answers 0 here) ---- */

static int rec_live(void)
{
    return 0;                               /* live recording: not on the Digitakt II yet */
}

/* The notes held from a list, as played: held low to high */
static void arp_hold(struct parp *a, const signed char *notes, int n)
{
    int i, j;
    signed char x;
    if (n > MAXHELD)
        n = MAXHELD;
    for (i = 0; i < n; i++)
        a->order[i] = a->held[i] = notes[i];
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && a->held[j - 1] > a->held[j]; j--) {
            x = a->held[j];
            a->held[j] = a->held[j - 1];
            a->held[j - 1] = x;
        }
    a->nheld = (unsigned char)n;
}

static int arp_period(int mode, int m)
{
    switch (mode) {
    case AM_UPDN:  return m < 2 ? 1 : 2 * m - 2;
    case AM_UPDN2: return 2 * m;
    case AM_PINKY:
    case AM_THUMB: return m < 2 ? 1 : 2 * (m - 1);
    default:       return m;
    }
}

static int conv_index(int p, int m)
{
    int k = p / 2;
    return (p & 1) ? m - 1 - k : k;
}

static int arp_index(struct parp *a, int mode, int p, int m)
{
    switch (mode) {
    case AM_DOWN:  return m - 1 - p;
    case AM_UPDN:  return p < m ? p : 2 * m - 2 - p;
    case AM_UPDN2: return p < m ? p : 2 * m - 1 - p;
    case AM_CONV:  return conv_index(p, m);
    case AM_DIV:   return conv_index(m - 1 - p, m);
    case AM_PINKY: return m < 2 ? 0 : (p & 1) ? m - 1 : p / 2;
    case AM_THUMB: return m < 2 ? 0 : (p & 1) ? p / 2 + 1 : 0;
    case AM_RAND:  return (int)(xorshift(&irq_rand) % (unsigned long)m);
    case AM_SHUF:  return a->perm[p % MAXARP];
    default:       return p;                /* UP, ORDER */
    }
}

static void arp_shuffle(struct parp *a, int m)
{
    int i, j;
    unsigned char x;
    for (i = 0; i < m; i++)
        a->perm[i] = (unsigned char)i;
    for (i = m - 1; i > 0; i--) {
        j = (int)(xorshift(&irq_rand) % (unsigned long)(i + 1));
        x = a->perm[i];
        a->perm[i] = a->perm[j];
        a->perm[j] = x;
    }
}

/* A note trig that plays: the arp starts with its notes */
static void arp_start(int t, unsigned units, const int8_t notes[SQ_NOTES])
{
    struct parp *a = &parp[t];
    signed char got[SQ_NOTES];
    int n = 0, i, v;
    if (notes[0] < 0)
        return;
    got[n++] = notes[0];
    if (t >= SQ_AUDIO)                      /* a MIDI track's chord */
        for (i = 1; i < SQ_NOTES; i++)
            if (notes[i] && (v = notes[0] + notes[i]) >= 0 && v <= 127)
                got[n++] = (signed char)v;
    arp_hold(a, got, n);
    a->active = 1;
    a->capvel = 1;
    a->elapsed = 0;
    a->units = (unsigned short)units;
    a->p = a->rep = a->oidx = 0;
    a->walk = 0;
}

/* The arp's next note */
static int arp_next(int t)
{
    struct parp *a = &parp[t];
    const struct arpset *s = &digiseq_arp[t];
    int n = a->nheld, rng = s->set[A_RNG], mode = s->set[A_MODE], rpt = s->set[A_RPT];
    int m, period, idx, len = s->len, note;
    const signed char *list;
    if (n < 1 || n > MAXHELD)
        return -1;
    if (rng < 1 || rng > 4) rng = 1;
    if (mode >= AMODES) mode = AM_UP;
    if (rpt < 1 || rpt > 4) rpt = 1;
    if (len < 1 || len > OFFS) len = OFFS;
    m = n * rng;
    period = arp_period(mode, m);
    if (a->p >= period)
        a->p = 0;
    if (mode == AM_SHUF && a->p == 0 && a->rep == 0)
        arp_shuffle(a, m);
    idx = arp_index(a, mode, a->p, m);
    if (idx < 0 || idx >= m)
        idx = 0;
    if (a->oidx >= len)
        a->oidx = 0;
    if (s->set[A_WALK] == WALK_MAX)
        a->walk = 0;                        /* WALK 0: back where it started */
    list = mode == AM_ORDER ? a->order : a->held;
    note = list[idx % n] + 12 * (idx / n) + s->off[a->oidx] + a->walk;
    digiseq_aoff[t] = a->oidx;
    if (++a->rep >= rpt) {
        a->rep = 0;
        a->oidx = (unsigned char)((a->oidx + 1) % len);
        if (++a->p >= period) {
            int w = a->walk + (s->set[A_WALK] - WALK_MAX);
            a->p = 0;
            a->walk = (signed char)(w > WALK_WRAP || w < -WALK_WRAP ? 0 : w);
        }
    }
    return aclamp(note);
}

static void arp_trig(int32_t track, int32_t first, uint32_t *trig)
{
    struct parp *a = &parp[track];
    if (!first && a->thru)
        return;
    if (first) {
        const struct arpset *s = &digiseq_arp[track];
        int spd = spd_steps[s->set[A_SPD] < SPDS ? s->set[A_SPD] : 0];
        if (TRANSPORT == 0) {               /* stopped: the first step, from nothing */
            arp_reset(a);
            prep[track] = 1;
            rewind[track] = 0;
        } else if (prep[track]) {
            prep[track] = rewind[track] = 0;
        } else if (rewind[track]) {
            rewind[track] = 0;
            arp_reset(a);
        }
        reset_req[track] = 0;
        a->thru = 0;
        if (track < SQ_AUDIO && live[track].n) {    /* the keyboard plays it */
            if (!a->live) {                 /* from its first note, on this step */
                a->live = a->active = 1;
                a->capvel = 0;
                a->elapsed = 0;
                a->p = a->rep = a->oidx = 0;
                a->walk = 0;
            } else if (a->elapsed < 0xfffe) {
                a->elapsed++;
            }
            a->srcp = 0;                    /* the track's trigs wait */
            a->fire = (unsigned char)(a->elapsed % spd == 0);
            goto out;
        }
        if (a->live)                        /* the keys let go */
            a->live = a->active = 0;
        a->thru = (unsigned char)rec_live();
        if (a->thru) {                      /* recording, no key: the trigs as stored */
            a->active = a->srcp = a->fire = 0;
            return;
        }
        if (a->active) {
            if (a->elapsed < 0xfffe)
                a->elapsed++;
            if (!s->set[A_HOLD] && a->units != A_INF && (unsigned long)a->elapsed * 16 >= a->units)
                a->active = 0;              /* the note is let go */
        }
        a->srcp = (unsigned char)((*trig & SQ_TRIG) != 0);
        a->fire = (unsigned char)(a->srcp || (a->active && a->elapsed % spd == 0));
    }
out:
    if (a->fire)
        *trig |= SQ_TRIG;
    else
        *trig &= ~SQ_TRIG;
}

/* Both reads of a step (first 1, then 0) must answer the same: the first
 * moves the sequence, the second reuses where it is. */
static void digiseq_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    int fire;
    if (!data_ready || (uint32_t)track >= TRACKS || !digiseq_trk[track].on)
        return;
    if (digiseq_trk[track].eng == E_ARP) {
        arp_trig(track, first, trig);
        return;
    }
    if (first) {
        if (TRANSPORT == 0) {            /* stopped: prepare the first step */
            prep[track] = 1;
            rewind[track] = reset_req[track] = 0;
            dj_seen[track] = dj_starts;
            eng_start(track);
        } else if (prep[track] || rewind[track]) {
            int n;
            prep[track] = rewind[track] = reset_req[track] = 0;
            dj_seen[track] = dj_starts;
            eng_start(track);
            for (n = 0; n < step && n < SQ_STEPS; n++)
                eng_advance(track, &irq_rand);
        } else if (reset_req[track] || dj_starts != dj_seen[track]) {
            reset_req[track] = 0;
            dj_seen[track] = dj_starts;     /* a pattern start: from the beginning */
            eng_start(track);
        } else {
            eng_advance(track, &irq_rand);
        }
        if (digiseq_trk[track].eng == E_185) {
            digiseq_stage[track] = p185[track].stage;
            digiseq_pulse[track] = p185[track].pulse;
        } else {
            struct play *p = &play[track];
            const struct snake *s = &digiseq_snake[track];
            int len = snake_len(s);
            if (p->w.k >= len)
                p->w.k = 0;
            p->cell = (unsigned char)cell_of(s, p->w.k, len);
            digiseq_cell[track] = p->cell;
        }
    }
    if (digiseq_trk[track].eng == E_185) {
        fire = p185[track].fire;
    } else {
        const struct play *p = &play[track];
        fire = p->moved && ((digiseq_snake[track].gate >> p->cell) & 1);
    }
    if (fire)
        *trig |= SQ_TRIG;
    else
        *trig &= ~SQ_TRIG;
}

static int quantise(int note, int root, int scale)
{
    unsigned short m;
    int i, deg;
    if (scale <= 0 || scale >= SCALES)
        return note;
    m = scale_mask[scale];
    for (i = 0; i < 12; i++) {              /* down to the nearest scale tone */
        deg = (note - root - i) % 12;
        if (deg < 0)
            deg += 12;
        if ((m >> deg) & 1)
            return note - i;
    }
    return note;
}

static int clamp_note(int n)
{
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

/* The track's NOTE and the scale's root: the scale is rooted on the track's NOTE. */
static int track_base(int32_t track, int *root)
{
    uint8_t *pat = PATTERN;
    int base = pat ? pat[TRK_STRIDE * track + TR_NOTE] : 60;
    if (base > 127)
        base = 60;
    *root = base % 12;
    return base;
}

static int seq_note(int32_t track, int base, int root)
{
    int n;
    if (digiseq_trk[track].eng == E_185) {
        const struct stages *g = &digiseq_trk[track].st;
        n = quantise(base + g->pitch[p185[track].stage], root, g->set[S_SCALE]);
    } else {
        const struct snake *s = &digiseq_snake[track];
        n = quantise(base + s->note[play[track].cell], root, s->set[P_SCALE]);
    }
    return clamp_note(n);
}

static int seq_vel(int32_t track)
{
    if (digiseq_trk[track].eng == E_185)
        return digiseq_trk[track].st.vel[p185[track].stage];
    return digiseq_snake[track].vel[play[track].cell];
}

/* 185: a HOLD's note lasts its stage; LEN (SETUP) sets the others' (TRK: the
 * track's LEN). SNAKE: the step's length lock, else the track's LEN. */
static const unsigned char nlen_halves[NLENS] = { 0, 1, 2, 4, 8 };
static int seq_len(int32_t track, int len)
{
    const struct play185 *p;
    const struct stages *g;
    if (digiseq_trk[track].eng != E_185)
        return len;
    p = &p185[track];
    g = &digiseq_trk[track].st;
    if (p->hold)
        return len_for_halves(2 * p->hold);
    if (g->set[S_NLEN] > 0 && g->set[S_NLEN] < NLENS)
        return len_for_halves(nlen_halves[g->set[S_NLEN]]);
    return -1;                              /* TRK: the track's LEN */
}

/* ---- the sequencer's step fire (sites at its two fire routines) ----------
 * dsq_audio / dsq_midi take the fire's place (fire.s jumps here, with the
 * fire's own arguments): before it, the step's word gets the type's trig or
 * loses its own; after it, the word is put back and the event gets the
 * type's note, velocity and length. A step the type fires plays whatever
 * trig condition a stored trig there had. Runs at the sequencer's interrupt
 * level. */
typedef uint32_t (*fire_fn)(int32_t, uint8_t *, uint32_t, int32_t, uint32_t, uint32_t, uint32_t);
static int32_t last_step[TRACKS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static uint32_t last_transport;
/* for checking (emulator, debugger): a magic, then the fires seen, the
 * types' trigs, their rests, and the last trig's track, step, note, velocity */
volatile uint32_t digiseq_stats[8] = { 0x44535132u };

static uint32_t fire(fire_fn orig, int midi, int32_t t, uint8_t *pat, uint32_t a2, int32_t step,
                     uint32_t a4, uint32_t a5, uint32_t a6)
{
    uint8_t *trk, *w;
    unsigned old;
    int idx, n, go, first;
    uint32_t ev, trig = 0;
    if (!data_ready || (uint32_t)t >= TRACKS || !digiseq_trk[t].on || !pat || pat != PATTERN
        || step < 0 || step >= SQ_STEPS)
        return orig(t, pat, a2, step, a4, a5, a6);
    trk = pat + TRK_STRIDE * t;
    if (trk[TR_MODE] == 1)                  /* the alternate step order: left alone */
        return orig(t, pat, a2, step, a4, a5, a6);
    idx = step;
    if ((n = trk[TR_ORDER_N]) != 0) {
        idx = (int)(int16_t)rd16(trk + TR_ORDER + 2 * (step % n));
        if (idx < 0 || idx >= SQ_STEPS)
            return orig(t, pat, a2, step, a4, a5, a6);
    }
    if (TRANSPORT != last_transport) {
        last_transport = TRANSPORT;
        for (n = 0; n < TRACKS; n++)
            last_step[n] = -1;
    }
    first = last_step[t] != step || TRANSPORT == 0;
    last_step[t] = step;
    w = trk + 2 * idx;
    old = rd16(w);
    if (old & 1u)                           /* a stored trig: ARP's note trigs start it */
        trig = SQ_TRIG;
    digiseq_trig(t, step, first, &trig);
    go = (trig & SQ_TRIG) != 0;
    digiseq_stats[1]++;
    digiseq_stats[go ? 2 : 3]++;
    wr16(w, go ? ((old | 1u) & ~4u) : (old & ~1u));
    ev = orig(t, pat, a2, step, a4, a5, a6);
    wr16(w, old);
    if (ev && go) {
        int root, base = trk[TR_NOTE], len = trk[TR_LEN], l;
        uint8_t *e = (uint8_t *)ev;
        int32_t *rec = (int32_t *)(e + 32);
        if (midi) {
            e = (uint8_t *)*(uint32_t *)(ev + 24);      /* the MIDI note record */
            if (!e)
                return ev;
            rec = (int32_t *)(e + 24);
        }
        if (*rec >= 0 && digiseq_trk[t].eng != E_185) {     /* SNAKE, ARP: keep a length lock */
            uint8_t *r = pat + PAT_LOCKS + 6 * *rec;
            if (r[4] != 0xff)
                len = r[4];
        }
        if (digiseq_trk[t].eng == E_ARP) {
            struct parp *a = &parp[t];
            int note;
            if (a->srcp) {                  /* the note trig plays: the arp starts with its note */
                int8_t notes[SQ_NOTES] = { 0, 0, 0, 0 };
                int vel = trk[TR_VEL];
                notes[0] = (int8_t)(*rec < 0 && e[38] <= 127 ? e[38] : (base > 127 ? 60 : base));
                if (*rec >= 0) {            /* the trig's record: +2 NOTE lock, +3 VEL lock (0xff none) */
                    const uint8_t *r = pat + PAT_LOCKS + 6 * *rec;
                    if (r[2] <= 127)
                        notes[0] = (int8_t)r[2];
                    if (r[3] >= 1 && r[3] <= 127)
                        vel = r[3];
                } else if (*rec < 0 && e[39] >= 1 && e[39] <= 127)
                    vel = e[39];
                a->srcp = 0;
                arp_start(t, units_for_len(len), notes);
                a->vel = (unsigned char)vel;
                a->capvel = 0;
            }
            if (!a->active || (note = arp_next(t)) < 0)
                return ev;
            {                               /* SCALE, rooted on the track's NOTE */
                int sc = digiseq_arp[t].set[A_SCALE];
                if (sc > 0 && sc < SCALES)
                    note = clamp_note(quantise(note, (base > 127 ? 60 : base) % 12, sc));
            }
            *rec = -1;
            e[38] = (uint8_t)note;
            e[39] = a->vel ? a->vel : 100;
            e[40] = (uint8_t)trk[TR_LEN];   /* every arp note is the track's LEN long */
            e[41] = 0;
            digiseq_stats[4] = (uint32_t)t;
            digiseq_stats[5] = (uint32_t)step;
            digiseq_stats[6] = e[38];
            digiseq_stats[7] = e[39];
            return ev;
        }
        if (base > 127)
            base = 60;
        root = base % 12;
        l = seq_len(t, len);
        *rec = -1;
        e[38] = (uint8_t)seq_note(t, base, root);
        e[39] = (uint8_t)seq_vel(t);
        e[40] = (uint8_t)(l < 0 ? len : l);
        e[41] = 0;
        digiseq_stats[4] = (uint32_t)t;
        digiseq_stats[5] = (uint32_t)step;
        digiseq_stats[6] = e[38];
        digiseq_stats[7] = e[39];
    }
    return ev;
}

extern uint32_t dsq_orig_audio(int32_t, uint8_t *, uint32_t, int32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t dsq_orig_midi(int32_t, uint8_t *, uint32_t, int32_t, uint32_t, uint32_t, uint32_t);

uint32_t dsq_audio(int32_t t, uint8_t *pat, uint32_t a2, int32_t step, uint32_t a4, uint32_t a5, uint32_t a6)
{
    return fire(dsq_orig_audio, 0, t, pat, a2, step, a4, a5, a6);
}

uint32_t dsq_midi(int32_t t, uint8_t *pat, uint32_t a2, int32_t step, uint32_t a4, uint32_t a5, uint32_t a6)
{
    return fire(dsq_orig_midi, 1, t, pat, a2, step, a4, a5, a6);
}

/* ==== not on the Digitakt II yet: 2X (the per-track speed) ================ */
static void speed_sync(void) { }
static int per_track_mode(void) { return 0; }

/* ==== PRINT (TRIG + FUNC + REC) ==========================================
 * Plays the track's type through the track's length, as it would from the
 * start (or, playing, from where it is), and writes each step as a stock
 * trig, then sets TYPE to OFF. It edits the pattern through core 2.6's
 * pattern service (the firmware's own editors, as the panel does). A step
 * the type fires becomes a note trig: placed if it was empty, its SYN trig
 * on, its first note's NOTE lock the note, every note's VEL lock the
 * velocity and micro timing 0 (on the step, as it played), no trig
 * condition (track +640, +768 and +896 back to 255, as on a new trig), and
 * (185's held and LEN-set notes) its LEN lock; its parameter locks stay. A
 * rest removes a note trig (with its locks, as the panel does) and keeps a
 * lock trig (SYN off). TRIG + NO puts the track back as it was. */
#include "core_pattern.h"
static void set_type(int t, int ty);
#define TR_DEF_TRIGS  1154                 /* the track's default trig types (word) */
#define TR_COND_A     640                  /* a step's trig condition: three bytes, 255 none */
#define TR_COND_B     768
#define TR_COND_C     896
#define W_TRIG        0x0001
#define W_SYN         0x0080
#define W_SYN_SET     0x0800
#define PLK_MAX       512
struct pstep {                              /* a step before PRINT */
    uint16_t w;
    unsigned char nn, cond[3];
    struct core_note n[4];
};
struct plk { unsigned char step, index; uint16_t v; };
static struct pstep print_bak[SQ_STEPS];
static struct plk print_plk[PLK_MAX];
static int print_nplk, print_len;
static signed char print_t = -1;
static uint8_t *print_pat;

static int word_syn(const uint8_t *trk, unsigned w)
{
    return (w & W_SYN_SET) ? (w & W_SYN) != 0 : (rd16(trk + TR_DEF_TRIGS) & W_SYN) != 0;
}

static void wr_word(uint8_t *trk, int step, unsigned w)
{
    trk[2 * step] = (uint8_t)(w >> 8);
    trk[2 * step + 1] = (uint8_t)w;
}

static void print_backup(int t, const uint8_t *trk, int len)
{
    int s, i, v;
    print_nplk = 0;
    for (s = 0; s < len; s++) {
        struct pstep *b = &print_bak[s];
        b->w = rd16(trk + 2 * s);
        b->nn = (unsigned char)((b->w & W_TRIG) ? core_trig_notes(t, s, b->n, 4) : 0);
        b->cond[0] = trk[TR_COND_A + s];
        b->cond[1] = trk[TR_COND_B + s];
        b->cond[2] = trk[TR_COND_C + s];
        if ((b->w & W_TRIG) && core_plock_count(t, s))
            for (i = 0; i <= 70 && print_nplk < PLK_MAX; i++)
                if ((v = core_plock_get(t, s, i)) >= 0) {
                    print_plk[print_nplk].step = (unsigned char)s;
                    print_plk[print_nplk].index = (unsigned char)i;
                    print_plk[print_nplk].v = (uint16_t)v;
                    print_nplk++;
                }
    }
}

static int print_track(int t)
{
    uint8_t *pat = PATTERN, *trk;
    struct track *k = &digiseq_trk[t];
    unsigned long rnd = 0x9e3779b9UL ^ frames;
    int len, base, root, step, i, first, playing, pending, no;
    struct play qs;
    struct play185 q8;
    const struct snake *s = &digiseq_snake[t];
    const struct stages *g = &k->st;
    int slen = snake_len(s);
    if (!pat || k->eng == E_ARP || (len = core_track_len(t)) < 1 || len > SQ_STEPS)
        return -1;
    trk = pat + TRK_STRIDE * t;
    if (trk[TR_MODE] == 1)                  /* the alternate step order: the type does not play it */
        return -1;
    base = trk[TR_NOTE];
    if (base > 127)
        base = 60;
    root = base % 12;
    print_backup(t, trk, len);
    print_t = (signed char)t;
    print_pat = pat;
    print_len = len;
    copy(&qs, &play[t], sizeof qs);
    copy(&q8, &p185[t], sizeof q8);
    playing = TRANSPORT == 1 && k->on && last_step[t] >= 0 && last_step[t] < len;
    pending = !playing || reset_req[t] || prep[t] || rewind[t] || dj_starts != dj_seen[t];
    first = playing ? ((int)last_step[t] + 1) % len : 0;
    no = trk[TR_ORDER_N];
    for (i = 0; i < len; i++) {
        int fire, n, vel, l = -1, pos = (first + i) % len, c, j;
        unsigned w;
        struct core_note nt[4];
        if (k->eng == E_185) {
            if (i == 0 && pending)
                st_start(&q8, g);
            else
                st_advance(&q8, g, &rnd);
            fire = q8.fire;
            n = quantise(base + g->pitch[q8.stage], root, g->set[S_SCALE]);
            vel = g->vel[q8.stage];
            if (q8.hold)
                l = len_for_halves(2 * q8.hold);
            else if (g->set[S_NLEN] > 0 && g->set[S_NLEN] < NLENS)
                l = len_for_halves(nlen_halves[g->set[S_NLEN]]);
        } else {
            int cell;
            if (i == 0 && pending)
                snake_start(&qs, s, slen);
            else
                snake_advance(&qs, s, slen, &rnd);
            if (qs.w.k >= slen)
                qs.w.k = 0;
            cell = cell_of(s, qs.w.k, slen);
            fire = qs.moved && ((s->gate >> cell) & 1);
            n = quantise(base + s->note[cell], root, s->set[P_SCALE]);
            vel = s->vel[cell];
        }
        step = pos;                         /* the step the sequencer plays at this position */
        if (no) {
            step = (int)(int16_t)rd16(trk + TR_ORDER + 2 * (pos % no));
            if (step < 0 || step >= SQ_STEPS)
                continue;
        }
        w = rd16(trk + 2 * step);
        if (fire) {
            if (!(w & W_TRIG) && core_trig_set(t, step, 1) < 0)
                continue;
            if (!word_syn(trk, rd16(trk + 2 * step)))
                core_step_flags(t, step, W_SYN_SET | W_SYN, 1);   /* a note trig, whatever the default */
            if ((c = core_trig_notes(t, step, nt, 4)) < 1) {
                c = 4;
                for (j = 0; j < 4; j++)
                    nt[j].note = nt[j].vel = nt[j].len = 0xff;
            }
            nt[0].note = (uint8_t)clamp_note(n);
            for (j = 0; j < c; j++) {
                nt[j].vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
                nt[j].micro = 0;
                if (k->eng == E_185)
                    nt[j].len = (uint8_t)l;     /* -1 (255): the track's LEN */
            }
            core_trig_set_notes(t, step, nt, c);
            trk[TR_COND_A + step] = trk[TR_COND_B + step] = trk[TR_COND_C + step] = 0xff;
        } else if ((w & W_TRIG) && word_syn(trk, w)) {
            core_trig_set(t, step, 0);      /* a rest: no note trig (lock trigs stay) */
        }
    }
    set_type(t, 0);
    return len;
}

/* TRIG + NO after PRINT: the track's steps as they were */
static void unprint(int t)
{
    uint8_t *trk;
    int s, i;
    if (print_t != t || !print_pat || print_pat != PATTERN)
        return;
    trk = print_pat + TRK_STRIDE * t;
    for (s = 0; s < print_len; s++) {
        const struct pstep *b = &print_bak[s];
        unsigned w = rd16(trk + 2 * s);
        if (b->w & W_TRIG) {
            if (!(w & W_TRIG))
                core_trig_set(t, s, 1);
            if (b->nn)
                core_trig_set_notes(t, s, b->n, b->nn);
            wr_word(trk, s, b->w);          /* its trig types as they were */
        } else if (w & W_TRIG) {
            core_trig_set(t, s, 0);
            wr_word(trk, s, b->w);
        }
        trk[TR_COND_A + s] = b->cond[0];
        trk[TR_COND_B + s] = b->cond[1];
        trk[TR_COND_C + s] = b->cond[2];
    }
    for (i = 0; i < print_nplk; i++)
        if (core_plock_get(t, print_plk[i].step, print_plk[i].index) != print_plk[i].v)
            core_plock_set(t, print_plk[i].step, print_plk[i].index, print_plk[i].v);
    print_t = -1;
    reset_req[t] = 1;
}

/* ==== the page ===========================================================
 * The key dispatcher's brain (each key event's first argument) keeps the
 * screens shown: +0x54 the bottom one (the page views), +0x5c the top one
 * (another while a menu or a browser is open over the pages). The bottom
 * one's +8 is the page manager, whose +0xbc is the view shown (OS 1.17); a
 * view's +136 is its page key, +0x90 the page's own page shown (TRIG 1/2: 0). */
#define BR_BOTTOM 0x54
#define BR_TOP    0x5c
#define SCR_PM    8
#define PM_VIEW   0xbc
#define VIEW_KEY  136
#define VIEW_SUB  0x90

#define KEY_TRIG  1             /* the Digitakt II's key codes (its panel's own) */
#define KEY_MOD   6
#define KEY_YES   10
#define KEY_UP    11
#define KEY_NO    12
#define KEY_LEFT  13
#define KEY_DOWN  14
#define KEY_RIGHT 15
#define KEY_TRK   16
#define KEY_FUNC  17
#define KEY_REC   19
#define KEY_PLAY  20
#define KEY_STOP  21
#define KEY_STEP1 25
#define EV_KEY(ev)   (*(unsigned long *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(unsigned long *)((char *)(ev) + 16))
#define EV_ENC(ev)   (*(unsigned long *)((char *)(ev) + 12))
#define EV_DELTA(ev) (*(long *)((char *)(ev) + 16))
#define F_DOWN    1
#define F_FUNC    0x100   /* not seen on the Digitakt II: FUNC is followed by its key */
#define F_REPEAT  8
#define F_UP      0x10

static uint32_t rd(uint32_t a)
{
    return a >= 0x40000000u && a < 0x48000000u ? *(volatile uint32_t *)a : 0;
}

/* the view on screen with nothing over it, or 0 */
static uint32_t page_view(void *brain)
{
    uint32_t b = (uint32_t)brain, bottom, pm;
    if (!b || !(bottom = rd(b + BR_BOTTOM)) || bottom != rd(b + BR_TOP))
        return 0;
    if (!(pm = rd(bottom + SCR_PM)))
        return 0;
    return rd(pm + PM_VIEW);
}

/* the page key whose view is on screen with nothing over it, or 0 */
static uint32_t page_view_key(void *brain)
{
    uint32_t view = page_view(brain);
    return view ? rd(view + VIEW_KEY) : 0;
}

void *digiseq_brain;

/* pages: SNAKE's and 185's, numbered within the type */
enum { PG_NOTES, PG_GATES, PG_VEL, PG_PATH, SNAKE_PAGES };
enum { PG8_PITCH, PG8_VEL, PG8_PULSES, PG8_MODE, PG8_DIV, PG8_SETUP, PAGES_185 };
static const char *const snake_pages[SNAKE_PAGES] = { "NOTES", "GATES", "VELOCITY", "PATH" };
static const char *const pages185[PAGES_185] = { "PITCH", "VELOCITY", "PULSES", "MODE", "DIV", "SETUP" };
static const char *const snake_what[SNAKE_PAGES] = { "Notes", "Gates", "Velocity", "Path" };
static const char *const what185[PAGES_185] = { "Pitch", "Velocity", "Pulses", "Modes", "Divisions", "Setup" };
enum { PGA_OFFS, PGA_ARP, ARP_PAGES };
static const char *const arp_pages[ARP_PAGES] = { "OFFSETS", "ARP" };
static const char *const arp_what[ARP_PAGES] = { "Offsets", "Arp" };

unsigned char digiseq_open;                         /* the page is up */
unsigned char digiseq_pg_snake, digiseq_pg_185;     /* the page shown, per type */
unsigned char digiseq_pg_arp;
static unsigned char arp_sel[TRACKS];               /* OFFSETS: the step selected (its half on knobs) */
void *digiseq_brain;
static unsigned char trig_held, trig_used, stock_held, stock_used, stock_check;
static unsigned char func_held, trk_held;
static long enc_acc[8];
#define COUNTS    4
static unsigned long ui_rand = 0x9e3779b9UL;

static int ui_track(void)
{
    uint32_t t = UI_TRACK;
    return t < TRACKS ? (int)t : -1;
}

static int eng_of(int t)
{
    int e = digiseq_trk[t].eng;
    return e == E_185 || e == E_ARP ? e : E_SNAKE;
}

static unsigned char *pg_of(int eng)
{
    return eng == E_185 ? &digiseq_pg_185 : eng == E_ARP ? &digiseq_pg_arp : &digiseq_pg_snake;
}

static int pages_of(int eng)
{
    return eng == E_185 ? PAGES_185 : eng == E_ARP ? ARP_PAGES : SNAKE_PAGES;
}

static int last_page(int eng)
{
    return pages_of(eng) - 1;
}

static int clampi(long v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : (int)v;
}

static int type_of(int t)
{
    return digiseq_trk[t].on ? eng_of(t) : 0;
}

static void set_type(int t, int ty)
{
    struct track *k = &digiseq_trk[t];
    if (ty == 0) {
        k->on = 0;
    } else {
        if (k->eng != ty)                   /* stay on TYPE: the new type's last page */
            *pg_of(ty) = (unsigned char)last_page(ty);
        k->eng = (unsigned char)ty;
        k->on = 1;
    }
    digiseq_snake[t].set[P_ON] = (unsigned char)(k->on && k->eng == E_SNAKE);
    if (k->on && k->eng == E_ARP)
        arp_reset(&parp[t]);                /* nothing held until the next note trig */
    reset_req[t] = 1;
}

/* ---- SNAKE's page edits ---- */
static void snake_randomise(struct snake *s, int pg)
{
    int c, r = s->set[P_RANGE];
    if (r < 1 || r > NOTE_MAX)
        r = 12;
    for (c = 0; c < CELLS; c++) {
        unsigned long x = xorshift(&ui_rand);
        if (pg == PG_NOTES)
            s->note[c] = (signed char)((long)(x % (unsigned long)(2 * r + 1)) - r);
        else if (pg == PG_VEL)
            s->vel[c] = (unsigned char)(40 + x % 88);
        else if (pg == PG_GATES) {
            if ((x >> 16) % 100 < 65)
                s->gate |= (unsigned char)(1u << c);
            else
                s->gate &= (unsigned char)~(1u << c);
        }
    }
    if (pg == PG_GATES && !s->gate)
        s->gate = (unsigned char)(1u << (xorshift(&ui_rand) % CELLS));
    if (pg == PG_PATH) {                    /* everything but TYPE and SCALE */
        static const unsigned char divs[8] = { 1, 1, 1, 1, 1, 2, 2, 4 };
        s->set[P_SHAPE] = (unsigned char)(xorshift(&ui_rand) % SHAPES);
        s->set[P_DIR] = (unsigned char)(xorshift(&ui_rand) % DIRS);
        s->set[P_LEN] = (unsigned char)(3 + xorshift(&ui_rand) % (CELLS - 2));
        s->set[P_DIV] = divs[xorshift(&ui_rand) & 7];
        s->set[P_RANGE] = (unsigned char)(5 + xorshift(&ui_rand) % (NOTE_MAX - 4));
        s->set[P_SHIFT] = (unsigned char)(xorshift(&ui_rand) % CELLS);
    }
}

static void snake_clear(struct snake *s, int pg)
{
    struct snake d;
    int c;
    snake_defaults(&d);
    for (c = 0; c < CELLS; c++) {
        if (pg == PG_NOTES)
            s->note[c] = 0;
        else if (pg == PG_VEL)
            s->vel[c] = d.vel[c];
    }
    if (pg == PG_GATES)
        s->gate = 0xff;
    if (pg == PG_PATH) {
        unsigned char on = s->set[P_ON];
        for (c = 0; c < NSET; c++)
            s->set[c] = d.set[c];
        s->set[P_ON] = on;
    }
}

static void snake_nudge(struct snake *s, int pg, int d, int func)
{
    int c;
    if (pg == PG_NOTES) {
        for (c = 0; c < CELLS; c++)
            s->note[c] = (signed char)clampi(s->note[c] + d * (func ? 12 : 1), -NOTE_MAX, NOTE_MAX);
    } else if (pg == PG_VEL) {
        for (c = 0; c < CELLS; c++)
            s->vel[c] = (unsigned char)clampi(s->vel[c] + d * 8, 1, 127);
    } else if (pg == PG_GATES) {
        unsigned g = s->gate;
        g = d > 0 ? ((g << 1) | (g >> 7)) : ((g >> 1) | (g << 7));
        s->gate = (unsigned char)g;
    }
}

static void snake_paste(struct snake *s, const struct snake *from, int pg)
{
    int c;
    if (pg == PG_NOTES)
        for (c = 0; c < CELLS; c++) s->note[c] = from->note[c];
    else if (pg == PG_VEL)
        for (c = 0; c < CELLS; c++) s->vel[c] = from->vel[c];
    else if (pg == PG_GATES)
        s->gate = from->gate;
    else {
        unsigned char on = s->set[P_ON];
        for (c = 0; c < NSET; c++) s->set[c] = from->set[c];
        s->set[P_ON] = on;
    }
}

/* ---- 185's page edits ---- */
static void st_randomise(struct stages *g, int pg)
{
    int c, r = g->set[S_RANGE];
    if (r < 1 || r > NOTE_MAX)
        r = 12;
    for (c = 0; c < CELLS; c++) {
        unsigned long x = xorshift(&ui_rand);
        switch (pg) {
        case PG8_PITCH:
            g->pitch[c] = (signed char)((long)(x % (unsigned long)(2 * r + 1)) - r);
            break;
        case PG8_VEL:
            g->vel[c] = (unsigned char)(40 + x % 88);
            break;
        case PG8_PULSES: {                  /* mostly short stages */
            static const unsigned char l[16] = { 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 4, 4, 6, 8, 2 };
            g->pulses[c] = l[(x >> 8) & 15];
            break;
        }
        case PG8_MODE: {                    /* ONE most, then HOLD, ALL, OFF */
            unsigned long v = (x >> 12) % 100;
            g->mode[c] = (unsigned char)(v < 45 ? M_ONE : v < 70 ? M_HOLD : v < 85 ? M_ALL : M_OFF);
            break;
        }
        case PG8_DIV: {
            static const unsigned char dv[8] = { 1, 1, 1, 1, 2, 2, 3, 4 };
            g->div[c] = dv[(x >> 4) & 7];
            break;
        }
        default:
            break;
        }
    }
    if (pg == PG8_MODE) {                   /* never all rests */
        for (c = 0; c < CELLS && g->mode[c] == M_OFF; c++)
            ;
        if (c == CELLS)
            g->mode[xorshift(&ui_rand) % CELLS] = M_ONE;
    }
    if (pg == PG8_SETUP) {                  /* DIR, STAGES, RANGE, SHIFT */
        g->set[S_DIR] = (unsigned char)(xorshift(&ui_rand) % DIRS);
        g->set[S_STAGES] = (unsigned char)(3 + xorshift(&ui_rand) % (CELLS - 2));
        g->set[S_RANGE] = (unsigned char)(5 + xorshift(&ui_rand) % (NOTE_MAX - 4));
        g->set[S_SHIFT] = (unsigned char)(xorshift(&ui_rand) % CELLS);
    }
}

static void st_clear(struct stages *g, int pg)
{
    struct stages d;
    int c;
    stages_defaults(&d);
    for (c = 0; c < CELLS; c++) {
        switch (pg) {
        case PG8_PITCH:  g->pitch[c] = 0; break;
        case PG8_VEL:    g->vel[c] = 100; break;
        case PG8_PULSES: g->pulses[c] = 1; break;
        case PG8_MODE:   g->mode[c] = M_ONE; break;
        case PG8_DIV:    g->div[c] = 1; break;
        default: break;
        }
    }
    if (pg == PG8_SETUP)
        for (c = 0; c < NSETS; c++)
            g->set[c] = d.set[c];
}

static void st_nudge(struct stages *g, int pg, int d, int func)
{
    int c;
    unsigned char tmp;
    switch (pg) {
    case PG8_PITCH:
        for (c = 0; c < CELLS; c++)
            g->pitch[c] = (signed char)clampi(g->pitch[c] + d * (func ? 12 : 1), -NOTE_MAX, NOTE_MAX);
        break;
    case PG8_VEL:
        for (c = 0; c < CELLS; c++)
            g->vel[c] = (unsigned char)clampi(g->vel[c] + d * 8, 1, 127);
        break;
    case PG8_PULSES:
        for (c = 0; c < CELLS; c++)
            g->pulses[c] = (unsigned char)clampi(g->pulses[c] + d, 1, 8);
        break;
    case PG8_DIV:
        for (c = 0; c < CELLS; c++)
            g->div[c] = (unsigned char)clampi(g->div[c] + d, 1, 4);
        break;
    case PG8_MODE:                          /* rotate the stages' modes */
        if (d > 0) {
            tmp = g->mode[CELLS - 1];
            for (c = CELLS - 1; c > 0; c--) g->mode[c] = g->mode[c - 1];
            g->mode[0] = tmp;
        } else {
            tmp = g->mode[0];
            for (c = 0; c < CELLS - 1; c++) g->mode[c] = g->mode[c + 1];
            g->mode[CELLS - 1] = tmp;
        }
        break;
    default:
        break;
    }
}

static void st_paste(struct stages *g, const struct stages *from, int pg)
{
    int c;
    for (c = 0; c < CELLS; c++) {
        switch (pg) {
        case PG8_PITCH:  g->pitch[c] = from->pitch[c]; break;
        case PG8_VEL:    g->vel[c] = from->vel[c]; break;
        case PG8_PULSES: g->pulses[c] = from->pulses[c]; break;
        case PG8_MODE:   g->mode[c] = from->mode[c]; break;
        case PG8_DIV:    g->div[c] = from->div[c]; break;
        default: break;
        }
    }
    if (pg == PG8_SETUP)
        for (c = 0; c < NSETS; c++)
            g->set[c] = from->set[c];
}

/* ---- ARP's page edits ---- */
static void arp_randomise(struct arpset *a, int pg)
{
    int c;
    if (pg == PGA_OFFS) {                   /* mostly roots, octaves and fifths */
        static const signed char pick[16] = { 0, 0, 0, 0, 12, 12, -12, 7, 7, 5, 3, 10, -5, 24, 2, -7 };
        for (c = 0; c < OFFS; c++)
            a->off[c] = pick[(xorshift(&ui_rand) >> 7) & 15];
        a->len = (unsigned char)(2 + xorshift(&ui_rand) % (OFFS - 1));
    } else {                                /* MODE, SPD, RNG, RPT, WALK */
        static const unsigned char spd[8] = { 0, 0, 0, 0, 1, 1, 2, 3 };
        static const unsigned char rpt[8] = { 1, 1, 1, 1, 1, 2, 2, 3 };
        static const signed char walk[8] = { 0, 0, 0, 0, 1, -1, 2, 5 };
        a->set[A_MODE] = (unsigned char)(xorshift(&ui_rand) % AMODES);
        a->set[A_SPD] = spd[xorshift(&ui_rand) & 7];
        a->set[A_RNG] = (unsigned char)(1 + xorshift(&ui_rand) % 3);
        a->set[A_RPT] = rpt[xorshift(&ui_rand) & 7];
        a->set[A_WALK] = (unsigned char)(WALK_MAX + walk[xorshift(&ui_rand) & 7]);
    }
}

static void arp_clear(struct arpset *a, int pg)
{
    struct arpset d;
    int c;
    arp_defaults(&d);
    if (pg == PGA_OFFS) {
        for (c = 0; c < OFFS; c++)
            a->off[c] = 0;
        a->len = d.len;
    } else {
        for (c = 1; c < NASETS; c++)
            a->set[c] = d.set[c];
    }
}

static void arp_nudge(struct arpset *a, int pg, int d, int func)
{
    int c;
    if (pg != PGA_OFFS)
        return;
    for (c = 0; c < OFFS; c++)
        a->off[c] = (signed char)clampi(a->off[c] + d * (func ? 12 : 1), -OFF_MAX, OFF_MAX);
}

static void arp_paste(struct arpset *a, const struct arpset *from, int pg)
{
    int c;
    if (pg == PGA_OFFS) {
        for (c = 0; c < OFFS; c++)
            a->off[c] = from->off[c];
        a->len = from->len;
    } else {
        for (c = 1; c < NASETS; c++)
            a->set[c] = from->set[c];
    }
}

/* ---- shared page actions ---- */
static void page_randomise(int t, int pg)
{
    ui_rand ^= frames;
    if (eng_of(t) == E_ARP)
        arp_randomise(&digiseq_arp[t], pg);
    else if (eng_of(t) == E_185)
        st_randomise(&digiseq_trk[t].st, pg);
    else
        snake_randomise(&digiseq_snake[t], pg);
}

static void randomise_all(int t)
{
    ui_rand ^= frames;
    if (eng_of(t) == E_ARP) {               /* the offsets and MODE */
        struct arpset *a = &digiseq_arp[t];
        arp_randomise(a, PGA_OFFS);
        a->set[A_MODE] = (unsigned char)(xorshift(&ui_rand) % AMODES);
    } else if (eng_of(t) == E_185) {
        st_randomise(&digiseq_trk[t].st, PG8_PITCH);
        st_randomise(&digiseq_trk[t].st, PG8_VEL);
        st_randomise(&digiseq_trk[t].st, PG8_PULSES);
        st_randomise(&digiseq_trk[t].st, PG8_MODE);
    } else {
        snake_randomise(&digiseq_snake[t], PG_NOTES);
        snake_randomise(&digiseq_snake[t], PG_GATES);
        snake_randomise(&digiseq_snake[t], PG_VEL);
    }
}

static const char *what_of(int eng, int pg)
{
    return eng == E_185 ? what185[pg % PAGES_185] : eng == E_ARP ? arp_what[pg % ARP_PAGES]
                                                             : snake_what[pg % SNAKE_PAGES];
}

/* COPY / PASTE: one page, from any track to any other of the same type */
static struct whole clip;
static signed char clip_pg = -1;
static unsigned char clip_eng;

static void edited(int t)
{
    run_active[t] = 0;
    changed();
}

static void reload(int t, const struct whole *from, int valid)
{
    if (!valid)
        return;
    put_whole(t, from);
    reset_req[t] = 1;
    changed();
    POPUP("Sequence reloaded");
}

static int next_stop(int v, long d, const signed char *stops, int n)
{
    int i;
    if (d > 0) {
        for (i = 0; i < n; i++)
            if (stops[i] > v)
                return stops[i];
        return stops[n - 1];
    }
    for (i = n - 1; i >= 0; i--)
        if (stops[i] < v)
            return stops[i];
    return stops[0];
}
static const signed char note_stops[5] = { -24, -12, 0, 12, 24 };
static const signed char vel_stops[5] = { 1, 32, 64, 96, 127 };
static const signed char range_stops[3] = { 1, 12, 24 };

static void open_page(void)
{
    digiseq_open = 1;
    trig_held = trig_used = 0;
    redraw = 1;
}

static void close_page(void)
{
    digiseq_open = 0;
    trig_held = 0;
    redraw = 1;
}

/* A key while the page is open: -> 1 to take it. */
static int page_key(int t, unsigned long key, unsigned long flags)
{
    int down = (flags & F_DOWN) && !(flags & F_REPEAT);
    int func = (flags & F_FUNC) || func_held;
    int eng = eng_of(t), pg;
    unsigned char *pgp = pg_of(eng);
    if (*pgp >= pages_of(eng))
        *pgp = 0;
    pg = *pgp;
    redraw = 1;
    if (key == KEY_TRIG && down && func && !trig_held) {   /* FUNC + TRIG: QUANTIZE */
        close_page();
        return 0;
    }
    if (key == KEY_TRIG) {
        if ((flags & F_UP) || !(flags & F_DOWN)) {      /* its release */
            if (trig_held && !trig_used)
                close_page();                           /* back to the TRIG page */
            trig_held = 0;
            return 1;
        }
        if (down) {
            trig_held = 1;
            trig_used = 0;
            print_t = -1;
            get_whole(t, &hold_copy[t]);
            hold_valid[t] = 1;
        }
        return 1;
    }
    if (key > KEY_TRIG && key <= KEY_MOD) {             /* another page key: to its page */
        close_page();
        return 0;
    }
    if ((key == KEY_PLAY || key == KEY_STOP || key == KEY_REC) && !trig_held)
        return 0;                                       /* the transport, as ever */
    if (!down)
        return key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN
            || key == KEY_YES || key == KEY_NO
            || (key >= KEY_STEP1 && key < KEY_STEP1 + CELLS && !trk_held &&
                ((eng == E_SNAKE && pg == PG_GATES) || (eng == E_185 && pg == PG8_MODE)))
            || (key >= KEY_STEP1 && key < KEY_STEP1 + OFFS && !trk_held &&
                eng == E_ARP && pg == PGA_OFFS);

    if (key == KEY_YES && func) {                       /* randomise the note pages */
        trig_used = 1;
        if (!run_active[t]) {
            get_whole(t, &run_copy[t]);
            run_valid[t] = run_active[t] = 1;
        }
        randomise_all(t);
        POPUP("%s randomised", eng == E_185 ? "Stages" : eng == E_ARP ? "Arp" : "Snake");
        return 1;
    }
    if (key == KEY_NO && trig_held) {
        trig_used = 1;
        unprint(t);
        reload(t, &hold_copy[t], hold_valid[t]);
        run_active[t] = 0;
        return 1;
    }
    if (key == KEY_NO && func) {
        trig_used = 1;
        reload(t, &run_copy[t], run_valid[t]);
        run_active[t] = 0;
        return 1;
    }
    if ((key == KEY_UP || key == KEY_DOWN) && trig_held) {     /* TYPE */
        int ty = (type_of(t) + (key == KEY_UP ? 1 : NTYPES - 1)) % NTYPES;
        trig_used = 1;
        set_type(t, ty);
        edited(t);
        POPUP("Type=%s", type_names[ty]);
        return 1;
    }
    if (key == KEY_YES && trig_held) {
        trig_used = 1;
        page_randomise(t, pg);
        if (pg == last_page(eng))
            reset_req[t] = 1;
        edited(t);
        POPUP("%s randomised", what_of(eng, pg));
        return 1;
    }
    if (key == KEY_REC && trig_held && func) {                               /* PRINT */
        int n;
        trig_used = 1;
        if (eng == E_ARP) {                 /* record it live instead */
            POPUP("Print N/A for ARP");
            return 1;
        }
        n = print_track(t);
        if (n < 0) {
            POPUP("No pattern to print to");
        } else {
            reset_req[t] = 1;
            edited(t);
            POPUP("Printed %d steps", n);
        }
        return 1;
    }
    if (key == KEY_REC && trig_held) {                                       /* COPY */
        trig_used = 1;
        get_whole(t, &clip);
        clip_pg = (signed char)pg;
        clip_eng = (unsigned char)eng;
        POPUP("%s copied", what_of(eng, pg));
        return 1;
    }
    if (key == KEY_STOP && trig_held) {                                      /* PASTE */
        trig_used = 1;
        if (clip_pg < 0) {
            POPUP("Nothing to paste");
            return 1;
        }
        if (clip_eng != eng) {
            POPUP("The copy is %s's", type_names[clip_eng]);
            return 1;
        }
        *pgp = (unsigned char)clip_pg;
        if (eng == E_ARP)
            arp_paste(&digiseq_arp[t], &clip.a, clip_pg);
        else if (eng == E_185)
            st_paste(&digiseq_trk[t].st, &clip.k.st, clip_pg);
        else
            snake_paste(&digiseq_snake[t], &clip.s, clip_pg);
        reset_req[t] = 1;
        edited(t);
        POPUP("%s pasted", what_of(eng, clip_pg));
        return 1;
    }
    if (key == KEY_PLAY && trig_held) {                                      /* CLEAR */
        trig_used = 1;
        if (eng == E_ARP)
            arp_clear(&digiseq_arp[t], pg);
        else if (eng == E_185)
            st_clear(&digiseq_trk[t].st, pg);
        else
            snake_clear(&digiseq_snake[t], pg);
        reset_req[t] = 1;
        edited(t);
        POPUP("%s cleared", what_of(eng, pg));
        return 1;
    }
    if (key == KEY_LEFT) {
        *pgp = (unsigned char)(pg ? pg - 1 : pages_of(eng) - 1);
        return 1;
    }
    if (key == KEY_RIGHT) {
        *pgp = (unsigned char)((pg + 1) % pages_of(eng));
        return 1;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        if (eng == E_ARP)
            arp_nudge(&digiseq_arp[t], pg, key == KEY_UP ? 1 : -1, func);
        else if (eng == E_185)
            st_nudge(&digiseq_trk[t].st, pg, key == KEY_UP ? 1 : -1, func);
        else
            snake_nudge(&digiseq_snake[t], pg, key == KEY_UP ? 1 : -1, func);
        edited(t);
        return 1;
    }
    if (!trk_held && eng == E_ARP && pg == PGA_OFFS && key >= KEY_STEP1 && key < KEY_STEP1 + OFFS) {
        int c = (int)(key - KEY_STEP1);
        struct arpset *a = &digiseq_arp[t];
        if (func) {                         /* FUNC + trig key: LEN */
            a->len = (unsigned char)(c + 1);
            edited(t);
            POPUP("Offset Length=%d", c + 1);
        } else {                            /* select: knobs A-H edit its half */
            arp_sel[t] = (unsigned char)c;
            POPUP("Offset %d=%c%d", c + 1, a->off[c] < 0 ? '-' : '+',
                  a->off[c] < 0 ? -a->off[c] : a->off[c]);
        }
        return 1;
    }
    if (!trk_held && key >= KEY_STEP1 && key < KEY_STEP1 + CELLS) {
        int c = (int)(key - KEY_STEP1);
        if (eng == E_SNAKE && pg == PG_GATES) {
            digiseq_snake[t].gate ^= (unsigned char)(1u << c);
            edited(t);
            return 1;
        }
        if (eng == E_185 && pg == PG8_MODE) {
            struct stages *g = &digiseq_trk[t].st;
            g->mode[c] = (unsigned char)((g->mode[c] + 1) % MODES);
            edited(t);
            POPUP("Stage %d=%s", c + 1, mode_names[g->mode[c]]);
            return 1;
        }
    }
    return key == KEY_YES || key == KEY_NO;
}

int digiseq_key(void *brain, void *ev)
{
    unsigned long key = EV_KEY(ev), flags = EV_FLAGS(ev);
    int t, down = (flags & F_DOWN) && !(flags & F_REPEAT);
    digiseq_brain = brain;
    /* FUNC and TRK, followed always, never taken: a release is 0x10, or 0 when
     * another key went down meanwhile */
    if (key == KEY_FUNC) {
        if (!(flags & F_REPEAT))
            func_held = (unsigned char)(flags & F_DOWN);
        return 0;
    }
    if (key == KEY_TRK) {
        if (!(flags & F_REPEAT))
            trk_held = (unsigned char)(flags & F_DOWN);
        return 0;
    }
    if (flags & F_FUNC)
        func_held = 1;
    t = ui_track();
    if (digiseq_open)
        return t < 0 ? (close_page(), 0) : page_key(t, key, flags);
    /* TRIG on the TRIG page is the firmware's: it turns the TRIG page's own
     * pages (1/2, 2/2, ...). When a press brings them back round to the first,
     * the DIGISEQ page opens over it instead (in the tick after TRIG's release),
     * so it sits after the TRIG page's last page. Another key meanwhile: no. */
    if (stock_held) {
        if (key == KEY_TRIG) {
            if ((flags & F_UP) || !(flags & F_DOWN)) {
                stock_held = 0;
                if (!stock_used)
                    stock_check = 1;
            }
            return 0;
        }
        if (flags & F_DOWN)
            stock_used = 1;
        return 0;
    }
    if (key == KEY_TRIG && down && t >= 0 && page_view_key(brain) == KEY_TRIG &&
        !(flags & F_FUNC) && !func_held) {  /* FUNC + TRIG: the stock's QUANTIZE */
        stock_held = 1;
        stock_used = 0;
    }
    return 0;
}

static void snake_enc(int t, struct snake *s, int pg, int c, long d)
{
    switch (pg) {
    case PG_NOTES:
        if (func_held) {
            s->note[c] = (signed char)next_stop(s->note[c], d, note_stops, 5);
            POPUP("Note %c=%c%d", 'A' + c, s->note[c] < 0 ? '-' : '+',
                  s->note[c] < 0 ? -s->note[c] : s->note[c]);
        } else {
            s->note[c] = (signed char)clampi(s->note[c] + d, -NOTE_MAX, NOTE_MAX);
        }
        break;
    case PG_GATES:
        if (d > 0)
            s->gate |= (unsigned char)(1u << c);
        else
            s->gate &= (unsigned char)~(1u << c);
        break;
    case PG_VEL:
        if (func_held) {
            s->vel[c] = (unsigned char)next_stop(s->vel[c], d, vel_stops, 5);
            POPUP("Velocity %c=%d", 'A' + c, s->vel[c]);
        } else {
            s->vel[c] = (unsigned char)clampi(s->vel[c] + d, 1, 127);
        }
        break;
    default:
        if (c == P_ON) {
            int ty = clampi(type_of(t) + d, 0, NTYPES - 1);
            set_type(t, ty);
            POPUP("Type=%s", type_names[ty]);
        } else if (func_held && c == P_RANGE) {
            s->set[c] = (unsigned char)next_stop(s->set[c], d, range_stops, 3);
            POPUP("Range=+-%d", s->set[c]);
        } else if (c == P_DIV) {
            s->set[c] = (unsigned char)div_turn(s->set[c], d);
            if (s->set[c] == 0 && !per_track_mode())
                POPUP("2X needs SCALE per track");
            else
                POPUP("Division=%s", div_name(s->set[c]));
        } else {
            s->set[c] = (unsigned char)clampi(s->set[c] + d, set_min[c], set_max[c]);
        }
        if (c == P_DIR || c == P_LEN || c == P_SHAPE)
            reset_req[t] = 1;
        break;
    }
}

static void st_enc(int t, struct stages *g, int pg, int c, long d)
{
    switch (pg) {
    case PG8_PITCH:
        if (func_held) {
            g->pitch[c] = (signed char)next_stop(g->pitch[c], d, note_stops, 5);
            POPUP("Pitch %d=%c%d", c + 1, g->pitch[c] < 0 ? '-' : '+',
                  g->pitch[c] < 0 ? -g->pitch[c] : g->pitch[c]);
        } else {
            g->pitch[c] = (signed char)clampi(g->pitch[c] + d, -NOTE_MAX, NOTE_MAX);
        }
        break;
    case PG8_VEL:
        if (func_held) {
            g->vel[c] = (unsigned char)next_stop(g->vel[c], d, vel_stops, 5);
            POPUP("Velocity %d=%d", c + 1, g->vel[c]);
        } else {
            g->vel[c] = (unsigned char)clampi(g->vel[c] + d, 1, 127);
        }
        break;
    case PG8_PULSES:
        g->pulses[c] = (unsigned char)clampi(g->pulses[c] + d, 1, 8);
        break;
    case PG8_MODE:
        g->mode[c] = (unsigned char)clampi(g->mode[c] + d, 0, MODES - 1);
        break;
    case PG8_DIV:
        g->div[c] = (unsigned char)clampi(g->div[c] + d, 1, 4);
        break;
    default:
        if (c == S_TYPE) {
            int ty = clampi(type_of(t) + d, 0, NTYPES - 1);
            set_type(t, ty);
            POPUP("Type=%s", type_names[ty]);
        } else if (func_held && c == S_RANGE) {
            g->set[c] = (unsigned char)next_stop(g->set[c], d, range_stops, 3);
            POPUP("Range=+-%d", g->set[c]);
        } else if (c == S_CLOCK) {
            g->set[c] = (unsigned char)div_turn(g->set[c], d);
            if (g->set[c] == 0 && !per_track_mode())
                POPUP("2X needs SCALE per track");
            else
                POPUP("Clock=%s", div_name(g->set[c]));
        } else {
            g->set[c] = (unsigned char)clampi(g->set[c] + d, sset_min[c], sset_max[c]);
        }
        if (c == S_DIR || c == S_STAGES || c == S_SHIFT)
            reset_req[t] = 1;
        break;
    }
}

static void arp_enc(int t, struct arpset *a, int pg, int c, long d)
{
    if (pg == PGA_OFFS) {                   /* knobs A-H: the selected step's half */
        int s = (arp_sel[t] & 8) + c;
        if (func_held)
            a->off[s] = (signed char)next_stop(a->off[s], d, note_stops, 5);
        else
            a->off[s] = (signed char)clampi(a->off[s] + d, -OFF_MAX, OFF_MAX);
        POPUP("Offset %d=%c%d", s + 1, a->off[s] < 0 ? '-' : '+', a->off[s] < 0 ? -a->off[s] : a->off[s]);
        return;
    }
    if (c == A_TYPE) {
        int ty = clampi(type_of(t) + d, 0, NTYPES - 1);
        set_type(t, ty);
        POPUP("Type=%s", type_names[ty]);
        return;
    }
    a->set[c] = (unsigned char)clampi(a->set[c] + d, aset_min[c], aset_max[c]);
    switch (c) {
    case A_MODE: POPUP("Arp Mode=%s", amode_names[a->set[c] % AMODES]); break;
    case A_SPD:  POPUP("Arp Speed=%s", spd_names[a->set[c] % SPDS]); break;
    case A_RNG:  POPUP("Arp Range=%d Oct", a->set[c]); break;
    case A_SCALE: POPUP("Arp Scale=%s", scale_names[a->set[c] % SCALES]); break;
    case A_RPT:  POPUP("Repeats=%dX", a->set[c]); break;
    case A_WALK: {
        int w = a->set[c] - WALK_MAX;
        POPUP("Walk=%c%d", w < 0 ? '-' : '+', w < 0 ? -w : w);
        break;
    }
    default:     POPUP("Arp Hold=%s", a->set[c] ? "ON" : "OFF"); break;
    }
}

int digiseq_enc(void *brain, void *ev)
{
    unsigned long e = EV_ENC(ev);
    long d;
    int t, eng, pg;
    digiseq_brain = brain;
    if (!digiseq_open || e < 1 || e > 8)
        return 0;
    t = ui_track();
    if (t < 0)
        return 0;
    enc_acc[e - 1] += EV_DELTA(ev);
    d = enc_acc[e - 1] / COUNTS;
    enc_acc[e - 1] -= d * COUNTS;
    if (d == 0)
        return 1;
    eng = eng_of(t);
    pg = *pg_of(eng);
    if (pg >= pages_of(eng))
        pg = 0;
    redraw = 1;
    edited(t);
    if (eng == E_ARP)
        arp_enc(t, &digiseq_arp[t], pg, (int)e - 1, d);
    else if (eng == E_185)
        st_enc(t, &digiseq_trk[t].st, pg, (int)e - 1, d);
    else
        snake_enc(t, &digiseq_snake[t], pg, (int)e - 1, d);
    return 1;
}

/* ==== drawing (ev_tick, ev_draw): 128 x 64, y = 0 the bottom row ========= */
typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef int (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);
#define FILLRECT  ((rect_fn)0x401131aeu)        /* Bitmap fillrect, OS 1.17 */
#define FRAMERECT ((rect_fn)0x40112f92u)        /* its outline */
#define TEXTF     ((text_fn)0x40113d84u)        /* textf(bmp, font, x, y, maxlen, fmt, ...) */
#define FONT5     ((const void *)0x402a9be0u)   /* the 5-pixel font */

static const char *const note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* The sequencer prepares each track's first step while stopped (a fire with
 * the transport at 0); the firmware prepares it again after an edit, through
 * 0x400d7244(track, or -1 for all), from the UI task as here. A type changed
 * while stopped prepares its track again, so PLAY starts on the type's step. */
typedef void (*prep_fn)(int32_t track);
#define PREPARE   ((prep_fn)0x400d7244u)
static struct whole prepared[TRACKS];

static int same_whole(const struct whole *a, const struct whole *b)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    unsigned i;
    for (i = 0; i < sizeof *a; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

void digiseq_tick(void *ctrl)
{
    int t;
    frames++;
    if (!data_ready)
        put_together();
    if (TRANSPORT == 0)
        for (t = 0; t < TRACKS; t++) {
            struct whole now;
            rewind[t] = 1;                  /* PLAY starts every sequence again */
            get_whole(t, &now);
            if (!same_whole(&prepared[t], &now)) {
                copy(&prepared[t], &now, sizeof now);
                if (PATTERN)
                    PREPARE(t);
            }
        }
    if (msg_ticks && !--msg_ticks)
        redraw = 1;
    speed_sync();
    if (stock_check) {                      /* TRIG turned the TRIG page round to 1/N */
        uint32_t view = page_view(digiseq_brain);
        stock_check = 0;
        if (view && rd(view + VIEW_KEY) == KEY_TRIG && (rd(view + VIEW_SUB) & 0xff) == 0
            && ui_track() >= 0 && !digiseq_open)
            open_page();
    }
    if (digiseq_open) {
        /* a menu over the pages, or another view (a page key we passed on):
         * the page closes */
        if (page_view_key(digiseq_brain) != KEY_TRIG || ui_track() < 0) {
            close_page();
        } else {
            static int last_pos = -1, last_play = -1, last_t = -1;
            int ut = ui_track(), playing = (int)TRANSPORT;
            int e = eng_of(ut);
            int pos = e == E_185 ? digiseq_stage[ut] * 8 + digiseq_pulse[ut]
                    : e == E_ARP ? digiseq_aoff[ut] + 32 * parp[ut].active : digiseq_cell[ut];
            if (pos != last_pos || playing != last_play || ut != last_t) {
                last_pos = pos;
                last_play = playing;
                last_t = ut;
                redraw = 1;
            }
        }
    }
    if (redraw) {
        redraw = 0;
        *((unsigned char *)ctrl + 0x20) = 1;
    }
}

static int path_order(const struct snake *s, int cell)
{
    int len = snake_len(s), k;
    for (k = 0; k < len; k++)
        if (cell_of(s, k, len) == cell)
            return k + 1;
    return 0;
}

static void draw_note(void *bmp, int x0, int y0, int n, int off)
{
    n = clamp_note(n);
    TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "%s%d", note_names[n % 12], n / 12);
    TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%c%d", off < 0 ? '-' : '+', off < 0 ? -off : off);
}

static void draw_vel(void *bmp, int x0, int y0, int v)
{
    int h = (v * 14) / 127;
    FILLRECT(bmp, x0 + 3, y0 + 4, x0 + 6, y0 + 4 + h, 1);
    TEXTF(bmp, FONT5, x0 + 10, y0 + 8, -1, "%d", v);
}

/* 185: a stage's pulses as a row of ticks under a mode: ONE a tick on the
 * first, ALL on every DIV-th, HOLD a bar across, OFF dots */
static void draw_pulses(void *bmp, int x0, int y0, const struct stages *g, int s, int cur)
{
    int n = g->pulses[s], m = g->mode[s], dv = g->div[s], i;
    int w = 3, x = x0 + 3;                  /* a pulse a slot: the stage's length shows */
    for (i = 0; i < n; i++) {
        int xl = x + i * w, xr = xl + 1;
        int hit = m == M_ALL ? (i % dv == 0) : (m == M_ONE || m == M_HOLD) ? i == 0 : 0;
        if (i == cur)
            FILLRECT(bmp, xl, y0 + 2, xr, y0 + 2, 1);       /* the playing pulse */
        if (m == M_HOLD) {
            FILLRECT(bmp, xl, y0 + 5, xr + (i < n - 1 ? 2 : 0), y0 + 7, 1);
            if (i == 0)
                FILLRECT(bmp, xl, y0 + 5, xl, y0 + 10, 1);
        } else if (hit) {
            FILLRECT(bmp, xl, y0 + 4, xr, y0 + 10, 1);
        } else {
            FILLRECT(bmp, xl, y0 + 5, xl, y0 + 5, 1);
        }
    }
}

static void draw_snake(void *bmp, int t, int base, int root)
{
    const struct snake *s = &digiseq_snake[t];
    int pg = digiseq_pg_snake % SNAKE_PAGES, c;
    int playing = digiseq_trk[t].on && TRANSPORT == 1;
    for (c = 0; c < CELLS; c++) {
        int x0 = 32 * (c & 3), y0 = (c < 4) ? 31 : 8;
        int x1 = x0 + 30, y1 = y0 + 21, order;
        FRAMERECT(bmp, x0, y0, x1, y1, 1);
        if (pg == PG_PATH) {
            int v = s->set[c];
            TEXTF(bmp, FONT5, x0 + 3, y0 + 14, -1, "%s", set_labels[c]);
            switch (c) {
            case P_ON:    TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", type_names[type_of(t)]); break;
            case P_SHAPE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", shape_names[v % SHAPES]); break;
            case P_DIR:   TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", dir_names[v % DIRS]); break;
            case P_SCALE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", scale_names[v % SCALES]); break;
            case P_RANGE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "+-%d", v); break;
            case P_DIV:   TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", div_name(v)); break;
            default:      TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%d", v); break;
            }
            continue;
        }
        order = path_order(s, c);
        if (order)
            TEXTF(bmp, FONT5, x1 - 6, y1 - 6, -1, "%d", order);
        if (playing && digiseq_cell[t] == c) {
            FRAMERECT(bmp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1);
            FRAMERECT(bmp, x0 + 2, y0 + 2, x1 - 2, y1 - 2, 1);
        }
        if (pg == PG_NOTES) {
            draw_note(bmp, x0, y0, quantise(base + s->note[c], root, s->set[P_SCALE]), s->note[c]);
        } else if (pg == PG_GATES) {
            if ((s->gate >> c) & 1)
                FILLRECT(bmp, x0 + 8, y0 + 5, x1 - 10, y1 - 5, 1);
            else
                FRAMERECT(bmp, x0 + 8, y0 + 5, x1 - 10, y1 - 5, 1);
        } else {
            draw_vel(bmp, x0, y0, s->vel[c]);
        }
    }
    TEXTF(bmp, FONT5, 72, 57, -1, "%s %d/%d", snake_pages[pg], pg + 1, SNAKE_PAGES);
}

static int in_sequence(const struct stages *g, int s)
{
    int k;
    for (k = 0; k < stages_n(g); k++)
        if (stage_of(g, k) == s)
            return 1;
    return 0;
}

static int total_steps(const struct stages *g)
{
    int k, n = 0;
    for (k = 0; k < stages_n(g); k++)
        n += g->pulses[stage_of(g, k)];
    return n * steps_of(g->set[S_CLOCK]);
}

static void draw_185(void *bmp, int t, int base, int root)
{
    const struct stages *g = &digiseq_trk[t].st;
    int pg = digiseq_pg_185 % PAGES_185, c;
    int playing = digiseq_trk[t].on && TRANSPORT == 1;
    for (c = 0; c < CELLS; c++) {
        int x0 = 32 * (c & 3), y0 = (c < 4) ? 31 : 8;
        int x1 = x0 + 30, y1 = y0 + 21;
        int cur = playing && digiseq_stage[t] == c;
        if (pg == PG8_SETUP) {
            int v = g->set[c];
            FRAMERECT(bmp, x0, y0, x1, y1, 1);
            TEXTF(bmp, FONT5, x0 + 3, y0 + 14, -1, "%s", sset_labels[c]);
            switch (c) {
            case S_TYPE:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", type_names[type_of(t)]); break;
            case S_DIR:   TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", dir_names[v % DIRS]); break;
            case S_CLOCK: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", div_name(v)); break;
            case S_SCALE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", scale_names[v % SCALES]); break;
            case S_RANGE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "+-%d", v); break;
            case S_NLEN:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", nlen_names[v % NLENS]); break;
            default:      TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%d", v); break;
            }
            continue;
        }
        if (in_sequence(g, c)) {
            FRAMERECT(bmp, x0, y0, x1, y1, 1);
            TEXTF(bmp, FONT5, x1 - 6, y1 - 6, -1, "%d", c + 1);
        } else {                            /* past STAGES: corners only */
            FILLRECT(bmp, x0, y0, x0 + 2, y0, 1);
            FILLRECT(bmp, x1 - 2, y0, x1, y0, 1);
            FILLRECT(bmp, x0, y1, x0 + 2, y1, 1);
            FILLRECT(bmp, x1 - 2, y1, x1, y1, 1);
        }
        if (cur) {
            FRAMERECT(bmp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1);
            FRAMERECT(bmp, x0 + 2, y0 + 2, x1 - 2, y1 - 2, 1);
        }
        switch (pg) {
        case PG8_PITCH:
            draw_note(bmp, x0, y0, quantise(base + g->pitch[c], root, g->set[S_SCALE]), g->pitch[c]);
            break;
        case PG8_VEL:
            draw_vel(bmp, x0, y0, g->vel[c]);
            break;
        case PG8_PULSES:
            TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "%d", g->pulses[c]);
            draw_pulses(bmp, x0, y0, g, c, cur ? digiseq_pulse[t] : -1);
            break;
        case PG8_MODE:
            TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "%s", mode_names[g->mode[c] % MODES]);
            draw_pulses(bmp, x0, y0, g, c, cur ? digiseq_pulse[t] : -1);
            break;
        default:                            /* DIV */
            if (g->mode[c] == M_ALL)
                TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "1/%d", g->div[c]);
            else
                TEXTF(bmp, FONT5, x0 + 3, y0 + 12, -1, "(1/%d)", g->div[c]);
            draw_pulses(bmp, x0, y0, g, c, cur ? digiseq_pulse[t] : -1);
            break;
        }
    }
    TEXTF(bmp, FONT5, 72, 57, -1, "%s %d/%d", pages185[pg], pg + 1, PAGES_185);
}

/* ARP. OFFSETS: the 16 steps as bars from a centre line (+-24), the steps
 * past LEN as dots, the knobs' half underlined, the step playing framed.
 * ARP: the settings, a box a knob, as SETUP. */
static void draw_arp(void *bmp, int t)
{
    const struct arpset *a = &digiseq_arp[t];
    int pg = digiseq_pg_arp % ARP_PAGES, c;
    if (pg == PGA_OFFS) {
        int len = a->len < 1 || a->len > OFFS ? OFFS : a->len;
        int half = arp_sel[t] & 8, mid = 31;
        int cur = digiseq_trk[t].on && TRANSPORT == 1 && parp[t].active ? digiseq_aoff[t] : -1;
        FILLRECT(bmp, 0, mid, 127, mid, 1);
        for (c = 0; c < OFFS; c++) {
            int x0 = 8 * c, v = a->off[c], h = (v * 20) / OFF_MAX;
            if (c >= len) {
                FILLRECT(bmp, x0 + 3, mid + 2, x0 + 4, mid + 2, 1);
                continue;
            }
            if (h > 0)
                FILLRECT(bmp, x0 + 2, mid, x0 + 5, mid + h, 1);
            else if (h < 0)
                FILLRECT(bmp, x0 + 2, mid + h, x0 + 5, mid, 1);
            else
                FILLRECT(bmp, x0 + 2, mid - 1, x0 + 5, mid + 1, 1);
            if (c == cur)
                FRAMERECT(bmp, x0, 9, x0 + 7, 53, 1);
            if (c == arp_sel[t])
                FILLRECT(bmp, x0 + 1, 52, x0 + 6, 52, 1);
        }
        FILLRECT(bmp, 64 * (half >> 3), 8, 64 * (half >> 3) + 63, 8, 1);   /* the knobs' half */
        TEXTF(bmp, FONT5, 72, 57, -1, "%s %d/%d", arp_pages[pg], pg + 1, ARP_PAGES);
        return;
    }
    for (c = 0; c < NASETS; c++) {
        int x0 = 32 * (c & 3), y0 = (c < 4) ? 31 : 8;
        int x1 = x0 + 30, y1 = y0 + 21, v = a->set[c];
        FRAMERECT(bmp, x0, y0, x1, y1, 1);
        TEXTF(bmp, FONT5, x0 + 3, y0 + 14, -1, "%s", aset_labels[c]);
        switch (c) {
        case A_TYPE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", type_names[type_of(t)]); break;
        case A_MODE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", amode_names[v % AMODES]); break;
        case A_SPD:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", spd_names[v % SPDS]); break;
        case A_RNG:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%d OCT", v); break;
        case A_SCALE: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", scale_names[v % SCALES]); break;
        case A_RPT:  TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%dX", v); break;
        case A_WALK: TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%c%d", v < WALK_MAX ? '-' : '+',
                           v < WALK_MAX ? WALK_MAX - v : v - WALK_MAX); break;
        default:     TEXTF(bmp, FONT5, x0 + 3, y0 + 4, -1, "%s", v ? "ON" : "OFF"); break;
        }
    }
    TEXTF(bmp, FONT5, 72, 57, -1, "%s %d/%d", arp_pages[pg], pg + 1, ARP_PAGES);
}

/* The stock drop-down is drawn in the title bar (rows 54-63) before the page:
 * while one shows, the page keeps those rows as the stock drew them. The
 * bitmap: +4 width, +8 height, +12 longs a column, +16 the pixels, 128
 * columns of two longs, the second's bits 9..0 rows 54..63 (as PUMP reads it). */
#define TITLE_BITS 0x3ffu
static uint32_t title_save[128];
static int title_keep(void *bmp, int restore)
{
    char *b = (char *)bmp;
    uint32_t *px;
    int x;
    if (*(int32_t *)(b + 4) != 128 || *(int32_t *)(b + 8) != 64 || *(int32_t *)(b + 12) != 2 ||
        !(px = *(uint32_t **)(b + 16)))
        return 0;
    for (x = 0; x < 128; x++) {
        if (restore)
            px[2 * x + 1] = (px[2 * x + 1] & ~TITLE_BITS) | title_save[x];
        else
            title_save[x] = px[2 * x + 1] & TITLE_BITS;
    }
    return 1;
}

void digiseq_draw(void *bmp, void *ctrl)
{
    int t, base = 60, root, kept = 0;
    (void)ctrl;
    if (!digiseq_open)
        return;
    t = ui_track();
    if (msg_stock && msg_ticks)
        kept = title_keep(bmp, 0);
    FILLRECT(bmp, 0, 0, 127, 63, 0);
    if (t < 0)
        return;
    base = track_base(t, &root);
    /* the title line: the track, its type, the page */
    if (eng_of(t) == E_ARP) {
        draw_arp(bmp, t);
        TEXTF(bmp, FONT5, 1, 57, -1, "%s%d ARP", is_midi(KIT, t) ? "MIDI " : "T", t + 1);
    } else if (eng_of(t) == E_185) {
        draw_185(bmp, t, base, root);
        TEXTF(bmp, FONT5, 1, 57, -1, "%s%d 185 %dST", is_midi(KIT, t) ? "M" : "T", t + 1,
              total_steps(&digiseq_trk[t].st));
    } else {
        draw_snake(bmp, t, base, root);
        TEXTF(bmp, FONT5, 1, 57, -1, "%s%d SNAKE", is_midi(KIT, t) ? "MIDI " : "T", t + 1);
    }
    FILLRECT(bmp, 0, 54, 127, 54, 1);
    if (kept)
        title_keep(bmp, 1);                 /* the drop-down, over the title */
    if (msg_ticks && msgbuf[0] && !msg_stock)
        TEXTF(bmp, FONT5, 1, 1, -1, "%s", msgbuf);
    else if (eng_of(t) == E_ARP && digiseq_pg_arp % ARP_PAGES == PGA_OFFS)
        TEXTF(bmp, FONT5, 1, 1, -1, "LEN %d   KNOBS %d-%d", digiseq_arp[t].len,
              (arp_sel[t] & 8) + 1, (arp_sel[t] & 8) + 8);
    else
        TEXTF(bmp, FONT5, 1, 1, -1, "TYPE %s  <> PAGE  TRIG BACK", type_names[type_of(t)]);
}
