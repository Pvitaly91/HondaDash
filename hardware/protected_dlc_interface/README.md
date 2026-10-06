# M3b — protected electrical-interface candidate, revision A

**Design and simulation package only. Hardware NOT VERIFIED. Do not connect to a
vehicle or a real ECU.** The software and the four existing firmwares remain M3a;
this directory adds no live backend, identity, firmware, transport or command.

The selected circuit uses a USB-powered classic Nano on one side of an
**ISO7721FDWR** and an independent **Energizer 522 battery / TPS70950DBVR** supply
on the field side. D3 HIGH enables a protected open-drain sink; D8/ICP1 sees a
noninverted receive signal. The field circuit uses **SN74LVC1G17DBVR**, **PMV88ENEA**,
two **22 ohm AC10** resistors, **SS16-M3/IA**, and **LM393BIDR** with a high-impedance
Schmitt network and a **BAT54** negative-input clamp. There is no pullup from the
adapter to ECU_DATA. The small RX feedback current is explicitly included in
loading/backfeed tests. VEHICLE_POWER_NC has no trace and is never a supply input.

| Candidate | Benefits | Limits / decision |
|---|---|---|
| Protected nonisolated sink + comparator, USB supply | Fewer parts; low delay | ECU_GND joins PC ground. RX/TX resistors do not protect the PC against a ground fault. Rejected for this revision. |
| Isolated signals + independent field battery | No intended DC path from field ground/power to USB; predictable 5V comparator supply | More parts, battery checks, two ground domains, added delay and layout constraints. **Selected**; system insulation is still untested. |

Signal isolation alone with a shared 5V rail is not this design. A same-PC second
USB cable or an earth-referenced scope can bypass the selected barrier externally.
No high-voltage insulation qualification or automotive compliance is claimed.

Read in this order:

1. [Requirements and evidence](REQUIREMENTS.md), including unknown ECU properties.
2. [Pin-level circuit](SCHEMATIC.md), [SVG](schematic.svg), [BOM](BOM.csv),
   [machine netlist](netlist.json). This is a labeled-net schematic, not PCB artwork.
3. [Calculations](calculations/README.md), [model limits](simulation/MODEL_LIMITS.md).
4. [Fault matrix](FAULT_MATRIX.md), then [physical validation](VALIDATION.md).
5. [Specific ECU / M4 gates](../../docs/M4_READINESS.md).

The conditional design envelope is 4.75–5.25V source, 1–2.2k ohm source resistance,
total DATA capacitance 200–500pF, and 15–35°C laboratory ambient. These are **project
test ranges**, not Honda specifications. Added RX leakage, comparator delay,
capacitances and off-state behavior have mandatory measurement gates. The model
and arithmetic evaluate those declared bounds; neither establishes them physically.

Run with Python 3.12 and **ngspice 42** (Ubuntu 24.04 package `42+ds-3build1`):

```sh
python3 hardware/protected_dlc_interface/design.py --check
python3 hardware/protected_dlc_interface/simulation/run.py --calculations-only
python3 hardware/protected_dlc_interface/simulation/run.py --out build/electrical
# Deliberately invalid circuits must exit 2, with failed numerical checks:
python3 hardware/protected_dlc_interface/simulation/run.py --bad-only --out build/electrical-negative
```

The separate [electrical workflow](../../.github/workflows/electrical.yml) installs
the test tool and publishes `HondaDash-protected-dlc-interface`. Normal application
CMake/builds do not discover or require SPICE. Reports include input parameters,
extrema, limits, margins and explicit expected rejections. Generated reports,
netlists for sweeps and simulator logs belong in ignored `build/`, not Git.
`design.py` generates the committed BOM/SVG/netlist; regenerate only after reviewing
an intentional circuit change. No manufacturer SPICE model is redistributed.
Numeric resistor/capacitor values in the pin map and BOM are ohms/farads respectively.

| Evidence class | Status |
|---|---|
| Software regression | Reported separately in [TESTING](../../docs/TESTING.md); unchanged application/firmware |
| Design calculations | Reproducible conditional bounds; generated `calculations.csv` |
| SPICE / model checks | Generated `summary.json`, `checks.csv`; behavioral models |
| M3a physical bring-up | **NOT VERIFIED** |
| Protected-interface measurements | **NOT VERIFIED** |
| Vehicle-interface qualification | **NOT VERIFIED**; automotive transients/ESD outside this revision |
| Real ECU | **NOT VERIFIED**, no live session permitted |

The package supports a reviewable design candidate. It does not establish a ready
automotive adapter. Provenance, third-party notices and simulator licensing are in
[SOURCES.md](SOURCES.md).
