# Revision A circuit and assembly contract

[schematic.svg](schematic.svg) is the authoritative visible **pin/net schematic**:
each pin's label is a wire name; all occurrences of a name connect. The drawing is
not a placement drawing. [design.py](design.py) is its machine source;
[netlist.json](netlist.json), [BOM.csv](BOM.csv) and
[simulation/frontend.cir](simulation/frontend.cir) must agree. `--check` verifies
that agreement plus independent critical pin/polarity assertions. A PCB layout,
Gerbers, routing/DRC inspection and a manufactured board are still absent.

## Domains and pins

| Connector/pads | Connection |
|---|---|
| J1.1 / .2 / .3 / .4 | Nano USB5V / GND_L / D3 / D8 respectively; classic 5V Nano only |
| J2.1 / .2 / .3 | ECU_DATA / ECU_GND=GND_F / VEHICLE_POWER_NC; **functional labels, no vehicle pinout** |
| J3.1 / .2 | Removable B1 positive / negative=GND_F; independent Energizer522 battery only |

J2.3 is an isolated labeled pad with **no copper trace**, not a parked wire from a
vehicle. Nano VIN is unused. B1 removal is the field-power switch. R14 must be the
first component after the insulated positive battery lead; insulated/keyed leads
must prevent a battery short upstream of R14. The battery has no charging, USB or
earth connection. Do not substitute a rechargeable battery or bench supply without
separately checking voltage, current limit and isolation. No cross-barrier DC/DC
converter, USB ground tie or shared power rail is present.

U1 is **ISO7721F, DW-16**, not ISO7720, non-F, D-8 or DWV-8. Top view:
GND1=1/7, VCC1=3, OUTA=4→D8, INB=5←D3; GND2=9/16, OUTB=12→TX_F,
INA=13←RX_F, VCC2=14. Pins2/6/8/10/11/15 are separately unconnected.
Its default LOW makes loss of logic power release TX; loss of field power makes
RX LOW so the driver faults rather than inventing an idle line.

U2 DBV-5: 1 NC,2 A=TX_F,3 GND_F,4 Y=GATE_DRIVE,5 V5_F.
U3 LM393B SOIC-8: 1 OUT=RX_F,2 IN-=VREF,3 IN+=SENSE,4 GND_F,
5 spareIN+=GND_F,6 spareIN-=VREF,7 spareOUT NC,8 V5_F.
U4 TPS70950 DBV-5: 1 IN,2 GND,3 EN **open**,4 NC,5 OUT. EN has internal
enable bias; **do not connect EN to 9V** (its maximum is lower than IN).
Q1 SOT23: 1 gate,2 source=GND_F,3 drain. BAT54 D2 SOT23:1 anode=GND_F,
2 NC,3 cathode=SENSE. D1 SS16 cathode band goes to DRAIN, anode to GND_F;
D3P band goes to LDO_IN, anode to BAT_LIMITED. C1–C10 are nonpolar ceramics.

## Signal and power paths

TX: D3 → U1.INB/OUTB → U2 → R7=100R → Q1 gate. R9=47k holds D3 LOW
during reset; R8=68k and C10=2.2nF hold Q1 off against leakage/Miller coupling.
Q1 source is field ground. DATA → R12=22R → TX_MID → R13=22R → Q1 drain.
Both resistors are **AC10, 10W at40°C /8.4W at70°C,5%**, not similarly named AC01.
R12/R13 dissipate a positive active-TX fault or a negative D1/body-diode fault.
There is no current path from the USB rail into DATA. D1 conducts negative drain
current locally; it is **not a TVS** and does not clamp positive DATA voltage.

RX: DATA → R1=220k → SENSE; R2=68k to GND_F; R3=1M from RX_F creates
hysteresis. R4=78.7k/R5=10k generate VREF, bypassed with C5=10nF.
U3 output is open collector with R6=2.2k to V5_F. RX_F → U1.INA/OUTA → D8;
R10=10k holds D8 LOW when unpowered. A HIGH bus produces HIGH D8; a LOW bus
produces LOW D8. D2 clamps negative SENSE through R1; positive faults are attenuated
by the divider. No MCU input protection diode is part of the field fault path.
R3 can source a few microamps into the line while RX is HIGH: include this in
unpowered-target tests; “no pullup” does not mean mathematically zero injection.

Power: B1+ → R14=47R/3W → D3P → U4.IN. C6=2.2uF input; C7/C8/C9=2.2uF each
parallel output. R11=1k bleeds V5_F. C1 at U1 logic supply, C2 at its field supply,
C3 at U2 and C4 at U3 are100nF. Rails have different grounds; no capacitor except
the isolator's parasitic capacitance intentionally crosses the barrier.
The field functional budget is20mA, including bleed, comparator, isolator, output
pullup and switching. At B1>=8V under load, input headroom is calculated separately.
The LDO cannot make a discharged battery acceptable: stop if LDO_IN<6V or V5_F
is outside4.75–5.25V. Power-ramp behavior remains a physical gate.

## Layout, test points and thermal limits

Keep a **minimum8mm copper/trace/pad clearance and creepage** between all logic and
field conductors, including underside planes, test leads, mounting metal and board
edges where relevant. Place U1 across the barrier; no ground fill under the gap.
This project layout rule does **not** establish a system safety-insulation rating.
Use a clean dedicated PCB with suitable creepage, strain relief and an enclosure;
solderless breadboard is not an isolation qualification fixture. Inspect U1's
actual package and land pattern against its mechanical drawing before manufacture.

Put C1–C4 at their associated supply pins. Keep SENSE/VREF short and away from TX;
the total SENSE input/diode/trace capacitance must fit20pF. Keep D1's return short
to field ground, separate from the comparator return. Put R12/R13 away from ICs,
battery, plastic and the barrier; provide air clearance and touch protection.
Thermal acceptance: ambient15–35°C, no resistor body above100°C, Q1/D1 package
above80°C, or adjacent battery above40°C during the specified fault. A rating on
a datasheet does not establish board surface temperature. Stop immediately at a
temperature/current/voltage limit; cool and inspect before resuming.

TP1 D3, TP2 D8, TP3 DATA, TP4 SENSE, TP5 VREF, TP6 RX_F, TP7 GATE, TP8 DRAIN,
TP9 V5_F, TP10 LDO_IN, TP11 GND_L, TP12 GND_F. Use the matching ground domain.
A passive grounded oscilloscope clip across domains defeats isolation and can
carry fault current; use rated differential probes or isolated battery instruments.
Never disconnect protective earth to obtain a floating scope.

## Difference from M3a

For functional bench A, remove **all bridge-side** M3a Q1/RB1/RBE1/U1/RI1/RD1/RL1/C1
and bridge-side2.2k pullup RPU, and remove its direct bridge-to-responder GND wire.
Connect only Nano bridge USB5V/GND/D3/D8 to J1 of this board. Keep the original
responder's Q2/RB2/RBE2/U2/RI2/RD2/RL2/C2. Put exactly one separate fixture2.2k
pullup from responder local5V to DATA; connect responder GND to **GND_F**.
No Nano5V rails are joined. The responder and all of its direct line circuitry
must be physically disconnected before bench B fault testing.

With both Nanos plugged into one PC, USB grounds join externally: bench A can test
bytes, but **cannot validate isolation**. The protected interface does not retrofit
protection into the unprotected responder. Full power/ground drawings for each
configuration and power-off assembly order are in [VALIDATION](VALIDATION.md).
