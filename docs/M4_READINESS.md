# M4 — readiness for one specifically identified ECU

M3b provides a protected-interface **candidate** and isolated lab test package.
It does not enable a real ECU in software. M3a physical bring-up, protected-board
measurements and vehicle-interface qualification are separate gates; currently
all hardware gates are **NOT VERIFIED**. Do not assume the owner's ECU is P07,
P1G or any other family, and do not infer pinout from a connector's shape.

| Required evidence | Current value / next evidence |
|---|---|
| Full ECU part number, hardware/board revision, ROM/software identifier if available | UNKNOWN; legible labels/photos and appropriate service documentation |
| Supply pins, power sequencing, current limits, all grounds | UNKNOWN; verified source specific to that ECU, then measured isolated bench setup |
| Diagnostic signal contact and reference ground | UNKNOWN; manufacturer/service evidence and continuity; no guessed pin number |
| Idle voltage, source resistance, sink thresholds/current, capacitance, polarity, ground offsets, transient exposure | UNKNOWN; measurements and evidence matching a reviewed adapter envelope |
| Protected-interface assembly revision and independent review | NOT VERIFIED; BOM/pin/PCB/ground inspection |
| Electrical validation report including faults and post-fault leakage | NOT VERIFIED; actual captures under [VALIDATION](../hardware/protected_dlc_interface/VALIDATION.md) |
| M3a physical USB/two-Nano A/B acceptance | NOT VERIFIED; actual explicit-port run through the assembled circuit |
| Specific ECU command support | UNKNOWN; current whitelist is reference evidence, not proof for this ECU |
| First read-only experiment | Future explicit authorization after the above gates; one proven supported request and controlled stop, no write/erase/DTC clear |
| Independent parameter comparison | Select a compatible trusted diagnostic/reference instrument; record units, timestamps and calibration limitations |
| Stop policy / late bytes | One outstanding request; stop on timeout/checksum/framing/unexpected identity/electrical limit; retain late raw RX without attaching it to a new read |

An initial real-ECU plan must name the ECU, verified pin/power diagram, adapter
revision, evidence, exact read-only bytes, timing, observations, independent
comparison and stop limits. No automatic retry, reset, wake loop or recovery after
an ambiguous response. NEW/ARM/QUIESCE/generation belongs to our external test
responder; parser reset or bridge ABORT does not flush a real ECU's pending bytes.
Do not change the200ms observation guard merely to increase apparent frequency.

Full reconstruction of the ECU firmware is **not** a prerequisite for validating
its documented diagnostic interface. Conversely, a protocol checksum, an inferred
memory address or a successfully built adapter cannot establish calibration or
vehicle compatibility. Real ECU/vehicle work needs a subsequent explicit scope;
this M3b branch contains no such session or live runtime mode.
