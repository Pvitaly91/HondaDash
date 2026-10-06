# Two-Nano M3a hardware bench

**СТЕНД: ДВІ NANO — ЕМУЛЯТОР ECU — НЕ ПІДКЛЮЧАТИ ДО АВТОМОБІЛЯ**

The bridge drives a real low-voltage wire; the second Nano supplies reference-derived
test bytes. This is neither a vehicle electrical interface nor a verified ECU connection.
Physical status at implementation: **NOT VERIFIED**. No board upload or physical test
is implied by the artifacts.

Build the single design in [SCHEMATIC.md](SCHEMATIC.md), [schematic.svg](schematic.svg)
and [BOM.csv](BOM.csv). It uses classic [Arduino Nano ATmega328P, 5 V, 16 MHz](https://store.arduino.cc/products/arduino-nano),
one NPN open-collector sink and one locally powered Schmitt receiver per board.
Only BUS and GND cross between interfaces; only the bridge rail supplies the bus pullup.

## Bring-up record — leave unperformed rows NOT VERIFIED

| Step | Procedure | Status / evidence |
|---|---|---|
| A1 | Each Nano separately, USB only, no signal wiring; record board/chip/revision and selected port | NOT VERIFIED |
| A2 | Explicitly upload bridge-bench to N1 and responder-bench to N2, with correct user-selected FQBN; read each identity | NOT VERIFIED |
| B1 | Both USB cables out: assemble exact circuit and check net list, orientations, rail isolation | NOT VERIFIED |
| B2 | Interface-only current-limited supply test described in SCHEMATIC.md; remove supply afterwards | NOT VERIFIED |
| C1 | USB both boards, Stop; record both rails, ground offset, idle HIGH, D3 release at reset | NOT VERIFIED |
| C2 | Record LOW current/voltage from each transmitter and receiver HIGH/LOW levels | NOT VERIFIED |
| D1 | Scope/logic analyzer: measure bit width, start/stop, turnaround, rise/fall, no overlap; save acquisition | NOT VERIFIED |
| D2 | Repeat with USB traffic at 115200; observe receiver overrun/error counters and ISR timing | NOT VERIFIED |
| E1 | Explicit two-port A/B/fault/recovery acceptance; save raw logs and report | NOT VERIFIED |
| F1 | Stop, coordinated quiesce, reset one board; require fresh identities/boundary and no phantom model update | NOT VERIFIED |
| F2 | Traffic stopped, sequential USB power-down/up; measure unpowered rail <0.1 V; redo identity/boundary | NOT VERIFIED |

Close GUI and Serial Monitor before an explicit upload. Signal wiring changes always
require both USB cables disconnected. Live BUS/GND connector insertion/removal and
ground-loss tests are prohibited by this bench design. VIN is unused. Do not connect
the two 5V outputs or add an external source while either board has USB power.

An acquisition file must identify its source: external analyzer, MCU timestamp, or
software-generated bit model. A functioning A/B run alone does not measure front edges,
interrupt latency or stack high-water. The driver contract and calculation are in
[ONE_WIRE_DRIVER.md](../../docs/ONE_WIRE_DRIVER.md); application procedure is in
[TWO_NANO_BENCH.md](../../docs/TWO_NANO_BENCH.md) and
[TWO_NANO_ACCEPTANCE.md](../../docs/TWO_NANO_ACCEPTANCE.md).

Manufacturer sources were checked on 2026-10-06 and are linked beside the relevant
parts/calculations. The design uses manufacturer operating specifications; arbitrary
modules with the same marketing name are not equivalent parts.
