# Physical validation plan and blank evidence record

**No measurements have been performed. Every row starts NOT VERIFIED.** A trained
bench operator must inspect the completed board, instruments and current-limited
fixture before applying power. Calculations and SPICE are preflight evidence only.
No vehicle, real ECU, VIN wiring, car battery, port scan or automatic upload is
part of this plan. Assembly, rewiring, connector changes and test-mode changes
are performed **with every supply disconnected**, not by live hot-plugging.

## A — functional two-Nano fixture

```text
PC USB-A -> classic Nano bridge-bench -> J1 logic pads
                                         | U1 isolation boundary |
separate B1 -> R14/D3P/U4 -> field circuit -> J2.1 DATA ---- responder BUS
                                             J2.2 GND_F -- responder GND
PC USB-B -> classic Nano responder-bench local5V -- 2.2k -- DATA
```

Keep exactly one artificial ECU pullup, in the **separate responder fixture**.
Use a measured resistor and supply that fit the source envelope; nominal2.2k+1%
can exceed the tested2.2k upper bound, so measure/select <=2.200k or use2.0k±1%.
The adapter itself has no fitted DATA pullup. Remove all original bridge M3a line
components and its pullup as itemized in SCHEMATIC.md; keep only the responder's
original protected-for-5V buffer/sink. Do not connect the two Nano5V rails.
Keep total DATA capacitance, including that responder and probes,<=500pF.
The retained M3a responder must also pass its original limits: DATA idle>=4.5V,
LOW<=0.4V and local receiver edge-to-valid<=2us. Select/measure fixture supply
and pullup to meet both boards' criteria; stop if the new board's loading violates
the responder's limits. Do not silently substitute the adapter's3.3V high criterion
for the retained responder's4.5V bench criterion.

**Two USB ports on the same PC join GND_L and GND_F outside U1.** This fixture can
verify bytes, timing and strict bench identities; it cannot establish galvanic
isolation or safe ground-fault handling. Even separate PCs may share earth through
their chargers. No faults above the original M3a5V envelope are allowed with the
responder attached. A passing A/B report does not qualify the new protection.

After electrical preflight, use the existing explicit-port procedure in
[TWO_NANO_ACCEPTANCE](../../docs/TWO_NANO_ACCEPTANCE.md). Both strict bench HELLO
identities must succeed before line TX. Firmware remains `bridge-bench` and
`responder-bench`; do not connect synthetic or bridge-lab endpoints to this GPIO
fixture. Any upload requires a separate user command naming a concrete port and
Nano variant. This document provides no auto-discovery or auto-upload command.

Verify init, each RPM/TPS/ECT request, fixture A, fixture B, fragmented USB traffic,
stop, failed/late response, drain and a deliberate new experiment. Save raw bytes,
decode results, source=simulation metadata, identities, generation and timestamps.
QUIESCE/generation from the external responder, bridge drain/free-line and explicit
NEW/ARM/INIT still apply. Bridge NEW/ABORT alone does not clear the peer. These
lab operations cannot be transferred to a real ECU as a recovery recipe.

## B — isolated protection fixture, separate from A

```text
Floating current-limited5V logic source -> J1.1 USB5V / J1.2 GND_L
Floating manual logic drive0/5V ----------> J1.3 D3; J1.4 D8 -> isolated instrument
                              [U1 barrier; no common ground/shield/USB]
Independent B1 battery ------------------> J3 -> V5_F / GND_F
Isolated source/load with current limit -> J2.1 DATA / J2.2 GND_F
Rated differential or battery instruments -> TP3/4/7/8/9 using GND_F
```

The Nano, PC, responder, responder pullup, USB cables and external M3a line board
are **physically absent**. A logic-source current limit of20mA is adequate for
U1-side static testing; use an isolated generator or manual level source for D3.
That drive must be powered by the same logic rail and track it down when it is
switched off: U1.INB must never exceed VCC1+0.3V in this test. Do not leave an
independently powered5V generator driving an unpowered isolator input.
GND_L and GND_F are not connected by the generator, oscilloscope, USB logger,
bench-supply earth strap, chassis, probe shield or a third instrument. Check those
paths unpowered with an appropriate meter. Never lift protective earth.
For this low-voltage lab gate, require >20M ohm between domains at the meter's
specified low test voltage, then <=1uA steady leakage in the matrix's±1V ground
offset fixture. Record instrument resolution. These limits are not high-voltage
withstand/insulation certification and do not permit a mains/hipot test.

The DATA source must support the specified polarity and a real current limit.
For negative tests use an isolated bipolar source or reverse an isolated source
only while unpowered. The physical source limit, resistance, overshoot and probe
rating must be measured, not assumed from a front-panel setting. A1k source used
for the24V pulse is a different case from the0.1R hard16V fault. Do not describe
the pulse's low energy as evidence against a direct battery short.

## Ordered bring-up

1. **Unpowered inspection:** verify every BOM/netlist pin, SS16 bands, BAT54 and Q1
   pin1 orientation, U1 F/DW marking, all NC pads, no RPU, J2.3 no trace, NanoVIN
   unused, R12/R13 are AC10 rather than AC01, R14 at the battery lead, barrier
   clearance/creepage and no external ground paths. Record photos and continuity.
2. **Rails without MCU:** in B, D3 held LOW, DATA disconnected. Start logic5V with
   a20mA limit. Connect B1 last. Verify LDO_IN>=6V, V5_F4.75–5.25V, B1 loaded8–9.6V,
   field draw<=20mA, no heating, regulator stability at min/max load. Stop on a
   current-limit event. Confirm actual output ceramic capacitance>=2.2uF.
3. **Idle/load, low-voltage only:** add the isolated artificial ECU source at
   4.75/5.25V,1/2.2k. Measure released current<=250uA, DATA>=3.3V (calculated worst
   >=4.20V), sink DATA<=0.6V, Q gate>=4.5V. Ramp DATA slowly; record both thresholds
   and hysteresis, compare with calculation/model envelope. Repeat15/25/35°C.
4. **Power/off and reset:** keep DATA at a defined normal source, stop traffic.
   Power each domain separately, then both orders, with0.1/1/100ms rails. Verify
   D3 reset LOW, Q release, no gate>=1.3V when commanded off, field-off V5_F<0.2V,
   USB-off rail<0.2V, off-target injection<=25uA, no parasitic powering. After a
   rail has collapsed, Q must release within1ms. The simplified model does not
   guarantee behavior while supply voltage traverses undefined logic regions.
   Keep the TX command logically asserted during separate loss-of-USB and
   loss-of-field tests; the electrical D3 level still tracks its own USB rail.
   Field restoration with D3 still HIGH can assert TX again: there is no hardware
   re-arm latch. Restore only with Stop/D3 LOW in the functional setup.
5. **Edges/bytes:** with a safe5V fixture, capture TP1/TP3/TP2 and TP6/TP7 as needed.
   Check both polarities, DATA C<=500pF, SENSE C<=20pF, comparator delay<=3us,
   total D3→D8 and incoming DATA→D8<=8us; check ringing and valid levels. Send
   00/FF/55/AA/back-to-back frames and both directions. No acceptance based on
   decode alone if voltage, temperature, leakage or timing fails.
6. **A/B functional fixture:** disconnect all power, assemble fixture A, run strict
   two-Nano acceptance. Capture worst comparator/ISR/echo timing while USB traffic
   runs. Record failures and stop; do not increase driver20us budget or200ms guard.
7. **Protection fixture B:** disconnect power; remove both Nanos/responder/USB and
   all fixture A circuitry. Reinspect isolation. Apply only the FAULT_MATRIX cases,
   starting at10mA and100ms; inspect, then50/100mA, and finally required0.5A limit
   for hard±16V with1s and up to10s exposures. At the low-limit stages the source
   will droop: label them as preliminary checks, not completed16V tests. Measure
   actual DATA voltage/current, SENSE minimum, gate, rail injection and temperatures
   for release/asserted/off-power combinations. Pulses follow only after DC checks.
8. **Post-fault:** disconnect, cool, inspect, remeasure leakage and thresholds;
   repeat5V byte test. Damage, altered leakage, unexpected gate turn-on, isolation
   leakage or inability to reproduce baseline means FAIL. Do not reset and silently
   continue. Preserve failed traces and the exact stimulus before revising hardware.

Stop at DATA current0.4A, SENSE<=−0.3V or>=38V, gate>=1.3V while off,
field/logic unpowered rail>=0.2V, full delay>8us, invalid logic amplitude,
oscillation, smoke/odor, or the temperature limits in SCHEMATIC. Approaching an
absolute maximum is already grounds to stop and redesign, not permission to hold
there. No arbitrary ground removal, hot-plug,48V bad-control or ESD experiment.

## Evidence record (copy into a new untracked measurement directory)

Record operator/date; hardware revision and photographs; exact component markings;
PCB/assembly differences; Nano versions/USB chips/clock; instrument model/serial/
calibration and probe capacitance; power/ground diagram including earth; ambient;
source voltage/R/current limit/duration/polarity/TX/power state; raw captures and
their SHA256; each measured minimum/maximum/limit/margin; failures and stop reason.
Keep screenshots with scales/units and original waveform files, not only exported
decoded bytes. Log actual source current-limit behavior and overshoot.

| Gate | Record / status |
|---|---|
| Assembly/net/ground review | NOT VERIFIED |
| Rails, draw, no MCU, stability | NOT VERIFIED |
| Load, thresholds, capacitance, timing | NOT VERIFIED |
| All off/reset/power sequences and stuck sink | NOT VERIFIED |
| M3a A/B functional exchange through new board | NOT VERIFIED |
| B positive/negative/pulse faults, temperatures and post-check | NOT VERIFIED |
| Actual isolation/leakage of board plus instruments | NOT VERIFIED |
| Vehicle qualification | NOT VERIFIED / outside current lab envelope |
| Specific ECU validation | NOT VERIFIED / M4 prerequisites missing |

Only measured rows may change to PASS. Software/AVR/SPICE results do not fill any
of these rows. Passing all lab rows still does not authorize a vehicle session.
