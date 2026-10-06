# What the SPICE model does and does not establish

Pinned engine: **ngspice42**, Ubuntu24.04 package `42+ds-3build1`. Python3.12 standard
library drives batch netlists and reads finite numeric waveform samples. Models
in `models.cir` are original project behavioral approximations, not vendor models
and not transistor-level internal replicas. No closed model was copied.

| Model element | Bound / origin | Important omission / acceptance gate |
|---|---|---|
| Q1 | Finite Ron=.32R allocation; smooth gate-controlled conductance;60V body diode; Cgd50pF/Cgs200pF allocations; drain leakage swept | No MOS SOA/avalanche/hot-spot/temperature feedback; measure gate, capacitances and temperature; DS0.146R at4.5V/25°C is the anchor |
| D1/D3P | Exponential diode, series.02R,60V breakdown; reverse leakage allocation up to200uA applied to TX leg | Not a constant-voltage clamp; equation is illustrative, not a manufacturer fit; no thermal runaway model; measure diode VF and leakage |
| D2 | Exponential finite diode with1R series,10pF Cj,30V breakdown; extra0–2uA reverse-leak budget | DC/temperature extrapolation of short-pulse datasheet limits remains conditional |
| U2 | Finite20R drive, high target Vcc−0.1V; output high impedance when off,10uA leakage | Gate load differs from datasheet timing test; ramp regions simplified; gate waveform must pass |
| U3 | Real divider/positive feedback and input capacitance; ±4mV offset, bias/leak corners; finite160R output;0.3–3us transmission delay |3us is a project gate, not guaranteed DS maximum; powered-off injection10uA is an assumption; measure backfeed and brownouts |
| U1F | Two noninverting controlled outputs with finite50R impedance,17ns equivalent RC midpoint delay,2pF barrier capacitance allocation | Behavior below valid supplies simplified; no dielectric failure, creepage, CMTI, USB shield or earth path; isolation model cannot prove isolation |
| U4 | Finite2R output switch, max available input−0.5V; separately allocated20mA input load | Behavioral regulator is **not power conserving**; use arithmetic for power, not source current/efficiency from this model; no loop/stability proof |
| DATA capacitance | Lumped at DATA; requested total includes50pF Q1 Cgd; remaining budget represents Q1/D1/cable/fixtures/probes | No distributed cable, package inductance or high-frequency ringing. Sub-microsecond surge/ESD extrapolation forbidden |
| Comparator node capacitance |10pF explicit +10pF D2 model | Capacitance varies with bias; assembled20pF ceiling must be measured |

TX series inductance, resistor heat storage and actual PCB layout are not modeled.
The current fault envelope is slow/DC or1us edges; it is not an RF/EMC model.
All ideal-source ground references in a test deck are fixture definitions. U1's
behavioral transfer across them is not evidence that an assembled board has no
unintended return path. Software-only “ground offset PASS” has precisely that limit.

The normal matrix sweeps source1/2.2k, totalC200/500pF, source4.75/5.25V, field/USB
rails and low/high parameter corners. Full streams cover00,FF,55,AA, back-to-back
frames, the current11-byte initialization, all three whitelisted requests and both
A/B fixture responses, with direction changes.102/106us peer bits,±2us jitter and
0/20us sample lateness are evaluated against simulated D8 levels. This is waveform
sampling of the existing timing contract; it does not execute the AVR ISR or USB
stack in SPICE. Those remain separate native/AVR/physical tests.

Additional15/35°C diode-model sweeps use opposite USB/field supply corners and
slow up/down DATA ramps to measure threshold/hysteresis and source load. Those
temperatures change the generic diode equations, not a vendor-qualified full-chip
temperature model. Separate loss-of-field and loss-of-USB cases retain the logical
TX command HIGH; D3's electrical voltage follows its USB rail, respecting U1's
VCC1+0.5V absolute input limit. Field rail collapse includes output capacitors,
and release is checked after that collapse. No independent hardware re-arm latch
prevents a held HIGH command asserting again when field power returns.

The checker requires finite samples through the requested stop time and rejects
ngspice transient/convergence errors even when the process exits0. Limits cover
bus high/low, actual data/echo sample values, missing edges, edge delay, RX absolute
input stress, gate/drain/rail voltage, fault current and resistor power. Reports
contain each case's parameters, extrema, criterion, numeric margin and status.
10–90% edge crossings use the stated unloaded source reference: absent crossings
are explicitly null, never reported as zero rise time. Primary receive criteria
use the actual valid logic levels.

Negative controls are100R pullup (LOW too high),47k/10nF line (slow/loaded HIGH),
and direct48V asserted fault (excess current/power). These must fail numerical
checks. Full mode labels their failures EXPECTED_REJECTION; `--bad-only` exits2.
An unexpected negative PASS fails the suite. No physical bad-control test is
authorized by the model. Simulation PASS leaves every hardware status NOT VERIFIED.
