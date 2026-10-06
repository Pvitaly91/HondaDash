# Protected interface revision B and production driver

This is a deterministic **model check**, not a Nano capture or an instruction-level
AVR simulator. The harness builds the exact `firmware/shared/one_wire.cpp` and
`one_wire.hpp` used by bridge-bench and responder-bench. It calls `Driver::enqueue`,
`service`, `fallingEdge`, `timer`, `outputApplied` and `pop`. There is no replacement
UART decoder. The driver itself decides echo, framing, collision, RX timestamps,
faults, queue progress and TX-complete.

The app does not acquire SPICE, Python or a compiler as runtime dependencies.
No transport, profile, identity, read whitelist, physical permission, scheduler or
200ms observation window changes are made by this integration.

## Signal and feedback paths

| Path | Signal fed to production driver | Feedback and limits |
|---|---|---|
| Precomputed analog replay | ngspice42 D8 voltage crossings extracted with the stated input thresholds into `time_s,level,valid` CSV | Driver receives recorded edge times and levels. Its output cannot change that already-computed trace. This path verifies real RX state and fixtures, not closed-loop TX fault behaviour. |
| Closed-loop frontend | Production D3 requests create delayed sink/DATA/D8 events using separate calibrated fall/rise delays | A driver echo/timing fault releases its D3. That release changes the frontend and subsequent D8 events. The circuit's timer/latch/power state can also remove sink. |
| Generated sender/receiver pair | A real production TX driver generates its D3/DATA/D8 waveform in the feedback model; a second real production RX driver receives those recorded D8 edges | The receiver is passive in this unidirectional case, so a later RX fault cannot change the sender's already-produced waveform. Both clock signs and separate ISR-entry/output phases are tested. |

The calibration CSV contains `name,tx_fall_us,tx_rise_us,rx_fall_us,rx_rise_us,echo_fall_us,echo_rise_us`.
These mean D3→DATA and DATA→D8 delays separately. The sum is checked against the
independent observed D3→D8 echo with a 0.5us extraction allowance. Different
thresholds and trace discretisation can make the sums slightly different; a larger
disagreement is a harness error. No delay is applied to all three paths a second
time. DATA uses the same project3.3V crossing on both polarities, so the model's
HIGH width is not artificially extended by using a different LOW threshold for
falling edges. This is a design reference, not an unknown ECU's guaranteed VIH.
Closed-loop inertial events are cancelled when a later input change supersedes
them. This bounded event approximation does not recreate analog overshoot or
arbitrary noise between crossings; the SPICE electrical checks and physical scope
acceptance remain separate.

The trace directory has `manifest.csv` with
`name,transitions,frames,expected`; each transition file has
`time_s,level,valid`; each fixture file has
`direction,start_s,period_s,value`. Manifest expectations are ACCEPT (the electrical
exporter's PASS is an explicit alias) or REJECT. A TX segment replayed into an independent RX
driver is labelled as such; only the closed-loop path checks own TX echo. A valid
marker is test metadata, not a new wire or a production driver feature. During an
undefined D8 level the fault paths sweep both sampled LOW and sampled HIGH during
an outgoing00 and require a sticky driver error. A framing byte can remain in raw RX, as production requires; it is not
a valid protocol sample. The firmware cannot infer a hardware-latch diagnosis from
a generic collision/timeout.

An arbitrary sequence of undefined input levels could resemble a complete UART
frame. Without electrical power/validity feedback the driver cannot prove otherwise;
that condition is **OPEN**, not a general "no corrupted telemetry" guarantee.
Stable supplies and measured D8 logic thresholds are required before accepting a
laboratory session. The two brownout branches demonstrate detection for their stated
outgoing00 stimulus only.

## Stop-period counterexample and minimal production change

Revision A anchored the final stop period at the actual **D3** release. For queued
`00 00`, actual DATA HIGH width is:

`104us + TX fall delay − TX rise delay`.

The reproducible baseline uses the production source from
`b2cf4ea12a783d4eb81736383f51790b361d9b04`. With the explicit sensitivity corner
TX fall=0.25us and rise=1.50us, DATA HIGH is **102.75us**. Echo still passes, so an
echo-only test misses the shortened physical stop. This baseline is a numerical
EXPECTED_REJECTION based on the measured model width, not the name of a case.

Revision B adds `Driver::StopSettleTicks=17` (8.5us nominal) to the own stop anchor. The
acceptance premise is DATA release settling ≤8us. A queued next TX then has
`112.5us + TX fall delay − TX rise delay` of DATA HIGH at nominal clock, at least104us inside that
allocation. Seventeen ticks are at least8.33us at the fast0.49us tick corner,
covering8us physical settling; sixteen ticks would only give7.84us and shorten
that limiting stop by0.16us. The sensitivity corner becomes **111.25us**. The bit period remains
208 ticks=104us; the final own stop and nominal frame duration become112.5us and
1048.5us. ISR output delay is still anchored after actual PORTD release.

The driver also retains `stopEarliestPeer_ = D3 release +104us`. A peer that starts
after its full actual DATA stop may arrive before the conservative own completion.
Its falling capture can complete the last own TX and begin RX once that earliest
deadline is met and HIGH echo was checked. Our queued next TX still waits for the
extended stop. This assumes a spec-compliant peer and does not measure the peer's
analog stop width. A captured start one tick early remains Collision. There is no
busy wait, GPIO polling loop, echo suppression, shorter stop or increased lateness.

The real sender/receiver tests exposed two additional errors. Revision A started a
queued frame with `beginTx(next_)` even if completion COMPA was late20us. Its actual
D3 start was late while subsequent edges could be on time: only32/256 or48/256
second bytes decoded at the stated default corners, without sticky UART errors.
A16us queued-start GPIO tail with later0us tails could also make the start-confirm
sample occur after the first data edge. These are generated production waveforms,
not a separately implemented UART formula.

Revision B starts queued TX at `now` and requires `startNeedsAnchor()` to be
completed after the actual PORTD write. `startOutputApplied` schedules the first
centre from that timestamp. The AVR HAL's bounded `applyTxStart` path reads TCNT1L
immediately after PORTD through low-first always-inline `gpioClockTicks`; its
compiled timestamp offset is2cycles=0.125us. Timer high and pending overflow are
handled with IRQ excluded. This avoids double timestamp calls in a generic HAL
path and keeps the fixed lateness budget. Streaming the TX byte's LSB and shifting
it once per data boundary replaces a variable seven-bit shift loop with equivalent
wire bytes and bounded execution. Actual all256-byte tests check that equivalence.

Both bench firmware use this shared source; legacy synthetic and bridge-lab do not
activate the GPIO driver. The extra state is one `uint32_t` and one `bool` (five AVR
bytes). Full ISR/stack/resource analysis is separate. The dedicated
`tests/protected_frontend/avr_gpio_timing.py` also checks the final disassembly's
normal even data phases2..16 and stop phase18. It records every source-constrained
branch, the disassembly SHA256 and numeric gates. Unknown compiler structure fails.
It is static instruction-path analysis, not an instruction-level CPU simulator or
a physical AVR measurement.

## Timing envelope

Timer1 input capture retains the actual falling timestamp even when CAPT dispatch
is delayed25us. COMPA samples are dispatched at deterministic lateness0 or40
device ticks (20us nominal). An already-enabled captured edge is retained across
the stop handler. CAPT precedes COMPA at equal event time. Generic host output
tails0/16us are sensitivity checks. The full paired production proof uses the
compiled normal-data bound150cycles=9.375us, normal-stop131cycles=8.1875us and
GPIO timestamp lag0.125us, with independently varied ISR-entry phases.

Production `ICNC1` filters for four CPU cycles, modelled as half a Timer1 tick
(0.25us nominal) before capture. GPIO and capture timestamp floors use the same
MCU clock phase, including the0.5us tick; the conservative combined
filter/quantisation allocation is0.5us nominal. It is counted once. Jitter
is±2us on data/stop edges relative to each actual falling start, not an additional
±2us start shift counted separately.

RX cases use peer periods101.92/104/106.08us (±2%), edge jitter±2us and all256 byte values.
TX cases sweep controller tick durations0.49/0.50/0.51us and keep the lateness
bound at40 ticks in that controller clock. Thus a physical20us delay at a fast
0.49us tick would exceed40 ticks and is correctly rejected; it is not silently
converted into a larger production allowance. The nominal receive budget is:

`52 −9.5×2.08 −20 −2 −8 −0.5 =1.74us`.

This conservatively allocates the full8us frontend envelope once. Actual start
capture shifts the RX sample origin; only relative fall/rise and width distortion
then move later samples. The harness processes those separate events rather than
subtracting start delay again from every centre. Timer, comparator and capture
allocations remain physical acceptance gates. The sampled sweeps do not establish
all possible oscillator, temperature, noise or ISR combinations.

The paired grid uses both receiver clock signs, RX lateness0/40ticks, CAPT
dispatch0/25us, three independent TX entry-lateness patterns and the measured
per-state GPIO tail bounds. It checks every byte in a queued`00,byte` transfer.
The JSON/CSV records the smallest signed distance from a production sample to a
D8 transition; a wrong expected bit gives a negative margin. The default maximum
asymmetry sensitivity corner gives a minimum **1.16us** observed margin. This is a
result for that explicit grid; the SPICE-calibrated run has its own reported minimum.
A deliberately unqualified16us **data** tail under the same independent phases
corrupts128/256 or192/256 second bytes and is EXPECTED_REJECTION. It is not silently
counted as a supported AVR data-tail envelope.

## Cases and rejection controls

- All256 bytes at both clock-skew signs, asymmetric/min/max delays, delayed CAPT,
  delayed COMPA and both edge-jitter signs.
- Back-to-back00; full production wake bytes; RPM/ECT/TPS requests; raw A/B/Boundary
  responses from `reference_fixtures.hpp`; trailing bytes retained as raw RX.
- Own TX/echo and no own-echo insertion into RX; fast peer after a full actual
  bridge DATA stop; TX-complete and release times recorded separately.
- False start: no byte; framing: raw byte plus sticky error; compare lateness20.5us
  nominal: Timing; frontend echo delay90us: Collision.
- A300us cutoff interrupts00: echo Collision and D3 release; a locked hardware
  latch prevents requested sink: Collision; power loss locks gate and a restored
  power-good cannot rearm it. Undefined D8 is not accepted as valid telemetry.
- Manual rearm is denied during active TX; a separate qualified action at stable
  power and inactive TX permits it. This functional predicate does not simulate
  button bounce; the LTC6994-2 analog/debounce model is checked in SPICE.

The normal timer lower bound is1.961698850ms, with LOW reset propagation ≤1us as an
explicit unguaranteed allocation. The longest nominal00 LOW is936us plus specified
clock/output/frontend tolerances. The full112.5us own stop gives timer-reset margin.
The separate READY qualification is conservatively modelled at its maximum21.285843ms
with physical button released, TX LOW and power-good continuously true. Power loss
or timeout clears both READY and ARMED. A button held across startup cannot qualify
READY; it must be released, then a fresh qualified/debounced press is required.
The harness's `rearm` call represents that explicit qualified edge; SPICE covers
the timer and button-bounce details. Startup traffic is only after that sequence.
Timeout/brownout remove own sink only; another peer, shorted DATA, MOSFET drain-source
short or negative-fault body diode can still hold DATA LOW.

Files missing, invalid calibration, malformed CSV, compiler failure, event-budget
exhaustion or parse errors exit3 and can never be counted as EXPECTED_REJECTION.
Each result identifies its feedback path and numerical/logical criterion. JSON/CSV
reports are generated under `build/`, never committed as physical captures.

## Reproduction

After the electrical workflow has generated checked traces and `corners.csv`:

```sh
python3 tests/protected_frontend/run.py \
  --out build/electrical/driver \
  --trace-dir build/electrical/driver-traces \
  --calibration build/electrical/driver-traces/corners.csv \
  --gpio-timing build/firmware-bridge-bench/atmega328/gpio-timing.json
```

The wrapper extracts only the two original production driver source files with
`git show`, compiles baseline A and revision B independently using standard C++20,
and requires both report sets to meet their explicit expectations. The baseline A
stop-width failure remains a historical baseline; other revision B results are not
retroactively attributed to A. Both driver anchors are compared using the same
revision-B calibrated/sensitivity frontend; this comparison is not a complete
electrical rerun of revision A. The separately preserved electrical baseline covers
the original revision-A circuit and stuck-TX failure. `--skip-baseline` is available for a shallow local
checkout; release/CI checks run both with the pinned commit available. Compiler
flags are `-std=c++20 -O2 -Wall -Wextra -Werror`. No Qt or SPICE runtime is linked
into this executable.

The GPIO proof is generated from each bench firmware's final compiled disassembly:

```sh
python3 tests/protected_frontend/avr_gpio_timing.py \
  build/firmware-bridge-bench/atmega328/disassembly.txt \
  --out build/firmware-bridge-bench/atmega328/gpio-timing.json
```

The wrapper records that exact proof and feeds its per-state bounds into the paired
model. Baseline A runs the original source with the same frontier assumptions and
only the stated historical counterexamples; the expanded joint phase grid is a
revision-B check and is not attributed to revision A.

Standalone closed-loop sensitivity checks (without analog trace calibration):

```sh
python3 tests/protected_frontend/run.py --out build/driver-integration
```

This command identifies its default corners as assumptions. Only the full command
with checked SPICE traces supplies the electrical/production integration result.
All physical TX-cutoff, D3/DATA/D8 timing, Nano exchange, isolation, vehicle
qualification and real ECU statuses remain **NOT VERIFIED**.
