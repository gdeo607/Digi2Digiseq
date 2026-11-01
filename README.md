# Digi2Digiseq: DIGISEQ for the Digitakt II

**DIGISEQ** is a sequencer mod for the **Elektron Digitakt II** (OS 1.17): every track picks a TYPE, and the
type's sequencer plays in place of the track's own trigs. It works on all 16 tracks, audio and MIDI.

- **OFF**: the track plays its own trigs, as stock.
- **SNAKE**: a Cartesian sequencer after MDD Snake (and Make Noise's Rene): eight cells in a 4 x 2 grid, walked by a path.
- **185**: a stage sequencer after ML-185 (and RYK's M-185): eight stages, each with a pitch, a velocity, a length of
  1-8 clock pulses and a mode (OFF, ONE, ALL every DIV pulses, HOLD for the whole stage); with DIR, STAGES, CLOCK
  (1/8 .. 1X), SCALE, RANGE, note LEN and SHIFT.

Ported from Robbie's DIGISEQ for Digitakt 0.2 (Digitakt mk1). Press TRIG on the TRIG page until its pages come round
to open it; hold TRIG + UP / DOWN changes TYPE. Not on the Digitakt II yet: PRINT and CLOCK 2X. Not saved with the
project. Experimental.

> Unofficial and unsupported. Flashing modified firmware is at your own risk; the stock OS always recovers the
> unit (send the stock .syx from the boot menu). This repository contains no Elektron code or firmware.

## What you need

| | |
|---|---|
| Unit | Digitakt II, OS 1.17 |
| Mods | **core-dt2 2.4** and this mod, `elemods/digiseq-dt2-0.2.elemod` |
| Not with | `digisnake-dt2` (it conflicts: DIGISEQ replaces it) |

Build the firmware from your own stock OS file:

```
python -m elekloader.patch --stock Digitakt_II_OS1.17.syx \
  --mod core-2.4.elemod --mod elemods/digiseq-dt2-0.2.elemod --out Digitakt_II_digiseq.syx
```

Never share the resulting .syx: it contains Elektron's OS.

## Layout

| path | what |
|---|---|
| `mod/digiseq_dt2.c` | the sequencers (SNAKE, 185), the keys, the encoders, the tick and the drawing |
| `mod/fire.s` | the entry into the audio and MIDI sequencing hooks |
| `mod/mod.json` | the manifest: subscriptions, hook sites, conflicts |
| `elemods/` | the built mod |

## Licence

GPL-2.0-or-later (see `LICENSE`), as Robbie's DIGISEQ. The SNAKE and 185 algorithms are credited above.
