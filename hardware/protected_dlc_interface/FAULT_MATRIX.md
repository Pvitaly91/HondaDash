# Fault envelope — no automotive qualification

All voltages refer to **GND_F**, except the explicit ground-offset rows. Normal
ambient15–35°C. Polarity, source impedance and current limit are part of a case.
Start each physical test at low current/short duration and increase only after
the previous stage meets [VALIDATION](VALIDATION.md). **Disconnect the M3a
responder and its pullup before any fault.** There is no vehicle, battery direct
on DATA, ordinary USB-connected scope, or unprepared PC in bench B.

`CANDIDATE` below means arithmetic/model suggests a reviewable experiment; actual
protection remains **NOT VERIFIED**. A model pass is not permission to omit
physical preflight or stop limits. The DC simulations settle over3ms; the separate
10s thermal procedure is not simulated thermal evidence.

| ID / condition | Applied V; source R; source limit; duration | TX; power state | Current path / intended result | Status / unresolved point |
|---|---|---|---|---|
| N / normal |4.75–5.25V;1–2.2k;10mA; continuous at bench | Both directions; USB+B1 on | Q sink <=5.25mA; released load<=250uA; no USB supply into line | CANDIDATE; actual ECU threshold unknown |
| U / USB removed | Same normal source;10mA;60s | Released then USB lost while asserted; B1 on | U1F output LOW; no field DC path to USB; R8 discharge | CANDIDATE; test real slow ramps and reset; possible short line transient |
| E / ECU fixture off |0V through1k, then open;10mA;60s | Release; USB+B1 on | No supply pullup; R3 can inject microamps, measure<=25uA into0V | CANDIDATE; open input is not a valid idle/ECU presence indication |
| B / field battery off | Normal source and±16V cases below | TX command0/1; USB on/off, B1 absent | U2 high impedance, R8 holds Q off; comparator input injection allocation, R11 bleeds rail | CANDIDATE; actual V5_F<0.2V and gate<1.3V required; no claim from model's missing parasitic junctions |
| G / DATA short ground |0V;<=0.1R;10mA;10s | Assert and release; all powered | Line LOW; driver sees stuckLOW/collision; no automatic recovery | CANDIDATE; a software fault is expected, not a data success |
| P12 / hard positive |+12V;<=0.1R;0.5A;<=10s | Assert and release; USB/B1 on and individual off | Assert: internal44R dissipates fault; release: Q/D1 reverse voltage and R1 attenuator | CANDIDATE; use current-limited lab source, not car battery |
| P16 / hard positive |+16V;<=0.1R;0.5A;<=10s | Same as P12 | WorstI<0.383A; each AC10<3.063W; DRAIN max16V | CANDIDATE; actual thermal, off leakage and slew must pass |
| N16 / negative DC |−16V;<=0.1R;0.5A; staged<=10s | Assert/release; all power combinations | D1/body diode or channel returns through44R to source; D2 protects SENSE through220k | **Conditional / NOT VERIFIED**. D2 VF guarantee is short-pulse25°C; cold/DC clamp may violate−0.3V. Stop immediately if it does |
| SL / MCU stuck sink | Normal pullup;10mA;60s; then P16 with10s limit | D3 held HIGH; power on | Current limited; bus remains LOW | **Unresolved availability fault. No independent hardware timeout.** Do not call software watchdog a hardware release guarantee |
| O / open DATA | No source, no pullup,<=0.5m harness;60s | Release; both rails on | RX generally settles LOW via local divider; no valid frames | CANDIDATE for no damage; communication OUT OF SCOPE |
| PS / power order |USB0↔5V/B18↔9V; rails current limited; slopes0.1/1/100ms;10 cycles | Hold LOW command, then HIGH only under fixture load | F defaults and pulldowns; loss of field forces RX LOW; release within1ms after rail collapse | Idealized sequence MODEL; brownout/undefined region physical NOT VERIFIED |
| GO / ground offset |GND_L versus GND_F =±1V; isolated source through1k;<=1mA;10s; no other ground link | Normal data; both on | Isolator passes logic, no intended DC return | Model assumes intact barrier; physical leakage/common-mode behavior NOT VERIFIED |
| GL / lost ECU_GND |Open return; voltage/current **not bounded** | Any | Return may move to shields, USB or instruments; data reference undefined | **OUT OF SCOPE**. No live ground-removal experiment and no safe-return claim |
| T± / project pulse |±24V;1k external;25mA limit;1ms flat,1us edges;1 pulse/s,10 pulses | Release; powered and separate off checks | Negative return via D1+44R; positive R1 attenuation/Q reverse hold; modeled energy integral reported | Conditional MODEL; fixture source/probe ringing must be measured; **not ISO7637** |
| H48 / deliberate bad control |+48V;0.1R;unlimited in model;3ms | Assert; rails on | Exceeds current/resistor design limits | **DESIGN FAIL**, simulated only; never a physical test recipe |
| Outside envelope |Load dump, alternator pulses, arbitrary battery short, reverse vehicle power, mains, severe ground offset, ESD gun, hotplug arcing | Any | No rated suppression or complete power-path protection for these cases | **OUT OF SCOPE**; no ISO7637/ISO16750/IEC61000/automotive/EMC/ESD claim |

There is **no TVS or positive zener** in this revision, so there is no fictitious
“5V clamp” rating to extrapolate to surge currents. D1 is a60V Schottky with a
finite current/temperature-dependent drop, specified0.75V maximum at1A/25°C short
pulse; at60V reverse its specified leakage is0.2mA/25°C and5mA/100°C. That increase
alone can destroy the normal idle-level budget outside the bench range. Its
capacitance curves are typical: include the assembled diode in the measured500pF
DATA budget. D2 capacitance/leakage/VF conditions are listed in REQUIREMENTS.
Negative fault current returns only to GND_F and the fault source; never through
USB ground in the intended isolated fixture. An external ground link invalidates
that argument. Positive pulse energy is mostly in the external source resistor
when released; the test does not represent a low-impedance automotive surge.

Protection is separate for four paths: RX uses high resistance/division and a
negative clamp; TX uses44R dissipation and60V parts; field power uses independent
B1/R14/D3P/U4; PC/USB relies on the complete U1/layout/cable/instrument barrier.
The weakest path controls acceptance. A successful RX waveform cannot prove TX
fault survival, supply safety, or PC isolation. Component absolute maxima are
stress ceilings, not recommended continuous operating points.
