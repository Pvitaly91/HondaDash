# Revision B fault envelope — no automotive qualification

All voltages refer to **GND_F**, except the explicit ground-offset rows. Normal
ambient15–35°C. Polarity, source impedance and current limit are part of a case.
Start each physical test at low current/short duration and increase only after
the previous stage meets [VALIDATION](VALIDATION.md). **Disconnect the M3a
responder and its pullup before any fault.** There is no vehicle, battery direct
on DATA, ordinary USB-connected scope, or unprepared PC in bench B.

`CANDIDATE` below means arithmetic/model suggests a reviewable experiment; actual
protection remains **NOT VERIFIED**. A model pass is not permission to omit
physical preflight or stop limits. The DC simulations settle over3ms; the separate
10s thermal procedure is not simulated thermal evidence. `OPEN` is an unresolved
protection requirement, even when a numerical waveform check passes. The report
must keep `model_status` separate from `protection_status`, and must not include
OPEN cases in an aggregate protection PASS. Revision A results belong to the
baseline commit/report only. Normal communication does not require the comparator
to function during a fault.

| ID / condition | Applied V; source R; source limit; duration | TX; power state | Current path / intended result | Status / unresolved point |
|---|---|---|---|---|
| N / normal |4.75–5.25V;1–2.2k;10mA; continuous at bench | Both directions; USB+B1 on | Q sink <=5.25mA; released load<=250uA; no USB supply into line | CANDIDATE; actual ECU threshold unknown |
| U / USB removed | Same normal DATA source;10mA;60s; raw USB5→0/1.9V via0.1/1/100ms profiles | D3 LOW then HIGH tracking raw rail; B1 on | Held U1/U13/input guard stay valid through bounded health window; raw SENSE/LED clear latch before held rail becomes undefined | Model requires no new own-sink pulse with LOW request; pre-hold32.73µs counterexample retained; physical hold/blocking **NOT VERIFIED**, off-state **OPEN** |
| E / ECU fixture off |0V through1k, then open;10mA;60s | Release; USB+B1 on | No supply pullup; R3 can inject microamps, measure<=25uA into0V | CANDIDATE; open input is not a valid idle/ECU presence indication |
| B / field battery off | Normal source and±16V cases below; unchanged source limits and10s fault duration | TX command0/1; USB on/off, B1 absent | Supervisor/power gate requests release; R8 holds Q1 off; comparator injection allocation/R11 bleed | **OPEN** off-state protection; actual V5_F<0.2V and gate<1.3V, rail/input currents and recovery required; body diodes still conduct negativefault |
| G / DATA short ground |0V;<=0.1R;10mA;10s | Assert and release; all powered | Line LOW; driver sees stuckLOW/collision; no automatic recovery | CANDIDATE; a software fault is expected, not a data success |
| P12 / hard positive |+12V;<=0.1R;0.5A;<=10s | Initially armed/asserted and separately released; USB/B1 on and individual off | While enabled44R dissipates fault; cutoff then releases own sink; Q/D1 reverse hold andR1 attenuator remain exposed for10s | CANDIDATE powered arithmetic; physical thermal NOT VERIFIED; individual-off combinations **OPEN** |
| P16 / hard positive |+16V;<=0.1R;0.5A;<=10s | Same as P12; do not re-arm duringHIGH | I<0.383A conservative44R bound; eachAC10<3.063W; DRAIN max16V; timeout does not shorten external stimulus | CANDIDATE arithmetic; actual thermal/slew required; off-state **OPEN** |
| N16 / negative DC |−16V;<=0.1R;0.5A;staged to10s target | Assert/release/locked; every USB/B1 combination;15/25/35°C | GND_F→D1 and parallel MOS body/channel paths→DRAIN→R13/R12→fault source. RX return viaD2 andR1 | **OPEN for every10s power/temperature combination**, excluded from protectionPASS. BAT54 shortpulseVF does not proveDCinputclamp; SS16 DC/temp/thermal also unresolved. Gateoff does not interruptnegativepath |
| SL / MCU stuck sink | Normal pullup4.75–5.25V;1–2.2k;10mA;60s | Arm while TX is inactive, then hold D3 HIGH; both rails valid | Independent timer clears enable and releases own sink by5ms; latch stays locked with HIGH or a short LOW glitch | Revision B failsafe model gate; physical TX cutoff **NOT VERIFIED**. Another device/short may still hold DATA LOW |
| RA / held re-arm/startup | Normal fixture;10mA;60s; button bounce and power cycles included | Hold SW1 at power-on/restoration and attempt re-arm with TX active | READY requires continuously released physical button, inactive TX and healthy power for≥19.528778ms, then fresh debounced press; timeout/power loss clears readiness and arm | Model gate; physical button/latches **NOT VERIFIED**; power or GUI recovery alone cannot arm |
| RA-edge / coincident clock/TX | Normal fixture;10mA; intentional violating transition only in model | ARM_CLK and TX/data change within latch setup/hold or1µs quiescence gate | Real latch may metastabilize; ideal DFF does not demonstrate analog race safety | **OPEN**; normal physical procedure keeps TX quiesced for qualification and around clock |
| QDS / Q1 drain-source short | Normal fixture, then±16V;0.1R;0.5A;10s | Gate commanded off/timeout/locked; all power states | Control cannot remove Q1 D-S short;44R limits current; power disconnect/body diodes assessed separately | **OPEN / unresolved component fault**; no single-fault safety claim; negative current still flows |
| O / open DATA | No source, no pullup,<=0.5m harness;60s | Release; both rails on | RX generally settles LOW via local divider; no valid frames | CANDIDATE for no damage; communication OUT OF SCOPE |
| PS / power order/brownout | USB0↔5V/B18↔9V; cold preflight logic30mA; fast logic charging300mA≤5ms then30mA; slopes0.1/1/100ms;10cycles | Nano/PC absent; D3 LOW and HIGH tracking raw USB; fresh local arm only after safe recovery | Hold island/guards plus raw-USB/field supervisors clear both latches; power alone cannot re-arm; actual waveform must be recorded | Physical ramp/inrush/hold **NOT VERIFIED**; preset100µs is not evidence when current limit reshapes it; off-state **OPEN** |
| GO / ground offset |GND_L versus GND_F =±1V; isolated source through1k;<=1mA;10s; no other ground link | Normal data; both on | Isolator passes logic, no intended DC return | Model assumes intact barrier; physical leakage/common-mode behavior NOT VERIFIED |
| GL / lost ECU_GND |Open return; voltage/current **not bounded** | Any | Return may move to shields, USB or instruments; data reference undefined | **OUT OF SCOPE**. No live ground-removal experiment and no safe-return claim |
| T± / project pulse |±24V;1k external;25mA limit;1ms flat,1us edges;1pulse/s,10pulses | Release/locked; powered and individual off | Negative D1+44R return persists after cutoff; positive R1 attenuation/Q reverse hold; model energy reported | Numerical MODEL only; negative1ms exceeds BAT54 VF test width, hence **OPEN** clamp/survival; off-state **OPEN**; notISO7637 |
| H48 / deliberate bad control |+48V;0.1R;unlimited in model;3ms | Assert; rails on | Exceeds current/resistor design limits | **DESIGN FAIL**, simulated only; never a physical test recipe |
| Outside envelope |Load dump, alternator pulses, arbitrary battery short, reverse vehicle power, mains, severe ground offset, ESD gun, hotplug arcing | Any | No rated suppression or complete power-path protection for these cases | **OUT OF SCOPE**; no ISO7637/ISO16750/IEC61000/automotive/EMC/ESD claim |

There is **no TVS or positive zener** in this revision, so there is no fictitious
“5V clamp” rating to extrapolate to surge currents. D1 is a60V Schottky with a
finite current/temperature-dependent drop, specified0.75V maximum at1A/25°C short
pulse; at60V reverse its specified leakage is0.2mA/25°C and5mA/100°C. That increase
alone can destroy the normal idle-level budget outside the bench range. Its
capacitance curves are typical: include the assembled diode in the measured500pF
DATA budget. D2 capacitance/leakage/VF conditions are listed in REQUIREMENTS.
Revision B retains this RX circuit and explicitly narrows functional operation
to normal nonnegative DATA. See [RX_REVIEW](RX_REVIEW.md) for stress/current,
common-mode and recovery distinctions. Small BAT54 dissipation does not establish
its DC clamp voltage. The calculated0.287W SS16 estimate assumes0.75V drop; its
typical105K/W gives about65°C at35°C ambient, but neither assumption is a10s
temperature measurement or worst-case production thermal bound.
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

PS field-power cases require healthy Ceff≥2.2µF/load≤20mA and ramps≥100µs.
An arbitrary faster externally forced collapse, rail short or violated C/load
bound is OPEN and is not an authorized physical stimulus here. The passive
≥214.5µs decay calculation cannot establish a general PG or single-fault safety
claim. R39/R40 guard-node leakage checks use a20µA aggregate allocation;
off-state protection remains OPEN despite numerical checks.

The specified off/+24V pulse produced modeled Q1 VGS≈−23.2V/U2 output≈−1.65V
stress failures before D4/D5/D6/R11 revision. New diode paths limit source/gate
excursions; actual VGS, output, clamp currents and off-rail sum are checked.
The source amplitude/resistance/current limit/duration are unchanged. Clamp
DC/temperature and complete off-state remain OPEN; neither a successful pulse
model nor low calculated dissipation closes them. D1 and MOS leakage paths are
separate so field injection is not attributed to the wrong terminal.
