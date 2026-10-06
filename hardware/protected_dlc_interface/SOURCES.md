# Source registry and notices

Evidence reviewed6 October2026 for revision A and rechecked for revision B.
Manufacturer documents are engineering evidence,
not a license to copy their models. Exact component MPNs, URLs, revision/date and
table/section references are in the BOM and `design.py:SOURCES`; the machine netlist
also embeds that registry. Archive the procurement revision and its hash with the
physical build record because manufacturer symlink URLs can change. No vendor PDF,
third-party circuit artwork or closed SPICE model is committed here.

* [Hondash troubleshooting](https://www.hondash.net/p/troubleshooting.html) reports
  a communication wire at4.90–5.05V in its troubleshooting context. This is an
  author's product-specific report, not an OEM electrical specification. It says
  nothing sufficient about the user's unknown ECU pullup, thresholds or transients.
  Its vehicle instructions and connector pictures are not this lab procedure.
* [Microchip ATmega328P datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf),
  **7810D–AVR–01/15 (January2015)**, sections pin configuration, I/O,16-bit Timer1 and
  electrical characteristics. Used for PB0/ICP1 and5V logic context; existing timing
  evidence is pinned in [ONE_WIRE_DRIVER](../../docs/ONE_WIRE_DRIVER.md). Nano USB
  board topology is separate from MCU absolute ratings.
* [TI ISO7721](https://www.ti.com/lit/ds/symlink/iso7721.pdf), SLLSEP3G,May2024:
  Table5-1 DW pinout, section6.15 switching, and Table8-2 functional modes.
  F-default LOW applies with the output domain powered and the input domain
  powered down. Output power loss and the1.7–2.25V interval are undetermined;
  a driven input can weakly power a floating rail. It is not a brownout supervisor.
  The IC's isolation rating
  is not a certified rating of an unbuilt PCB and its attached instruments.
* [TI SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf), SCES351Y,
  October2025: DBV pins, DC output drive, Ioff and timing. Field-only gate buffer;
  DATA is not connected to this part's input.
* [TI LM393B](https://www.ti.com/lit/ds/symlink/lm393.pdf), SLCS005AH,April2025:
  sections5.1/5.2/5.5/5.7 and7.2.2.1. Revision B distinguishes input voltage
  stress ratings, input-current stress rating, common-mode operation and recovery.
  Negative parasitic input current can increase supply current and corrupt output;
  the−50mA absolute rating is not a functional or long-duration no-damage guarantee.
  Do not substitute legacy LM393 or treat typical switching figures as maxima.
  Off-state injection is a model allocation requiring measurement.
* [TI TPS709](https://www.ti.com/lit/ds/symlink/tps709.pdf), SBVS186H,July2021:
  TPS70950DBVR pins, EN voltage, dropout, stability/output-capacitance requirements.
  The similarly named TPS709A/B have different DBV pins and are not substitutions.
* [Nexperia PMV88ENEA](https://assets.nexperia.com/documents/data-sheet/PMV88ENEA.pdf),
  3August2026: pins,60V rating, Rds at4.5V, gate charge, thermal conditions, DC SOA
  plot and typical capacitance. Q1/Q3`.32R`, Q2`.5R`, Cgd50pF and temperature
  allocations are project assumptions. Q2/Q3's loaded PG drive can be below4.5V;
  the4.5V datasheet Rds limit cannot be applied at that lower Vgs. These are not
  guaranteed all-temperature limits; gate current and actual resistance are gates.
* [Nexperia BAT54](https://assets.nexperia.com/documents/data-sheet/BAT54.pdf),
  1July2022, Tables2/5/6/7: pins, stress ratings, thermal conditions and
  forward/leakage/capacitance conditions. The240mV value at0.1mA is specified
  for<=300us pulses, duty<=0.02,25°C. Neither that limit nor typical curves
  substantiates the requested−16V/10s DC clamp over15–35°C or with power off.
* [Vishay SS16-M3/IA](https://www.vishay.com/docs/98653/ss16hm3_bia_ss16-m3ia.pdf),
  document98653,12March2025, pages1–3: cathode marking, ratings, voltage/leakage and
  typical thermal/capacitance data. It is a rectifier, not an automotive TVS.
* [Vishay MRS25](https://www.vishay.com/docs/28724/mrs16m25.pdf),28724,7March2016:
 1%,0.6W at70°C,50ppm/K; order codes in the BOM follow the manufacturer's table.
  Revision B RX review includes another±0.05% for15–35°C relative to25°C.
  Offset, leakage and propagation allocations still require measured acceptance;
  this is not exhaustive proof over the component's full temperature rating.
* [Vishay AC series](https://www.vishay.com/docs/28730/ac_ac-at_ac-ni.pdf),28730,
  5December2024: AC10/AC03 ratings, derating, heat/clearance guidance and ordering.
  MPNs are `AC10000002209JAB00` (22R AC10) and `AC03000004709JAC00` (47R AC03).
  No claim that the axial resistor is noninductive; AC-NI is a different variant.
* KEMET generated per-part specifications for
  [100nF](https://search.kemet.com/component-documentation/download/specsheet/C315C104K5R5TA),
  [10nF](https://search.kemet.com/component-documentation/download/specsheet/C315C103K5R5TA),
  [2.2nF C0G](https://search.kemet.com/component-documentation/download/specsheet/C315C222J5G5TA),
  [2.2uF X7R](https://search.kemet.com/component-documentation/download/specsheet/C340C225K5R5TA),
  retrieved6October2026; tables Specifications/Dimensions. The2.2nF and10nF PDFs
  were generated6October2026. Nominal X7R capacitance is not minimum capacitance
  at bias, temperature and age; LDO capacitor verification remains a gate.
* [Energizer522](https://data.energizer.com/pdfs/522_ap.pdf), FormEBC-1108L,undated:
  nominal9V alkaline.8–9.6V is the measured project acceptance window, not a quoted
  manufacturer's regulated output or life guarantee.

Revision B adds these field-domain parts; their pin numbers and source IDs are
also recorded by the updated design source/BOM. The circuitry is original project
work and its behavioral timing models are not vendor SPICE models.

* [ADI LTC6994-1/LTC6994-2](https://www.analog.com/media/en/technical-documentation/data-sheets/LTC6994-1-6994-2.pdf),
  **Rev.C,November2019** (revision history/page27 and footer/page28):
  `LTC6994IS6-1#TRPBF` continuous-HIGH qualifier and
  `LTC6994IS6-2#TRPBF` two-edge re-arm debounce. Page2 S6 pinout; pages3–5
  electrical conditions; Table1 DIVCODE; operation startup/edge behavior;
  applications SET capacitance/current and power budget. For NDIV>=512 use the
  full-temperature±3.0% delay limit, not the25°C±2.3% headline. Output propagation,
  SET leakage and layout allocations are identified separately in FAILSAFE.
* [TI SN74LVC1G74](https://www.ti.com/lit/ds/symlink/sn74lvc1g74.pdf),
  SCES794G,September2021: `SN74LVC1G74DCUR`; section5 pins and function table,
  sections6.3/6.5/6.6/6.7 operating levels, Ioff, asynchronous clear,
  setup/recovery and switching bounds. Power-up state is not an arm permission.
* [TI SN74LVC2G02](https://www.ti.com/lit/ds/symlink/sn74lvc2g02.pdf),
  SCES194N,May2019: `SN74LVC2G02DCUR`; sections5/6.3/6.5–6.7/8.4,
  DCU pins, Boolean function, supply/logic limits, Ioff and timing.
* [TI SN74LVC2G08](https://www.ti.com/lit/ds/symlink/sn74lvc2g08.pdf),
  SCES198N,December2015: `SN74LVC2G08DCUR`; sections5/6.3/6.5–6.7/8.4,
  DCU pins, Boolean function, supply/logic limits, Ioff and timing.
* [TI SN74LVC1G04](https://www.ti.com/lit/ds/symlink/sn74lvc1g04.pdf),
  SCES214AF,October2025: `SN74LVC1G04DBVR`; sections4/5.3/5.5–5.9/7.4,
  DBV pins, inverter polarity, supply/logic limits, Ioff and switching bounds.
* [TI TPS3808](https://www.ti.com/lit/ds/symlink/tps3808.pdf),
  **SBVS050N,August2026**: `TPS3808G01DBVR` supervises the logic and field rails;
  sections4/5/6.5/6.6/7.3/7.4 specify threshold variant, pins, RESET pullup and
  leakage conditions, power-up reset, hysteresis and CT-open12–28ms release delay.
  Threshold tolerance plus hysteresis must permit release at the lowest accepted
  rail. Typical threshold accuracy is not the maximum corner. RESET's behavior
  below its specified supply/drive conditions is not modeled as valid logic.
* [Vishay VO617A](https://www.vishay.com/docs/83430/vo617a.pdf),
  **83430,Rev.2.8,22January2025**: `VO617A-4X016` carries supervised USB health
  across the second isolation path. Pages1/2 pins and option6 ordering;
  pages3/4 DC, CTR and switching conditions; page5 option6>=8mm creepage/clearance.
  CTR minimum160% is at5mA/VCE5V/25°C. The project's80% bench-temperature
  allocation and loss-detection delay are assumptions requiring sensitivity
  checks and physical acceptance;25µs saturated turn-off is typical only.
* [TI SN74LVC1G32](https://www.ti.com/lit/ds/symlink/sn74lvc1g32.pdf),
  **SCES219W,August2026**: `SN74LVC1G32DBVR`; sections4/5.3/5.5–5.8/7.4,
  DBV pins, OR polarity, operating levels, Ioff and switching limits.
* [TI LM66100](https://www.ti.com/lit/ds/symlink/lm66100.pdf),
  **SLVSEZ8A,June2019**: `LM66100DCKR`, SC70-6. Section5 pins are VIN1,GND2,
  CE3,NC4,ST5,VOUT6; CE must not float and unused ST is grounded. Sections6.3–6.6
  and8.3.2 distinguish operation, leakage, CE thresholds and dynamic switching.
  OUT-to-IN leakage is≤2.7µA for−40..85°C and differential≤5.5V; off CE leakage
  is≤610nA. The2µs turn-off and27µs turn-on figures are typical at25°C with
  CL100nF/RL1kΩ, not maxima for the held bulk/load.
  The hold-up model therefore uses a finite blocking-delay allocation and
  sensitivity controls, plus a physical timing gate. CE senses the held rail
  after the feed resistor against **direct raw VIN**; placing the resistor ahead
  of VIN would make limited reverse current hide the CE differential.
* [KEMET Goldmax X7R family](https://content.kemet.com/datasheets/KEM_C1050_GOLDMAX_X7R.pdf),
  **C1050_GOLDMAX_X7R,5August2025**: ordering table and Table1E/page11 support
  `C340C475K5R5TA`,4.7µF/50V/10% X7R. The bulk capacitor's effective minimum
  is a measured assembly requirement including bias/temperature/age; nominal
  capacitance is not the hold-up proof. C28/C29 provide9.4µF nominal, with a
  combined effective minimum4.7µF required. The generated per-part URL was unavailable
  during this review, so this primary family document is the cited evidence.
* U12/U16/U21/U23/U24 use the same `SN74LVC1G17DBVR` source above; Q2 uses the same
  `PMV88ENEA,215` source above. Q2 is driven directly by the supervisor;
  U12 conditions its power-good output for logic. The series power gate does
  not interrupt the direct D1 negative-current return.
  Q3, also `PMV88ENEA,215`, switches the USB-domain optocoupler LED without
  joining grounds.
  The third qualifier U17 reuses LTC6994-1, U18 reuses LVC1G74, and U19/U20 reuse
  LVC2G02/LVC2G08. Their separate pins/programming implement post-fault readiness;
  they are not another MCU or new isolated ground connection.
* [OMRON B3F catalog](https://omronfs.omron.com/en_US/ecb/products/pdf/en-b3f.pdf),
  **Cat.No.A070-E1-08**, footer code1014(0207)(O), retrieved6October2026:
  `B3F-1002-G` gold-contact local re-arm switch. Page2 ordering, page3 ratings,
  page4 terminal/internal-connections **top view** (the numbering convention is
  defined by the header's bottom-view drawing); internal pairs are1+2 and3+4.
  The gold-contact rating is
 100µA–50mA at3–24VDC; bounce5ms maximum. The selected10k/1k arm network uses
  about0.43–0.48mA when pressed. Debounce exceeds specified bounce with margins;
  verify the actual paired legs with an unpowered continuity test before mounting.

[ngspice](https://ngspice.sourceforge.io/) is a build/test dependency only. Its
[release archive](https://sourceforge.net/projects/ngspice/files/ng-spice-rework/42/)
identifies release42; Debian/Ubuntu package `42+ds-3build1` is pinned separately.
ngspice includes code under multiple licenses; do not replace its notices with a
single invented project license. The electrical CI artifact includes the installed
package's complete `/usr/share/doc/ngspice/copyright` as
`build/electrical/licenses/ngspice-copyright`. No ngspice binary is shipped in the
HondaDash application or committed. HondaDash's own license decision remains with
its owner; existing copyright and notice bundles are unchanged.

Protocol evidence and independent reference-derived fixtures retain their existing
provenance in [HONDA_DLC_EVIDENCE](../../docs/HONDA_DLC_EVIDENCE.md). No HondaEcu,
Hondash application code/resources, ROM or manufacturer SPICE models were imported.

The selected revision B RX disposition is documented in [RX_REVIEW](RX_REVIEW.md):
retain the existing parts, narrow functional claims to the nonnegative normal
lab DATA domain, and keep−16V/10s and powered-off protection targets **OPEN**.
Those targets keep their original source resistance/current limit/duration.
Numerical model success is recorded separately and does not close an evidence gap.

The R39/R40 review uses SCES351Y§5.5: each10kΩ guard load can exceed100µA,
so the VCC−0.1V/100µA output row is not used for U23. Its3.8V anchor is specified
at4.5V/−32mA; loaded levels over the held-rail range remain measured acceptance
gates. ISO powered-off output injection is an explicit allocation, not a
manufacturer Ioff guarantee. The healthy field-decay arithmetic does not extend
supervisor behavior to arbitrary forced hard collapse.

Final D4/D5/D6 reuse BAT54,215 and the original Nexperia Table7 source:0.24V at
0.1mA,0.32V at1mA,0.40V at10mA and0.50V at30mA are25°C/≤300µs/duty≤0.02
conditions. None guarantees a10s DC or full-temperature clamp. The model's0.45V
acceptance allocation,2µA leakage and10pF capacitance retain physical gates.
R11=680Ω uses the same MRS25 source/order system. D1's worst reverse allocation
returns to GND_F; Q1/Q2 channel leakage uses separate25°C anchors, with200µA Q1
adverse sensitivity explicitly identified. No new closed vendor model was imported.
