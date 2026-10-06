# Electrical requirements and evidence classes

Values below are SI unless units are written. `DS` means a manufacturer limit at
its stated conditions; `REPORT` means an author's observation; `ASSUMPTION` is a
project test range; `MODEL` is a computed result; `MEASURED` requires an actual
instrument trace. **There are no MEASURED entries in this revision.** Datasheet
conditions must not be silently widened. Source IDs resolve in [SOURCES](SOURCES.md)
and in the BOM; the latter records the manufacturer URL and document revision.

| Requirement / range | Source and status | Design consequence | Physical validation |
|---|---|---|---|
| A communication wire near 5V; author reports 4.90–5.05V with ignition on | Hondash troubleshooting, REPORT; not OEM specification | Nominal 5V is a candidate reference only | Identify the actual ECU, pin, idle voltage, ground and source resistance before M4 |
| Classic Nano ATmega328P, 5V/16MHz; D3 sink enable; D8=PB0/ICP1 | Microchip pin functions + existing M3a driver, DS/software | USB powers logic only; preserve polarity and Timer1 | Verify exact board, clock, pin labels, rails and capture |
| DATA Thevenin source 4.75–5.25V; Rsource 1–2.2k; Ctotal 200–500pF | ASSUMPTION, swept; **actual ECU values UNKNOWN** | 44R sink resistance; no adapter pullup; C includes MOS/diode/cable/probes | Measure rise waveform/source resistance with a controlled fixture, then actual ECU separately |
| RX high acceptance >=3.3V; low <=0.6V | ASSUMPTION, not ECU input thresholds | Internal thresholds must stay between them | Slow ramp up/down, min/max supply and temperature |
| Rising threshold 2.161–3.200V; falling 1.245–2.051V; same-corner hysteresis >=0.897V | MODEL, independent 1% resistor corners, offset/leak allocations | Noninverting LM393B Schmitt stage | Measure hysteresis, noise response and actual leakage |
| LM393B offset <=4mV, bias <=50nA over specified temperature; VOL <=0.55V at <=4mA | CMP section 5.5, DS; offset table starts at 5V | Below 5V use these as an **ASSUMPTION**, not a guaranteed extension | Specifically test 4.75–5.00V comparator rail |
| Comparator delay 0.3–3us | CMP section 5.7 gives 0.3/1us typical only; **3us is ASSUMPTION** | Budget finite propagation and RC, keep driver unchanged | Measure both directions, low overdrive, rails, ambient and boards; >3us fails this envelope |
| Total adapter line load <=250uA at released high | ASSUMPTION; D1 max200uA specified at 60V/25°C, RX ~20uA, Q leakage | At 2.2k source idle >=4.20V | Measure 4.75/5.25V at 15/25/35°C; a warm Schottky can invalidate it |
| RX input negative limit -0.3V, positive abs max38V independent of supply | CMP section 5.1, DS; abs max is not operating range | R1=220k plus D2; positive divider; never MCU clamp protection | TP4 against field ground, including power-off |
| BAT54 VF<=0.240V at 0.1mA, 25°C, <=300us pulses; IR<=2uA at25V; C<=10pF at1V | BAT table 7, DS at stated conditions | -16V through R1 implies <74uA; DC/temperature extrapolation is conditional | Negative DC acceptance needs actual TP4 and diode temperature; cold/longer tests may fail |
| Rds(on)<=0.146R at Vgs4.5V,25°C; Vds60V | MOS section 10, DS | Gate >=4.5V; calculate with .32R thermal allocation | Gate waveform, Q current/temperature; .32R is not a manufacturer maximum at all temperatures |
| Gate high >=4.642V; gate capacitance 2.2nF ±5%; buffer <=100uA static output load | BUF DC table, CG spec + MODEL | 100R gate series, 68k pulldown; no reliance on typical transistor hFE | Check Q gate at minimum rail under repeated 0x55 |
| ISO7721F channel delay 6–17ns at5V±10%; default output LOW after input power loss | ISO section 6.15 / function table, DS | TX releases when logic disappears; field loss yields D8 LOW/fault | Slow power ramps and brownouts; not merely ideal 0/5V simulation |
| Full D3 edge to D8 valid <=8us, both directions | ASSUMPTION; 104us bit; existing20us lateness and±2% clock budget | No firmware deadline, TTL or guard relaxation | Scope D3/DATA/D8; both directions and USB traffic; budget includes RC/asymmetry |
| Separate B1 8.0–9.6V under load; LDO_IN >=6V; field rail target5V; draw<=20mA | CELL nominal9V DS; range ASSUMPTION; LDO DS | TPS70950,47R reverse-protected battery feed; 1k field bleed | Measure battery loaded voltage, draw, regulator stability and ramp behavior |
| Field output ceramic C effective >=2.2uF | LDO table5-1/application DS | Three 2.2uF/50V ceramics in parallel; DC-bias/temp/aging allowance | Measure effective capacitance or obtain vendor bias data; scope stability; input cap effective>=1uF |
| LAB ambient15–35°C, no condensation; no vehicle power input | ASSUMPTION / scope | Thermal and leakage gates restricted to this bench range | Log ambient and component temperatures for each run |

The ECU's sink threshold, pullup source, common-mode offset, harness capacitance,
transient environment, factory diagnostic pin and allowed commands are **UNKNOWN**.
Neither good checksum nor a successful two-Nano test answers those questions.
12V K-line, CAN, ELM and internal CN2 are different interfaces. No connector pin
number from another product is reused here. Hondash code/resources are not imported.

Software semantics remain strict M3a: both recognized bench identities before line
TX; selected endpoint bytes, parser and decoder before the model; no new whitelist;
200ms observation unchanged; fixed M2c scheduling/freshness; explicit coordinated
QUIESCE/generation/drain/NEW/ARM/INIT for our responder only. These operations do not
reset or make an unknown ECU recoverable.
