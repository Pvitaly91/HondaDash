# Reviewable arithmetic

`check.py` uses Python standard-library arithmetic and enumerates 512 combinations
of independent supply, resistor, input-offset/bias and clamp-leakage endpoints,
then both output-low endpoints. Results go to `build/electrical/calculations.csv`
and the JSON summary. PASS means the stated inputs satisfy the stated inequality.
It does not make an unmeasured input a manufacturer guarantee.

The comparator switches at

```
Vdata = (Vref + Voffset)*(1 + R1/R2 + R1/R3)
        - Vout*R1/R3 + (Ibias + Ileak)*R1
Vref = V5_F*R5/(R4+R5)
```

R1/R2/R3=220k/68k/1M; R4/R5=78.7k/10k. Independent1% resistors,
4.75–5.25V rail, ±4mV offset, ±50nA bias,0–2uA clamp leakage and VOL0–0.55V
produce rising2.161–3.200V, falling1.245–2.051V, same-corner hysteresis>=0.897V.
The bounds include a **project extrapolation** of offset below the datasheet's5V
test condition and leakage/temperature limits requiring measurement. Vout HIGH
uses V5_F-0.04V, bounding the R6/feedback load. Thresholds of the unknown ECU input
do not follow from these adapter thresholds.

At Rpull=1k and Vpull=5.25V, R12+R13 at+5% and Q Rds=.32R give busLOW<=0.234V,
sink<=5.25mA. The receive fixture's LOW must separately be <=0.6V. At released
line load<=250uA, Vpull>=4.75V and Rpull<=2.2k, idle>=4.20V. Normal RX network
load is about (5-1.35)/220k=17uA; count D1/Q1 leakage as well. Feedback injection
with DATA held0 is limited by R3 and is a separate off-target measurement.

For a **hard16V fault** with no helpful external series resistance, the internal
44R at-5% limits current to0.383A. Each22R dissipates<=3.063W, below the project
4.2W ceiling (half AC10's70°C rating). Q1 at.32R dissipates<0.047W, an estimated
<11.5°C rise using244K/W; a negative D1 path dissipates<0.288W using0.75V.
SS16's thermal resistance is typical, not a system guarantee. Thermal coupling,
mounting and actual diode drop need measurement. No arbitrary-duration direct
battery short claim follows: the specified experiment is current-limited,10s
maximum, with staged approach and temperature stop limits.

R1 negative fault current at-16V is<74uA; BAT54's0.240V bound at100uA is specified
for25°C short pulses. Thus indefinite negative DC across temperature is **not
proven**. The negative scenarios test a finite diode equation and a physical gate
requires SENSE>-0.3V throughout the actual fault. No ideal clamp enforces PASS.
R1 power under16V is<1.2mW. Positive DATA reaches an attenuated comparator input;
there is no positive TVS and no guarantee above the matrix voltage/time envelope.

For release, `tau <= Rpull*Ctotal`; worst2.2k*500pF=1.10us before loading and
threshold correction. Loaded threshold crossing uses
`t = tau*ln((Vfinal-Vinitial)/(Vfinal-Vthreshold))` where Vfinal includes leakage.
The additional SENSE pole has Rth about49k and C<=20pF (about0.98us). These poles,
hysteresis, gate motion and comparator delay are simulated together; they must
not be replaced by a single unloaded10–90% estimate. Rising and falling delays
are reported separately from signal amplitude and decoding criteria.

| Timing item | Minimum / upper allocation | Evidence |
|---|---|---|
| ISO TX + ISO RX |12–34ns | Two DS6–17ns channels at5V±10% |
| LVC buffer | Datasheet ns switching; gate charge dominates | Actual2.2nF gate loading exceeds the logic timing test load |
| Gate switching |0–1us allocation |100R, finite buffer drive, Q charge6nC maximum at its specified test, C10; physical gate required |
| Comparator |0.3–3us allocation |0.3/1us typical DS; no maximum guarantee |
| DATA + SENSE RC | Derived per case, polarity asymmetric | Swept physical resistances/capacitances, not an ideal delay |
| Entire D3→D8 / incoming DATA→D8 |<=8us acceptance ceiling | Numerical waveform check and mandatory scope capture |

The existing bit is104us. A conservative remaining half-bit margin is
`52 - 9.5*2.08 - 20 - 2 - 8 = 2.24us`, using the prior±2% clock,20us maximum
sample lateness and±2us jitter envelope. The harness also checks every start/data/
stop centre on actual simulated waveforms for102/106us peer periods, both lateness
endpoints and both jitter signs. The20us software guard and200ms transaction
observation window are unchanged. The old M3a≤2us electrical criterion belongs
to its original board; this new board has its own8us total allocation and margin.
If scope results exceed it, this revision is incompatible until the electronics
are revised; do not increase firmware lateness or hide an echo error.

At B1>=8V, D3P drop<=0.75V and20mA through47R+5%, LDO_IN>=6.263V. The LDO's
50mA dropout maximum is0.5V at its stated conditions. At9.6V/20mA its conservative
power estimate is92mW. A downstream battery-feed short puts<=2.065W in R14,
below AC03 P70=2.5W; actual battery/wire temperature still gates acceptance.
This is independent battery power, not vehicle12V power and not a DC/DC-isolation
certificate. Gate pulldown, worst Ioff and the declared Cgd bound predict release
within1ms and off-state gate<1.3V, including the specified24V step; Cgd is a
measurement allocation, not the MOSFET's typical15pF converted into a maximum.
