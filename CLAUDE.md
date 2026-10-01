# zbitxre: notes for Claude

## CW latency is left at 25 ms for now

Decided by the author on 2026-10-02: paddle to RF is 25 ms, steady, with
perfectly shaped dots, and that is what goes to operators to try. Do not
build anything further to shorten it until the author asks.

The measurements, the reasons, where the 25 ms is spent, and the faster
scheme that was discussed and set aside (wake on the key, rewrite the sound
card's queue) are written at the top of `modems/modem_cw.c`, under "HOW
LATE THE RF IS, AND WHY IT IS LEFT THERE". Read that before touching CW
timing. The T/R waits are in `data/hw_settings.ini`; `core_sdr.c` says why.
