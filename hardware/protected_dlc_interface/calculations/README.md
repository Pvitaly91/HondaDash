# Revision B reviewable arithmetic

`check.py` uses Python standard-library arithmetic, enumerates512 supply/
resistor/offset/bias/leakage corners and both output-LOW endpoints, then checks
TX, power and failsafe budgets. Results are `calculations.csv` and the JSON
summary under ignored `build/electrical/`. A numerical PASS means declared inputs
satisfy an inequality; it does not turn an allocation into a manufacturer maximum.
The final source has135 BOM/pad rows and52 conditional calculation checks.
**Hardware NOT VERIFIED. Negative DC/off-state protection remains OPEN.**

Revision A source and reports are reproduced separately by `simulation/baseline.py`
from `b2cf4ea12a783d4eb81736383f51790b361d9b04`, without resetting/checking out the
working tree. Its continuous-HIGH gap is an expected baseline failsafe failure.
The new report cannot relabel historical A checks as revision B checks.

## RX and load

```
Vdata = (Vref + Voffset)*(1 + R1/R2 + R1/R3)
        - Vout*R1/R3 + (Ibias + Ileak)*R1
Vref = V5_F*R5/(R4+R5)
```

R1/R2/R3=220k/68k/1M, R4/R5=78.7k/10k. Precision-resistor factors0.9895/1.0105
include1% and50ppm/K over±10°C from25°C. With4.75–5.25V, offset±4mV, bias±50nA,
leakage0–2µA, LOW0..0.55V and HIGH=V5_F−0.04V, rising thresholds are2.157–3.205V,
falling1.242–2.056V and same-corner hysteresis≥0.896V. Offset/VOL below5V and
leakage over15–35°C are conditional allocations, not widened datasheet guarantees.
The constant leakage term is a switching-corner allocation, not a physical diode
law through negative SENSE. Actual resistor temperatures, not ambient alone,
must fit the TCR range. [RX_REVIEW](../RX_REVIEW.md) separates operation/stress/
fault function/survival/recovery.

Q1's0.32Ω and Q2's0.5Ω resistance allocations plus44Ω at+5% give bus LOW≤0.236V
at5.25V/1kΩ. Q2 is driven by the loaded supervisor PG node; its0.5Ω is a measured
acceptance allocation and is not the4.5V datasheet limit applied at a lower gate
voltage. At released load≤250µA,4.75V/2.2kΩ gives idle≥4.20V. Include RX division,
D1 and MOS leakage. The retained M3a responder has its own stricter idle/LOW/edge
limits; functional fixture A must satisfy both.

For powered-off RX, the10µA input-to-rail allocation plus the direct
SENSE→R3→R6→V5_F→R11 path gives roughly9.1mV at+16V under those restricted paths.
It is not a complete off-rail leakage guarantee. Input voltage tolerance alone
does not prove no back-power, and the actual V5_F<0.2V criterion remains physical.

## Independent cutoff and qualified re-arm

U5 LTC6994-1 uses RSET200kΩ/N512: nominal2.048ms. The datasheet's full-temperature
±3% accuracy includes supply/temperature drift; those terms are not added twice.
External resistor error is`eR=.01+50ppm/K*10K=.0105`. PCB SET leakage is allocated
±10nA, with VSET≥0.97V. Leakage changes oscillator current; use reciprocal bounds,
not a first-order correction presented as a strict maximum:

```
tmin = tnom*(1-eR)*.97 / (1 + Ileak*Rnom*(1-eR)/.97)
tmax = tnom*(1+eR)*1.03 / (1 - Ileak*Rnom*(1+eR)/.97)
```

The resulting U5 bounds are **1.961698850–2.136039569ms**. The1µs project
NOR/clear/gating/buffer/gate-discharge allocation makes own release≤**2.137039569ms**,
below5ms. The timer datasheet has typical output propagation, not a guaranteed
maximum covering every assembled load; the1µs term is a sensitivity and physical
gate. It does not assert DATA HIGH when another device or negative/body path
holds the line LOW.

The longest legal TX request is start plus eight zero bits plus actual release
latency. `check.py` includes slow-clock nine-bit duration,40-tick COMPA lateness,
the compiled131-cycle stop-output tail or16µs generic sensitivity tail, a two-cycle
stamp and0.1µs input-width allocation. It checks both against a conservative
**1.025ms legal LOW envelope**. The timer minimum remains0.936698ms above that
envelope. A nominal-only936µs or clock-only956µs calculation is not the production
release bound. Minimum ordinary stop is`104µs/1.02 =101.961µs`, exceeding the1µs
timer-reset allocation. All256 bytes/back-to-back0x00 and full fixtures exercise
these conditions; no stop-bit shortening or timer-period retry is permitted.

U6 debounce and U17 released-button qualifier use RSET249kΩ/N4096,
nominal20.398080ms, with reciprocal bounds **19.528778949–21.285842342ms**.
The5ms switch bounce is below their minimum. U18 remembers a qualified physical
button release plus inactive TX plus healthy power. Timeout/unsafe power clears
readiness and arm. A button held through startup cannot create readiness; a new
press is accepted only after the release qualifier. Firmware/USB commands have
no path to clear these latches. SET leakage and<10pF stray capacitance remain
layout/assembly measurement gates.

G01 supervisor100k/10.2k dividers give nominal fall4.375588V. Including±2% threshold,
up to3% hysteresis, resistor/TCR corners and±25nA SENSE bias gives fall
**4.204737–4.551579V**, rising maximum **4.688051V**, below4.75V startup rail.
CT open release delay is12–28ms. This safety floor prevents the model from relying
on ISO/timer logic in undefined supply regions; it does not certify valid normal
RX timing at4.3V. Q2 is driven directly from raw supervisor RESET through2.2kΩ;
Schmitt U12 cleans PG only for logic. Its input/gate/leakage load must fit the
supervisor's low-supply sink conditions. USB health is supervised separately and
optically clears field permission, with CTR/loss-delay allocations requiring
measurement. Stable power alone never restores arm.

R24/R36=2.2kΩ bound an initial5.25V gate-capacitor discharge to about2.42mA.
This is below RESET's5mA **stress** ceiling; it is not a guaranteed dynamic VOL
or propagation bound. The low-supply VOL tables use0.4mA or1mA conditions, not
that peak. The model's finite100Ω dynamic sink and50µs assertion allocation,
with a100µs PG gate-release measurement gate, are explicit project assumptions.
Test their sensitivity and actual ramps; do not cite the absolute current rating
as evidence of functional brownout release. The healthy-rail continuous-TX cutoff
uses the separate timer/latch/buffer path.

## Held logic rail and raw-Nano guards

U22 LM66100 adds a held logic island: direct raw VIN, output→R37=22Ω AC03→V5_L,
CE=V5_L after R37. The resistor cannot be moved ahead of VIN: at limited reverse
current the ON switch's small voltage difference could remain below the80mV
maximum CE turn-off threshold. C28/C29 each4.7µF provide9.4µF nominal but require
**combined effective C≥4.7µF** at bias/temperature/age. Held load is allocated
≤10mA, including the original ISO/guard/supervisor currents. The model's extra
6.2mA is adverse model bookkeeping, not a fitted physical resistor.

With Rmax=23.1Ω and LM Ron0.14Ω allocation,
`Vstart=4.75−.010*(23.1+.14)=4.5176V`. Before the finite switch opens, conservatively
bound reverse current by `Ipk=5.25/(22*.95)=0.251196A`. For15µs blocking and a
250µs total USB-health window:

```
Vheld_min = Vstart - (Ipk*15µs + Iheld*250µs)/4.7µF
          = 3.183995V > 2.25V
```

The reverse interval overlaps that250µs window; load is not omitted during it or
charged twice. This peak-current bound avoids treating the diode as instantaneous
or using only`C*deltaV/I`. Turn-off15µs, startup150µs and Ron0.14Ω are project
allocations; the datasheet has typical dynamic timing, not guaranteed maxima at
this bulk/load. Finite body diode, reverse leakage and blocking are modeled.
Sensitivity/poor-hold controls and physical rail-order measurements are required.
Own permission must clear before V5_L reaches the ISO undefined interval.

R37 peak power≈1.319W is below AC03's2.5W70°C rating, avoiding an unsupported MRS25
overload inference. R38=4.7kΩ bounds25µA aggregate raw-rail injection to0.118734V;
U24 Ioff10µA plus R10 bounds D8 to about0.101V when raw power is absent. These
are separate leakage allocations, not zero-backfeed or full off-state proof.

Manual arm data/TX must stay stable at least1µs around qualified ARM_CLK, and the
bridge is quiesced throughout readiness/press qualification. Setup/hold violations
and an unquiesced manual action are **OPEN**: the ideal DFF model does not prove
metastability/race safety. Steady active-TX rejection is a different tested case.

## Fault currents and thermal estimates

For the unchanged hard16V/≤0.1Ω/0.5A/10s target,44Ω at−5% conservatively limits
current to0.383A without helpful diode drops. Each22Ω resistor dissipates≤3.063W,
below the project4.2W ceiling. A0.5Ω individual MOS resistance gives≤0.0733W and
about17.9°C rise using244K/W. Current sharing, PCB mounting/coupling and actual
device temperatures are not established by this estimate. Timeout releases the
normal controlled sink; it does not remove a drain-source short or shorten the
external stimulus.

For negative DATA, D1 and body/channel paths still return current through44Ω
with gates off. Assuming0.75V across D1 gives about0.287W; its quoted thermal
resistance is typical and the voltage maximum is a short-pulse25°C figure.
R1 current at−16V is<74µA and resistor power<1.2mW, but BAT54's240mV at0.1mA is
also a≤300µs/25°C pulse specification. Those numbers cannot prove10s DC clamp
voltage, no damage, valid comparison or recovery across power/temperature.
**Negative DC/off-state targets remain OPEN and excluded from protection PASS**,
even if finite-diode SPICE waveforms satisfy numerical limits. No thermal model
or temperature measurement is implied. No positive TVS or automotive surge
qualification has been added.

## Frontend and actual production driver

For bus release, `tau<=Rpull*Ctotal`;2.2kΩ*500pF=1.10µs before loading/threshold
correction. A crossing uses
`t=tau*ln((Vfinal−Vinitial)/(Vfinal−Vthreshold))`, with leakage included in Vfinal.
SENSE's R1||R2||R3≤49.898kΩ and20pF allocation give tau≤0.998µs. These are
separate poles and are simulated together with gate motion, hysteresis and finite
comparator delay. Probe/device capacitance must satisfy the measured budgets.

| Timing item | Bound / evidence |
|---|---|
| ISO TX/RX | Each6–17ns at the datasheet's5V±10% conditions |
| LVC/gates | Datasheet switching plus actual gate-loading allocation; R7=220Ω bounds peak drive |
| Comparator |0.3–3µs sensitivity range;3µs is a project gate, not manufacturer maximum |
| DATA/SENSE RC | Per-case threshold crossing; rise/fall are asymmetric |
| D3→DATA, DATA→D8, D3→D8 echo | Separate extracted/calibrated paths,≤8µs frontend acceptance envelope |
| Capture filter/quantization |0.5µs nominal allocation; ICNC1 four-cycle filter plus Timer1 timestamp rounding |
| CAPT/COMPA | Declared existing dispatch/sample envelope;40-tick lateness, not measured Nano latency |

Nominal conservative receive margin is
`52−9.5*2.08−20−2−8−.5 =1.74µs`: oscillator, ISR, relative edge jitter,
frontend and capture/filter each appear once. The production integration sweeps
both skew signs, capture/COMPA lateness and actual waveform asymmetry; it does not
infer validity outside its sampled grid. Start capture changes the sampling origin,
so the start delay must not be charged again to every bit centre.

The old D3 stop anchor can produce DATA HIGH`104µs+fall−rise`: the explicit
0.25/1.50µs corner gives102.75µs despite good echo. Revision B's17-tick8.5µs own
stop settlement gives111.25µs there and at least8.33µs settling at the fast clock
corner, covering the8µs release allocation. Nominal final own stop is112.5µs and
frame1048.5µs. Data bit208 ticks/104µs, lateness40 ticks and200ms observation remain
unchanged. An independently captured fast peer may start before conservative own
TX-complete; it is handled using the separate earliest-peer anchor, not discarded.

The integration builds the actual baseline/current `one_wire.cpp` state machines,
not another decoder. Precomputed RX traces cannot respond to driver output;
closed-loop calibrated events do respond to output release after echo/line faults.
See [integration](../../../docs/PROTECTED_INTERFACE_DRIVER_INTEGRATION.md) for
feedback limits, reports, capture handling and reproducible command. A physical
scope result exceeding the declared envelope requires electronics revision;
do not raise lateness, disable echo or reinterpret model PASS as AVR execution.

## Separate battery and USB budgets

Field static allocation is **16.733567mA**: declared ISO DC maximum, comparator
allocation, bleed, RX pullup, reference/gate/miscellaneous load, three timers,
latches/gates, optical collector, dividers and pressed button. The steady design
budget is18mA; measured normal field acceptance remains20mA with switching
headroom. The model's2.5mA ISO resistor is not a manufacturer maximum. Startup
capacitor charging is logged separately from steady draw.

USB-health LED bounds are7.859742–11.013074mA using390Ω and declared LED/Q3
corners, plus logic-side ISO/supervisor/divider draw. CTR80% over15–35°C is a
project gate; the datasheet minimum160% is only at5mA/VCE5V/25°C. Saturated turn-off
has typical figures only; sweep and measure loss detection. R36=2.2kΩ limits Q3
gate discharge into the USB supervisor; R24 does the same for Q2. Neither is an
isolation-ground connection.
Raw logic steady acceptance is25mA, including the held-load allocation, LED,
raw bleed, guards and dividers. Cold preflight uses30mA current limit; a separately
bounded300mA/≤5ms electronics-only charging window can reproduce fast rises, then
returns to30mA. Actual ramps are recorded; a current-limited preset is not a
waveform measurement. Nano/PC are absent in these tests.

With B1≥8V, assumed D3P drop≤0.75V and20mA through47Ω at+5%, LDO_IN≥6.263V.
At9.6V/20mA the conservative LDO dissipation estimate is92mW. A downstream feed
short gives≤2.065W in R14 versus AC03 P70=2.5W; battery/wire/component temperature
still gates acceptance. These are laboratory battery calculations, not vehicle
power design or certified insulation. Effective output capacitance, stability,
power ramps, off-state gate/rail behavior and fault recovery all remain physical
acceptance gates.

R39/R40 each10kΩ terminate the two logic-side ISO guard nodes. The20µA
aggregate allocation gives`20µA*10k*1.0105=.2021V`; ISO off-output injection is
an assumption, not manufacturer Ioff. Healthy loading≤0.54mA exceeds the100µA
VOH test, so U23 uses the3.8V4.5V/32mA datasheet anchor plus a measured gate
against U1's maximum3.675V HIGH threshold. RX output/input margins are checked
separately. Voltage-controlled models are not whole-board power-conserving
models; actual held current≤10mA must be measured, including both pulldowns.

Healthy field loss gives`Ceff*(4.2−2.25)/I >=214.5µs` for Ceff≥2.2µF/load≤20mA.
This exceeds the100µs PG release allocation within declared ramps≥100µs. An
arbitrary externally forced collapse/rail short or violated bounds is OPEN and
cannot inherit that release claim.

The final three BAT54 field rail clamps add finite diode capacitance/leakage and
R11 becomes680Ω. Static field allocation16.733567mA fits the18mA steady design
budget and20mA measured ceiling. Q1 gate HIGH calculation is4.631987V. With the
added gate capacitances, modeled cutoff RC+logic allocation is0.922638µs, still
inside the unchanged1µs gate term. Conservative off injection uses
`(200+13.4+6)µA *680Ω*1.0105 =0.150759V`; the200µA Q1 branch is an adverse
sensitivity, not its manufacturer nominal leakage or D1's misplaced current.
D1 leakage returns DRAIN→GND_F; MOS channel leakages are modeled separately.

A pre-clamp+24V/off transient reached gate≈2.8585V, Q1 VGS≈−23.2V and U2 output
≈−1.65V. The new oracles check actual source-relative VGS and driver output, not
a proxy gate voltage. Diode0.45V/2µA allocations and thermal/DC qualification
remain OPEN until measured. The held fixture-load law is now C1 continuous below
1V and exactly full above1V: u=clip(V5_L/1V,0,1), I=Iextra*(3u²−2u³). This
removes a nonphysical constant-current discontinuity at zero without changing
load at any valid≥2.25V logic state or relaxing acceptance limits.
