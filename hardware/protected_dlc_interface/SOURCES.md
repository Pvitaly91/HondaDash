# Source registry and notices

Evidence reviewed6 October2026. Manufacturer documents are engineering evidence,
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
  Table5-1 DW pinout;5V switching and F-default behavior. The IC's isolation rating
  is not a certified rating of an unbuilt PCB and its attached instruments.
* [TI SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf), SCES351Y,
  October2025: DBV pins, DC output drive, Ioff and timing. Field-only gate buffer;
  DATA is not connected to this part's input.
* [TI LM393B](https://www.ti.com/lit/ds/symlink/lm393.pdf), SLCS005AH,April2025:
  sections5.1/5.5/5.7 and7.2.2.1. Do not substitute legacy LM393 or treat typical
  switching figures as maxima. Off-state input injection is an explicit model
  assumption requiring measurement; independent input voltage tolerance alone
  does not prove absence of every back-power path.
* [TI TPS709](https://www.ti.com/lit/ds/symlink/tps709.pdf), SBVS186H,July2021:
  TPS70950DBVR pins, EN voltage, dropout, stability/output-capacitance requirements.
  The similarly named TPS709A/B have different DBV pins and are not substitutions.
* [Nexperia PMV88ENEA](https://assets.nexperia.com/documents/data-sheet/PMV88ENEA.pdf),
  3August2026: pins,60V rating, Rds at4.5V, gate charge, thermal conditions, DC SOA
  plot and typical capacitance. `.32R`, Cgd50pF and temperature allocations are
  project assumptions; they are not mislabeled guaranteed all-temperature limits.
* [Nexperia BAT54](https://assets.nexperia.com/documents/data-sheet/BAT54.pdf),
  1July2022, Tables2/5/7: pins, forward/leakage/capacitance conditions.
* [Vishay SS16-M3/IA](https://www.vishay.com/docs/98653/ss16hm3_bia_ss16-m3ia.pdf),
  document98653,12March2025, pages1–3: cathode marking, ratings, voltage/leakage and
  typical thermal/capacitance data. It is a rectifier, not an automotive TVS.
* [Vishay MRS25](https://www.vishay.com/docs/28724/mrs16m25.pdf),28724,7March2016:
 1%,0.6W at70°C,50ppm/K; order codes in the BOM follow the manufacturer's table.
  The1% calculation corners omit the additional small TCR drift over15–35°C;
  threshold acceptance therefore uses guard room and measured limits, not a claim
  of exhaustive all-temperature resistor tolerance proof.
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
