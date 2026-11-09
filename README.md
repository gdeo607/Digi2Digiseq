# Digi2Digiseq: DIGISEQ for the Digitakt II

**DIGISEQ** is a sequencer mod for the **Elektron Digitakt II** (OS 1.17): every track picks a TYPE, and the
type's sequencer plays in place of the track's own trigs. It works on all 16 tracks, audio and MIDI.

- **OFF**: the track plays its own trigs, as stock.
- **SNAKE**: a Cartesian sequencer after MDD Snake (and Make Noise's Rene): eight cells in a 4 x 2 grid, walked by a path.
- **185**: a stage sequencer after ML-185 (and RYK's M-185): eight stages, each with a pitch, a velocity, a length of
  1-8 clock pulses and a mode (OFF, ONE, ALL every DIV pulses, HOLD for the whole stage); with DIR, STAGES, CLOCK
  (1/8 .. 1X), SCALE, RANGE, note LEN and SHIFT.
- **ARP**: an arpeggiator played by the track's own note trigs: a trig holds its note (its NOTE and VEL locks) for its
  LEN and the arp plays from it every SPD steps, with OFFSETS, MODE, RNG, SCALE, RPT, WALK and HOLD.

**DIGISEQ is Robbie's.** It is [DIGISEQ for Digitakt](https://github.com/ClaidArremer/digitakt-digiseq) by Robbie
(ClaidArremer), for the Digitakt mk1. I did not create it: this repository adapts Robbie's work to the Digitakt II
(its hooks, its pattern and its screens), and the credit for the sequencers and their design is Robbie's. Adapted
here: 0.2, then 0.8's ARP. Press TRIG on the TRIG page until its
pages come round to open it; hold TRIG + UP / DOWN changes TYPE. **PRINT** (TRIG + FUNC + REC) writes what SNAKE or
185 plays through the track's length into the track's trigs (each note a note trig with its NOTE and VEL locks, on
the step, no condition; 185's held notes with their LEN), removes the note trigs on its rests (lock trigs stay) and
sets TYPE to OFF; TRIG + NO, with TRIG still held, puts the track back. Not on the Digitakt II yet: CLOCK 2X, ARP from
held keys, live recording. The types are not saved with the project (PRINT keeps a sequence). Experimental.

> Unofficial and unsupported. Flashing modified firmware is at your own risk; the stock OS always recovers the
> unit (send the stock .syx from the boot menu). This repository contains no Elektron code or firmware.

## What you need

| | |
|---|---|
| Unit | Digitakt II, OS 1.17 |
| Mods | **core-dt2 2.6.1** or later ([Digi2Core](https://github.com/gdeo607/Digi2Core): its pattern service) and this mod, `elemods/digiseq-dt2-0.5.elemod`. Core 2.4-2.6 crash the unit at boot: use 2.6.1 |
| Not with | `digisnake-dt2` (it conflicts: DIGISEQ replaces it) |

Build the firmware from your own stock OS file:

```
python -m elekloader.patch --stock Digitakt_II_OS1.17.syx \
  --mod core-2.6.1.elemod --mod elemods/digiseq-dt2-0.5.elemod --out Digitakt_II_digiseq.syx
```

Never share the resulting .syx: it contains Elektron's OS.

## Layout

| path | what |
|---|---|
| `mod/digiseq_dt2.c` | the sequencers (SNAKE, 185, ARP), PRINT, the keys, the encoders, the tick and the drawing |
| `mod/core_pattern.h` | core 2.6.1's pattern service, which PRINT uses (a copy of Digi2Core's `core/pattern.h`) |
| `mod/fire.s` | the entry into the audio and MIDI sequencing hooks |
| `mod/mod.json` | the manifest: subscriptions, hook sites, conflicts |
| `elemods/` | the built mod |

## Changes

- **0.5**: PRINT (TRIG + FUNC + REC) and its undo, on core 2.6.1's pattern service.
- **0.4**: ARP starts from a trig's own NOTE and VEL locks.
- **0.3**: ARP, from DIGISEQ for Digitakt 0.8 (0.3's ARP with 0.6's WALK 0 and 0.7's SCALE).
- **0.2**: SNAKE and 185 per track.

Checked on a Digitakt II (1.17, core 2.6.1): SNAKE, 185, ARP and its locks, PRINT and its undo, a removed rest trig,
lock trigs kept, conditions cleared and the printed trigs saved with the project.

## Credits and licence

- **DIGISEQ** is by **Robbie** (ClaidArremer): [DIGISEQ for Digitakt](https://github.com/ClaidArremer/digitakt-digiseq),
  designed and tested on a Digitakt mk1 (GPL-2.0-or-later). Its SNAKE is Robbie's
  [DIGISNAKE for Digitakt](https://github.com/ClaidArremer/digitakt-digisnake).
- SNAKE is after Maxime Dangles' **MDD Snake** and Make Noise's **René**; 185 is after **ML-185**, Nico Starke's free
  Max for Live device, itself after RYK's **M-185** for the Roland System 100m.
- core-dt2 2.x ([Digi2Core](https://github.com/gdeo607/Digi2Core)) and irpina's
  [elekloader](https://github.com/irpina/elekloader), which it is built with.
- The Digitakt II adaptation (this repository): gdeo607.

Licence: GPL-2.0-or-later (see `LICENSE`), as Robbie's DIGISEQ.

## Trademarks

Elektron and Digitakt are trademarks of Elektron Music Machines; René is Make Noise's; M-185 is RYK's. They are named
only to say what this works with or was inspired by. This project is not affiliated with or endorsed by any of them.
