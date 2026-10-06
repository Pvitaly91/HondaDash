# What the SPICE model does and does not establish

## Revision B additions and evidence boundary

Revision A is separately exported and reproduced by `simulation/baseline.py` from
`b2cf4ea12a783d4eb81736383f51790b361d9b04`. Its model PASS and independent stuck-TX
failure are historical results. They do not validate the following revision.

Revision B adds the actual generated interconnect of three LTC6994 timers, two
arm/readiness flip-flops, logic gates, field/USB TPS3808G01 supervisors, optical
USB health, the direct PG-controlled series Q2 and the USB logic hold-up island.
LM66100 VIN sees raw USB, CE sees held V5_L, and the22-ohm resistor is after OUT;
placing it before VIN would mask reverse-blocking detection. Two4.7uF capacitors
provide9.4uF nominal, with effective4.7uF as the physical acceptance floor. U23
guards D3 into the held domain, and raw-powered U24 isolates the unpowered D8.
R39/R40 explicitly bias the intermediate TX/RX nodes when their drivers are off;
raw D3/D8 pulldowns alone do not constrain these new nodes. The voltage oracles
probe actual U1.INB against held V5_L, and U24.A against its input stress limit.
The final135-part revision adds D4/D5 positive clamps on SINK_RET/GATE and
D6 negative gate clamp, and changes field bleed R11 to680ohm. The off-state
oracles separately check Q1 gate-source voltage and U2 output stress: ground-
referenced gate voltage alone missed floating-source overstress in the earlier
candidate. D1 reverse leakage flows from DRAIN to GND_F; independent Q1/Q2
off-channel budgets are1uA each. A separate200uA Q1 leakage case is an adverse
sensitivity, not a manufacturer guaranteed maximum. Clamp pulse VF allocations
do not close DC/temperature or unpowered survival qualification.
These original project models
include finite output impedance, capacitance, leakage and stored state. Internal
counter voltages are **normalized digital timer state**, not physical capacitor
voltages or a substitute nominal RC timer circuit. Their smooth asymptote is4;
the coefficient `4*ln(4/3)` makes the threshold1 occur exactly at the calculated
timer interval. Removing the external SET resistor changes timing. Smooth bounds
avoid numerical chatter at a hard counter clamp; this is a solver correction,
not a relaxation of hardware or acceptance limits.

Startup uses explicit capacitor initial conditions and transient `UIC` with Gear
integration. Undefined low-supply logic outputs are adversarial HIGH, not silently
correct LOW. Independent supervisor/Q2 inhibition is checked against those states.
Flip-flop clocks use normalized rising logic transitions, not rail ripple as an
invented extra clock. The readiness timer requires real button release, inactive
TX and healthy power; its memory is cleared with the arm latch on timeout/power
fault. A held button cannot be used to authorize TX on restoration.

The comparator/supervisor/opto delays use ngspice42's bundled XSPICE analog code
module and differential analog ports, with8192-entry bounded delay buffers.
`run_spice` resolves the module shipped with the pinned package, records its hash,
and loads a copied test-only module through a case-local `SPICE_SCRIPTS/spinit`.
No model binary is committed or included in the hardware artifact. Legacy ideal
transmission-line delay histories produced repeated tiny breakpoints; the bounded
delay representation preserves the intended causal delay and finite surrounding
impedances. The earlier candidate's0.25us/0.125us convergence check observed
0.169997us maximum D8 edge difference. The current-model result and model hash
are recorded in `convergence/summary.json`. Use0.4us numerical timing
allowance; this is not a physical scope measurement or a guarantee off-grid.

PG release12–28ms and threshold corners derive from the specified supervisor;
50us assertion, its finite100-ohm dynamic sink, optical CTR80% and storage50us
are **project allocations**, not invented manufacturer guaranteed maxima. These
have explicit sensitivity cases and physical gates. The2.42mA capacitor discharge
peak is below a stress ceiling but does not prove the output-voltage/delay bounds
specified at smaller currents. Normal performance requires measured4.75–5.25V
rails; PG merely supervises logic safety at its distinct lower threshold range.

The pre-hold candidate produced a32.726us unrequested gate pulse with D3 LOW
during USB loss; checking only after restoration falsely missed it. Its matching
historical source fixture is retained and rerun separately. The new whole-phase
oracle checks the commanded channel current as well as both gate levels. The
hold-up calculation uses10mA load, effective4.7uF,15us finite reverse blocking,
and250us health propagation, leaving3.184V above the2.25V ISO supply boundary.
LM66100's2us turn-off is typical only;15us and the load/capacitance envelope are
conditional project allocations requiring physical measurement. Prescribed rail
profiles do not prove regulator stability, charging/inrush or current-limiter
behavior. Flip-flop setup/hold violations are outside the quiescent local re-arm
contract and remain OPEN; the ideal state model cannot establish metastability.
The earlier constant6.2mA fixture load discontinuity at zero held-rail voltage
caused a reproducible slow-USB solver failure. Its C1 smoothstep now rises from
zero to the same6.2mA over0–1V and remains exactly constant above1V. This only
regularizes the invalid low-voltage fixture load; adversarial undefined logic,
healthy-domain load, rail profiles and physical criteria are unchanged. Solver
failure remains an infrastructure error and cannot count as expected rejection.
The field power grid includes100us,1ms and10ms falls. Effective field C≥2.2uF
and healthy load≤20mA imply at least214.5us from4.2V to2.25V. A hard rail short,
arbitrary faster collapse or excess load is outside that decay bound and remains
OPEN; finite gate discharge is not an instantaneous power-loss disconnect.

The default batch first qualifies released controls, presses the local button at
65–90ms and begins bytes at100ms. That is fixture stimulus, not automatic arming
in hardware or software. It preserves all raw request/fixture bytes. Timer cutoff
releases **our channel**, not an external short or the MOSFET body-diode path.
Shorted Q1, negative DC10s, off-state survival and hardware measurements remain
OPEN/NOT VERIFIED independently of numerical model success.

The integration exporter uses the same3.3V DATA threshold for rise and fall and
0.3/0.6 times local USB supply for D8 transitions; intermediate D8 voltages retain
the selected prior digital state. Input-power1.7–2.25V is invalid metadata, not a
guaranteed logical level. The production-driver harness separately replays actual
extracted edges and runs closed-loop feedback. A precomputed trace cannot change
after a driver fault; that limitation is explicit. See
[PROTECTED_INTERFACE_DRIVER_INTEGRATION](../../../docs/PROTECTED_INTERFACE_DRIVER_INTEGRATION.md).

Pinned engine: **ngspice42**, Ubuntu24.04 package `42+ds-3build1`. Python3.12 standard
library drives batch netlists and reads finite numeric waveform samples. Models
in `models.cir` are original project behavioral approximations, not vendor models
and not transistor-level internal replicas. No closed model was copied.

| Model element | Bound / origin | Important omission / acceptance gate |
|---|---|---|
| Q1/Q2 | Finite Ron=.32R allocation; smooth gate-controlled conductance;60V body diode; Q1 Cgd50pF/Cgs200pF allocations; separate1uA off-channel leaks, adverse Q1 sweep200uA | No MOS SOA/avalanche/hot-spot/temperature feedback; measure gate-source voltage, capacitances and temperature; DS0.146R at4.5V/25°C is the anchor |
| D1/D3P | Exponential diode, series.02R,60V breakdown; D1 reverse leakage allocation up to200uA from DRAIN to GND_F | Not a constant-voltage clamp; equation is illustrative, not a manufacturer fit; no thermal runaway model; measure diode VF and leakage |
| D2 | Exponential finite diode with1R series,10pF Cj,30V breakdown; extra0–2uA reverse-leak budget | DC/temperature extrapolation of short-pulse datasheet limits remains conditional |
| D4/D5/D6 | BAT54 finite-diode gate/source clamps and explicit leakage; generated pin map and680ohm field bleed | Short-pulse VF allocation is not guaranteed DC clamp or full-temperature off-state survival |
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
frames, the current11-byte initialization, all three whitelisted requests and
A/B/Boundary fixture responses, with direction changes.102/106us peer bits,±2us jitter and
0/20us sample lateness are evaluated against simulated D8 levels. This is waveform
sampling of the existing timing contract; it does not execute the AVR ISR or USB
stack in SPICE. Those remain separate native/AVR/physical tests.

Additional15/35°C diode-model sweeps use opposite USB/field supply corners and
slow up/down DATA ramps to measure threshold/hysteresis and source load. Those
temperatures change the generic diode equations, not a vendor-qualified full-chip
temperature model. Separate loss-of-field and loss-of-USB cases retain the logical
TX command HIGH. Main electrical cases let D3 follow raw USB; independent failsafe
cases also retain an external5V D3 input through raw USB loss, behind U23's input
guard. Field rail collapse includes output capacitors. Revision B's latch inhibits
restored HIGH until qualified button release and a fresh press; retained button
through power cycles is checked separately. RX invalid-supply states are sampled
as both HIGH and LOW without claiming firmware can know validity from a wire.

The checker requires finite samples through the requested stop time and rejects
ngspice transient/convergence errors even when the process exits0. Limits cover
bus high/low, actual data/echo sample values, missing edges, edge delay, RX absolute
input stress, gate/drain/rail voltage, fault current and resistor power. Reports
contain each case's parameters, extrema, criterion, numeric margin and status.
10–90% edge crossings use the stated unloaded source reference: absent crossings
are explicitly null, never reported as zero rise time. Primary receive criteria
use the actual valid logic levels.

Negative controls include100R pullup (LOW too high),47k/10nF line (slow/loaded HIGH),
direct48V asserted fault (excess current/power),60us comparator delay, missing
negative clamp, short/disabled timer, unsafe arm, reversed gate, missing PG and
bypassed hold-up island. These must fail numerical
checks. Full mode labels their failures EXPECTED_REJECTION; `--bad-only` exits2.
An unexpected negative PASS fails the suite. No physical bad-control test is
authorized by the model. Simulation PASS leaves every hardware status NOT VERIFIED.

CI partitions each full suite into four disjoint ordered subsets without reducing
scenario duration or the original physical limits. `merge_reports.py` requires
the current Git/model hashes, exact complete case coverage, numerical checks and
matching CSV/deck/log/module/trace evidence. It publishes complete status only
after validation and rejects13 deliberately corrupted report controls. Missing,
cancelled or failed shards leave an incomplete ERROR, never an aggregate PASS.
