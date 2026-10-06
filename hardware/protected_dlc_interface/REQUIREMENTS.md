# Revision B electrical requirements and evidence classes

Revision A remains reproducible at `b2cf4ea12a783d4eb81736383f51790b361d9b04`;
its report is a historical baseline, not revision B validation. Values below are
SI unless units are written. `DS` means a manufacturer limit at
its stated conditions; `REPORT` means an author's observation; `ASSUMPTION` is a
project test range; `MODEL` is a computed result; `MEASURED` requires an actual
instrument trace. **There are no MEASURED entries in this revision.** Datasheet
conditions must not be silently widened. Source IDs resolve in [SOURCES](SOURCES.md)
and in the BOM; the latter records the manufacturer URL and document revision.

| Requirement / range | Source and status | Design consequence | Physical validation |
|---|---|---|---|
| A communication wire near 5V; author reports 4.90–5.05V with ignition on | Hondash troubleshooting, REPORT; not OEM specification | Nominal 5V is a candidate reference only | Identify the actual ECU, pin, idle voltage, ground and source resistance before M4 |
| Classic Nano ATmega328P, 5V/16MHz; D3 sink enable; D8=PB0/ICP1 | Microchip pin functions + existing M3a driver, DS/software | USB powers logic only; preserve polarity and Timer1 | Verify exact board, clock, pin labels, rails and capture |
| Raw USB/field4.75–5.25V; held V5_L≥4.5V in normal use; held load≤10mA | ASSUMPTION/corner arithmetic; LM66100 source conditions separate | U22 supplies U1/U13/U23 through R37; raw U24 guards D8; no new ground/barrier power | Measure all three rails/load; raw supervisor safety floor is not normal MCU/RX validity |
| Combined effective held bulk≥4.7µF, nominal2×4.7µF; block≤15µs, startup≤150µs allocations | CHOLD nominal DS; HOLD dynamic figures typical only; effective C/delays are MODEL assumptions | Include finite reverse-current charge loss, then the250µs health window; V5_L remains≥2.25V until own inhibit | Verify C at bias/temp/age, reverse current/blocking/startup and ordered rawPG→USB_BAD→ARMED→gate release |
| Raw off-rail injection allocation25µA; R38=4.7k gives about0.119V | HOLD reverse leakage DS plus guard-input/output allocations; no zero-backfeed claim | Raw bleed and Ioff receiver buffer bound leakage; retain D8 pulldown | Measure raw rail/D8 leakage and voltage with source absent/open; overall off-state protection OPEN |
| DATA Thevenin source 4.75–5.25V; Rsource 1–2.2k; Ctotal 200–500pF | ASSUMPTION, swept; **actual ECU values UNKNOWN** | 44R sink resistance; no adapter pullup; C includes MOS/diode/cable/probes | Measure rise waveform/source resistance with a controlled fixture, then actual ECU separately |
| RX high acceptance >=3.3V; low <=0.6V | ASSUMPTION, not ECU input thresholds | Internal thresholds must stay between them | Slow ramp up/down, min/max supply and temperature |
| Rising threshold 2.157–3.205V; falling 1.242–2.056V; same-corner hysteresis >=0.896V | MODEL, independent ±1% plus ±0.05% resistor TCR corners, offset/leak allocations; see RX_REVIEW | Noninverting LM393B Schmitt stage | Measure hysteresis, noise response and actual leakage at15/25/35°C |
| LM393B offset <=4mV, bias <=50nA over specified temperature; VOL <=0.55V at <=4mA | CMP section 5.5, DS; offset table starts at 5V | Below 5V use these as an **ASSUMPTION**, not a guaranteed extension | Specifically test 4.75–5.00V comparator rail |
| Comparator delay 0.3–3us | CMP section5.7 gives0.3/1us typical only; **3us is ASSUMPTION** | Budget finite propagation/RC and integrate with the production driver; lateness and observation window unchanged | Measure both directions, low overdrive, rails, ambient and boards; >3us fails this envelope |
| Total adapter line load <=250uA at released high | ASSUMPTION; D1 max200uA specified at 60V/25°C, RX ~20uA, Q leakage | At 2.2k source idle >=4.20V | Measure 4.75/5.25V at 15/25/35°C; a warm Schottky can invalidate it |
| LM393B functional RX claim: nonnegative DATA and normal input common-mode region; V5_F4.75–5.25V | CMP§5.2 lists−0.1V lower recommended input;§5.5 electrical common-mode uses0..VCC−2V over temperature; project uses normal nonnegative input domain | Negative DATA is a fault target, not a supported communication voltage | Measure TP4/TP5 through low level and ramp; separately prove4.75V parameter allocations |
| LM393B stress ceilings: input−0.3..38V and negative input current−50mA | CMP§5.1, DS stress ratings only; parasitic current may corrupt output and raise ICC | R1 limits current but does not establish safe input voltage or valid comparison | Capture TP4, input/supply currents and recovery; no functional/no-damage inference from abs max |
| BAT54 VF<=0.240V at0.1mA,25°C,<=300us pulses/duty<=0.02; IR<=2uA at25V under pulse conditions; C<=10pF at1V/1MHz/25°C | BAT table7, DS only at stated conditions |−16V through R1 implies<74uA;10s DC clamp and15–35°C extrapolation are **OPEN** | TP4 must remain within approved stress limit; measure current/temperature and recovery |
| RX SENSE capacitance<=20pF; Rparallel<=49.898kΩ; RC tau<=0.998us | MODEL using10pF BAT54+10pF extra input/PCB allocation and resistor corners; no guaranteed all-bias20pF capacitance | Separate SENSE RC from DATA capacitance and comparator propagation | Characterized low-capacitance probes; include their loading; sensitivity sweep |
| Off-state input-to-rail injection allocation10uA; including R3/R6 feedback gives about9.1mV at+16V | ASSUMPTION, not manufacturer maximum; full protection **OPEN** | Retain680Ω bleed; direct SENSE→R3→R6→V5_F route remains even with comparator output high impedance | Actual V5_F<0.2V and gate<1.3V, rail currents and all power combinations; unmet limit rejects candidate |
| Rds(on)<=0.146R at Vgs4.5V,25°C; Vds60V | MOS section 10, DS | Gate >=4.5V; calculate with .32R thermal allocation | Gate waveform, Q current/temperature; .32R is not a manufacturer maximum at all temperatures |
| Q1 gate high>=4.6319V; gate capacitance2.2nF±5%; buffer<=100uA static output load | BUF DC table, CG spec + MODEL; Q2 loaded PG voltage/resistance is separate |220Ω Q1 gate series/68k pulldown; Q2 direct PG through2.2kΩ with10M pulldown | Check gate-source voltage/current in both MOS paths; Q2's0.5Ω allocation at loaded PG requires measurement |
| ISO7721F delay6–17ns at5V±10%; input-power-loss default LOW only when output domain is powered | ISO§6.15/Table8-2, DS; PU>=2.25V, PD<=1.7V, intermediate/output-off state undetermined | Independent power-good/reset and arm latch gate TX; D8 without valid rails is not valid RX | Slow ramps/brownouts withD3 alreadyHIGH; no guarantee from Fsuffix alone |
| Independent continuous-TX cutoff<=5ms; minimum timeout longer than conservative1.025ms legal LOW envelope | HondaDash requirement, not Honda; clock,40-tick lateness, actual PORTD tail/stamp and input skew in revised calculation | Timer/latch/gate release own sink independently of Nano; normal stop resets qualifier | Scope timer input/output, ARMED and both gates; all256 bytes, back-to-back0x00, stuckHIGH and reset |
| G01 supervisor thresholdnom4.376V; worst rising allocation<=4.689V with100k/10.2k and declared corners | PG§6.5 accuracy±2%, hysteresis<=3%, input bias plus resistor/TCR MODEL | Permits startup at4.75V; supervisor safety floor is distinct from the4.75V normal timing domain | Measure fall/rise thresholds,12–28ms release, loss recognition and intermediate supply; power alone never re-arms |
| Isolated USB health: VO617A-4X016 CTR allocation>=80% over15–35°C | OPTO DS minimum160% is at5mA/VCE5V/25°C;80% and loss-detection delay are ASSUMPTIONS | USB loss clears local arm through optical path; no shared ground wire | Measure LED current, collector low/high, slow ramps and saturated turn-off; include opto aging/board variation |
| Local re-arm gold-contact B3F-1002-G; bounce<=5ms; separate released-button qualification>=19.528778ms | SW page3 DS; valid contact load>=100uA, selected~0.43–0.48mA; LTC6994-1/2 calculation | Third timer/readiness latch requires released physical button plus inactive TX/healthy power, then a fresh debounced press | Verify pairs1+2/3+4; held-button startup/cycle must stay locked; bounce and active-TX rejection |
| TX/arm-data stable at least1µs around the qualified ARM_CLK; keep bridge quiesced throughout release/press qualification | ASSUMPTION/operator gate, longer than LVC latch setup/hold/recovery conditions | Asynchronous manual/firmware coincidence is not proven by an ideal DFF model | Capture clock/data if investigated; violated setup/hold/quiescence is **OPEN**, not guaranteed safe rejection |
| Full D3 edge to D8 valid <=8us, both directions | ASSUMPTION; 104us bit; existing20us lateness and±2% clock budget | No firmware deadline, TTL or guard relaxation | Scope D3/DATA/D8; both directions and USB traffic; budget includes RC/asymmetry |
| Separate B1 8.0–9.6V under load; LDO_IN >=6V; field rail target5V; draw<=20mA | CELL nominal9V DS; range ASSUMPTION; LDO DS | TPS70950,47R reverse-protected battery feed; 680Ω field bleed | Measure battery loaded voltage, draw, regulator stability and ramp behavior |
| Field output ceramic C effective >=2.2uF | LDO table5-1/application DS | Three 2.2uF/50V ceramics in parallel; DC-bias/temp/aging allowance | Measure effective capacitance or obtain vendor bias data; scope stability; input cap effective>=1uF |
| LAB ambient15–35°C, no condensation; no vehicle power input | ASSUMPTION / scope | Thermal and leakage gates restricted to this bench range | Log ambient and component temperatures for each run |

The ECU's sink threshold, pullup source, common-mode offset, harness capacitance,
transient environment, factory diagnostic pin and allowed commands are **UNKNOWN**.
Neither good checksum nor a successful two-Nano test answers those questions.
12V K-line, CAN, ELM and internal CN2 are different interfaces. No connector pin
number from another product is reused here. Hondash code/resources are not imported.

Revision B adds the independent TX cutoff, persistent arm/fault latch, supervised
power startup and local re-arm described in [FAILSAFE](FAILSAFE.md). The design
must pass the normal longestLOW (`0x00`: start plus eight zero data bits), release
its own sink by5ms on continuously asserted TX, and stay locked after a fault or
unsafe power cycle. Re-arm is local and accepted only with stable power and
inactive TX. Existing GUI/USB recovery commands do not clear this hardware state;
there is no new firmware latch feedback. The normal electrical model, failsafe
model, physical cutoff, isolation and external-fault protection have separate
statuses. **−16V/10s and off-state protection remain OPEN**; no global protection
PASS may include them. [RX_REVIEW](RX_REVIEW.md) states the selected narrow RX
domain and unchanged fault targets.

Software semantics remain strict M3a: both recognized bench identities before line
TX; selected endpoint bytes, parser and decoder before the model; no new whitelist;
200ms observation unchanged; fixed M2c scheduling/freshness; explicit coordinated
QUIESCE/generation/drain/NEW/ARM/INIT for our responder only. These operations do not
reset or make an unknown ECU recoverable.

R39/R40 each10kΩ are mandatory guard-node terminations. Aggregate20µA gives
≤0.2021V; source basis is buffer Ioff/ISO input conditions plus an explicit
ISO off-output injection assumption. Healthy loading≤0.54mA uses loaded HIGH
criteria, not the100µA VOH condition. Both nodes and all parasitics are physical
gates; overall off-state protection remains OPEN.

The healthy field-loss requirement is Ceff≥2.2µF/load≤20mA and ramps≥100µs:
4.2→2.25V decay≥214.5µs under those assumptions versus100µs gate release.
Arbitrary forced collapse/rail short or violated bounds remains OPEN.

Final field clamps D4/D5/D6 and R11=680Ω add source-relative Q1 VGS±20V and
actual U2 output−0.5V/upper-bound checks. D4 SINK_RET→V5_F, D5 GATE→V5_F and
D6 GND_F→GATE use finite BAT54 models;0.45V/2µA/10pF are declared allocations
with current/duration/temperature sensitivity and physical gates. Static field
draw16.733567mA has18mA steady design budget/20mA acceptance. Negative DC, clamp
DC/temperature and whole off-state remain OPEN, even after numeric stress PASS.
