| SPDX-License-Identifier: GPL-2.0-or-later
| DIGISEQ-DT2: the sequencer's two step-fire routines, called past the sites.
| Each site (jmp, mod.json) took the routine's first two instructions; these
| run them and go on into the stock body, so a C call of dsq_orig_* is a call
| of the stock routine with the same arguments and result (d0).
        .text
        .globl dsq_orig_audio, dsq_orig_midi
dsq_orig_audio:
        lea     -52(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x400d698a
dsq_orig_midi:
        lea     -52(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x400d6e90
