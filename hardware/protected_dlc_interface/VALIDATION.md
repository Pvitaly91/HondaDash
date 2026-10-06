# Revision B physical validation and blank evidence record

**No physical measurement has been performed. Every physical row starts NOT
VERIFIED.** Calculations, SPICE, host driver tests and AVR compilation are
preflight evidence only. A competent bench operator must review the completed
assembly, sources and instruments. No vehicle, real ECU, VIN, battery directly
on DATA, automatic upload, port search, PCB order or Gerber generation is part of
this plan. All wiring, mode/connector changes and source-polarity changes occur
with **every supply disconnected**.
Before rewiring, measure raw USB, V5_L and V5_F below0.2V; the held capacitor
can retain charge after its source is disconnected, so no fixed waiting time
is assumed to discharge it.

Negative DC and off-state protection targets are **OPEN**. The matrix retains
±16V,≤0.1Ω source,0.5A limit and10s target; it has not been shortened or softened
to claim PASS. Stop at a stress limit and keep the target OPEN if it cannot be
completed safely. Functional exchange, cutoff, fault survival and isolation have
separate records. Hardware re-arm is local SW1; USB NEW/ABORT/ARM/INIT does not
clear its latch. Firmware has no ARMED/LOCKED input and cannot report the exact
cause from a USB timeout. Inspect local test points and expected echo/line fault.

## Fixture A — normal two-Nano exchange only

```text
PC USB-A -> Nano bridge-bench -> J1: USB5V / GND_L / D3 / D8
                                  [U1 data barrier; U14 USB-health barrier]
independent B1 -> R14/D3P/U4 -> field circuit -> J2 DATA ----- responder BUS
                              local SW1        J2 GND_F ---- responder GND
PC USB-B -> Nano responder-bench local5V ------ one pullup -- DATA
```

Remove all original bridge-side M3a line components and pullup listed in
[SCHEMATIC](SCHEMATIC.md). Keep the original responder line circuit. Do not join
the Nano5V rails. Exactly one artificial ECU pullup belongs to the **separate
responder fixture**, not the adapter. Select/measure source and pullup inside the
4.75–5.25V/1–2.2kΩ domain; nominal2.2k±1% can exceed the2.2k limit, so measure/select
≤2.200k or use2.0k±1%. Total DATA capacitance includes both boards/cable/probes.

The retained responder still requires its original **DATA idle≥4.5V, LOW≤0.4V
and receiver edge-to-valid≤2µs**. It must satisfy those limits as well as the new
adapter's limits. Do not substitute the adapter's3.3V HIGH acceptance criterion
for the responder's4.5V criterion. Stop if added loading invalidates either board.
No above5V fault is allowed with the responder attached.

Two USBs on one PC join GND_L/GND_F externally. Shared chargers and scope earth
can do the same across separate PCs. Fixture A can establish bytes/timing only,
**not isolation or safe ground-fault handling**. The adapter does not protect the
retained responder against the new fault envelope.

Use the existing explicit-port procedure in
[TWO_NANO_ACCEPTANCE](../../docs/TWO_NANO_ACCEPTANCE.md), after electronics checks
below. Both strict bench identities must pass before line TX. Firmware variants
remain `bridge-bench` and `responder-bench`; synthetic/bridge-lab do not use this
GPIO fixture. No new firmware identity or fifth image is needed. Any upload needs
a separate user command naming the exact port/Nano variant.

With traffic stopped and D3 LOW, verify both normal rails, PG_F and local latch;
release SW1, then perform the explicit local re-arm action. If unable to arm,
diagnose locally; do not bypass latch/gate wiring. Only then start the coordinated
software experiment. QUIESCE/generation from our responder, bridge drain/free-line
and explicit NEW/ARM/INIT retain their software meaning. Bridge NEW/ABORT alone
does not clear the peer. These steps cannot be transferred to a real ECU as a
recovery recipe.

Record init, RPM/TPS/ECT requests, fixtures A/B/Boundary, fragmented USB, stop,
failed/late RX and a deliberate new experiment. Save raw bytes, decoded results,
source=simulation metadata, strict identities, generation and timestamps.

## Fixture B — electronics and isolated faults

```text
floating current-limited5V source -> J1.1 USB5V / J1.2 GND_L
manual/generator0..logic rail ----> J1.3 D3; J1.4 D8 -> isolated instrument
                         [both barriers; no USB/earth/shared shield]
independent B1 battery ----------> J3 -> V5_F / GND_F; local SW1 only
isolated bipolar source/load ----> J2 DATA / GND_F
isolated/differential instruments -> D3/DATA/D8, gates, rails, local test points
```

Nano, PC, responder, USB cables and the whole fixture A line circuit are
physically absent. The cold preflight logic source has a **30mA current limit**;
use a slow initial rise and stop if steady logic draw exceeds25mA. The extra
isolated USB-health LED draws about8–11mA,
so revision A's U1-only current estimate is not revision B's total. Characterize
actual ramps/current-limit action; a source that droops is not a valid5V test.
Normally drive D3 from that same raw logic rail and track it down on power loss.
The separate already-HIGH test may hold an **independent isolated5V** on guarded
**U23.A/J1.D3**, within its5.5V input range, while raw USB is removed/restored.
This special fixture has no Nano or PC and uses GND_L only. Never drive bare
U1.INB or bypass U23; record raw/held rails and input overshoot. U23 output supplies
U1.INB from the held rail; do not bypass either raw/held GPIO guard.

R37 limits cold charging to about0.252A. After30mA preflight, the separate
electronics-only source may use a **300mA limit for≤5ms cold/restore charging**,
then return to30mA steady. Nano/PC/responder remain absent. Use a programmable
source that bounds this peak window, or leave the unreproduced fast-rise/restore
case OPEN. Measure actual rail/current waveforms: a nominal100µs preset is not
a100µs interface ramp if current limiting changes it. Initial slow bring-up does
not establish the fast case. This is a validation plan only; no such operation
has been performed.

Do not join domains through generators, probes, logger USB, bench-supply earth
straps, chassis or shields. Never lift protective earth. Verify paths unpowered
with an appropriate meter. For the separate low-voltage isolation gate require
>20MΩ at the meter's stated test voltage, then≤1µA steady leakage in the matrix's
±1V/1kΩ/≤1mA ground-offset test. Record resolution and transient current. This
is not mains/hipot/withstand certification.

Use an isolated bipolar DATA source or reverse an isolated source only when
unpowered. Measure its resistance/current limit, applied voltage, ringing and
probe rating. The±24V/1kΩ/1ms pulse is a separate limited-energy case from the
hard±16V/0.1Ω/10s target; neither represents an arbitrary battery short.

## Ordered bring-up

1. **Unpowered assembly:** inspect every generated BOM/pin/net, U1 F/DW package,
   U14 option6 orientation, Q1/Q2/Q3 gate/source/drain, SS16 bands, BAT54 pins,
   SW1 pairs1+2/3+4, U22 VIN/CE/OUT/ST pins, U23/U24 guards, NC pads and J2.3
   no trace. Confirm R12/R13 are AC10, R37 is AC03 after U22.OUT with CE after
   the resistor and direct raw VIN, R38 raw bleed, C28/C29 held bank, R14 is
   at the battery lead, no DATA pullup or Nano VIN, and no external ground bridge.
   Verify SET/DIV/bypass wiring, clearances, heat separation and touch protection.
   Save photographs and continuity records; do not order/manufacture a PCB here.
2. **Electronics rails/default release:** in B, D3 LOW, SW1 released, DATA absent.
   Power logic with30mA limit and slow initial rise, then B1 last. Require loaded
   B1≥8V, LDO_IN≥6V, raw USB/field4.75–5.25V, held V5_L≥4.5V, held draw≤10mA,
   steady normal field≤20mA/logic≤25mA, no heating/oscillation, effective LDO
   output capacitance≥2.2µF and combined held capacitance≥4.7µF at bias/temp/age.
   Record startup charging currents/ramp
   separately; a steady current budget is not a zero-transient claim. Observe PG
   release12–28ms, ARMED LOW
   and both own sink paths released. Power alone is not arm permission.
3. **Normal short pulses and RX:** add the isolated normal DATA source and select
   declared source/capacitance corners. Release/press local SW1 with inactive TX
   and stable power. Observe READY_REQ/READY_QUAL/REARM_READY: the released button
   and inactive TX must qualify continuously for at least19.528778ms before the
   fresh press/debounce can arm. Measure released load≤250µA, DATA LOW≤0.6V,
   Q1 gate≥4.5V and Q2 PG gate/resistance within its allocation. Apply short
   00/FF/55/AA and normal stop bits. Measure threshold/hysteresis with a slow
   ramp; repeat15/25/35°C and note probe capacitance.
4. **Continuous TX cutoff and retained lock:** hold D3 HIGH under only the normal
   source. Capture TX_F, TIMEOUT, ARMED, Q1/Q2 gates and DATA. Qualifier bounds
   are1.961698–2.136040ms; own gate-release allocation adds1µs, total≤2.137040ms
   and always≤5ms. Keep HIGH for60s: no periodic reassertion. Inject brief LOW
   glitches; they must not arm. A different source/short may still hold DATA LOW
   after our sink releases; distinguish those currents.
5. **Explicit re-arm:** attempted button press during active TX must fail. Release
   TX, release the button, allow debounce to return, then press with stable power.
   Verify REARM_READY qualified after the last fault, then the fresh edge can arm
   once; bounce/held button must not make periodic retries. Minimum two-edge
   debounce19.528778ms exceeds the switch's5ms bounce.
   Keep the bridge TX quiesced throughout release/press qualification and at
   least1µs around ARM_CLK; preserve the recorded quiescence. A coincident TX/
   clock transition violating setup/hold is OPEN, not proven by ideal DFF logic.
   Record ARMED/LOCKED locally; no software command is assumed to read or clear it.
6. **Power loss, ramps and brownout:** stop traffic; use the source/peak-window
   limits above for0.1/1/100ms shapes and record actual input ramps,
   both power orders and repeated brownouts. Test D3 LOW and D3 logically held
   HIGH tracking its logic rail, including HIGH throughout restoration. Also test
   the independent isolated5V HIGH only on guarded U23.A with Nano/PC absent.
   Field or supervised USB loss must clear arm; restored power/HIGH cannot restore sink.
   Test SW1 already held during power-on and throughout each power cycle with
   D3 LOW and HIGH: no arm is permitted until the button is released, inactive TX/
   healthy power qualify READY, and a fresh press occurs. Record thresholds,
   PG/optical loss delay and all undefined supply intervals; do not label D8
   valid there. Capture raw USB, ISO_FEED, V5_L, PG_L, USB_BAD, ARMED and gate:
   blocking≤15µs/startup≤150µs are measured allocations; permission/gate must
   clear within the250µs health window before V5_L reaches2.25V. D3 LOW during
   fast USB loss must never create an own sink pulse, unlike the preserved
   pre-hold32.73µs model counterexample. Check both GPIO guard leakage paths.
   After rail collapse own sink release must satisfy1ms allocation. Held V5_L
   is intentionally powered during hold-up and is not a raw off-rail criterion.
   After the specified settling/discharge, field/raw off rails<0.2V, D8<0.2V,
   off gates<1.3V and unpowered-target injection≤25µA are
   physical gates, not guarantees from the model; off-state protection stays OPEN
   until the required combinations are actually measured.
7. **D3/DATA/D8 timing and bytes:** capture separate D3→DATA, DATA→D8 and D3→D8
   rise/fall paths, start-edge shift, width distortion and release. Require normal
   DATA200–500pF, SENSE≤20pF, comparator≤3µs and total accepted frontend delay≤8µs.
   Test all256 bytes, back-to-back0x00, both directions and actual raw fixtures.
   Confirm the revised own stop duration/TX-complete anchor,17-tick settling and
   fast peer handover. Production frame is1048.5µs nominal; data bit104µs and
  40-tick/20µs lateness remain. No passing decode overrides a failed analog gate.
8. **Normal A/B exchange:** disconnect all supplies, assemble A, and perform the
   explicit local arm plus strict two-Nano acceptance. Capture worst edges/echo
   while USB traffic runs. Preserve late/trailing/fault logs. Do not raise ISR
   lateness, disable echo or alter the200ms observation window to pass.
9. **Separate B fault tests:** disconnect power; remove both Nanos, PC/USB,
   responder/pullup and all A wiring. Reinspect B. Apply only FAULT_MATRIX cases.
   Start10mA/100ms, inspect, then50/100mA, and finally the specified0.5A limit at
   ±16V/≤0.1Ω with1s then10s target. Drooping low-limit stages are preliminary,
   not completed16V tests. Keep the external10s stimulus even when hardware
   timeout releases own sink. Record actual DATA/SENSE, diode/channel currents,
   gates, off rails and temperatures for asserted/released/locked/all power cases.
   Stop before a stress limit; inability to reach the unchanged target means OPEN.
   Negative gate-off current is expected through D1/body paths. Pulses follow only
   after DC review; no arbitrary ground-loss,48V control, hot-plug or ESD experiment.
10. **Post-fault recovery and isolation:** disconnect, cool and inspect; repeat
    leakage, thresholds and timing at declared component temperatures and then
    normal bytes. Altered behavior, unexpected turn-on or damaged parts means FAIL;
    do not reset and silently continue. Finally use the separate domain-leakage
    setup without A's USB/earth links. Record low-voltage isolation independently;
    it does not establish automotive or high-voltage qualification.

Stop at DATA current0.4A, SENSE≤−0.3V or≥38V, an off gate≥1.3V, unpowered
raw/field rail≥0.2V after settling, frontend delay>8µs, invalid logic amplitude,
unrequested own sink with D3 LOW, lost hold-before-inhibit order, oscillation, smoke/odor
or the temperature limits in SCHEMATIC. Approaching an absolute maximum is already
a reason to stop/redesign; it is not permission to hold there. Thermal arithmetic
and SPICE without a thermal model do not measure component temperatures.

## Evidence record

Copy this blank record into a new ignored measurement directory. Include operator/
date, revision/commit, photographs and every assembly deviation, component markings,
Nano/clock/USB versions where relevant, instruments/serials/calibration/probe
capacitance, full power/ground/earth diagram, ambient and component temperatures,
source voltage/resistance/current limit/duration/polarity/TX/power/button state,
raw waveform files with SHA256, numeric extrema/limits/margins and stop reasons.
Keep original traces with scales/units and actual source current-limit/overshoot;
decoded bytes alone are insufficient.

| Physical gate | Status |
|---|---|
| Assembly and domain review | NOT VERIFIED |
| Rails, default release and static/dynamic power | NOT VERIFIED |
| Short pulses, all bytes, cutoff and retained latch | NOT VERIFIED |
| Re-arm, rail ramps, USB/field loss and restoration | NOT VERIFIED |
| DATA load/threshold/capacitance and D3/DATA/D8 fronts | NOT VERIFIED |
| M3a A/B exchange through revision B | NOT VERIFIED |
| Specified positive/negative/pulse fault targets and recovery | NOT VERIFIED; negative DC/off-state protection OPEN |
| Actual assembly/instrument isolation | NOT VERIFIED |
| Automotive qualification | NOT VERIFIED / OUT OF SCOPE |
| Specific real ECU | NOT VERIFIED / M4 prerequisites missing |

Only measured records can change these rows. Software, instruction analysis and
model PASS never fill them. Passing laboratory gates still does not authorize a
vehicle session.

Step1 additionally verifies R39/R40 each10kΩ from TX_L_ISO/RX_L_ISO to GND_L.
Step6 captures actual U1.INB and U24 input relative to their supplies, including
aggregate leakage and both pulldowns; no unlisted default clamp is assumed.
Healthy field decay requires measured Ceff≥2.2µF/load≤20mA and ramps≥100µs. A
forced rail collapse/short outside these bounds remains OPEN and is not a
physical test authorized by the ordinary power-loss case. Retain these limits
in the copied measurement record.
