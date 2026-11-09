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

Ported from Robbie's DIGISEQ for Digitakt (Digitakt mk1): 0.2, then 0.8's ARP. Press TRIG on the TRIG page until its
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
| Mods | **core-dt2 2.6** ([Digi2Core](https://github.com/gdeo607/Digi2Core): its pattern service) and this mod, `elemods/digiseq-dt2-0.5.elemod` |
| Not with | `digisnake-dt2` (it conflicts: DIGISEQ replaces it) |

Build the firmware from your own stock OS file:

```
python -m elekloader.patch --stock Digitakt_II_OS1.17.syx \
  --mod core-2.6.elemod --mod elemods/digiseq-dt2-0.5.elemod --out Digitakt_II_digiseq.syx
```

Never share the resulting .syx: it contains Elektron's OS.

## Layout

| path | what |
|---|---|
| `mod/digiseq_dt2.c` | the sequencers (SNAKE, 185, ARP), PRINT, the keys, the encoders, the tick and the drawing |
| `mod/core_pattern.h` | core 2.6's pattern service, which PRINT uses (a copy of Digi2Core's `core/pattern.h`) |
| `mod/fire.s` | the entry into the audio and MIDI sequencing hooks |
| `mod/mod.json` | the manifest: subscriptions, hook sites, conflicts |
| `elemods/` | the built mod |

## Licence

GPL-2.0-or-later (see `LICENSE`), as Robbie's DIGISEQ. The SNAKE and 185 algorithms are credited above.
