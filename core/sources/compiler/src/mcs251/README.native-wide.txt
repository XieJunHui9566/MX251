MCS251 whole-function wide MIR, experimental (through R46)
========================================================

Activation
  -mmcs251 --model-small --stack-auto --mcs251-wide-backend
  Explicit __reentrant __sdcccall(4) declarations on selected functions.
  The large/stack-auto model is also covered. The default remains R29 lowering.
  Native ABI4 is a project ABI; it is not the Keil C251 binary ABI.

Design
  ralloc.c attempts this path before byte-register packing/allocation.
  wide.c imports the complete iCode function into typed nodes, computes CFG
  fixed-point liveness and physical alias interference, and allocates byte,
  word and dword registers including DR24/DR28. Spills use a private SPX frame.
  wide_emit.c.inc emits assembly independently of the old peephole machinery.
  Only a fully accepted function is emitted. A rejected function leaves the
  original iCode/symbol state intact and goes through the old backend.

Current support
  Integer functions, first two scalar register arguments, 8/16/32-bit
  temporaries, exact-width 8/16/32-bit _BitInt temporaries, arithmetic add/sub,
  bitwise operations, shifts, casts, comparisons, logical not, branches/loops.
  Register-only temporary bit conditions are supported; memory bits are not.
  Direct ordinary reentrant ABI4 calls with zero to two integer or xdata-pointer
  arguments, integer/pointer/void returns, recursion, and eligible direct tail
  calls are supported.
  SEND operands are collected into a parallel copy at CALL, including cycles
  and mixed-width physical aliases. All live allocated values are saved before
  the call; the return value is placed before saved values are restored.
  Temporary push depth is included in every private SPX spill displacement.
  Lowered integer runtime calls are accepted when their declarations satisfy
  the same ABI. Inline multiply/divide iCodes are still rejected.
  After import, compiler-owned 32-bit multiply support calls with one literal
  may become a bounded add/subtract chain. Binary and signed-digit forms are
  costed modulo 2^32. Only compiler symbol identity authorizes this rewrite;
  ordinary user calls are not matched by name. Chains with cost above seven
  stay calls. This speed-oriented budget is not a proof of minimum code size:
  some constants can grow the caller while avoiding the runtime call.
  Comments in generated assembly show acceptance, allocation and frame size.
  Fixed analysis limits: 128 values, 512 nodes, at most 512 spill bytes.

Memory and pointer subset (R33)
  Explicit __xdata pointers retain the SDCC three-byte object/ABI representation.
  First argument and return use R5:R6:R7; second argument uses R13:R14:R15,
  most significant byte first. R4/R12 are unspecified padding at ABI boundaries.
  MIR holds pointers in four-byte registers or private spill slots, clearing the
  padding on receipt, definition and non-tail call return. Integer-to-pointer
  casts retain the low 24 bits. This does not change C object bounds or make
  out-of-bounds pointer arithmetic valid.
  ABI3 pointers remain rejected. ABI4 pointer arguments may interoperate with
  the old backend built by this compiler; pre-R33 compilers do not implement
  this pointer ABI. Scalar integer/float ABI layouts are unchanged.
  Supported dereferences are exact 8/16/32-bit integral xdata objects. Ordinary
  memory uses word accesses where possible. Volatile accesses retain the old
  SDCC byte access width and increasing address order, including repeated reads.
  No new memory caching, alias speculation or reordering is performed. Volatile
  is not atomic; compound accesses can be interrupted between bytes/words.
  Pointer arithmetic, direct calls, returns, spilled pointers and xdata global
  addresses are supported. Global address relocation emits the high byte using
  a byte relocation; a shifted word immediate does not preserve that relocation.
  SRAM tests cover unaligned accesses, byte aliases, same/different pointer
  aliases, guards, high-padding poisoning and ISR overlap on separate buffers.

Whole-function fallback
  Indirect/legacy/special-attribute calls, third or stack arguments, noncontiguous
  SEND sequences, address-taken locals, direct global value accesses, generic/
  code/idata pointers, pointer-valued loads/stores, bitfields, aggregate and float
  memory, atomic operations, float work, inline multiply/divide, non-native widths
  (including optimizer-generated _BitInt(24)), debug, xstack, ISR, naked,
  critical, special register bank, callee-saves/banked and variadic functions.
  Ordinary non-stack-auto models often expose real parameter stack slots;
  these are rejected rather than pretending the slots are pure temporaries.
  Third arguments/stack parameters and remaining address spaces are future work.
  Unsupported native ABI declarations and atomics can be diagnosed by the
  frontend rather than falling back; the old backend does not make them valid.

Memory emission refinements (R34)
  Ordinary loads/stores use allocated address/data registers directly, with
  base-plus-two indexing for a second word. A multiword load uses scratch data
  registers if its destination overlaps the still-needed address register.
  Byte memory operations use R0..R15 directly when available. Byte casts use
  a direct low-byte register copy only when that byte is addressable; values
  in DR24/DR28 and spilled values retain the general extraction path.
  Pointer copies, unsigned narrow integer conversions, clean literals and
  generated global addresses avoid redundant high-byte clearing. This relies
  on the MIR invariant that every pointer definition is canonical. RECEIVE,
  arbitrary integer conversion, signed negative conversion, pointer arithmetic
  and call return keep normalization unless zero padding is already proven.
  No branch range assumption or out-of-bounds arithmetic assumption is used.
  Volatile byte access width/order and the ABI layouts are unchanged.

Direct word extraction (R41)
  GETWORD retains the R40 import and range restrictions. Emission selects an
  existing aligned word, snapshots two real low-register byte aliases into
  A/B, combines upper-register words through reserved scratch, or reads only
  the two required spill-slot bytes with the established big-endian offset.
  No full-width snapshot or unused high-word result is needed. Spill offsets
  retain the common frame/push-depth adjustment; input memory accesses are
  not widened or suppressed, including volatile loads performed before extraction.
  Tests/check-wide-word-extract.py checks 24 functions per model, all three
  byte offsets in a dword, signed inputs, both spill byte parities, low/upper
  register locations, and preservation of four ordered volatile byte reads.

Direct byte extraction (R44)
  GETBYTE accepts unsigned byte results and literal byte-aligned bit offsets
  wholly inside an integral 8/16/32-bit source. Pointer sources, signed result
  nodes and other unsupported forms retain whole-function fallback.
  Addressable low registers use a direct byte copy; upper registers snapshot
  the containing word into reserved B:A and select its high or low byte.
  Spilled sources read exactly one private-slot byte with the common big-endian
  offset and frame/push-depth adjustment. Destinations may overlap dead inputs
  or occupy byte spills; the source byte is captured before any destination write.
  This selects bytes from computed values. It does not narrow, merge or remove
  preceding volatile memory reads. The tests include 30 functions per model,
  all four byte positions, signed-source windows, upper registers, overlapping
  uses, spills at both parities, calls and explicit 8-bit ABI results.
  Existing float runtime source is retained: 8-bit sign/rounding variants became
  larger even though they now compile through wide MIR. Acceptance alone is not
  evidence that a narrower representation improves size or execution time.

Single-use unsigned extensions (R45)
  A zero extension from a non-pointer unsigned integer to a wider integer can
  be folded into its sole add/subtract/bitwise use in the same straight-line
  region. The source may not be redefined in between; labels, branches, calls,
  receives and returns stop the search. Signed inputs, multiple definitions
  or uses, pointer conversions and other consumers retain explicit casts.
  This removes virtual temporaries before liveness/allocation, not memory reads.
  Use weights are rebuilt after folding. Wide arithmetic preserves a narrow
  right input before a result write if their register byte ranges overlap;
  equal starting register numbers alone are insufficient for mixed widths.
  Tests exercise 8/16-bit ABI inputs inside DR4, noncommutative subtraction,
  all five arithmetic/bitwise consumers, signed and multiple-use exclusions,
  call/branch paths, plus the existing pressure/interrupt/volatile corpora.
Unsigned narrow products and extraction (R40)
  Integral unsigned 8/16-bit factors with a full unsigned 16/32-bit result
  may enter the wide MIR directly. The importer requires the result to hold
  the sum of the operand widths; signed or truncating products are not
  reinterpreted. Compiler-generated word extraction is accepted only at
  byte-aligned constant offsets wholly contained in an integral input.
  MUL uses reserved WR16 and WR20, with product DR16; both inputs are copied
  before writing an overlapping output, including real stack spills.
  No ordinary user function is recognized by its name. Front-end lowering
  can still route mixed byte/word expressions through the old runtime call;
  enabling this pass is not a guarantee that every C multiplication is native.
  The fsmul bit-pattern fallback uses four typed C products; its exact 48-bit
  construction, one nearest-even rounding and special-value policy are unchanged.
  Tests/check-wide-word-multiply.py checks native and fallback boundaries;
  the separate board harness uses Python integer and Fraction references.

Constant shift emission (R39)
  Valid constant shifts strip 16-bit and 8-bit portions using
  overlap-safe register copies before emitting the remaining 0..7 bit shifts.
  Signed right shifts retain sign extension. Upper wide registers use A/B and
  WR20 as scratch because only R0..R15 have byte aliases. Variable shifts and
  the existing invalid-count rejection retain their previous paths. Dedicated
  small/large board checks cover all legal 16/32-bit constant counts, signed
  right shifts, upper registers, real spills and ISR overlap (8192 results).
  Tests/check-wide-constant-shifts.py compiles the same 150-function corpus in
  both models and checks byte-alias legality and removal of fixed bit loops.
  Full runtime rebuild changes only the multiply fallback; its separate board
  oracle checks cover exact rounding, subnormals, zeros, infinities and NaNs.

Scratch and interrupt contract
  A/B and DR16/DR20 are scratch; DR24/DR28 may hold live allocated values.
  Calls conservatively treat all allocated registers as caller-clobbered,
  including DR24/DR28. No callee-specific clobber summary is assumed.
  A spilled byte load uses A, so variable shift counts are preserved in B
  only when that load would destroy them. Register alias masks are byte based.
  Existing R29 ISR generation saves upper wide registers. External assembly
  ISRs must preserve every interrupted register they clobber; an old external
  handler preserving only the 8051 byte bank is not sufficient.

Target separation
  This backend contains no AI8051U peripheral addresses, clocks, TFPU/MDU
  commands or download protocol. It targets the selected MCS251 ISA/ABI.
  Startup, linker memory map, device headers, accelerators and programmer
  settings remain target-specific. R30-R34 board evidence is AI8051U-34K64 CPU32
  only. STC32 requires model-specific qualification; its support is not implied
  by sharing the ISA. R32 rebuilds the opt-in integer runtime in all four
  supported memory models; the floating-point runtime remains unchanged.
  Its unsigned divide/modulus uses native 16-bit DIV for the high half when
  the divisor fits 16 bits, then processes the low half in sixteen rounds.
  The quotient high half and remainder use caller-clobbered WR18/WR16.
  Divisor-zero and signed-overflow direct-runtime behavior is retained; this
  does not define otherwise undefined C division expressions.
  R33/R34 retain both R32 integer and floating-point runtime archives unchanged.

Validation
  tests/check-wide-core.py checks acceptance, genuine word/byte spills and
  exact whole-function fallback. It performs offline compilation only.
  tests/check-wide-calls.py checks direct calls, tail copies, upper-register
  saves and actual spill-argument loads while the temporary push depth is nonzero.
  tests/check-wide-constant-multiply.py checks constant expansion and retained
  runtime/user-call boundaries. R32 also retains an independent fixed-width
  C oracle and hardware vectors including spills, negative multipliers and ISR
  overlap. Timings link separately named R31/R32 runtimes in the same image.
  tests/check-wide-memory.py checks both stack-auto models and volatile byte
  access structure. R33 delivery scripts also verify linked 24-bit global
  relocation, fallback/ABI diagnostics and prior integer/call object equivalence.
  R34 changes byte-cast code in the opt-in integer and call corpora, so those
  corpora require new hardware execution rather than object identity claims.
  Separate pointer checks cover signed/unsigned casts, conditional copies and
  an assembly callee returning deliberately dirty padding. The bit-condition
  pointer-loop fixture still falls back; this does not establish general
  pointer-loop support. Tests of pointer bit representation do not dereference
  arbitrary addresses or qualify the whole 24-bit address space as valid RAM.
  Hardware evidence uses an independent byte-addressed Python oracle and
  memory/guard digests, plus separate-buffer interrupt checks. C251 volatile
  word accesses differ from SDCC byte accesses, so their RAM timings must not
  be presented as equivalent peripheral bus transactions.
  The R30 delivery also retains fixed-width host-C oracle sources, seeded
  vectors, board images, raw serial frames, interrupt results and a common
  assembly timing harness comparing R29, R30 and unmodified C251 machine code.
  This milestone is an opt-in experimental integer/xdata subset, not a complete
  replacement of the legacy backend and not a claim of full C251 parity.

R36 runtime support
  Unsigned 32-bit unary minus is normalized to 0 - x modulo 2^32. Other unary
  widths and signed unary minus keep whole-function fallback. This supports
  safe INT_MIN magnitude conversion without assuming signed overflow.
  Native float comparisons retain their public legacy ABI and use explicit
  ABI4 bridges into integer bit-pattern kernels. The native-runtime profile
  also prevents unordered comparisons and self-comparisons being rewritten
  using identities that fail for NaNs. Default ABI and MCS51 remain unchanged.

R46 typed binary32 transport
  The opt-in wide MIR now represents float separately from integral values.
  Four-byte nonvolatile float temporaries support receive, return, assignment,
  same-kind copies, spills and direct ordinary ABI4 calls. Constants use the
  existing byteOfVal binary32 serializer, not a numeric float-to-integer cast.
  Formal/actual arguments, call results and returns must agree on float kind.
  With native-runtime enabled, already lowered add/sub/mul/div calls can use
  this path; this does not introduce hardware floating-point instructions.
  Raw float arithmetic, negation, truth tests, comparisons, numeric casts and
  float memory access are not interpreted as integer operations. Unless
  already lowered to a supported ABI4 call, the entire function falls back.
  No range-reduction algorithm, rounding policy or public library ABI changes.
  check-wide-float-transport.py verifies 19 functions in both stack-auto models,
  literal bit patterns, a genuine float spill and 24 exact fallback objects.
  Board evidence, independent rational references and same-body C251 timings
  are retained separately in R46; this is not whole-library C251 parity.

R54 bounded allocation alternatives and spill-slot reuse
  For already accepted ABI4 functions with real local objects and spilling,
  compare up to four deterministic allocations of the same MIR. Candidate 0
  is the original weighted greedy allocation. Candidate 1 adds spill-slot
  reuse; candidates 2 and 3 also divide use weight by interference degree
  (weighted by value width), with a one-unit bias in candidate 3.
  All ABI pins, physical-register overlap rules and reserved scratch lanes
  remain unchanged. Only non-addressable MIR spill values may share slots,
  and only when the full-width regions have no interference. Real objects
  retain exclusive storage throughout their lifetime; frames still cap at
  512 bytes. Every candidate passes register and spill consistency checks.
  Select using emitted instruction count (size mode), or count weighted by
  iCode loop depth (speed mode), with smaller frame as a tie-breaker. This is
  a heuristic, not exact machine-code size or measured cycle count, and does
  not guarantee that every program shrinks or speeds up. The old allocation
  is retained as a candidate; unqualified functions retain legacy fallback.
  The float stack-memory/call migration guard remains in place. No float
  precision, memory side effects, public ABI or address-space rules change.
  check-wide-spill-allocation.py covers small/large and speed/size, mixed
  widths, actual reused spill regions, graph overlap and deterministic code.
  Separate AI8051U board fixtures validate randomized inputs, calls, branches,
  sequential lifetimes and interrupt nesting against independent integers
  and the prior compiler. See the sealed R54 report for counts and limits.

R55 internal read-only CODE values
  CPOINTER values now retain their own address-space bit in the wide MIR.
  Function-internal addresses, copies, integer casts and pointer arithmetic
  use the existing 24-bit normalization and four-byte register containers.
  Scalar CODE loads (1/2/4-byte integers and binary32 transport) use MOVC
  byte reads in increasing address order, with full DPX increments and
  big-endian assembly. Volatile reads are not cached or merged; atomics,
  pointer-valued memory and bitfields retain their existing boundaries.
  CODE stores and surviving CODE/data pointer conversions are rejected by
  this importer. A frontend-proven dereference may already be lowered to
  CODE, as on the legacy path; that does not establish generic CODE ABI.
  Public CODE pointer parameters, return values and call arguments remain
  unsupported. The entry explicitly defers CODE parameters to the legacy
  validator, because a successful wide emission bypasses genFunction.
  The allocated register pool excludes A/B, WR16/18 and WR20/22; DPX is a
  scratch address register, with ISR preservation checked on the board.
  Tests cover integer/float bit patterns, non-aligned reads, a 256-byte page
  crossing, branches, loops, recursion, calls and volatile transaction count.
  They do not claim physical Flash access across the board's 64-KiB extent.

  The native-runtime tan reducer calls a separate ABI4 helper for its exact
  24-bit significand times 224-bit 2/pi multiplication. Both the helper and
  non-native in-place expansion include _mcs251_tan_product.inc, preserving
  all 31 product bytes and the previous arithmetic verbatim. The non-native
  four-model tancotf objects remain byte-identical. The rest of the reducer
  stays on the legacy path. Before factoring it reached WV_MAX=128; the
  factored ABI4 probe imports 101 values and 193 nodes, then is deferred by
  the existing float-stack-memory-with-call migration guard. No capacity
  or float-stack migration guard was removed for this change.

R56 integer range-reduction core
  The native tan reducer now separates the integer fraction/parity work
  from the float bit-container wrapper and final pi/2 multiplication.
  mcs251_reduce_fraction uses ABI4 with a binary32 bit-pattern input and
  output pointer. Its caller first excludes non-finite inputs and |x|<=0.5.
  It keeps all 31 product bytes, sticky/guard decisions, normalization,
  sign handling and the previous rounding steps. The shared fraction
  include changes only the bit-container expression; non-native models
  expand the original core in place and remain machine-code identical.
  The compiler and its float-stack/call migration guard are unchanged.
  The core imports 87 values and 170 nodes (40-byte selected frame in the
  size-oriented runtime build); the float wrapper stays on the legacy path.
  Qualification covers independent integer/rational references, all exponent
  fields 126..254 with both signs, near-pole vectors, fixed-seed random inputs,
  two memory models, speed/size, nested IRQ calls and existing tan/cot tests.
  These bounded tests do not certify the entire binary32 domain. The internal
  helper is not a new public math API and must not be called for non-finite or
  small inputs. The project ABI remains incompatible with Keil C251 objects.

R57 tan/cot polynomial pair
  The native runtime evaluates the original numerator and denominator in
  mcs251_tancot_poly(float, float *) using ABI4. It returns the numerator and
  writes the denominator into the caller's local object. The exact original
  P/Q coefficients and operation order are retained, including the separate
  multiplications and additions. The threshold, signs, quotient choice,
  division and special/errno paths remain in the original entry. There is
  no reciprocal rewrite or fused multiply-add. Non-native builds retain the
  original inline expressions; no public API or ABI is added.
  The helper imports 10 values and 32 nodes with zero local frame. The compiler,
  integer library and existing float-stack/call migration guard are unchanged.
  Full sincos/tancot migration is still blocked by legacy float comparisons,
  16-bit conversion calls, and unsupported wide float negation/boolean casts;
  those paths were not silently declared supported by this runtime change.

R58 low-byte unsigned products
  The importer additionally accepts unsigned8 x unsigned8 -> unsigned8 iCodes.
  Both operands are preserved and zero-extended; the existing WR16/WR20 MUL
  computes the complete product, then the existing byte store selects its low
  byte. This is modulo256 arithmetic, not a range assumption about the operands.
  All other truncating widths, signed operands, pointers and wider factors keep
  the prior boundaries. In particular a generic u16*u16 expression still uses
  the legacy runtime call path; this change does not migrate that ABI.
  This also covers byte products introduced by frontend range propagation in
  structure indexing. The same unmodified structure workload can consequently
  use wide code for its complete body rather than falling back at one product.
  check-wide-byte-multiply.py covers two models and speed/size with calls,
  retained inputs, real spills and conditional paths. Board qualification adds
  all65536 byte pairs checked against separate MUL AB assembly, independent
  integer oracle vectors and interrupt reentry. Mathematical runtime archives
  remain the pinned R57 versions; no library or public ABI change is required.

R59 native switch jump tables
  JUMPTABLE now imports its complete ordered target list, including repeated
  default/case targets. Every target participates in fixed-point liveness and
  register interference; there is no implicit fallthrough successor. Missing
  labels reject the whole function. Stack-address propagation and extension
  folding stop at table dispatch, just as at other control-flow boundaries.
  The frontend's original-width range comparisons and index normalization are
  retained. A directly adjacent wider-integer-minus-literal -> unsigned8
  normalization is accepted with modulo256 semantics. Other narrowing binary
  operations keep their earlier boundaries; no general arithmetic rewrite is
  enabled by matching a function name or a particular switch's case values.
  Dispatch uses reserved DR16/DR20 and A, a full-width scaled offset and a
  relocated 24-bit table address. Each entry remains a four-byte EJMP. No live
  allocated register is used as scratch, and offsets above255 do not wrap in A.
  Limits are256 entries/table and2048 entries/function, in addition to existing
  node/value limits. Overflow falls back without emitting a partial table.
  This is deliberately one uniform layout for speed and size. A future compact
  three-byte address table may reduce large-table data, with a measured speed
  tradeoff; this change does not claim optimal table layout for every program.
  Qualification covers all256 selectors, gaps/defaults, signed/nonzero/high32
  minima, nested switches, shared targets, fallthrough, direct calls, volatile
  stack objects, real register spills and IRQ reentry. At2048 entries native
  emission is accepted;2304 entries preserve the legacy object byte-for-byte.
  Runtime archives, public ABI and all disabled-wide/MCS51 paths are unchanged.

R60 bounded scalar stack arguments and high-byte logical shifts
  ABI4 direct calls may use up to eight scalar parameters. The existing first
  two register slots (DR4/DR12, or their low byte/word aliases) are unchanged.
  Remaining 1/2/4-byte integers or binary32 values use their declared widths,
  big-endian byte order, reverse push order and caller cleanup. There is no
  padding and no new object ABI. Three-byte pointers beyond the first two
  slots, more than eight parameters and special calling conventions fall back.
  Legacy ABI4 objects can call or be called by the new path. Keil objects still
  cannot be linked with this project ABI.

  Incoming parameters are loaded relative to the entry SPX after the existing
  three-byte ECALL return address. Their physical width is retained when the
  frontend combines a read with an integer CAST: truncation selects the low
  bytes, and widening uses the original signedness. Address-taken or volatile
  stack parameters, numeric float conversions and boolean casts retain the
  existing fallback boundaries. Non-stack-auto frontend forms are not generally
  migrated by this change.

  The importer only defers a contiguous sequence of pure SEND/IPUSH values.
  Computation, memory effects, control flow or nested calls inside that sequence
  reject the whole function rather than moving observable effects. All stack
  inputs participate in use counts and liveness. The emitter saves live values,
  pushes arguments while updating spill displacements, assigns the first two
  register parameters in parallel, calls, cleans up arguments, stores the return
  value, then restores saved registers. Calls with outgoing stack parameters
  cannot use the current tail-jump optimization. Stack-object lifetime guards
  remain active.

  Unsigned32 logical right shifts by constants24..31 extract the contributing
  high byte before writing the result. A bounded rotate/mask sequence then zero
  extends it. Input/result aliasing, registers without byte aliases, and spills
  are handled explicitly; signed/variable/other-count shifts are unchanged.
  This generic improvement avoids a regression exposed when a four-argument
  helper first moved from the old backend to the wide backend.

  check-wide-stack-arguments.py checks four model/optimization configurations,
  native stack layouts and whole-function fallback object equivalence. Board
  qualification separately tests old/new ABI interoperability, mixed widths,
  recursive calls, exact floating bit patterns, real spill frames, local objects,
  all256 high-byte values and IRQ reentry. Integer stack-load conversions are
  additionally checked against a Python integer reference. Runtime archives
  remain pinned, and no default-wide or MCS51 behavior is changed.

R61 single-use indexed data addresses
  A pointer addition/subtraction with one definition and exactly one use may
  be folded into that ordinary data load/store. The pointer temporary no longer
  needs a register or spill slot. Base and index instead participate in the
  consumer's liveness and allocation weights. The emitter computes the same
  four-byte sum/difference in reserved DR16, retaining24-bit pointer normalization
  before the memory access. Small nonnegative literals use immediate arithmetic;
  signed/narrow/large indices use the existing extension and operand rules.

  This is a bounded local transformation, not loop induction rewriting. It
  never moves an actual memory operation. It stops at calls and all control-flow
  boundaries or a redefinition of either operand. Multiple-use addresses and
  pointer returns keep the original path. CODE, volatile and direct stack-object
  accesses are excluded; existing access order and widths are preserved. The
  parameter ABI, runtime libraries and unsupported frontend forms do not change.
  A narrow signed-index frontend expression may still create an unsupported
  three-byte CAST; explicitly32-bit indices are tested, not silently substituted
  in user code. The new corpus widens the test expression before pointer addition.

  check-wide-indexed-memory.py compares four configurations against a supplied
  baseline compiler: same access-site sequence, actual spills/local objects,
  deterministic output and12 excluded-boundary objects byte-for-byte unchanged.
  Separate board tests use an independent byte-array model for18 functions and
  verify both return bits and a full128-byte post-call buffer checksum. Two
  complementary128-input halves cover256 inputs per optimization across the
  two memory models; each image executes128 inputs, not256. Main and IRQ use
  disjoint memory buffers, and the old compiler builds the driver/helper.
  These bounded tests do not establish universal pointer/aliasing conformance.

R62 direct byte comparisons and bitwise operations
  Integer operands that are both exactly one byte now use native CMP Rm,Rm
  or CMP Rm,#imm8. Signed conditions still use N/OV-aware JSL/JSG/JSLE/JSGE;
  unsigned conditions use carry/zero. Existing branch fusion and Boolean
  materialization are preserved. Wider, mixed-width and floating expressions
  retain their existing paths and frontend acceptance boundaries.

  One-byte AND/OR/XOR uses byte registers and byte immediates directly. R10/B
  and R11/A are reserved scratch aliases, excluded from allocation. A spilled
  right operand is sampled into B before loading a spilled left operand via A.
  Commutative operands are swapped when the result overlaps the right input;
  a spilled result uses A then writes exactly one byte. No memory operations
  are moved or combined, and no volatile access is removed.

  check-wide-byte-operations.py checks34 native functions across small/large
  and speed/size, real two-spilled-operand comparisons and spilled byte results,
  volatile access-site order, determinism and12 unchanged wider/floating objects.
  Board qualification separately covers all65536 byte pairs for30 functions
  in each of four configurations, R61-compiled exact per-result checks, and
  independent Python per-row rolling hashes. Hashes are not collision-free
  proofs. Literal boundaries, cross-call spill pressure and IRQ reentry are
  checked separately; the corpus is not universal compiler conformance.

R63 conservative short branch selection
  MIR conditional branches and GOTO still emit the existing far-safe form
  first. Once the complete function is known, each instruction from the
  explicit mnemonic whitelist contributes a six-byte upper bound. Current
  source/binary assembler matrix forms use at most five bytes. A branch may
  become a direct short conditional or SJMP only when the original layout
  proves its signed displacement fits. Backward bounds include the new branch
  itself (up to three bytes for a conditional in binary opcode mode).

  Unknown instructions/directives form barriers; incomplete emission lines,
  metadata-capacity exhaustion, missing/duplicate targets or internal targets
  retain the original form. All decisions use original upper bounds, so later
  shrinkage cannot invalidate them. No alignment directives can be crossed.
  Jump-table entries, indirect jumps and tail calls are never candidates.
  In particular every256-entry switch-table element stays a four-byte EJMP.
  This is intentionally conservative, not an exact assembler-size model.

  No arithmetic, flags, memory order, volatile semantics, ABI or runtime
  precision changes are made. The instruction-cost accounting removes the
  extra conditional-path EJMP when evaluating allocation candidates.
  check-wide-branch-bounds.py compiles the real helper with a host harness and
  exercises2072 distance/fallback cases. check-wide-branches.py verifies four
  model/optimization configurations, actual assembler branch bytes and signed
  displacements, unchanged nonbranch instructions, far-branch fallback,
  deterministic objects and all256 fixed-stride jump-table entries. Separate
  board qualification compares3072 exact results per configuration with an
  independent Python model and checks IRQ reentry. Byte exhaustive/pressure,
  general MCU and math regressions are retained; this is not full conformance.

R64 direct byte movement and Boolean values
  One-byte assignments and same-width casts copy the byte directly to its
  allocated register or private spill slot. Storing the low byte of an
  addressable word uses R(source+1); upper words still snapshot through B:A.
  Byte results from calls use R7 directly, and byte RETURN writes only R7,
  as already specified by native ABI4. Callers perform any signed/unsigned
  widening by the declared type. Wider integer and pointer returns retain
  their existing paths. No new source type or function ABI is accepted.

  Byte truth tests compare the byte against zero without extending to WR.
  Materialized one-byte Boolean values write literal0 or1 to the allocated
  destination after consuming the comparison flags. Spilled results use the
  reserved A register and a single-byte private-slot write. Wider Boolean
  result destinations retain the old implementation. The comparison itself
  may still be16/32-bit: its narrow Boolean intermediate can now be shorter.
  No preceding volatile access is removed, widened or reordered.

  check-wide-byte-moves.py checks four configurations, signed/unsigned byte
  returns, native/legacy call result transport, memory access-site order and
  deterministic objects. Two existing unsupported bit-test forms still fall
  back with byte-identical legacy objects. The byte-operation boundary checker
  no longer requires wider comparison functions' entire objects unchanged:
  it verifies their actual wide comparisons and runtime calls, while the
  separate board suite verifies results with an independent Python model.
  Wider bitwise and float-comparison boundary objects remain unchanged.
  Board qualification also repeats all65536 byte pairs across four models/
  optimization configurations, genuine spills and IRQ reentry. These bounded
  tests are not universal C conformance or a new unsupported-type guarantee.
R65 frame-base rematerialization (size mode only)
  --opt-code-size can replace a dynamic pointer add/subtract based on a proven
  immutable local-object address with an SPX-based reconstruction at the same
  operation. Provenance requires a single definition of every propagated MIR
  value and starts at a real frame object; joins/redefinitions and unknown or
  cyclic sources do not become constants. Constant derived offsets must remain
  within the object or one-past. The dynamic offset keeps its original width,
  signedness and 24-bit pointer normalization. Loads/stores retain their order,
  widths and volatile byte strategy; calls may still modify object contents.
  Reconstruction includes the current allocation frame and call-save depth.
  The index is read into reserved DR20 before the destination register is used.
  Only dead pure pointer calculations are removed; frame-object lifetimes and
  actual memory accesses remain intact.

  This is a space/stack tradeoff, not a speed improvement. Two general-board
  loops were slower when the transform was enabled in speed mode, so that mode
  retains its previous representation. This pass does not claim all size-mode
  programs are smaller/faster than speed-mode programs. It has not changed the
  allocator's existing instruction-count size heuristic.
  wide-stack-index.c/check-wide-stack-index.py cover small/large, speed/size,
  unchanged speed objects, deterministic output, volatile accesses and narrow
  signed-index whole-function fallbacks. Hardware fixtures add an independent
  Python model, signed32 indexing, one-past subtraction, branch-selected arrays,
  legacy callees, recursion, multiple objects and interrupt re-entry.

R66 direct scalar objects
  Explicit ABI4 functions can import direct RAM scalar loads/stores (1/2/4
  bytes, including binary32 transport) and byte SFR transactions. Each true
  object access creates a fresh MIR snapshot; objects are never cached as
  persistent virtual registers. Loads precede their original iCode operation,
  and its store follows it. Normalization never crosses an iCode boundary.
  Reads are retained even when the resulting value is unused. Calls and
  recursion can modify objects, which are read again at subsequent accesses.

  Volatile and SFR accesses retain the existing high-to-low byte sequence.
  Byte-addressable allocated registers can transfer bytes directly; high
  register groups and spills retain reserved scratch transport. Ordinary
  direct RAM can use a single native word/dword MOV. This is not an atomic
  access guarantee, and multi-byte SFR access is deliberately unsupported.
  Signed scalar transport is allowed, but frontend-created _BitInt(24)
  arithmetic remains a whole-function fallback. Near/global pointer objects,
  bits, idata/pdata/xdata true-symbol operands, parameters and atomic objects
  are not admitted by this path. Already-supported far pointer dereferences
  are a separate path. SEND/IPUSH arguments that remain true object operands
  also fall back: their reads must not be deferred until the CALL node.

  check-wide-symbols.py checks transaction order and actual byte instructions,
  signed transport, static objects, SFR encoding and fallback equivalence in
  four model/optimization combinations. Atomics are rejected by the frontend.
  wide-symbols.c and wide-symbol-pressure.c provide board fixtures for repeated
  accesses, old-backend mutation through aliases, call results, recursion,
  binary32 bit transport, register spills and interrupt context preservation.
  IRQ fixtures use separate objects to avoid unsynchronized shared-data races.
  Byte SFR qualification is offline only in R66; no generic peripheral claim.

  This does not select ABI4 automatically or change public ABI. An explicitly
  annotated driver experiment is qualified separately from the unchanged
  standard benchmark. Preserve that distinction in all size/timing reports.

R67: Direct bit-object transactions (2026-10-25)
  ABI4 function bodies may read/write direct __bit RAM and __sbit objects.
  Each load is an independent snapshot; MOV C,bit reads the addressed bit once.
  Integer-to-bit casts use the full input width for nonzero, yielding exactly 0/1.
  A single-use cast immediately followed by a bit store may be folded, retaining
  the original source width, position and one executed bit write. No access is
  moved across a label, call or another memory node. A sole bit condition uses C.
  Immediate direct scalar stores preserve the existing high-to-low byte order
  for volatile/SFR objects. Adjacent volatile accesses are never coalesced.
  Public bit return/parameter ABI is still unsupported; legacy diagnostics can
  include an internal error for a bit parameter. _Bool is not a new bit-object
  capability; frontend-normalized bool parameters retain prior behavior.
  GETABIT (including bt_mask), bitfields and address-taken parameters still use
  whole-function fallback. This is not automatic ABI selection or a new default.
  Tests: wide-bits.c, check-wide-bits.py and updated check-wide-symbols.py.

R68: Checked constant bit selection and clean ABI rejection (2026-10-25)
  GETABIT accepts a literal bit index within an integer value of 1/2/4 bytes,
  producing an unsigned byte 0/1. Source values remain live and unmodified;
  byte registers, upper word registers and spilled big-endian slots share the
  existing byte-selection transport. A/C are reserved scratch registers.
  A sole GETABIT -> bit cast -> bit store chain may write C directly to the
  addressed bit. Nodes must be adjacent and each temporary single-use. This
  does not move, merge, widen or omit actual volatile/SFR transactions.
  Public bit parameters/results remain unsupported. ABI validation now runs
  before both native and legacy register allocation for definitions and call
  targets, including indirect calls. Invalid interfaces produce an ABI error
  and no object file, instead of continuing to legacy frame lowering.
  Tests: check-native-abi-rejection.py, check-wide-bit-extract.py,
  wide-bit-extract.c and the strengthened check-wide-bits.py. SFR checks remain
  offline encoding/access-count checks. No implicit ABI selection is enabled.

R70: Flat pointer stack parameters (2026-10-25)
  ABI4 arguments three through eight may now be supported flat data pointers.
  XDATA/generic pointers remain exactly three bytes in the public stack ABI;
  wide MIR stores a zero-padded four-byte value. Every outgoing stack argument
  records its physical width separately from its MIR width. Pointer pushes
  send the high address byte followed by its low word, without the padding.
  Incoming pointer loads zero-extend the high byte and read the low word from
  the next address. Truncating integer casts read only the required low bytes.
  Caller cleanup, local frame references and saved-register depth use the
  physical widths. No public interface, stack ordering or mangling changes.

  Reentrant pointer calls can interoperate with the previous byte backend,
  including spilled pointer values, recursion and local buffer addresses.
  Volatile pointee accesses remain separate; volatile pointer parameters,
  address-taken formals, more than eight parameters, public CODE/near pointer
  types and unsupported calling conventions retain rejection/fallback rules.
  See R71 below for direct calls to ordinary reentrant ABI2 functions.

R71 explicit ordinary ABI2 direct calls
  ABI4 functions may call declared ordinary reentrant, bank-0 ABI2 functions
  with zero to eight supported scalar/flat-pointer parameters. The first
  parameter and return value use DPL/DPH/B/A in logical low-byte order;
  remaining parameters use the unchanged big-endian physical stack layout.
  Three-byte pointers remain three bytes on the public stack, four in MIR.
  Float arguments/returns transport bit patterns without numerical conversion.
  Live allocated values are saved before marshaling, return bits are captured
  before restoring them, and mixed-ABI calls cannot use an ABI4 tail jump.
  Compiler-defined ordinary runtime helpers (symbol.cdef) retain fallback
  until their separate library contract is qualified; no function-name filter.
  Variadic, indirect, non-reentrant, special-bank, naked, critical, CODE/near
  pointer and other unsupported conventions retain whole-function fallback.
  Addressed scalar locals/formals remain separate migration boundaries.
  Stack-parameter writes are supported by R72 below.
  Tests: check-wide-ordinary.py and wide-ordinary-{callers,callees}.c.
  No implicit ABI selection is enabled. This does not make every application
  smaller: the R69 application remains byte-identical due to another fallback.
  Tests: wide-stack-pointers.h, wide-stack-pointer-callers.c,
  wide-stack-pointer-callees.c and check-wide-stack-pointers.py. The old
  third-pointer fallback probes moved to this positive qualification corpus.

R72: Non-addressed stack formal write-back (2026-10-25)
  ABI4 parameters three through eight can be read, assigned and updated using
  distinct memory snapshots at their original iCode positions. Incoming slot
  offsets derive from declared physical widths and the three-byte return
  address; accesses account for local frames and dynamic saved-register depth.
  A write updates exactly 1/2/3/4 physical bytes. Flat pointers retain their
  three-byte stack representation, without writing the MIR padding byte.
  Float updates transport binary32 bits; no arithmetic or precision change.
  Snapshot reads precede the original operation and write-back follows it,
  without crossing calls, branches or volatile operations. True-symbol call
  arguments are still not deferred from SEND/IPUSH into CALL.
  Address-taken, volatile and atomic formals keep their existing fallback.
  Register parameters, public ABI layout and default toolchain are unchanged.
  The R69 protocol application passes its seed assignment but still falls back
  when reading address-taken local byte 'value' after ring_pop (iCode key 80).
  Tests: wide-parameter-write.h, wide-parameter-write-{callers,callees}.c and
  check-wide-parameter-write.py. Pointer regression accepts an explicit
  --baseline-has-stack-pointers option for already-qualified R70+ baselines;
  both legacy bytes and enabled bytes must remain identical in that mode.

R73: Addressed local scalar snapshots (2026-10-25)
  Local 1/2/4-byte integer and float symbols now share their existing stack
  object with ADDRESS_OF. Every read creates a fresh memory snapshot and each
  write updates that same object, before/after the original iCode only.
  Pointer aliases, ordinary ABI calls, loops and recursion therefore observe
  the same storage; objects remain live for the whole function and disjoint
  from spills. Volatile locals preserve individual ascending byte accesses.
  No caching across calls or alias writes, no change to public ABI layout.
  The front end's 64-bit literal structure offsets may enter pointer +/- only
  when exactly representable as signed or unsigned 32-bit offsets. Bounds use
  integer bits, not floating conversion. Other 64-bit operations and offsets
  keep fallback. This allows the R69 protocol app_run to use the wide backend.
  Address-taken formals, local pointer objects, bool/bit/atomic/aggregate
  direct symbol accesses and compiler-defined ordinary runtime helpers remain
  outside this migration. No precision reduction or new default is enabled.
  Tests: wide-local-scalar-{callers,callees}.c, check-wide-local-scalar.py,
  check-wide-pointer-offset.py, and the updated stack-object/volatile suites.

R74: Address-taken scalar formals (2026-10-25)
  ABI4 addressed integer/float parameters one and two now receive into local
  object homes; subsequent direct reads/writes and &parameter share those homes.
  Parameters three through eight retain their original caller stack slots.
  WM_PARAM_ADDRESS derives its address from the same entry offset used by
  WM_PARAM_LOAD/STORE, accounting for local frame and dynamic saved depth.
  There is no caller layout change and no independent shadow copy of stack
  arguments. Adjacent three-byte flat pointers keep their physical width.
  Tail calls are disabled when a formal address exists, preserving lifetime.
  Addressed pointer objects, volatile/atomic formals and other unsupported
  public types retain fallback. Ordinary compiler-defined helpers remain gated.
  Tests: wide-formal-address-{callers,callees}.c, check-wide-formal-address.py;
  prior scalar-formal fallback fixtures become positive storage/call checks.

R75: Physical three-byte pointer objects (2026-10-25)
  pointerAccess explicitly distinguishes a four-byte MIR value from its
  three-byte physical object. Data pointer locals, addressed register/stack
  formals, and pointer-to-pointer loads/stores now share typed storage paths.
  The high byte is zero-extended on load; padding is never stored. Volatile
  pointer objects use exactly three ascending byte transactions. Objects and
  spills remain disjoint; values are sampled again after aliases and calls.
  Stack address folding checks physical width; indexed address fusion remains
  disabled for these new pointer accesses. ABI layout is unchanged. Register
  formals have three-byte homes; later formals retain original entry offsets.
  CODE pointer values/objects, near/function pointers and volatile/atomic
  formals keep fallback or existing rejection. Compiler-defined ordinary
  runtime calls remain gated. No default opt-in policy changes.
  Tests: wide-pointer-objects-{callers,callees}.c and
  check-wide-pointer-objects.py. Former pointer-address negatives now check
  physical storage and entry offsets; other unsupported fallbacks still match.

R76: Opt-in ordinary ABI2 leaf bodies (2026-10-25)
  --mcs251-wide-ordinary implies --mcs251-wide-backend, and additionally admits
  ordinary reentrant/stack-auto ABI2 leaf functions. Without this new switch,
  existing ordinary code generation is unchanged. Static-parameter functions,
  special banks/critical/ISR/varargs/unsupported types retain legacy behavior.
  No CALL is admitted in an ordinary-entry function yet; compiler-defined
  helper calls and non-leaf/recursive bodies keep whole-function fallback.
  DPL/DPH/B/A carry the first argument and result, low byte first. Parameters
  two through eight retain declared physical stack widths and original slots.
  Addressed first arguments use local homes; later addresses reference caller
  slots. Floating values move bits unchanged, pointers are physically3/MIR4.
  Low-group allocated registers transfer directly to/from ABI SFRs; reserved
  wide temporaries remain the path for high-group/spilled values. Exact literal
  returns write ABI SFRs directly. ABI4 entry/return sequences are unchanged.
  Tests: check-wide-ordinary-leaf.py, original ordinary/pointer-object corpora,
  and wide-ordinary-leaf-{callers,callees}.c for old-caller compatibility,
  addressed scalar/pointer formals, unused first argument and void frame exit.
  Measured speed benefits vary; very short scalar leaves are not uniformly
  faster. Keep the opt-in and verify workloads before library migration.


R76 qualification boundary: complete ordinary-leaf coverage was verified with
--stack-auto. Merely marking a function __reentrant without --stack-auto can
produce different RECEIVE forms and conservatively fall back to legacy. A
small/large two-argument scalar fixture verifies byte-identical R75 fallback;
do not claim full no-stack-auto wide coverage from the stack-auto results.

R77: Explicit direct calls inside ordinary ABI2 bodies (2026-10-25)
  The experimental ordinary opt-in no longer restricts the body to a leaf.
  Direct calls reuse per-call ABI2/ABI4 validation, live-value saving, physical
  stack arguments and return reconstruction. Ordinary-entry tail jumps remain
  disabled. No compiler-generated ordinary runtime helper gate was removed.
  The first parameter/return convention and caller-owned parameter slots are
  unchanged; local homes and spills survive calls and aliases.
  Tests: wide-ordinary-calls-{callers,callees}.c, check-wide-ordinary-calls.py.
  12 caller functions cover recursive calls, cross-ABI calls, legacy callbacks,
  live values and spills, mixed eight arguments, pointer returns, addressed
  first/second parameters, volatile frame guards and float bit-only transport.
  The 10 helper functions and driver are deliberately fixed to R76 in the
  application comparison, preserving old/new interoperability evidence.
  Code size and runtime results apply only to that same-source application.
  Do not infer deep-recursion speed: its timed input is n=0, while semantic
  and interrupt tests cover recursion depths0..3. No-stack-auto partial RECEIVE
  coverage and float-stack-memory-with-CALL deferral remain for separate audit.

R78: Local direct-stack float CALL migration (2026-10-25)
  The ordinary ABI2 opt-in may now migrate a sampled-qualified local-float
  wrapper with calls only after every float memory operation is proven to
  address a known local object in bounds. Run stack-address folding before
  this migration decision. Do not cache memory values, remove volatile byte
  transactions, change object lifetime or relax the ordinary cdef call gate.
  Unknown/external float addresses, provenance lost at conditional joins,
  and the established ABI4 migration deferral retain old fallback behavior.
  Tested six local wrapper forms with exact four-byte volatile transactions,
  guards, alias mutation and spills in small/large, speed/size. The hardware
  corpus uses original NaN/Inf/signed-zero bit patterns; arithmetic precision
  is unchanged. A conditional-alias supplemental fixture remains a negative
  case, while a fixed-address variant exercises actual newly admitted code.
  Application-wide opt-in gains must be separated from compiler changes:
  fixed helper1079B -> R77 ordinary894B -> R78 ordinary874B. Only20B is new
  compiler lowering in that main corpus; c_float286B ->266B. Several short
  helpers are larger with the ordinary opt-in and remain explicitly measured.
  old_store still rejects a true-symbol first RECEIVE. Future work must audit
  that representation and no-stack-auto coverage without dropping qualifiers.

R79: Ordinary true-symbol register parameter home (2026-10-25)
  A non-address-taken ordinary ABI2 first parameter can still be a real stack
  symbol, especially under explicit __reentrant without global --stack-auto.
  The opt-in now admits that representation to the existing physical-home
  path. RECEIVE writes the home, later uses resample it, and ordinary result
  writes use the same home. Do not turn real symbols into reusable ITEMPs.
  The symbol must be the actual register formal in the current signature.
  Existing onStack, type, size, volatility and atomic gates are retained.
  Volatile qualification on a pointee does not qualify the pointer object;
  dereferences still emit ordered byte transactions. Top-level qualified
  parameters and non-address-taken ABI4 true symbols retain old fallback.
  No ABI, library, default flag or precision changes. The new home has a cost:
  stack-auto old_store grows103->109B, whole application2737->2743B. Without
  global stack-auto the same22 application functions shrink3976->3266B;
  ordinary coverage grows1->20, while two ABI4 functions retain fallback.
  Driver/link still use stack-auto. Do not combine these option configurations
  as one performance trajectory. Next work is safe home-overhead reduction.

R80: Proven immutable ordinary parameter homes (2026-10-25)
  Eliminate a home only when the ordinary first parameter is not address-taken
  or top-level volatile/atomic, every MIR object reference is a same-width
  direct snapshot, and there is exactly one write immediately after RECEIVE
  using that entry value's unique definition. All reads then copy the entry
  value, whose lifetime and call preservation use the existing CFG allocator.
  Mutable/addressed/volatile homes remain, as do all ABI4 boundaries. Repack
  other independent object slots without changing their storage lifetimes.
  Preserve the pre-transformation object-presence flag for the established
  float-call migration gate; eliminating a home must not relax that gate.
  Pointee-volatile accesses retain their byte transactions. The parameter
  pointer itself is an ordinary immutable scalar, distinct from its pointee.
  The22-function stack-auto corpus shrinks2743->2711B; old_store109->77B.
  Explicit-reentrant-only application modules shrink3266->2818B. These are
  separate option configurations and exclude harness/table/startup/libraries.
  A supplementary signed/unsigned16-bit corpus exercises entry values over
  real calls and loop edges. Existing math/runtime behavior is unchanged.
  R80 is the hard automatic-development limit. Preserve evidence, restore
  hardware and pause the task; no R81 without an explicit human instruction.
