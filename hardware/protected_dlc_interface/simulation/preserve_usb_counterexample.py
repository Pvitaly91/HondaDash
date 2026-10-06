#!/usr/bin/env python3
"""Reproduce frozen pre-hold failure with ngspice42; never count tool errors.

Historical post-restore PASS remains unchanged. A new whole-transient positive
own-leg current criterion must demonstrate the admissible unrequested sink.
No raw trace or engine log is a committed fixture.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import run as electrical

FIXTURE = Path(__file__).resolve().parent / 'fixtures/pre_hold'
MODEL_SHA = '52e3aa131cca41d2890e173c3cfb976b80e4c30115180cfd0ae31180a8bd8eb6'


def require(value, reason):
    if not value:
        raise RuntimeError(reason)


def reproduce(args, out):
    fixture = args.fixture_dir.resolve()
    metadata = json.loads((fixture / 'metadata.json').read_text(encoding='utf-8'))
    hashes = {}
    for name, expected in metadata['files'].items():
        actual = hashlib.sha256((fixture / name).read_bytes()).hexdigest()
        require(actual == expected, name + ': frozen fixture bytes changed')
        hashes[name] = actual
    require(hashes['models.cir'] == MODEL_SHA, 'Wrong pre-hold model revision')
    historical = json.loads((fixture / 'original-summary.json').read_text(encoding='utf-8'))
    historical_gate = json.loads((fixture / 'historical-counterexample.json').read_text(encoding='utf-8'))
    require(historical['model_sha256'] == MODEL_SHA and historical['status'] == 'PASS',
            'Historical post-restore report no longer matches this snapshot')
    require(len(historical['simulations']) == 1, 'Exactly one historical counterexample expected')
    scenario = historical['simulations'][0]['scenario']
    require(scenario['name'] == metadata['scenario_name'], 'Historical scenario mismatch')
    version = subprocess.run([args.ngspice, '--version'], check=True, capture_output=True, text=True)
    require(re.search(r'ngspice-42\s', version.stdout), 'Exactly ngspice42 required')
    folder = out / 'reproduction'
    folder.mkdir(exist_ok=True)
    for name in ['models.cir', 'frontend.cir']:
        shutil.copyfile(fixture / name, folder / name)
    source = (fixture / 'scenario.cir').read_text(encoding='utf-8')
    relocation = metadata['portability_relocation']
    require(source.count(relocation['original_include']) == 1, 'Historical include relocation is not unique')
    runtime = source.replace(relocation['original_include'], relocation['runtime_include'], 1)
    (folder / 'scenario.cir').write_text(runtime, encoding='utf-8', newline='\n')
    stop = float(metadata['transient_end_s'])
    match = re.search(r'(?m)^tran\s+\S+\s+(\S+)', runtime)
    require(match and math.isclose(float(match[1]), stop, rel_tol=0, abs_tol=1e-12), 'Transient duration mismatch')
    electrical.run_spice(folder, args.ngspice)
    trace = electrical.read_trace(folder / 'trace.dat')
    require(len(trace) == 21 and all(len(col) == len(trace[0]) for col in trace), 'Frozen vector layout mismatch')
    require(all(math.isfinite(value) for col in trace for value in col), 'Nonfinite simulation samples')
    require(all(b >= a for a, b in zip(trace[0], trace[0][1:])), 'Nonmonotonic transient time')
    require(trace[0][0] >= 0 and trace[0][-1] >= stop - 1e-9, 'Truncated transient is not an expected failure')
    criterion = metadata['criterion']
    low, high = criterion['raw_usb_range_V']
    candidates, violations = [], []
    for time, raw, field, request, gate, pg, bus, drain in zip(
            trace[0], trace[11], trace[7], trace[3], trace[5], trace[17], trace[1], trace[6]):
        if (low <= raw <= high and field >= criterion['field_min_V']
                and request <= criterion['D3_low_max_V']
                and gate > criterion['gate_on_min_V'] and pg > criterion['gate_on_min_V']):
            current = max((bus-drain)/criterion['own_leg_resistance_ohm'], 0)
            row = {'time_s':time, 'current_A':current, 'raw_usb_V':raw,
                   'field_V':field, 'D3_V':request, 'gate_V':gate, 'pg_gate_V':pg, 'DATA_V':bus}
            candidates.append(row)
            if current > criterion['off_current_limit_A']:
                violations.append(row)
    require(candidates, 'No qualified power-phase samples; fixture no longer demonstrates this experiment')
    peak = max(row['current_A'] for row in candidates)
    margin = criterion['off_current_limit_A'] - peak
    # Actual negative margin + violating samples, never a scenario-name oracle.
    require(margin < 0 and violations, 'Expected physical/logical criterion did not fail')
    begin, end = violations[0]['time_s'], violations[-1]['time_s']
    require(end > begin, 'No finite-duration unrequested conduction interval')
    report = {
        'status':'EXPECTED_DESIGN_FAILURE', 'complete':True,
        'fixture_revision':metadata['fixture_revision'], 'engine':'ngspice42',
        'source_sha256':hashes,
        'runtime_scenario_sha256':hashlib.sha256((folder/'scenario.cir').read_bytes()).hexdigest(),
        'source_relocation':relocation, 'scenario':scenario,
        'historical_post_restore_status':historical['status'],
        'historical_gate_interval_duration_s':historical_gate['interval_duration_s'],
        'criterion':criterion['name'], 'measured_peak_own_current_A':peak,
        'relation':'<=', 'limit_A':criterion['off_current_limit_A'], 'margin_A':margin,
        'criterion_status':'FAIL', 'violating_samples':len(violations),
        'interval_start_s':begin, 'interval_end_s':end, 'interval_duration_s':end-begin,
        'DATA_min_during_violation_V':min(row['DATA_V'] for row in violations),
        'D3_max_during_violation_V':max(row['D3_V'] for row in violations),
        'transient_end_s':trace[0][-1],
        'scope':'Admissible worst undefined ISO input-supply model; an engineering counterexample, not measured hardware',
        'physical_status':'NOT VERIFIED', 'protection_status':'OPEN'
    }
    for name in ['counterexample.json','summary.json']:
        (out/name).write_text(json.dumps(report, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    shutil.copyfile(fixture / 'original-summary.json', out / 'original-post-restore-summary.json')
    shutil.copyfile(fixture / 'historical-counterexample.json', out / 'historical-gate-counterexample.json')
    if not args.keep_traces:
        (folder / 'trace.dat').unlink()
    print(report['status'], f"own current {peak:.9g}A > {criterion['off_current_limit_A']:.9g}A; interval {(end-begin)*1e6:.6g}us")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture-dir', type=Path, default=FIXTURE)
    ap.add_argument('--ngspice', default='ngspice')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--keep-traces', action='store_true')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out/'summary.json').write_text(json.dumps({'status':'RUNNING','complete':False})+'\n',encoding='utf-8')
    try:
        reproduce(args, out)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        (out/'summary.json').write_text(json.dumps({'status':'ERROR','complete':False,'error':str(error)})+'\n',encoding='utf-8')
        print('Counterexample reproduction ERROR; never an expected rejection:',error,file=sys.stderr)
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
