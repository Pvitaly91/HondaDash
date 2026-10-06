# Revision B RX, negative DC and off-state review

The selected revision retains **LM393BIDR, BAT54,215, R1=220kΩ, R2=68kΩ,
R3=1MΩ and the existing reference divider**. No replacement clamp is implied.
The supported functional model domain is the normal nonnegative, nominal5V
laboratory DATA source in [REQUIREMENTS](REQUIREMENTS.md). Negative DATA is a
separate fault target. **−16V for10s and powered-off protection remain OPEN**;
numerical success does not close them. Source≤0.1Ω, limit0.5A, duration10s and
15/25/35°C checkpoints are retained. No higher source resistance or shorter
target was substituted to obtain PASS.

Revision A is reproduced from `b2cf4ea12a783d4eb81736383f51790b361d9b04` and its
baseline report. This review does not rewrite its results as revision B results.
The TX timer and latch address a continuously commanded sink; they do not supply
the missing DC diode evidence or qualify the interface for a vehicle.

## Separate the five claims

| Claim | Revision B disposition |
|---|---|
| Comparator works for normal communication | Conditional electrical-model claim at the declared rails/load/capacitance/temperature; physical NOT VERIFIED |
| Comparator works during negative fault | Not claimed; input is outside the normal common-mode domain |
| Comparator input stays within stress voltage | OPEN for10s and power-off; requires TP4 waveform and DC measurements |
| No damage during the specified fault | OPEN; current arithmetic and transient SPICE cannot prove long-duration voltage or thermal survival |
| Normal thresholds/timing recover afterward | OPEN until measured before/after comparison; parser recovery is not electrical recovery |

The [TI LM393B datasheet](https://www.ti.com/lit/ds/symlink/lm393.pdf),
SLCS005AH,April2025, distinguishes these domains. Section5.2 lists the B-version
recommended input lower limit−0.1V; section5.5's electrical common-mode range
uses0V toVCC−2V over temperature. Section5.1 lists−0.3..38V input and−50mA input
current as stress ratings. Its input-current footnote warns of parasitic
conduction, additional supply current and erroneous output. A small R1-limited
current does not establish a valid comparison or permit widening the voltage
rating. Its recovery note does not qualify this assembled board after10s stress.

For positive+16V with field power valid, the divider can raise SENSE above the
normal common-mode ceiling while VREF remains in range. Section7.2.2.1 describes
that one-input-high output case; it is distinct from supply-off and negative
conditions. During any fault, valid byte reception is not an acceptance criterion.

## BAT54 evidence gap and retained target

The [Nexperia BAT54 datasheet](https://assets.nexperia.com/documents/data-sheet/BAT54.pdf),
1July2022, Table7 specifies240mV maximum at0.1mA,25°C, pulse width≤300µs and
duty≤0.02. Its2µA reverse-current limit has the same pulse/temperature conditions;
10pF is at1V,1MHz,25°C. Typical forward curves and a low calculated dissipation
do not establish a worst-case10s DC clamp at15–35°C or with field power off.

At−16V, `|I_R1| <= 16/(220k*0.9895) = 73.50µA` is a conservative resistance
bound using zero clamp drop. The hypothetical `VF=.240V` would dissipate only
about18µW in D2. That small heat is useful arithmetic, but the hypothetical
voltage remains unproven under the required conditions. A model with a finite
diode equation can explore it; a different model corner can exceed−0.3V and must
be rejected by the voltage criterion. Neither corner is a physical capture.

Do not use the−50mA stress-current rating to erase the−0.3V voltage requirement.
Do not accept an over-limit TP4 voltage because a comparator still toggles.
The physical procedure stops before exceeding the approved stress ceiling and
records the target as OPEN if it cannot be completed safely.

## Negative DATA current continues with gate off

For an applied negative source the conventional-current return is:

`GND_F -> D1 anode/cathode -> DRAIN -> R13 -> R12 -> negative source -> GND_F`.

Q1 body diode, an enabled Q1 channel, and revision B's series power-gating MOSFET
body/channel paths are parallel or additional routes according to the actual
state. The generated pin netlist is authoritative. D1 directly connects GND_F
to DRAIN in the negative direction and bypasses the gate controls. **Turning off
the gate does not turn off a body diode.** Neither the timeout nor the power gate
interrupts D1's negative return. Do not attribute this sustained current to RX,
USB ground or a firmware command in the intended isolated fixture.

Ignoring diode drops gives `I <= 16/(44*0.95) = .3828A`. Independent22Ω ±5%
corners give at most3.0623W in either AC10 resistor. A hypothetical0.75V D1 drop
gives0.2871W. [Vishay's SS16 datasheet](https://www.vishay.com/docs/98653/ss16hm3_bia_ss16-m3ia.pdf),
98653,12March2025, specifies that forward-voltage figure at1A/25°C in a300µs
pulse test. Its105K/W junction-to-ambient value is typical for the stated FR4
mounting. Combining those assumptions yields about65°C junction at35°C ambient;
this is an estimate, not a maximum guaranteed10s temperature or thermal model.
Current sharing, mounting, resistor heat and repeated faults remain physical
gates. If Q1 is shorted drain-to-source, the44Ω path persists despite cutoff;
there is no whole-board single-fault safety claim.

## Threshold, load and timing arithmetic

For `a=R1, b=R2, f=R3`, the switching equation is

`V_DATA = V_REF*(1+a/b+a/f) - V_RX*a/f + a*(I_bias+I_leak)`.

Use independent resistor factors0.9895/1.0105 (1% plus50ppm/K over±10°C),
field rail4.75/5.25V, offset±4mV, bias±50nA, leakage0..2µA,
RX_LOW0..0.55V and RX_HIGH=V5_F−0.04V. This gives rising2.157–3.205V,
falling1.242–2.056V and same-corner hysteresis≥0.896V. These are conditional
allocations: offset/VOL below5V and diode leakage over the bench temperature
range need measurement. The constant leakage term is an adverse threshold
allocation near switching; it is not a diode law valid through negative SENSE.
Physical reverse leakage changes direction/conduction near zero. The±0.05% TCR
term assumes the precision resistors themselves remain15–35°C in normal use;
ambient alone does not prove that after heating nearby fault resistors. After a
fault, measure component temperatures and repeat thresholds in the declared
temperature range. There is no correct-comparison claim while the fault is applied.

The released line-load gate remains≤250µA. With4.75V and2.2kΩ, idle is then≥4.20V.
That load includes D1 reverse leakage, Q leakage and RX division. D1's200µA
specified at60V/25°C is not an all-temperature bound at the actual line voltage.
Increasing temperature can invalidate idle margin; test15/25/35°C assembled
boards and record actual currents.

`R1 || R2 || R3 <= 49.898kΩ`;20pF SENSE allocation gives`tau <= .998µs`.
The20pF combines the datasheet's condition-specific10pF D2 with a10pF
comparator/PCB allowance. DATA's200–500pF total is a separate budget including
MOS Cgd, D1, cable, fixture and probe loading. Do not count either RC term twice
in integration. Comparator0.3–3µs is a project sensitivity range; the datasheet
has typical response figures, not a guaranteed3µs maximum. Revision B adds its
gating path to D3→DATA; DATA→D8 and D3→D8 are measured separately in the
production-driver integration report. A valid model requires declared finite
asymmetry, capture phase, clock skew and ISR envelope.

## Off-state and partial power

The pre-hold-up revision B model exposed another safety gap: raw USB5→1.9V
in100µs, with D3 LOW and field5V, allowed the ISO undefined supply interval to
produce HIGH TX_F. Own gate reached about4.883V and DATA about0.224V for32.73µs
before the optical clear arrived. This is an **admissible model counterexample**,
not a measured Nano waveform or a prediction that every ISO7721 behaves that way.
It invalidates a default-LOW-only argument during1.7–2.25V input-domain supply.

Revision B therefore separates raw USB from a local held logic rail. LM66100
VIN sees direct raw USB; its output feeds the held rail through the22Ω resistor,
and CE senses the held rail **after that resistor**. This placement matters:
putting22Ω before VIN can limit reverse current enough that the ON switch's small
voltage difference never reaches CE's80mV maximum turn-off threshold. The bulk
could then discharge while the switch remains on. Current limiting alone is not
reverse blocking.

U1 logic power and the USB supervisor VDD use the held rail. The supervisor SENSE
and optocoupler LED use raw USB, so hold-up does not pretend that USB remained
healthy. A held-powered, overvoltage-tolerant input buffer guards D3; a raw-powered
Ioff output buffer guards D8. These remain GND_L connections and add no barrier
power or field-ground link. A driven Nano input cannot be assumed harmless merely
because the isolator has a different supply.

The LM66100 datasheet's2µs turn-off is typical. The model must include finite
reverse-conduction time, not an instantaneous ideal diode. The project's blocking
delay and effective bulk minimum are physical gates. Compute charge lost through
the feed resistor during that delay, then subsequent load/leakage discharge;
`C*deltaV/I` alone omits the initial reverse loss. A delayed-blocking/small-capacity
control must fail the same voltage/order criterion. Own sink permission must be
cleared before U1's held input supply reaches its undefined interval; the required
hold/order margin is reported separately from functional normal RX timing.
The final island uses U22 LM66100DCKR, R37=22Ω AC03, C28/C29 each4.7µF
(combined effective≥4.7µF), held-powered U23 input and raw-powered U24 output.
The10mA held-load ceiling, Ron0.14Ω,15µs blocking and150µs startup are model/
measurement allocations. Reverse-charge-plus-load arithmetic retains3.183995V
at the end of the250µs total health window versus the2.25V boundary. It is not
a manufacturer-guaranteed tOFF or a physical capture. R38's4.7kΩ raw bleed
bounds25µA aggregate injection to about0.119V and R10 bounds U24's off output
to about0.101V; whole off-state protection remains OPEN. The held rail is
intentionally powered briefly after raw USB disappears; that stored energy is
distinguished from raw-rail backfeeding. Measure actual discharge below0.2V
before rewiring rather than assuming a fixed delay.

The comparator model's10µA input-to-rail injection is **an assumption**, not a
manufacturer limit. Add the direct `SENSE -> R3 -> RX_F -> R6 -> V5_F -> R11`
route; it remains when the comparator output is high impedance. Solving the
resistor network with10µA injection, R1/R3/R6 factors0.9895 and R2/R11 factors
1.0105 gives about10.81mV/10.70µA at DATA+5.25V and13.31mV/13.17µA at DATA+16V.
These are restricted-path DC allocations, not an assembled-board off-rail bound.
An absolute input-voltage tolerance does not prove zero injection through every
RX/output, isolator, supervisor or powered test connection. The field rail must
actually remain below0.2V and the relevant MOS gate below1.3V in the specified
off-state tests; log rail current and repeat normal thresholds/timing afterward.

[TI ISO7721F Table8-2](https://www.ti.com/lit/ds/symlink/iso7721.pdf) defines
powered≥2.25V and powered-down≤1.7V. Output-side power loss and intermediate
levels are undetermined. A strongly driven input can weakly power a floating
rail. Revision B's independent reset/power gate handles sink permission; the
model must mark undefined D8 intervals as no-valid-level. Default LOW alone
does not justify slow-ramp logic behavior. Stable power restoration alone must
not restore arm, even when D3 was already HIGH.

There are no MEASURED values in this review. Required captures are TP4/TP5,
DATA, both MOS gates and source nodes, field rail and currents, with known probe
loading and local field ground. Raw/held logic and PG captures use GND_L with
matching isolated probes; no scope ground crosses domains. Fault tests use the separate current-limited
fixture without Nano, responder, PC or ECU. Functional two-Nano success,
hardware cutoff, negative DC survival, off-state behavior and isolation remain
separate acceptance results.

R39/R40 each10kΩ now terminate TX_L_ISO/RX_L_ISO against GND_L. The20µA
aggregate allocation gives≤0.2021V; missing pulldowns can turn a bounded leakage
into an unbounded/high node in a behavioral model. RX ISO off-output injection
is not a manufacturer Ioff specification. Healthy HIGH loads are checked
separately from the100µA buffer condition. Actual total held draw≤10mA is a
measurement gate; the model's6.2mA extra is bookkeeping rather than a fitted
physical resistor. Field decay requires Ceff≥2.2µF/load≤20mA and ramps≥100µs;
arbitrary forced hard collapse is OPEN, without a general PG release guarantee.
