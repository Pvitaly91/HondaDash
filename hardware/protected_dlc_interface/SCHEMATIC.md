# Revision B circuit and assembly contract

[schematic.svg](schematic.svg) is the visible pin/net schematic: equal labels
connect, and each `NC_*` label remains a separate open pad. Its machine source is
[design.py](design.py); [netlist](netlist.json), [BOM](BOM.csv) and
[SPICE interconnect](simulation/frontend.cir) must agree. `design.py --check` plus
independent topology assertions detect drift and critical pin/polarity errors.
This is not a placement drawing. **PCB layout, Gerbers, manufacture and ordering
are outside M3b.1.** No hardware assembly or measurement is claimed.

## Power and ground domains

| Pads | Connection |
|---|---|
| J1.1/.2/.3/.4 | Nano USB5V / GND_L / D3 / D8; classic5V Nano only |
| J2.1/.2/.3 | ECU_DATA / ECU_GND=GND_F / VEHICLE_POWER_NC; functional labels, no vehicle pinout |
| J3.1/.2 | Removable B1 positive / negative=GND_F |

J2.3 has no trace; Nano VIN is unused. B1 is a removable independent Energizer522
alkaline battery, accepted only at8–9.6V under load. R14 is the first component
after its insulated positive lead. Prevent a short upstream of R14 with insulated
leads/strain relief. There is no charging, vehicle supply, USB power tie or DC/DC
converter across the barrier. B1 removal is the field-power disconnect.

U1 isolates the two data directions. U14 is a second optical barrier for USB
health only; its LED circuit uses GND_L and its detector uses GND_F. Neither the
button, test points, PG wiring nor indicators join grounds. Both barriers and all
external cables/instruments participate in the system isolation review.

## Exact IC and polarity map

Every pin below is checked against the part/package in SOURCES and the generated
map; do not substitute a similarly named family, package or non-F isolator.

| Ref / package | Pin/net contract |
|---|---|
| U1 ISO7721FDWR, DW16 | GND_L1/7, V5_L3, RX_L_ISO/OUTA4, TX_L_ISO/INB5; GND_F9/16, TX_F/OUTB12, RX_F/INA13, V5_F14;2/6/8/10/11/15 individually NC |
| U2 SN74LVC1G17DBVR |1NC,2TX_ALLOWED,3GND_F,4GATE_DRIVE,5V5_F |
| U3 LM393BIDR, SOIC8 |1RX_F,2VREF/IN−,3SENSE/IN+,4GND_F,5GND_F/spareIN+,6VREF/spareIN−,7NC,8V5_F |
| U4 TPS70950DBVR |1LDO_IN,2GND_F,3EN open,4NC,5V5_F; EN must not be tied to9V |
| U5 LTC6994IS6-1#TRPBF, S6 |1TX_F,2GND_F,3TIMER_SET,4TIMER_DIV,5V5_F,6TIMEOUT |
| U6 LTC6994IS6-2#TRPBF, S6 |1ARM_RAW,2GND_F,3ARM_SET,4ARM_DIV,5V5_F,6ARM_CLK |
| U7 SN74LVC1G74DCUR |1ARM_CLK,2ARM_DATA,3LOCKED/!Q,4GND_F,5ARMED/Q,6CLR_N/!CLR,7V5_F/!PRE,8V5_F |
| U8 SN74LVC2G02DCUR |1TX_F,2TIMEOUT,3CLR_N,4GND_F,5TIMEOUT,6POWER_BAD,7IDLE_OK,8V5_F |
| U9 SN74LVC2G08DCUR |1TX_F,2ARMED,3ARM_SAFE,4GND_F,5IDLE_OK,6PG_CLEAN,7TX_ALLOWED,8V5_F |
| U10 SN74LVC1G04DBVR |1NC,2PG_CLEAN,3GND_F,4NOT_PG,5V5_F |
| U11 TPS3808G01DBVR |1PG_F/!RESET,2GND_F,3V5_F/MR,4CT open,5PG_SENSE_F,6V5_F |
| U12 SN74LVC1G17DBVR |1NC,2PG_F,3GND_F,4PG_CLEAN,5V5_F |
| U13 TPS3808G01DBVR |1PG_L/!RESET,2GND_L,3V5_L/MR,4CT open,5PG_SENSE_L(rawUSB divider),6V5_L |
| U14 VO617A-4X016, wideDIP4 |1LED_A,2LED_K,3GND_F/emitter,4USB_BAD/collector |
| U15 SN74LVC1G32DBVR |1NOT_PG,2USB_BAD_CLEAN,3GND_F,4POWER_BAD,5V5_F |
| U16 SN74LVC1G17DBVR |1NC,2USB_BAD,3GND_F,4USB_BAD_CLEAN,5V5_F |
| U17 LTC6994IS6-1#TRPBF, S6 |1READY_REQ,2GND_F,3READY_SET,4READY_DIV,5V5_F,6READY_QUAL |
| U18 SN74LVC1G74DCUR |1READY_QUAL,2V5_F,3spare!Q NC,4GND_F,5REARM_READY,6CLR_N/!CLR,7V5_F/!PRE,8V5_F |
| U19 SN74LVC2G02DCUR |1ARM_RAW_CLEAN,2POWER_BAD,3spareY NC,4GND_F,5V5_F,6V5_F,7RELEASE_OK,8V5_F |
| U20 SN74LVC2G08DCUR |1RELEASE_OK,2IDLE_OK,3ARM_DATA,4GND_F,5ARM_SAFE,6REARM_READY,7READY_REQ,8V5_F |
| U21 SN74LVC1G17DBVR |1NC,2ARM_RAW,3GND_F,4ARM_RAW_CLEAN,5V5_F |
| U22 LM66100DCKR, SC70-6 |1USB5V/VIN,2GND_L,3V5_L/CE,4NC,5GND_L/ST unused,6ISO_FEED/VOUT |
| U23 SN74LVC1G17DBVR |1NC,2D3,3GND_L,4TX_L_ISO,5V5_L |
| U24 SN74LVC1G17DBVR |1NC,2RX_L_ISO,3GND_L,4D8,5USB5V |

Q1/Q2/Q3 are PMV88ENEA,215 SOT23 with gate1/source2/drain3. Q1 source is now
**SINK_RET**, Q2 source GND_F, Q3 source GND_L. BAT54 D2:1A=GND_F,2NC,3K=SENSE.
SS16 D1 band/cathode=DRAIN, anode=GND_F; D3P band=LDO_IN, anode=BAT_LIMITED.
All capacitors are nonpolar. SW1 B3F-1002-G has internal pairs1+2 and3+4:
1/2=V5_F,3/4=ARM_BUTTON. Manufacturer terminal connections are top view, with
numbering defined by its header bottom-view convention. Verify pairs unpowered.

## TX cutoff, local re-arm and power gating

`D3 -> held-powered U23 -> U1 -> TX_F -> U9(TX_F AND ARMED) -> U2 -> R7 -> Q1.G`.

U5 watches TX_F independently of Nano execution. R15=200kΩ and R16/R17=357k/100k
select rising-edge delay, DIVCODE3/N512: nominal2.048ms. TIMEOUT clears U7 through
U8. Returning TX LOW resets the timer but cannot set U7. The latch requires a
debounced local button action, healthy power and inactive TX. Debounce uses
U6/R18=249kΩ with R19/R20=255k/100k, DIVCODE4/N4096; minimum19.528778ms exceeds
the specified5ms button bounce. R22=10k pulldown and R23=1k button series keep
ARM_RAW LOW with the switch open. Firmware does not read ARMED/LOCKED.

U17 is a third LTC6994-1: R33=249kΩ SET and R34/R35=255k/100k DIVCODE4/N4096
require a continuous qualified **physical button release, inactive TX and healthy
power** before U18 latches REARM_READY. U21 cleans ARM_RAW; U19/U20 form READY_REQ.
Minimum qualification19.528778ms occurs after stable health, not from power-on time.
TIMEOUT or POWER_BAD clears both permission latches. U9's second AND makes
ARM_SAFE=IDLE_OK AND PG_CLEAN; U20 then makes ARM_DATA=ARM_SAFE AND REARM_READY.
Thus a button held through startup/brownout cannot arm: it must be released long
enough to qualify, then freshly pressed and debounced. The generated pin map
includes the extra latch/gates and C23–C27 local100nF bypasses.

Q1.G has R7=220Ω, R8=68kΩ and C10=2.2nF. The current route is
`DATA -> R12=22Ω -> R13=22Ω -> Q1.D -> Q1.S/SINK_RET -> Q2.D -> Q2.S/GND_F`.
Q2 receives **raw PG_F directly** through R24=2.2kΩ; R25=10MΩ and C11=2.2nF
form its gate network. U12 cleans the slow PG edge for logic and does not drive
Q2. This independent series gate prevents unspecified low-supply logic from
being the sole sink-permission barrier. Q2's0.5Ω resistance is an allocation at
its loaded PG gate level, not the4.5V datasheet Rds maximum applied at a lower Vgs.

U11/U13 use100kΩ/10.2kΩ SENSE dividers,150kΩ RESET pullups and CT open.
PG release is12–28ms after a valid sensed supply; thresholds are in FAILSAFE.
USB PG_L drives Q3's gate through R36=2.2kΩ; U14's LED current is limited by
R27=390Ω from **raw USB5V**, not V5_L. U13 senses raw USB through R31/R32 but
is powered by V5_L, so its valid reset can clear permission before U1 loses
valid input-domain power. The gate resistors also bound capacitive discharge current into the
buffer/supervisors; they are not optional jumpers. Field R28=10kΩ makes USB_BAD
HIGH when USB health is absent; U16 cleans that detector edge. U15 combines
USB_BAD_CLEAN and NOT_PG, so field or USB loss clears permission. Restoration
with D3 already HIGH stays locked; local re-arm is separate from USB recovery.
See [FAILSAFE](FAILSAFE.md) for the state table, timing/corner bounds and model
limitations. RESET and isolator intermediate supplies are not valid logic levels.
The2.2kΩ PG gate paths can initially draw about2.42mA while discharging. Staying
below the supervisor's5mA stress rating does not prove its dynamic VOL; the
100µs PG release limit is a project model/measurement gate at that load.

R12/R13 are **AC10:10W at40°C/8.4W at70°C,5%**, not AC01. Cutoff does not protect
against a shorted Q1 channel or force a released bus HIGH. Negative current can
return through D1/body diodes even with both gates off. The original±16V/10s
fault target is unchanged; negative DC/off-state protection remains **OPEN**.

## RX and battery paths

DATA→R1=220k→SENSE; R2=68k to GND_F; R3=1M from RX_F supplies hysteresis.
R4/R5=78.7k/10k make VREF with C5=10nF. U3's open collector has R6=2.2k to V5_F;
RX_F→U1→raw-powered U24→D8 is noninverted. R10=10k holds the logic receiver when
unpowered. U24's Ioff bounds output leakage; it is not a zero-injection guarantee.
D2's short-pulse specification is not a10s DC guarantee: [RX_REVIEW](RX_REVIEW.md)
separates normal operation, stress, function during fault, survival and recovery.
No MCU clamp is the field protection. R3/R6 also create a small off-state
injection route; no DATA pullup does not mean zero current.

B1+→R14=47Ω/AC03→D3P→U4.IN. C6=2.2µF input and C7/C8/C9=2.2µF each parallel
output must meet effective-capacitance requirements. R11=1kΩ bleeds V5_F.
C1–C4 and C12–C27 provide local100nF bypasses in their specified domains;
C10/C11 are2.2nF gate capacitors. Match the generated net/pin map when placing
each bypass. Field static allocation is about14.26mA with a16mA steady design
budget and **20mA measured acceptance ceiling**, leaving switching/headroom.
USB-health LED current is allocated7.86–11.01mA separately from field draw.
Stop if B1<8V loaded, LDO_IN<6V, raw USB/field rail outside4.75–5.25V or held
V5_L<4.5V during normal exchange.
Power-good defines a safety floor and does not establish valid normal RX timing
at every voltage above its trip point.

Logic hold-up is `raw USB5V -> U22.VIN1 / U22.OUT6 -> R37=22Ω AC03 -> V5_L`.
U22.CE3 connects to V5_L **after R37**, and VIN sees raw USB directly. A resistor
ahead of VIN can prevent the limited reverse current from producing the specified
CE differential, so its placement is critical. C28/C29 each4.7µF connect V5_L to
GND_L; their combined effective minimum is4.7µF, not their9.4µF nominal sum.
C1/C20/C30 bypass V5_L; C31 bypasses raw USB. R38=4.7kΩ bleeds raw USB. With a
25µA reverse/input injection allocation it gives about0.119V off rail; actual
back-power remains a measurement gate. U23 tolerates raw D3 input above its held
supply during startup; U24 isolates held RX drive from an unpowered Nano D8.

Normal V5_L is required≥4.5V with held load≤10mA. LM66100 Ron0.14Ω, blocking≤15µs
and startup≤150µs are project allocations requiring measurement. Inrush is
limited by R37 to about0.252A; its≈1.319W peak is below AC03's2.5W70°C rating.
The LM contains no project current-limit function. Both held and raw rails share
only GND_L; no new field-power/barrier path is added. The input named VIN on U22
is a component pin for raw USB, not Nano VIN or vehicle power.

## Assembly review, thermal limits and test points

Maintain at least8mm copper/pad/trace clearance and creepage between all logic and
field conductors, including underside planes, leads and mounting metal. Place
U1 and option6 U14 at the barrier with no ground fill or crossing signal/power
trace beneath it. These are candidate assembly constraints, not a rated completed
PCB or system isolation certificate. Inspect actual package drawings; layout and
fabrication are outside this substage. Solderless breadboard is not an isolation
qualification fixture.

Place bypasses at each supply pin; keep SET/DIV nodes short and clean, SET stray
capacitance below10pF, and leakage within the declared10nA allocation. Keep SENSE/
VREF away from TX and heat; SENSE's20pF includes device/diode/trace/probe budget.
D1 return is short to GND_F and separate from comparator return. Keep R12/R13
clear of ICs, battery, plastic, button and the barrier; provide touch protection.

Stop at resistor body>100°C, any Q/D1 package>80°C or adjacent battery>40°C.
Ambient15–35°C alone does not establish precision resistor/semiconductor
temperatures after a fault. Cool, inspect and repeat baseline measurements;
transient SPICE is not a thermal measurement.

TP1D3, TP2D8, TP3DATA, TP4SENSE, TP5VREF, TP6RX_F, TP7Q1.G, TP8DRAIN,
TP9V5_F, TP10LDO_IN, TP11GND_L, TP12GND_F, TP13TX_F, TP14TIMEOUT,
TP15ARMED, TP16PG_F, TP17ARM_RAW, TP18Q2.G, TP19LOCKED, TP20REARM_READY,
TP21READY_QUAL, TP22READY_REQ, TP23USB_BAD(field), TP24PG_L(logic), TP25V5_L(logic)
and TP26ISO_FEED(logic). Button/latch diagnostic pads remain field-local; the
new logic-rail/PG pads use GND_L. None adds Nano latch feedback. Probe each node
against its matching ground, with rated differential or isolated instruments.
Never lift scope protective earth to obtain a floating instrument.
Before assembly changes, disconnect all sources and verify V5_L has actually
discharged below0.2V; bulk-capacitor charge is not removed by a software command.

## Functional fixture A versus the original M3a board

Remove **all bridge-side** M3a Q1/RB1/RBE1/U1/RI1/RD1/RL1/C1, bridge-side2.2k
RPU and the direct bridge-to-responder ground wire. Connect only bridge USB5V/
GND/D3/D8 to J1. Keep responder Q2/RB2/RBE2/U2/RI2/RD2/RL2/C2 unchanged.
Use exactly one separate measured fixture pullup from responder-local5V to DATA;
responder ground goes to GND_F. Do not join Nano5V rails. Its original5V idle,
LOW and≤2µs receiver-edge limits still apply, alongside this board's limits.

The responder and its whole line fixture must be disconnected before faults.
Both USBs on one PC join grounds externally, so fixture A tests bytes and timing
only; it cannot validate isolation. This board does not retrofit protection into
the retained responder. Follow [VALIDATION](VALIDATION.md) for power-off wiring,
manual arm, strict identities, ordered bring-up and separate fault/isolation work.

## Guard termination and field-loss envelope

R39=10kΩ connects TX_L_ISO to GND_L; R40=10kΩ connects RX_L_ISO to GND_L.
Both are mandatory. The20µA aggregate source/injection allocation produces
≤0.2021V with resistor/TCR corners; the RX ISO off-output contribution is a
project assumption. Healthy load is below0.54mA per node, so the100µA buffer
VOH row cannot justify U23. Its3.8V anchor at4.5V/32mA and the measured HIGH
criterion are checked against U1; the RX path has a separate ISO output bound.
The final held-model extra load is6.2mA bookkeeping, not a physical resistor;
actual total held draw≤10mA remains a gate.

Healthy field loss assumes Ceff≥2.2µF/load≤20mA and ramps≥100µs. Its4.2→2.25V
passive-decay estimate is≥214.5µs versus the100µs PG release allocation. External
forced collapse, rail short or violated C/load bounds is OPEN and cannot inherit
a general PG release guarantee.
