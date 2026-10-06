# Revision B: independent TX duration cutoff

This is a laboratory design candidate. **Physical TX cutoff, power ramps,
exchange, isolation, vehicle qualification and real ECU operation: NOT VERIFIED.**
Revision A is reproducible at `b2cf4ea12a783d4eb81736383f51790b361d9b04`.
Its direct isolated HIGH-to-gate path has no duration limiter: with both supplies
healthy, held D3 HIGH keeps its own sink active indefinitely.

## One concrete circuit

All timer, latch, manual button and sink-control parts belong to GND_F. They
receive V5_F from the existing independent laboratory battery/LDO. No MCU,
firmware loop, ISR, USB command or application completion participates in cutoff.

| Ref | Component / connection | Purpose |
|---|---|---|
| U5 | LTC6994IS6-1#TRPBF; IN1=TX_F, GND2, SET3=R15 200k to ground, DIV4=R16 357k/R17 100k, V+5, OUT6=TIMEOUT | Delay **rising** edge; DIVCODE3, Ndiv512. Falling TX resets timing |
| U6 | LTC6994IS6-2#TRPBF; same pin order; IN=ARM_RAW, SET=R18 249k, DIV=R19 255k/R20 100k | Both-edge qualification; DIVCODE4, Ndiv4096; switch debounce |
| SW1 | OMRON B3F-1002-G, field-side gold SPST-NO; pairs1+2=V5_F,3+4=ARM_BUTTON | Explicit local re-arm; R23=1k series/R22=10k pulldown. Datasheet pin numbers use bottom-view definition; internal diagram is top view |
| U7 | SN74LVC1G74DCUR; CLK1=ARM_CLK, D2=ARM_DATA, /Q3=LOCKED, GND4, Q5=ARMED, /CLR6=CLR_N, /PRE7=V5_F, VCC8 | Persistent arm state; asynchronous clear overrides clock/data |
| U8 | SN74LVC2G02DCUR; NOR1 TX_F/TIMEOUT→IDLE_OK; NOR2 TIMEOUT/POWER_BAD→CLR_N | Idle qualification and timeout clear |
| U9 | SN74LVC2G08DCUR; AND1 TX_F/ARMED→TX_ALLOWED; AND2 IDLE_OK/PG_CLEAN→ARM_SAFE | Qualified sink enable and preliminary arm data |
| U10,U12 | SN74LVC1G04DBVR inverter and SN74LVC1G17DBVR Schmitt buffer | U12 makes a fast PG_CLEAN edge; U10 produces NOT_PG |
| U11 | TPS3808G01DBVR; /RESET1=PG_F, GND2, /MR3=V5_F, CT4 open, SENSE5=R29/R30 divider, VDD6=V5_F | Independent field undervoltage/reset and 12–28ms release delay |
| Q2 | PMV88ENEA,215; gate1=PG_GATE, source2=GND_F, drain3=SINK_RET | Series source-path disconnect. Q1 source moves from GND_F to SINK_RET |
| R21,R24,R25,C11 | PG_F pullup150k; direct gate series2.2k; gate pulldown10M; C0G2.2nF to GND_F | Q2 is driven **directly** by supervisor, so undefined low-voltage logic outputs cannot command Q2. U12 does not drive this gate |
| U13,Q3,U14 | Logic-side TPS3808G01, PMV88ENEA LED switch, VO617A-4X016 wide DIP4 | Independent optical USB-health indication; no common ground added |
| U15,U16 | SN74LVC1G32DBVR OR and SN74LVC1G17DBVR Schmitt buffer | NOT_PG OR USB_BAD_CLEAN→POWER_BAD; either supply loss clears U7 |
| U17,U18 | LTC6994IS6-1#TRPBF and SN74LVC1G74DCUR | Post-fault/post-power released-button/inactive-TX qualification, followed by a readiness memory |
| U19,U20,U21 | SN74LVC2G02DCUR, SN74LVC2G08DCUR and SN74LVC1G17DBVR | Clean physical button; qualify RELEASE_OK and READY_REQ; require REARM_READY in ARM_DATA |
| U22,U23,U24 | LM66100DCKR and two SN74LVC1G17DBVR | Held logic power, guarded raw D3 input and guarded raw-powered D8 output |
| R37,R38,C28,C29 | AC03 22R feed after LM.OUT; rawUSB4.7k bleed; two4.7uF/50V X7R | Minimum measured held capacitance4.7uF; finite reverse-blocking/rail discharge analysis |
| D4,D5,D6,R11 | BAT54,215 source/gate rail clamps; R11=680R field bleed | Bound floating Q1 source and unpowered buffer excursions; local field connections only |

The optical health path is USB5V→R27 390R→U14 LED_A1/LED_K2→Q3 drain;
Q3 source is GND_L and its gate receives U13 /RESET throughR36=2.2k,
with150k pullup. U14 emitter3
is GND_F, collector4 is USB_BAD with10k field pullup. U16 cleans this slow
collector transition. U13 uses the same100k/10.2k SENSE divider and CT-open delay
as U11. USB loss or an unsafe USB cycle removes light and clears ARMED. Returning
USB power while D3 remains HIGH cannot arm the latch.

### Floating-source and gate rail clamps

The series Q2 disconnect leaves Q1 source/SINK_RET floating when field power
is absent. The previous model placed the entire200uA DATA leakage allocation
in Q1's drain-to-source branch. For the1ms/+24V/1k fault pulse with1us edges,
that charged SINK_RET to23.9134V. Q1's Cgs then coupled the source excursion
into GATE:2.8585V peak and−1.6549V minimum. Q1 VGS reached−23.2279V,
beyond its−20V absolute limit, and the U2 off output fell below−0.5V.
Passing steady release or measuring GATE relative to ground alone missed
these stresses. This is a simulated counterexample, not a hardware capture.

D4 is **BAT54,215**, pin1 anode=SINK_RET, pin3 cathode=V5_F; D5 uses
pin1=GATE, pin3=V5_F; D6 uses pin1=GND_F, pin3=GATE. Each pin2 is NC.
D4 returns source leakage and edge charge to the local field capacitors and
bleeder. D5/D6 limit positive and negative gate excursions, including U2's
powered-off output through R7. These connections add no resistor around Q2
and no ground-barrier crossing. With normal powered sink operation, SINK_RET
is near GND_F and the clamps remain reverse biased.

R11 is now **680R, MRS25000C6800FCT00**. A conservative200uA adverse MOS
branch plus13.4uA RX injection and6uA aggregate clamp-leak allocation gives
at most0.150759V at the bleed resistor corner. Adding the0.45V clamp
acceptance allocation bounds positive unpowered GATE to0.600759V, below
the unchanged1.3V criterion. The normal field static budget becomes16.7336mA,
within the existing20mA ceiling. D6's reverse2uA allocation is included in
the U2 gate load; its load remains below the100uA VOH source condition.

The corrected normal leakage paths are physically separated: TX_LEAK applies
to D1 cathode/DRAIN→GND_F; Q1_OFF_LEAK=1uA is DRAIN→SINK_RET and
Q2_OFF_LEAK=1uA is SINK_RET→GND_F. The MOSFET1uA datasheet value is at
60V,VGS=0,25°C. Q1_OFF_LEAK=200uA is retained only as an explicitly adverse
source-branch sensitivity, not a manufacturer maximum. The clamps also bound
the previous conservative leakage placement.

Each clamp model has a finite diode,10pF allocation and an explicit2uA
reverse-leak allocation. BAT54 source data gives VF≤0.4V at10mA,25°C,
at most300us pulses/duty≤0.02. The0.45V allocation over15–35°C, individual
diode current, full DC clamp behavior, field injection and powered-off U2
stress require isolated physical measurement. Negative DC and whole
off-state qualification remain **OPEN**. No diode model guarantees survival.

Targeted final ngspice42 checks passed the unchanged numerical criteria.
For the+24V unpowered pulse, GATE was−0.264590..0.289917V,
Q1 VGS−0.051139..0.193297V and U2 output−0.262390..0.292117V.
Individual D4/D5/D6 forward-current peaks were3.219uA,1.013mA and0.216mA.
The−24V pulse also passed. With the explicitly adverse Q1_OFF_LEAK=200uA,
GATE remained−0.247169..0.289731V and Q1 VGS−0.514685..0.177080V.
The normal2200R/500pF/4.75V/slow corner retained all byte sample centres
and edges, with maximum echo6.227767us against the unchanged8us limit.
These are focused model checks; the complete final matrix is a separate result.

### USB hold-up island

The earlier revision-B circuit still powered U1 logic directly from raw USB.
For a5V-to1.9V USB fall over100us, with D3 commandLOW and healthy field power,
its undefined ISO input-supply interval generated a32.726us own-sink pulse
(gate4.883V, DATA0.224V) before optical health cleared the latch. Restoration
checks alone missed it. The frozen pre-hold model/trace and failed criterion
are preserved in the generated hardware artifact; these are model evidence,
not a physical capture.

The final circuit adds **U22 LM66100DCKR**, **U23/U24 SN74LVC1G17DBVR**,
R37 and C28/C29. U22 VIN1 receives **direct raw USB**, GND2=GND_L,
CE3=heldV5_L, NC4 open, unusedST5 grounded, OUT6=ISO_FEED. R37 is anAC03
**22R power resistor after OUT**, fromISO_FEED toV5_L. Putting this resistor
beforeVIN can mask the80mV worst off threshold and let the capacitor discharge
without reverse blocking; that placement is forbidden. CE senses the downstream
held capacitor directly.

C28/C29 are eachKEMET C340C475K5R5TA,4.7uF/50V X7R, giving9.4uF nominal.
Acceptance requires **combined effective capacitance at least4.7uF** under
voltage, temperature and aging. U1 VCC1/C1 move toV5_L; U13 VDD/MR/C20/R26
also move toV5_L, while its SENSE divider and optical LED power remain raw USB.
Thus U1 and the supervisor retain defined supply while raw-USB loss is detected.

U23 buffers rawNanoD3 intoTX_L_ISO/U1 INB5 and is powered byV5_L. Its input
tolerance protects U1 during startup with rawD3HIGH and a charging held rail.
U1 OUTA4/RX_L_ISO feedsU24, powered by **raw USB**, then NanoD8. Ioff and the
existing10k D8 pulldown bound powered-off output injection; they do not prove
zero input-to-raw-rail leakage. R38 **4.7k rawUSB-to-GND_L bleed** limits a
25uA aggregate backfeed allocation to about0.119V. Whole off-state is still a
physical acceptance measurement. Never connectV5_L to the Nano5V pin or field
power. All these parts remain in GND_L.

R39 andR40 are **10k pulldowns** onTX_L_ISO andRX_L_ISO. The rawD3/D8
pulldowns do not bound these intermediate nodes. R39 budgets U23Ioff10uA plus
U1 input-current10uA, giving at most0.2021V at the resistor corner. R40 uses
an aggregate20uA output/input-injection allocation with the same voltage bound;
the ISO off-output contribution is a measurement allocation, not a manufacturer
Ioff guarantee. Each steady load stays below0.54mA. Use U23's4.5V/32mA
VOH at least3.8V against U1's3.675V high requirement, rather than the100uA
table. U1's rated output drive covers R40/U24. The model's aggregate additional
hold-load reserve is6.2mA; behavioral outputs do not conserve power. It is not
a physical resistor or permission to add that current without measuring the
complete held current, whose acceptance limit remains10mA.

Below1V the additional-load model uses the continuous law
`I=6.2mA*u²*(3−2u)`, with `u=clip(V5_L/1V,0,1)`. This partial-power
allocation has zero slope at both endpoints and prevents the previous
constant-current jump at0V from creating an impossible operating point
during a slow body-diode-fed USB ramp. It is exactly6.2mA at and above1V,
so all valid held-power states at2.25V and above retain the full load.
This numerical correction changes no power or acceptance limit.

Declare total held load at most10mA. At rawUSB4.75V, feed resistor+5% and
0.14R diode allocation, steadyV5_L is at least4.5176V. R37 limits startup and
reverse current to0.2512A; peak1.319W is belowAC03 P70=2.5W. Blocking time
is allocated at most15us; the datasheet2us is **typical, not a guaranteed max**.
The model has CE threshold limits, finite Ron, a body diode and2.7uA reverse
leakage. Worst charge `Ipeak*15us` plus10mA for the entire250us inhibit
allocation, with effectiveC4.7uF, leaves at least **3.18399V**, above2.25V.
Reverse discharge before blocking is included; an instantaneous ideal diode
would hide it. Blocking delay, capacitance, inrush and total hold load are
measurement gates. Missing hold-up or slow-block controls must fail the same
numeric unrequested-own-enable criterion at an adverse delay corner.

The corrected targeted100us USB fall with minimum effectiveC4.7uF and finite
15us blocking produced **zero** unrequested own-gate samples at rawUSB1.7–2.25V;
TX_F stayedLOW and fixture DATA stayed above5.03V. This result covers its stated
model stimulus. Wire timing, protocol policy and firmware identities remain
unchanged; physical power-loss qualification is NOT VERIFIED.

Each new field IC has a local100nF bypass capacitor; U13 has its own USB-domain
100nF. U1 and U14 are the only signal-barrier components. Their local pins,
capacitors, buttons and test points must not create a copper or instrument-ground
connection between GND_L and GND_F. U14 option6 has at least8mm package creepage
and clearance; the board/instrument isolation remains unqualified.

## State and truth table

`CLR_N = NOT(TIMEOUT OR POWER_BAD)`;
`IDLE_OK = NOT(TX_F OR TIMEOUT)`;
`READY_REQ = NOT(ARM_RAW_CLEAN OR POWER_BAD) AND IDLE_OK`;
`ARM_SAFE = IDLE_OK AND PG_CLEAN`;
`ARM_DATA = ARM_SAFE AND REARM_READY`;
`TX_ALLOWED = TX_F AND ARMED`.

U17 qualifies READY_REQ HIGH for20.39808ms. Its RSET isR33=249k and divider
R34=255k/R35=100k (DIVCODE4/N4096). U18 clocksREARM_READY HIGH at this
qualification edge (D tied toV5_F), and clears it asynchronously withCLR_N.
READY retains throughout a subsequent press; do not clear it on the same edge
as ARM_CLK, which would introduce a race. U19 NOR1 computes RELEASE_OK,
its unused NOR2 is held LOW; U20 computes READY_REQ and final ARM_DATA.
U21 supplies a fast Schmitt-cleaned physical button level to U19.

| Power / health | TX request | Timer | ARMED before | Explicit qualified button edge | ARMED after / own sink |
|---|---|---|---|---|---|
| First startup; PG delay running | Any / undefined | Any / not relied on | Undefined | Any | Clear0 before functional release; Q2 opens own sink |
| Stable PG and USB health | LOW | Reset |0 | None |0 / released |
| Stable PG and USB health, button released/TXLOW for qualified20ms, READY=1 | LOW | Reset |0 | Fresh rising edge |1 / released; next normal TX can pass |
| Stable PG and USB health, READY=0 | LOW | Reset |0 | Any button edge |0 / released; complete safe release qualification first |
| Stable PG and USB health | HIGH | Running below timeout |0 | Rising edge |0 / released: D is0 |
| Stable PG and USB health | HIGH | Running below timeout |1 | None |1 / sink active |
| Stable PG and USB health | HIGH | TIMEOUT asserted |1 | Any |0 / forcibly released |
| Stable PG and USB health | LOW after timeout, including a short accidental LOW | Reset |0 | None / button remains held |0 / released; no automatic retries |
| Stable PG and USB health | LOW | Reset |0 | Release button fully, then new press |1 / released, ready for new short TX |
| Brownout or lost USB health | Any | Any | Any | Any | Clear0; field Q2 and/or latch suppress own sink |
| Power returns with TX HIGH | HIGH | May start or assert |0 | None or active-TX button edge |0 / released until an explicit inactive-TX re-arm |
| Field rail below component recommended operation | Unknown | Unknown | Unknown | Not allowed | Do not claim correct IC logic; direct supervisor/Q2 path and bounded supply/Miller model are checked separately |

A button held during active TX produces only one qualified clock. Later TX LOW
does not create another clock. Release and press again are required. A button
held during power recovery also is not an authorization to transmit.
Its physical HIGH blocks READY_REQ before the press-debounce clock occurs.
After any unsafe cycle or duration fault, stable healthy power, inactive TX and
a continuously released button are required for at least21.286ms before READY
can be relied on. Only a subsequent new press can arm TX. This additional
readiness latch closes the12ms-minimum PG versus19.529ms-minimum debounce race;
it does not rely on the timer's typical startup behavior.

## Timeout arithmetic and limits

U5 `t = Ndiv × (RSET/50k) ×1us =512×4us=2.048ms`.
The datasheet full-temperature accuracy is **±3.0%**, rather than the headline
±2.3% at25°C. The resistor adds±1%,50ppm/K×10K=±0.05% for the15–35°C
laboratory domain. External SET-node leakage≤10nA is an explicit board
acceptance allocation; relative to5uA nominal SET current it adds±0.2%.
The leakage changes SET current reciprocally. With VSET>=0.97V,
`tmin=2.048ms×0.9895×0.97/(1+10nA×200k×0.9895/0.97)` and the analogous
minus-leak/max-resistor expression give **Tmin1.961699ms and Tmax2.136040ms**.
Internal timer supply/temperature error is already in±3%; it is not added again.

Add **1us** for timer output, NOR/clear, AND, buffer and gate discharge as a
project allocation: own sink release≤**2.137040ms**, comfortably below the
HondaDash5ms requirement. This1us is not an invented guaranteed maximum for the
LTC falling-edge propagation, whose table gives typical values. Sensitivity
sweeps and physical timing acceptance must cover it. Do not change driver
lateness or the200ms DLC observation window to make electronics pass.

Longest legal continuous LOW is start+eight zero data bits, with the real
production output tail included. The conservative envelope is **1.025ms**.
The derived compiled path adds nine104us bits at-2% clock,40tick lateness
(20us/.98),131 STOP-path AVR cycles/(16MHz*.98),two GPIO timestamp cycles,
and0.1us frontend width allocation. The integration additionally explores a
16us output-tail sensitivity; the rounded1.025ms covers this larger path.
These are timing/model assumptions and instruction analysis, not physical Nano
measurements. Tmin exceeds the full envelope by approximately0.9367ms.
Normal stop releases the input for at least104us/1.02=101.961us; counter
reset is allocated at most1us. Back-to-back0x00 must fully reset between LOW
runs. No timeout occurs for all256 legal bytes,0/FF/55/AA or existing
initialization/read fixtures within the checked envelope.

U6 and U17 nominal delays are20.39808ms; corresponding independent corners
give19.528779–21.285843ms. OMRON specifies switch bounce≤5ms. Hold a press and its
release for at least25ms, with TX inactive throughout the accepted press edge.
The production integration additionally requires arm data stable at least1us
around that edge; this bounds the numerical latch aperture and is stricter than
the electrical IC setup/hold test. Button/TX coincidences are not a recovery
mechanism.

## Power-good and undefined ranges

TPS3808G01 nominal0.405V uses100k/10.2k divider, giving a nominal rail
threshold4.375588V. Resistor/TCR,±2% supervisor accuracy and±25nA SENSE
current corners give falling threshold**4.204737–4.551579V**. Maximum rising
threshold, including3% hysteresis, is**4.688050V**; therefore release is possible
at the functional minimum4.75V. A fixedG50 part was rejected because its worst
rising threshold would exceed that minimum. PG does not certify that a rail has
already reached the declared functional4.75V floor: measure both rails.

CT is intentionally open: reset release after12–28ms stable supply. LTC startup
is described with a typical frequency-dependent value, not a guaranteed maximum;
the project acceptance gate is startup/valid DIV code≤10ms. Wait at least50ms
after both rails stabilize for timer/PG checks, then allow the released-button
READY interval. A simple conservative bring-up sequence holds TXLOW/button
released for at least65ms after stable rails, presses for25ms, and starts TX no
earlier than100ms. Then release and press the field button before a new experiment.
Normal automatic NEW/ARM/INIT commands cannot reset this hardware latch.

The model allocates supervisor assertion≤50us and saturated opto storage≤50us;
both need physical validation. TPS SENSE propagation20us and opto turn-off25us
are typical, not guaranteed maxima for the added capacitive load or temperature.
PG gate charge/reset loading, component input leakage and slow ramps must be
scoped. Defined brownout stimuli fall to≤4.0V for sufficient time; a shallow dip
inside the4.20–4.55V threshold corner band need not reset every unit.

At0.8–1.3V, R21 worst pull current plus one5uA logic input,1uA Q2 gate leakage
and10M pulldown stays below the TPS15uA POR test current. Above1.3V its stated
low-voltage output-current conditions support holding PG LOW. Below0.8V RESET
is explicitly **undefined**; the model leaves it high impedance, includes local
pullup and capacitance, and drives undefined logic adversarially HIGH. Bound
MOS threshold at25°C, off-state injection, parasitic Miller kick and temperature
are conditional allocations and remain physical acceptance gates. This is not
proof for arbitrary ground loss, unbounded transients or a broken supervisor.

R7=220R limits buffer capacitive peak current below24.2mA, within the stated
32mA recommended output-current condition. With the project20R output and
capacitance allocations, gate release plus50ns logic is below1us. R24 andR36
each2.2k limit supervisor gate-capacitance discharge below2.42mA, below its
5mA absolute current ceiling. Q2 gate fall adds about8.3us after reset asserts;
the separate project PG path budget remains100us including50us assertion.
The150k pullup×capacitance approximately0.37ms constant applies to **rise**,
not to the low-impedance reset sink's falling edge.

A healthy field rail with effective output capacitance at least2.2uF and total
load at most20mA takes at least214.5us to decay from4.2V to2.25V. This supports
the100us allocated sense-to-PG/sink inhibition budget for battery removal or
bounded healthy loads. An external hard short or an arbitrarily faster forced
field collapse violates that premise and remains **OPEN**, rather than a
general power-sequence PASS. The component-undervoltage model deliberately
does not guarantee correct logic in that unsupported transient.

At healthy power, rawPG worst static allocation is about3.723V. Q2 Rds≤0.5R
therefore is a **measured acceptance allocation**, not the Q1 datasheet's4.5V
Rds guarantee. Q1 retains its4.5V drive and0.32R thermal/model allocation.

## Power budget, tests and residual faults

The conservative steady field budget is about16.734mA, including ISO7721
maximum DC3.4mA, comparator1.25mA allocation,7.721mA bleed,2.42mA RX
pullup worst LOW, dividers/gate loads, three175uA timers, both latches/gates/supervisor,
opto collector, held button and6uA clamp-leak allocation. Allow switching/board margins within the existing
**20mA measured field limit**; measure LDO input≥6V under load. USB health adds
approximately7.86–11.01mA LED current on the USB domain. Its80% CTR allocation
over15–35°C requires measurement; the datasheet160% minimum is only at25°C,
5mA LED and5V collector. There is no field indicator LED in this revision.

`design.py --check` verifies generated BOM/SVG/netlist agreement. Calculations
contain timer, debounce, PG, leakage and power rows. SPICE independently checks
held TX, persistent lockout, active re-arm, startup, slow ramps, brownout and
USB loss/recovery with held HIGH. Its faulty timer, short cutoff, unsafe arm,
reversed gate and missing-PG controls must be rejected for measured behavior.
Transient analyses use explicit initial states (`uic`) and Gear integration;
DC equilibrium is not interpreted as an actual power-on history. Timer counter
capacitors and regenerative latch state are behavioral internal states, not
extra physical timing capacitors or manufacturer transistor models.

The normalized counter follows `dc/dt=(K/T)*(1-c/4)`, where
`K=4*ln(4/3)`, so it crosses1 at exactlyT and approaches4 smoothly. This avoids
hard-clamp/Roff chatter while retaining the programmed threshold time. The
clock aperture uses a rising transition of normalized Boolean clock state;
analog ripple on an already-HIGH clock must not become another arm action.
Comparator, PG and optical-health transport use the ngspice42 built-in analog
`delay` code model with bounded8192-cell ring buffers. The runner loads the
packaged `analog.cm` through an isolated generated `spinit` before parsing the
netlist; no application runtime dependency or manufacturer model is added.
This built-in behavior is documented in the
[ngspice42 manual, section12.2.33](https://ngspice.sourceforge.io/docs/ngspice-42-manual.pdf).
On the targeted normal trace, halving maximum step from0.25us to0.125us kept
44 D8 transitions and changed their times by at most0.202us; use a conservative
0.4us numerical edge allowance. This comparison bounds the tested trace only.
The former lossless-transmission-line approximation differed by0.342us and
generated excess reflected breakpoint history; its old trace is not a result
of a physical capture or of this revised numerical implementation.

Releasing our sink does not make DATA HIGH if the peer or an external short
still holds it LOW. Q1/Q2 drain-source shorts and an incorrect assembly are
separate faults; no single-fault safety claim is made. D1 is still a direct
GND_F→DRAIN negative-current path, and both MOS body diodes remain present:
gate cutoff does not stop an external negative fault. Negative DC and whole
off-state qualification stay OPEN as described in RX_REVIEW/FAULT_MATRIX.

Firmware receives no latch-feedback wire. LOCKED/ARMED test points are local
field measurements; an echo/line fault is the software symptom. A USB timeout
alone does not identify an electrical timer fault. Local re-arm is an additional
physical prerequisite before existing strict two-Nano recovery commands, which
continue to manage only the documented responder/bridge software state.
