# Frozen pre-hold power-phase counterexample

These are the exact original HondaDash model/interconnect/deck bytes captured
before the logic-side hold island was added. `metadata.json` pins all source and
historical report hashes. The models are original project behavioral models,
not manufacturer models. No raw trace, engine log, binary or physical capture is
committed here.

The original post-restore report said PASS because it checked the later restored
state and only the field-side undefined supply. The independent gate review found
an admissible unrequested LOW pulse of about32.7us while D3 remained LOW and raw
USB traversed the ISO7721F1.7–2.25V undefined input-power region. This does not
predict the exact physical behaviour in that undefined range. It disproves a
guarantee of inhibition based on those incomplete criteria.

`preserve_usb_counterexample.py` reruns this circuit with ngspice42, checks finite
samples/full duration, and requires an actual positive own-leg current violation:
`(DATA−DRAIN)/44 >0.3mA`, with both gates above1.3V, healthy field power, LOW D3 and
undefined raw USB. No external peer or transistor short is in this fixture. The new
report is **EXPECTED_DESIGN_FAILURE**, with a negative current margin. Simulator,
parse and missing-file errors never count as that expected result.

The frozen deck retains its original absolute model include. For portability the
helper replaces only that single, hash-checked include in a generated runtime copy
with `.include "models.cir"`; the original source remains unchanged. The historical
JSON files retain their original CRLF bytes via the local `.gitattributes`. Their
earlier PASS is preserved, not rewritten as a later result.

```sh
python3 hardware/protected_dlc_interface/simulation/preserve_usb_counterexample.py \
  --ngspice ngspice --out build/electrical/pre-hold-counterexample
```

Exit0 means the frozen design failure was reproduced numerically. It does not mean
the old design passes. Final revision-B sources and power criteria are tested
separately. All physical hardware statuses remain **NOT VERIFIED**.
