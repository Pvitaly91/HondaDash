# M3a low-voltage two-Nano schematic, revision 1

**Only the two USB-powered laboratory boards. No vehicle, 12 V, VIN, or vehicle connector.**
This is a buildable bench design with calculated limits; physical verification is **NOT VERIFIED**.
The [vector schematic](schematic.svg) and the following net list describe the same circuit.
Component references ending in 1 belong to the bridge; references ending in 2 to the responder.

## Exact connections

| Net / component | Connections |
|---|---|
| GND | N1 GND, N2 GND, Q1/Q2 emitter pin 1, U1/U2 pin 3, both local decoupling/bleed/pulldown returns |
| +5V_A | N1 5V, U1 pin 5, C1 upper lead, RL1 upper lead, RPU upper lead |
| +5V_B | N2 5V, U2 pin 5, C2 upper lead, RL2 upper lead; **never +5V_A** |
| BUS | RPU lower lead, Q1/Q2 collector pin 3, RI1/RI2 upper lead, TP_BUS |
| RB1/RB2, 10k | Local Nano D3/PD3 → local Q base pin 2 |
| RBE1/RBE2, 47k | Local Q base pin 2 → GND |
| RI1/RI2, 1k | BUS → local U input pin 2 |
| U1/U2 | SN74LVC1G17DBVR: pin 1 NC, pin 2 A, pin 3 GND, pin 4 Y, pin 5 VCC |
| RX1/RX2 | Local U output pin 4 → local Nano D8/PB0/ICP1 and local RD upper lead |
| RD1/RD2, 10k | Local RX → GND |
| RL1/RL2, 1k | Local 5V → GND; intentional 5 mA bleed, not a link between rails |
| C1/C2, 100nF | Local U pin 5 → pin 3, short leads, one per buffer |
| RPU, 2.2k | +5V_A → BUS; **one pullup only** |

No other Nano pins are connected. D0/D1 remain the board's USB UART; D9/D10 PWM,
Servo/Timer1 libraries and all other Timer1 users are forbidden in these two sketches.
Use manufacturer pin numbering, not a generic TO-92 photograph: the selected
[onsemi 2N3904](https://www.onsemi.com/download/data-sheet/pdf/2n3904-d.pdf)
is emitter 1, base 2, collector 3. The DBV pin assignment and powered-off limits are in
[TI's SN74LVC1G17 datasheet, pages 3–7](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf).

## Design calculations and operating limits

Operate indoors at 15–35 °C. Confirm **each local rail is 4.75–5.25 V** with both boards
powered from the same PC and a dedicated signal-ground wire; rail-to-rail ground offset
must be below 0.1 V. These are bench acceptance limits, not a claim about every USB clone.
High comes only from RPU. D3 HIGH turns Q on and pulls BUS LOW; D3 LOW or reset releases it.
Two simultaneous LOW drivers share a current-limited pullup, never oppose push-pull outputs.

Nominal LOW current at an assumed 0.2 V collector voltage is `(5−0.2)/2200 = 2.18 mA`;
the resistor-limited upper bound is `5.25/(2200×0.99) = 2.41 mA`. RPU dissipates at most
12.7 mW. A 10k base resistor supplies roughly 0.4 mA; subtract roughly 17 µA in RBE.
The transistor's published 0.2 V saturation figure is specified at 10 mA/1 mA; extrapolation
to this lower-current circuit is a design estimate. **Measure BUS LOW ≤0.4 V before acceptance**.
No absolute maximum rating is used as a normal operating target.

Use a BUS/GND twisted pair no longer than **0.5 m**, with combined wiring, transistor,
receiver and probe capacitance **≤200 pF**. The pullup RC bound is
`2.2k×1.01×200pF = 0.444 µs`; ideal 10–90% rise is `2.197τ = 0.977 µs`.
Allow **≤2 µs measured rise/fall to valid level** including transistor storage and wiring.
This RC calculation is not an observed edge. No termination is fitted.

The selected Schmitt buffer accepts inputs through 5.5 V independently of local VCC and
specifies Ioff ≤10 µA at VCC=0. Its thresholds at the 4.5/5.5 V table endpoints bound
this bench: a 0.4 V LOW and ≥4.5 V HIGH have comfortable margins. Its output uses its
own board's supply; BUS never reaches an AVR input directly. RD keeps a powered-off
RX node near ground (10 µA ×10k =0.1 V); RL bleeds each local rail (20 µA ×1k =0.02 V
under a conservative two-leakage-path estimate). The off NPN has a reverse-biased
collector junction and RBE holds its base down. These choices prevent a functional
phantom supply through the signal path; verify the unpowered rail remains below 0.1 V.

This **does not certify hot-plug safety**. Ground-loss, miswiring, arbitrary module substitutions,
external supplies and live signal connector insertion/removal are excluded. Assembly and
all signal changes require both USB cables disconnected. Once assembled, sequential USB
power-up/down is allowed with traffic stopped; operation requires both boards powered.
An unexpected single-board disconnect is a stop/fault event, never a recovery signal.
Do not perform ground-disconnect or live signal hot-plug tests.

All resistors are [Vishay MRS25, 1%, 0.6 W at the stated rating conditions](https://www.vishay.com/docs/28724/mrs16m25.pdf).
The [KEMET capacitor](https://search.kemet.com/component-documentation/download/specsheet/C315C104K5R5TA)
is a 100 nF, 50 V X7R decoupler. Their voltage/power margins are deliberate; the interface
remains limited to the stated 5 V bench. See [BOM.csv](BOM.csv) for exact orderable parts.

## Probe points and preflight

| Point | Expected / criterion |
|---|---|
| TP_A, TP_B | Local rails 4.75–5.25 V; rails not connected to one another |
| TP_GND | Shared signal return, ground offset <0.1 V |
| TP_BUS | Idle HIGH ≥4.5 V; asserted LOW ≤0.4 V; 9600 8N1 |
| TP_TX1/2 (D3) | LOW idle/reset; HIGH enables sink, inverse of the driven bit |
| TP_RX1/2 (D8) | Buffered noninverted BUS, local rail levels |

With power absent, check every row of the net list and component orientation, measure
RPU=2.2k and the two separate rail bleeds=1k, and check no short joins +5V_A/+5V_B.
Inspect the unpopulated buffer adapters for solder bridges and extra pullups/LEDs.
First verify the interface alone with a current-limited 5 V bench source and no Nano:
leave D3 inputs disconnected (RBE must release the bus), then drive each RB input HIGH
through its normal 10k path and measure LOW/current. Remove power and the test source
before connecting the two Nano boards. With both boards on USB and Stop active, verify
rails, idle, reset release and the off-rail leakage criterion before starting a transaction.

Record actual board revisions, USB chips, meters/analyzers and measured results during
bring-up; none are inferred from successful software tests. The complete sequence is in
[README.md](README.md).
