# M3b.1 — protected interface, revision B

**Prepared design/model candidate; hardware NOT VERIFIED. No vehicle or real ECU
connection.** Revision B adds a Nano-independent TX cutoff, a persistent arm
latch, supervised startup and local re-arm. It integrates the analog frontend
with the actual production `hd_onewire::Driver`; it adds no live backend,
identity, raw command, emulator or fifth firmware.

The classic Nano remains USB-powered. **LM66100DCKR** and a local bulk-capacitor
island hold the isolator/input-buffer supply while raw USB loss clears permission.
**ISO7721FDWR** isolates D3/D8 signals;
**VO617A-4X016** separately carries supervised USB health. Field power is the
independent **Energizer522 / TPS70950DBVR** battery supply. **LTC6994IS6-1** times
continuous TX, **SN74LVC1G74** retains arm state, and **LTC6994IS6-2** debounces the
local button. A third LTC6994-1 and readiness latch require a qualified button
release before a fresh press; a button held through power-up cannot arm.
**TPS3808G01** supervisors monitor both rails. A second **PMV88ENEA**
disconnects the sink return while field power is not ready. LM393B/BAT54 RX and
the two22Ω AC10 limiting resistors are retained.

D3 HIGH requests sink; it is accepted only while armed and power is healthy.
Continuous HIGH triggers at **1.961698–2.136040ms**, with another **1µs gate-release
allocation**. Own sink release is allocated by2.137040ms, below the project5ms
limit. It stays locked after timeout or an unsafe power cycle. A fresh debounced
local button action follows at least19.528778ms of continuous released button,
inactive TX and healthy power. Existing USB recovery commands
do not clear this latch, and firmware has no new electrical latch feedback.
Releasing our sink cannot force DATA HIGH if another device/short holds it LOW.

| Revision A → B | Result and remaining limit |
|---|---|
| Unbounded D3 HIGH → hardware qualifier/latch/gating | Model releases own sink and retains lock; physical cutoff NOT VERIFIED |
| F-default-only startup → both rail supervisors plus isolated USB-health path | Power restoration alone cannot arm; opto CTR/timing allocations need measurement |
| Fast USB fall with undefined ISO supply → held logic island and Ioff buffers | Pre-hold model exposed an unrequested32.73µs sink pulse with D3 LOW; finite hold/blocking and ordered inhibit checks address that case |
| Q1 direct ground → supervised Q2 return disconnect | Covers the declared power-state model; negative D1/body return persists |
| Short-pulse BAT54 evidence extrapolated to DC → explicit RX review |−16V/10s and off-state protection remain **OPEN**, excluded from protection PASS |
| Formula-only byte checks → production driver integration | SPICE traces and closed feedback path are distinct; physical ISR/edges remain unverified |
| D3-anchored stop →17-tick frontend settling before full own stop | Nominal frame1048.5µs; bit104µs,40-tick lateness and200ms observation unchanged |

Revision A remains reproducible at
`b2cf4ea12a783d4eb81736383f51790b361d9b04`. `simulation/baseline.py` exports that
commit without changing the checkout, runs its original checks, and separately
reproduces continuous HIGH holding its sink. Historical A results are not relabeled
as B. Generated baseline/after reports belong in ignored `build/`.
The final design source has132 BOM/pad rows and49 conditional calculation checks;
the completed model/integration counts belong to their generated reports.

The selected architecture remains isolated signals plus independent battery.
The nonisolated USB-powered alternative would join field and PC grounds and is
not selected. There is no adapter DATA pullup, cross-barrier power converter,
charger or vehicle supply input. `VEHICLE_POWER_NC` is not routed. A second USB on
the same PC, shared chargers or scope earth can bypass the barrier externally.

Read [requirements](REQUIREMENTS.md), [schematic](SCHEMATIC.md),
[SVG](schematic.svg), [BOM](BOM.csv) and [netlist](netlist.json), then
[failsafe](FAILSAFE.md), [RX review](RX_REVIEW.md),
[calculations](calculations/README.md), [model limits](simulation/MODEL_LIMITS.md),
[driver integration](../../docs/PROTECTED_INTERFACE_DRIVER_INTEGRATION.md),
[fault matrix](FAULT_MATRIX.md) and [physical checklist](VALIDATION.md).
[M4 prerequisites](../../docs/M4_READINESS.md) remain specific to an identified ECU.
The source is a pin/net design, **not PCB layout, Gerbers or a manufacturing order**.

The conditional normal domain remains DATA source4.75–5.25V, source1–2.2kΩ,
total DATA200–500pF, raw USB/field rails4.75–5.25V, held logic rail≥4.5V in normal
use and bench15–35°C. These are project
ranges, not Honda specifications. Supervisor thresholds define a separate safety
floor; power-good does not certify the normal4.75V timing domain. Comparator3µs,
capacitance, leakage, Q2 resistance and opto-temperature limits are explicit
allocations with sensitivity sweeps and physical acceptance gates.

Two4.7µF X7R bulk parts provide nominal9.4µF, with **combined effective minimum
4.7µF** required at actual bias/temperature/age. The hold calculation includes
reverse charge for the **15µs blocking allocation**,10mA total held load and a
250µs USB-health window; it retains about3.184V versus the2.25V ISO boundary.
Blocking/startup delays are project gates, not invented manufacturer maxima.
Raw-powered RX and held-powered TX buffers bound leakage; the raw4.7kΩ bleed
does not establish zero back-power. Off-state protection remains OPEN.

With Python3.12 and **ngspice42** (`42+ds-3build1` on Ubuntu24.04):

```sh
python3 hardware/protected_dlc_interface/design.py --check
python3 hardware/protected_dlc_interface/simulation/baseline.py --out build/electrical-baseline
python3 hardware/protected_dlc_interface/simulation/run.py --calculations-only --out build/electrical
python3 hardware/protected_dlc_interface/simulation/run.py --out build/electrical
python3 tests/protected_frontend/run.py --out build/electrical/driver --trace-dir build/electrical/driver-traces --calibration build/electrical/driver-traces/corners.csv
python3 hardware/protected_dlc_interface/simulation/run.py --bad-only --out build/electrical-negative
```

The last command must exit2 for rejected numerical controls; a simulator failure,
missing input or parse error is an infrastructure failure. The separate
[electrical workflow](../../.github/workflows/electrical.yml) packages design,
sources/notices, baseline/after reports and production-driver results. Application
CMake/runtime needs no SPICE/Python/compiler dependency. Normal software/AVR
regressions remain in [TESTING](../../docs/TESTING.md); workflow existence is not
evidence of a run. No vendor model or PDF is redistributed.

| Evidence class | Status / record |
|---|---|
| Software regression | Separate actual-run record in TESTING; all four firmware variants retained |
| Failsafe design calculations | Conditional bounds in generated calculations/report |
| Electrical model | Numerical checks; **protection status OPEN** for negative DC/off-state |
| Production-driver integration | Generated baseline-A/revision-B reports and source/compiler provenance |
| Physical TX cutoff | **NOT VERIFIED** |
| Physical exchange/fronts | **NOT VERIFIED** |
| Isolation | **NOT VERIFIED**; same-PC fixture cannot establish it |
| Automotive qualification | **NOT VERIFIED / OUT OF SCOPE** |
| Real ECU | **NOT VERIFIED**, no live session permitted |

The package is ready for the ordered electronics/fixture review and physical
measurements in VALIDATION. It is not a qualified automotive adapter.
Provenance and license notices are in [SOURCES](SOURCES.md).

R39/R40 each10kΩ terminate TX_L_ISO/RX_L_ISO. The20µA aggregate off-node
allocation gives≤0.2021V; bounded Ioff does not make a floating node safe. Healthy
guard loading is included in held draw≤10mA. Field release is bounded only for
Ceff≥2.2µF/load≤20mA and ramps≥100µs. Arbitrary forced rail collapse or violated
bounds is OPEN, rather than a general supervisor safety claim.
