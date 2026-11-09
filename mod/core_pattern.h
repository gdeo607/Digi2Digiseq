/* core-dt2 2.6's pattern service (pattern.c): include it in a mod that reads or edits the active pattern's
 * trigs and locks. Tracks 0-15, steps 0-127. Writers go through the firmware's own pattern editors (as the
 * panel does): call them from ev_tick, ev_key, ev_enc or ev_draw, not from the sequencer's fire hook. */
#ifndef CORE_DT2_PATTERN_H
#define CORE_DT2_PATTERN_H
#include <stdint.h>

struct core_note {                      /* one of a trig's notes (a trig the panel places has four); 0xff: the track's */
    uint8_t note, vel, len;             /* NOTE (first note), VEL and LEN locks */
    int8_t micro;                       /* micro timing, 0 on the step (LEFT / RIGHT with the trig held) */
};

uint8_t *core_pattern(void);            /* the active pattern's bytes (docs/PATTERN.md), or 0 */
uint8_t *core_pattern_obj(void);        /* the firmware's object for it, or 0 */
uint8_t *core_track_obj(int track);     /* the firmware's editor for one of its tracks, or 0 */
int core_track_len(int track);          /* the track's length in steps */

int core_trig_get(int track, int step);                         /* 1: the step has a trig */
int core_trig_set(int track, int step, int on);                 /* place or remove a trig (a trig key in GRID RECORDING) */
int core_step_flags(int track, int step, int flags, int on);    /* trig types 7-10 (7 SYN), overrides 11-14 */
int core_trig_notes(int track, int step, struct core_note *out, int max);   /* the trig's notes; how many */
int core_trig_set_notes(int track, int step, const struct core_note *notes, int n);  /* 1-4 notes */
int core_trig_lock(int track, int step, int which, int value);  /* which: 0 NOTE, 1 VEL, 2 LEN; 0xff clears */

int core_plock_get(int track, int step, int index);             /* value index 0-70 -> its 8.8 lock, or -1 */
int core_plock_set(int track, int step, int index, int value);  /* on a step with a trig; -1 if no slot is free (80) */
int core_plock_clear(int track, int step);                      /* every parameter lock of the step */
int core_plock_count(int track, int step);

#endif
