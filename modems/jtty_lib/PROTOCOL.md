# Study of the local JTTY implementation

This study uses the source tree at `../wsjtx-3.2.0-rc1`. Checksums of the main
reference files are recorded in `tests/reference.sha256`; its paths are
relative to the parent directory of `jtty`.

## Transmitted frame

- 31.25 symbols/s, with 384 samples per symbol at 12000 Hz.
- 59 symbols, or 1.888 seconds: 13 synchronization tones + 46 payload tones.
- Synchronization sequence: `0 2 2 3 0 0 3 2 1 3 1 2 0`.
- Source payload: 32 grammar bits + reserved-zero + EOM, MSB first.
- CRC-12: polynomial `0x80F`, implicit leading term `x^12`, initial register 0,
  no reflection or final XOR. Appending it produces 46 information bits.
- Tail-biting convolutional code, K=10, 512 states, rate 1/2. Generator
  polynomials are octal `1167` and `1545`. The initial state contains the last
  nine information bits, including CRC bits.
- Output bit pair to tone: `00→0, 01→1, 10→3, 11→2`.
- 4-GFSK, BT=2, h=1, tone spacing 31.25 Hz. Phase remains continuous between
  frames of one message. The frequency pulse spans three symbols. Edge symbols
  are extended at the message boundaries; amplitude ramps span 1/8 symbol.

Sources: [genjtty.f90](../wsjtx-3.2.0-rc1/lib/jtty/genjtty.f90),
[tbcc.f90](../wsjtx-3.2.0-rc1/lib/jtty/tbcc.f90),
[gen_jttywave.f90](../wsjtx-3.2.0-rc1/lib/jtty/gen_jttywave.f90),
[gfsk_pulse.f90](../wsjtx-3.2.0-rc1/lib/gfsk_pulse.f90),
[sjtty.f90](../wsjtx-3.2.0-rc1/lib/jtty/sjtty.f90).

## Source grammar

The last two bits of the 32-bit grammar word select the type:

| Type | Contents |
| --- | --- |
| 0 | Standard 28-bit callsign + 2 action bits: CQ, CALL, TU/CQ, CALL/TU |
| 1 | The same callsign representation; CALL/AGN and TU NOW/CALL actions; other values invalid |
| 2 | STRUCT30: 27-bit body and 3-bit family selector |
| 3 | TEXT5: five 6-bit characters |

STRUCT30 covers numeric exchange, location exchange, zone/location,
class/section, serial/time, control phrases, and GRID4. All reserved fields,
ranges, and canonical base36 representations are validated. Families 5–7
are invalid. The all-zero 32-bit grammar word is invalid regardless of EOM.
Standard calls exclude hashes, special tokens, and forms that do not round-trip
exactly through call28.

The earlier type=2 meaning, "599 followed by five characters," was replaced by
STRUCT30 without a version discriminator. This library implements the new
format; it does not support the previous type=2 encoding.

The packer uses dynamic programming over character positions. Compact atoms
are allowed at token boundaries and must reproduce the normalized source span
exactly. Interior TEXT5 atoms always consume five characters. A structured
atom supplies one implicit space. Equal-cost choices prefer a structured
atom, then a longer span, then lower kind/subtype/role values. The Unknown,
Field Day, and RTTY Roundup profiles are described in [API.md](API.md).
The typed API also supports values that the automatic text recognizer does
not infer from context.

Sources: [jtty_source_encoding.txt](../wsjtx-3.2.0-rc1/lib/jtty/jtty_source_encoding.txt),
[jtty_source_codec.f90](../wsjtx-3.2.0-rc1/lib/jtty/jtty_source_codec.f90),
[jtty_mod.f90](../wsjtx-3.2.0-rc1/lib/jtty/jtty_mod.f90),
[packjt77.f90](../wsjtx-3.2.0-rc1/lib/77bit/packjt77.f90),
[packjt77_grammar.f90](../wsjtx-3.2.0-rc1/lib/77bit/packjt77_grammar.f90),
[chkcall.f90](../wsjtx-3.2.0-rc1/lib/chkcall.f90).

A detail from the executable code: `~` becomes a space before packing, although
the short alphabet description does not mention this separately. Fortran also
handles embedded NUL characters within its fixed 80-character field. The C API
accepts ordinary NUL-terminated strings, so NUL ends its input.

## Independent receiver

The C implementation uses this pipeline:

1. Sliding complex correlations on a 7.8125 Hz grid, with a one-symbol window
   and a 48-sample (4 ms) time step.
2. Synchronization scoring: average energy share of the expected tone among
   four tones for each of the 13 symbols. Extract up to 128 peaks with local
   suppression.
3. Refine timing and frequency on two small grids, reaching steps of six
   samples and 0.9765625 Hz.
4. Correlate all four tones for each of the 46 payload symbols.
5. Apply wrap-around Viterbi, trellis closure, CRC, reserved-zero, and complete
   source-grammar validation. Invalid payloads are not delivered to the application.
6. Suppress duplicate detections and return frames in chronological order.

WSJT-X reception is more elaborate: coherent list decoding, additional metrics,
retries, handling of adjacent/overlapping signals, and subtraction. These
mechanisms are not part of the wire format and are not reproduced here.
The C receiver's numerical results can be reproduced with `build/benchmark`.
It uses a unit-amplitude sinusoidal waveform and real white noise at 12000 Hz;
for a specified SNR in 2500 Hz, `sigma = sqrt(1.2 / 10^(SNR/10))`.
Timing and frequency vary between trials; search is restricted to 950–1050 Hz.
The direct comparison with WSJT-X is documented in
[tests/sensitivity/README.md](tests/sensitivity/README.md).
