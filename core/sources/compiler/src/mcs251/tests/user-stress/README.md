# Synthetic user programs

These programs are authored for MX251 testing. They do not come from an SDK,
device example, or a real user's project. `stress.h` only adapts fixed-width
types and EDATA syntax for the host reference compiler.

Each of six cases has a structured and a deliberately awkward equivalent:

| Case | Pressure |
| --- | --- |
| fields | Mixed struct/member syntax, pointer aliases, indexed arrays |
| parser | Switch state machine versus branches and goto |
| ring | Circular queue, wraparound, full/drop paths and pointer aliases |
| bits | Signed/unsigned volatile bitfields, aliases, byte neighbours |
| callback | Function pointers, stack arrays of callbacks, separate library TU |
| giant | Loop versus 96 copy/pasted unsigned arithmetic steps |

The generator in `tools/research/user_stress.py` materializes the giant case.
Unsigned arithmetic has explicit widths; shift counts are bounded. Pointer
accesses stay within initialized objects. This is difficult but defined C,
not a test of how the compiler handles undefined behavior.

`measure` runs 256 host seeds, checks every clean/messy pair, and records compiler
refusals and object sizes for small/size, small/speed and large/size. The two
registered targets and an independent architecture-only description must emit
identical pure-C REL objects. This is a code-generation check, not support or
hardware qualification for a third device. Direct compiler tests use default
128-value/512-node limits; the public board build uses its normal 512/2048 limits.

`board` generates a program for the existing AI8051U UART harness. `capture`
compares 64 seeds for all 12 functions against host results, records every
mismatch, and returns failure if any result differs or a case is missing.
Timer data does not establish a performance target.

Known baseline issue found on 2026-11-14: `bits_messy` fails all 64 board seeds;
the other 704 cases match. The parent and architecture-policy candidate produce
identical firmware. The legacy DPTR address cache survives a runtime pointer
load and incorrectly removes a subsequent fixed-member address reload. Keep
the failing test intact; do not count compile success as runtime success.

The pointer-cache repair invalidates fixed-address facts after runtime loads,
far-symbol materialization and pointer decrement. The unchanged corpus now
passes all 768 board cases (2026-11-14); firmware SHA256
`720b8dc56f80e633d38c3773d6ef7a267072c4ac6d7dada770541f3f18441577`.
The earlier failing result remains recorded in COMMON-DEVICE-SEMANTICS-20261114.

The byte-bitfield lowering candidate also passes all 768 board cases. Both
`bits` functions now use the wide backend; the original C sources are unchanged.
The companion `tools/research/bitfield_fixture.py` generates all 36 byte-contained
layouts, varying signedness, declared integer type, storage, volatility and
pointer aliases. Its 2304 board results match the independent host reference.
See BITFIELD-LOWERING-20261114 for sizes, timing observations and remaining scope.

The callback-pointer candidate moves both unchanged `callback` functions to the
wide backend. Function addresses, three-byte pointer objects and ABI2/ABI4
indirect calls share the existing value, memory and call-save machinery.
`tools/research/callback_fixture.py` exercises 19 equivalent forms: local and
global callbacks, DATA/EDATA/XDATA arrays, CODE tables, struct members, callback
parameters in each argument position, returned callbacks and legacy/wide calls
in both directions. Each ABI passes 1216 host-referenced board cases; the
original corpus passes 768/768. See CALLBACK-POINTERS-20261114 for qualification.

The boolean-conversion candidate adds `tools/research/boolean_fixture.py`:
30 cases cover integer/address truth, byte objects, arrays, struct members,
boolean parameters/returns and equivalent branch/state-machine/parser forms.
ABI2 and ABI4 each pass 1920 host-referenced board cases. The original 63 REL
objects remain identical to the callback candidate. A high-address pointer
case also fixes the legacy truth test: flat generic pointers must examine all
24 address bits. See BOOLEAN-CONVERSIONS-20261114 for per-function sizes,
remaining size differences and the official-project regression.
