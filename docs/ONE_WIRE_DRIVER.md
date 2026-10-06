# M3a single-wire driver

## Current M3b.1 production refinement

The shared production source now has three narrowly scoped timing corrections,
each reproduced against the original `b2cf4ea` driver. Actual DATA HIGH for an old
queued `00 00` stop was shorter than104us when release was slower than assertion.
The new stop anchor includes17ticks (8.5us nominal, >=8.33us at the fastclock) for
the <=8us frontend release allocation. Queued TX starts from actual service time
and its first sample is anchored after the actual PORTD start; this fixes silent
second-byte corruption under mixed COMPA-entry/GPIO-output phases. Streaming TX
LSB shifting removes the data-dependent mask loop while preserving all wire bytes.

The bounded AVR `applyTxStart` path and low-first always-inline `gpioClockTicks`
stamp the physical output within2CPUcycles=0.125us. The ordinary COMPA input
timestamp remains unchanged. A qualified fast peer can begin after its full
actual DATA stop, before our conservative own completion, through the separate
earliest-peer deadline. Lateness remains40ticks, echo remains enabled, and
transaction observation remains200ms. No software command clears the independent
revision-B hardware latch or provides firmware with its state.

One data bit remains208ticks=104us. The extended own stop is112.5us nominal;
frame1048.5us, five queuedbytes5.2425ms, eleven initbytes11.5335ms, before extra
ISR/queue-handoff latency. These are project/model timings, not Honda guarantees.
The original M3a timing/evidence below is retained as historical baseline and must
not be mistaken for the following current build results.

Pinned AVR GCC7.3.0-atmel3.6.1-arduino7 / Boards1.8.6, both benchfirmware and both
Nano targets, final local static instruction analysis:

| Current path | cycles | us at16MHz |
|---|---:|---:|
| TIMER1_COMPA complete maximum |522|32.625|
| TIMER1_CAPT complete maximum |411|25.6875|
| Runtime main atomic service |232|14.500|
| Startup-only begin, before traffic |337|21.0625|
| COMPA entry through actual TCNT1 snapshot |66|4.125|
| Normal TX data snapshot to PORTD, source-constrained |150|9.375|
| Normal stop snapshot to PORTD, source-constrained |131|8.1875|
| Actual PORTD to GPIO timestamp |2|0.125|

Runtime compare lateness bound is `14.5+4.125+0.25=18.875us`, below the unchanged
20us limit. Complete ISR length is distinct from sample lateness. The conservative
valid-traffic USB recurrence is54.1875us lower-priority/main work plus
`ceil(R/52)*32.625us`, converging at152.0625us; allowing eight4-cycle instructions
gives154.0625us <170us for the documented two-level RX FIFO. It depends on the
same valid-frame/interrupt assumptions; added clients/noise invalidate it.

`tests/protected_frontend/avr_gpio_timing.py` runs after every benchdisassembly,
records compiler/source branch constraints and disassembly SHA256, and rejects
unknown instruction structure. This is static instruction-path evidence,
**not an instruction-level CPU simulator or physical measurement**. Full ISR and
non-LTO stack reports remain in the firmware artifacts. The production Driver is
147AVRbytes; board stack high-water and actual IRQ/USB-overrun performance remain
NOT VERIFIED. Integration and explicit supported phase/clock grids are in
[PROTECTED_INTERFACE_DRIVER_INTEGRATION](PROTECTED_INTERFACE_DRIVER_INTEGRATION.md).

`firmware/shared/one_wire.cpp` is the C++11 state machine used by both new firmware
and host bit-level tests. `one_wire_avr.cpp` supplies the actual ATmega328P registers
and ISR wrappers. The two older sketches do not activate this GPIO backend.

## Resources and timing

| Resource | Exclusive M3a assignment |
|---|---|
| D8 / PB0 / ICP1 | Falling-edge hardware input capture from the local Schmitt receiver |
| D3 / PD3 | GPIO HIGH enables the external NPN sink; LOW releases BUS |
| Timer1 | Normal 16-bit counter, prescaler 8, 2 MHz at F_CPU=16 MHz |
| TIMER1_CAPT | Captured start edge, with hardware four-cycle input filter |
| TIMER1_COMPA | One next bit-boundary/sample event, no polling loop |
| TIMER1_OVF | Extends the device tick counter to 32 bits |
| D0/D1, USART | Arduino HardwareSerial for USB control at 115200 |
| Timer0 | Arduino millis remains enabled; no timer register changes |

The [ATmega328P datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
defines ICP1/PB0, Timer1 capture/compare/overflow and interrupt priority. No Servo,
Timer1 PWM, or other Timer1 client may be linked. D3's timer alternate function is
not enabled. No SoftwareSerial is used: the pinned
[ArduinoCore-avr 1.8.6 transmitter](https://raw.githubusercontent.com/arduino/ArduinoCore-avr/1.8.6/libraries/SoftwareSerial/src/SoftwareSerial.cpp)
disables interrupts across its start/data bits, incompatible with this USB timing budget.

The wrapper masks capture during TX/RX data and resynchronization: own TX and RX data
edges cause no redundant capture ISR. It arms capture after the last RX data sample
and at the final TX stop start, before stop processing can overlap the next start.
An already-enabled capture flag is never cleared by main or the stop ISR. A new start
captured during a delayed stop ISR retains its actual ICR timestamp. A peer start
before the completed TX stop is Collision; at/after completion it can complete TX
accounting before beginning RX even if CAPT outranks the pending COMPA.
Main `service()` rearms registers only when its state changed, never during an active frame.

One bit is **208 ticks =104 µs**, giving **9615.3846 baud**, +0.16026% from nominal 9600.
Half-bit is exactly 104 ticks. Frames are idle HIGH, one LOW start, eight LSB-first data
bits, one HIGH stop; ten bits occupy 1040 µs. Five request bytes require 5.20 ms and
eleven init bytes 11.44 ms before main-loop handoff. The engine's 100 ms TX timeout,
300 ms init and **200 ms response observation window** remain intact.

After a captured falling edge, start confirmation is at 0.5 bit; data at 1.5…8.5;
stop at 9.5. TX changes at integer bit boundaries and checks its own echo at every
half-bit centre. TX-complete occurs at 10 bits, after the stop bit elapsed and its
HIGH echo was seen. The AVR wrapper anchors that final stop period after the actual
PORTD release, so ISR output latency cannot shorten it. The host line calls the same
`outputApplied()` hook at its logical edge. Back-to-back queued frames then start.

The host tests cover relative sender period 204…212 ticks (about ±2%), edge jitter
±4 ticks (±2 µs), and sample lateness through 40 ticks (20 µs), including all 256
byte values at the extremes. Approximate remaining centre margin is
`52 − 9.5×2.08 − 20 − 2 =10.24 µs`, before unmodelled board effects.
This defines a software test envelope, **not measured oscillator/ISR performance**.
The electrical bench adds a separately verified ≤2 µs edge-to-valid-level condition.
More than 40 ticks late latches Timing and releases TX; events are never replayed
in a burst to catch up. Data corruption outside the envelope need not be a UART
framing error; the transaction layer must still validate length/checksum/guard.

## Bounded state, ownership and failures

The queues hold 16 TX bytes and 16 received byte/timestamp records. ISR work is one
state transition, a bounded queue operation and saturating 16-bit counters; no heap,
delay, Serial call, packet parse, or byte-duration interrupt mask. Main operations
use `ATOMIC_BLOCK(ATOMIC_RESTORESTATE)` around shared state. Time conversion/division
is outside the critical section. Queues are never copied as a whole in an ISR.

False start yields no RX byte and a visible error. Bad stop preserves the observed
raw byte but latches Framing before the engine can accept it. Continuous LOW triggers
StuckLow after twelve bits and waits for HIGH before resynchronizing. RX/TX overflow,
line busy, mismatched echo and overdue timer events latch a fault and cancel/release
TX. `takeError()` consumes the notification; it does not re-enable TX. RX continues
to collect late bytes. Only explicit coordinated new-experiment calls `clearFault()`.

Own TX never enters RX. Echo checking follows the transmitted bit state; it does not
discard the first N incoming bytes. A dominant LOW while a HIGH was sent, or a failed
LOW sink, produces Collision. Identical simultaneous bit streams are electrically
indistinguishable and cannot be detected by this circuit; protocol direction and
turnaround remain required. Error handling does not manufacture a response.

`abort()` cancels queued/current TX and releases D3 immediately in the wrapper, while
preserving pending RX and the peer's activity. Reset tri-states D3 and the external
47k base resistor releases Q. A stuck MCU may leave D3 HIGH; software has no independent
hardware guarantee of release in that case. The pullup limits current but is not a
watchdog. Disconnect power only under the documented bench procedure.

## Device timestamps and wrap

Timer1 ticks are 0.5 µs. The 16-bit counter wraps every 32.768 ms; its overflow ISR
extends it to 32 bits (wrap every 2147.483648 s). Captures account for a pending
overflow flag, including capture just before/after wrap. A received record carries
the **actual stop-sample ISR tick**, never a scheduled virtual event or queue-pop time.
No physical timestamp is inferred from the desktop clock.

Main snapshots current ticks and Arduino millis with interrupts excluded, then maps
`observedAt = nowMillis − ceil((nowTicks − receivedTicks)/2000)` using unsigned arithmetic.
This conservatively rounds down and carries about 1 ms quantization plus the pending
Timer0 overflow/ISR latency uncertainty. It is suitable for the existing millisecond
engine deadlines, not a claim of sub-millisecond absolute time. RX must be drained
within half the 32-bit tick range; the 16-byte queue and normal 1 ms main service make
overflow visible far earlier under traffic. Device millis wraps independently at
2^32 ms and the subtraction preserves that domain. Capture and compare differences
are valid only within half their respective unsigned ranges.

## USB/ISR coexistence and evidence

At nominal 115200 8N1 a USB UART byte spans 86.806 µs; the AVR double-speed divisor
at 16 MHz actually yields 117647 baud / 85 µs. Timer1 TX events are 52 µs apart.
The compare ISR, capture ISR, Timer0 ISR and short atomic main operations can defer
the USART handler. The design target is **<20 µs compare lateness** and well below
one USB byte for any continuous interrupt exclusion. The driver itself detects a
missed compare budget; HardwareSerial still needs physical overrun verification.

`scripts/analyze-avr-isr.py` explores conditional paths and calls in the deployed LTO
disassembly. It includes prologue/epilogue, hardware entry and vector JMP; shift loops
are bounded by the source phase invariant (0…7 shifts), and eight synthetic checks
exercise branch/skip/call/loop/nested-atomic accounting and rejection of unknown paths.
Unknown instructions, indirect calls or unbounded loops fail the analysis. Cycle
costs follow the [Microchip AVR instruction manual](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/ReferenceManuals/AVR-InstructionSet-Manual-DS40002198.pdf)
for the ATmega328P core. Each bench build writes `isr-timing.json` with a disassembly
SHA256 and explicit assumptions. Python is build tooling only; Windows can pass
`-Python <python.exe>` if no interpreter is on PATH.

With pinned AVR GCC 7.3.0-atmel3.6.1-arduino7 / Arduino AVR Boards 1.8.6, both bench
images and both FQBN variants give these conservative static path bounds:

| Path | CPU cycles | µs at 16 MHz |
|---|---:|---:|
| Timer1 compare, complete longest branch | 528 | 33.000 |
| Timer1 capture, including deferred TX completion | 403 | 25.188 |
| Timer1 overflow | 44 | 2.750 |
| Timer0 overflow | 103 | 6.438 |
| USART RX | 77 | 4.813 |
| USART data-register-empty | 135 | 8.438 |
| Longest runtime main atomic section (`service`) | 224 | 14.000 |
| Startup-only `begin`, before serial traffic | 329 | 20.563 |
| Atomic received-record pop plus two-clock snapshot | 89 | 5.563 |
| Compare entry through actual TCNT1H load | 66 | 4.125 |

The 33 µs complete ISR duration is distinct from lateness of its input sample.
COMPA has priority over Timer1 overflow, Timer0 and USART. For valid 8N1 traffic,
capture is masked through data bits and armed before the next start; the initial
capture establishes the first sample 52 µs later. Thus one already-running lower-priority ISR or atomic main section
can block a sample, then COMPA runs before other pending lower-priority handlers.
The maximum main-section bound plus compare prefix and one 4-cycle instruction is
`14 + 4.125 + 0.25 =18.375 µs`, inside the 20 µs software lateness limit. The former
91-cycle clock-return prefix included instructions after the hardware timestamp was
already latched and is retained separately in the JSON. A newly
captured edge is timestamped by hardware; its capture handler and possible preceding
atomic section finish before the 52 µs start-confirm sample. The focused capture test
also delays a stop sample by 20 µs and its ISR tail by 25 µs across a fast peer's next
start, retaining that edge. Arbitrary noise or invalid
start timing is outside this valid-frame scheduling argument and may fault visibly.

For USB, a deliberately pessimistic valid-traffic recurrence includes one 14 µs main
block, one capture 25.188 µs, Timer1 overflow 2.75 µs, Timer0 6.438 µs, RX handler
4.813 µs, and `ceil(R/52)×33 µs` compares. It converges at 152.19 µs (three compares);
allowing eight 4-cycle inter-handler instructions gives <155 µs. This exceeds one
85 µs byte, so it relies on the MCU's documented two-level RX FIFO (170 µs for two
bytes), the valid-frame capture rate (at most one start per byte), and the listed
interrupt sources only. It is a conservative static scheduling argument, **not a
physical overrun result**; noise, added ISRs or new atomic work invalidate its premises.
Physical concurrent-USB/line stress and actual ISR latency remain mandatory bring-up
measurements, not claims derived from the ideal host model.

Non-LTO stack diagnostics give a driver chain of compare wrapper 18 + `timer` 19 +
`pushRx` 4 + `fail`/counter 2 =43 bytes; budget **64 bytes extra interrupt reserve**
over the deepest main call chain to include entry/rounding. Interrupts do not nest
inside these handlers. The AVR Driver object is 142 bytes. Full firmware Flash/SRAM
and main-stack estimates are recorded separately in TESTING/TWO_NANO_BENCH.
All these are static estimates. Physical USART overrun and stack high-water remain
**NOT VERIFIED**.

`one_wire_tests` checks all values, back-to-back timing, start/stop/sample points,
skew/jitter/late events, echo, false start, framing, stuck LOW, conflicts, overflow,
abort/release and tick/millis wrap. `tests/support/one_wire_line.hpp` connects two
production drivers with a wired-AND logical line; this is not SPICE or a physical
waveform acquisition. The desktop integration fixture uses this same path, not a
direct byte callback. Transaction tests separately verify driver errors reach the
engine and cannot refresh a model.
