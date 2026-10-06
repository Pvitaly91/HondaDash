#!/usr/bin/env python3
"""Merge only complete disjoint CI shards from the current source revision.

Publish the completed summary last. Missing evidence and simulator errors
never count as expected rejections. Physical hardware remains NOT VERIFIED.
"""
import argparse
import csv
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess

import run as electrical
import failsafe_checks


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def finite(value):
    if isinstance(value, float):
        require(math.isfinite(value), 'Non-finite report value')
    elif isinstance(value, dict):
        for child in value.values():
            finite(child)
    elif isinstance(value, list):
        for child in value:
            finite(child)


def close(a, b):
    return math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-12)


def validate_results(reports, expected, model, revision, count, suite):
    """Pure coverage validator with authoritative source facts supplied by caller."""
    require(count > 0 and len(reports) == count, 'Missing shard report')
    indices = [r.get('shard', {}).get('index') for r in reports]
    require(len(set(indices)) == count and set(indices) == set(range(count)) and
            all(r.get('shard', {}).get('count') == count for r in reports),
            'Missing or duplicate shard index')
    names = [c['name'] for c in expected]
    require(len(names) == len(set(names)), 'Duplicate authoritative scenario name')
    ordered = {}
    tolerance = 1e-12 if suite == 'electrical' else 1e-9
    for report in reports:
        index = report['shard']['index']
        finite(report)
        require(report.get('complete') is True and report.get('status') == 'PASS',
                'Incomplete/failed shard is never a completed result')
        require(report.get('model_sha256') == model, 'Stale model shard')
        require(report.get('git_sha') == revision, 'Stale source revision shard')
        require(report.get('physical_status') == 'NOT VERIFIED', 'Invalid physical status')
        simulations = report['simulations']
        require([s['scenario']['name'] for s in simulations] == names[index::count],
                'Missing, duplicate, extra or misplaced scenario in shard '+str(index))
        for result, case in zip(simulations, expected[index::count]):
            actual = result['scenario']
            target = case if suite == 'electrical' else failsafe_checks.shifted(case)
            require(all(actual.get(k) == v for k, v in target.items()),
                    'Scenario parameters differ from the current matrix: '+case['name'])
            allowed = {'simulated_duration_s', 'fixture_current_limit_a', 'source_model'} if suite == 'electrical' else {'trace_save_start_s'}
            require(set(actual) <= set(target) | allowed, 'Unexpected scenario parameter: '+case['name'])
            wanted = 'PASS' if case['expected'] == 'PASS' else 'EXPECTED_REJECTION'
            require(result.get('status') == wanted and result.get('model_status', wanted) == wanted,
                    'Scenario result does not satisfy its expected outcome: '+case['name'])
            require(result.get('physical_status') == 'NOT VERIFIED', 'Invalid scenario physical status')
            checks = result['checks']
            require(bool(checks) and len({c['check'] for c in checks}) == len(checks),
                    'Missing or duplicate numerical check: '+case['name'])
            failed = []
            for check in checks:
                require(check['relation'] in ('<=', '>='), 'Unknown numerical relation')
                value, limit = check['value'], check['limit']
                require(type(value) in (int, float) and type(limit) in (int, float), 'Missing numerical criterion')
                margin = limit-value if check['relation'] == '<=' else value-limit
                status = 'PASS' if margin >= -tolerance else 'FAIL'
                require(close(check['margin'], margin) and check['status'] == status,
                        'Numerical check and reported outcome disagree: '+case['name'])
                if status == 'FAIL':
                    failed.append(check['check'])
            require(result.get('failed') == failed and bool(failed) == (wanted == 'EXPECTED_REJECTION'),
                    'Negative control lacks a failed numerical criterion: '+case['name'])
            ordered[case['name']] = result
        if suite == 'electrical':
            require(report['engine'] == 'ngspice 42' and report['topology']['status'] == 'PASS',
                    'Missing pinned simulator or passing topology')
            require(report['calculations']['rows'] and all(c['status'] == 'PASS' for c in report['calculations']['rows']),
                    'Failed/missing arithmetic criteria')
            require(report['topology'] == reports[0]['topology'] and report['calculations'] == reports[0]['calculations'],
                    'Mixed topology or calculation evidence')
        else:
            require(report['structural'] == failsafe_checks.structural_controls(),
                    'Structural controls differ from the current source')
    return [ordered[name] for name in names]


def negative_controls(reports, expected, model, revision, count, suite):
    """Exercise rejection on copies of genuine passing evidence; never fake SPICE."""
    def swap_cases(parts):
        parts[0]['simulations'][0], parts[1]['simulations'][0] = parts[1]['simulations'][0], parts[0]['simulations'][0]

    def change_parameter(parts):
        scenario = parts[0]['simulations'][0]['scenario']
        key = 'rpu' if suite == 'electrical' else 'scale'
        scenario[key] += 1

    def change_negative(parts):
        result = next(s for r in parts for s in r['simulations'] if s['scenario']['expected'] == 'REJECT')
        result['failed'] = []

    controls = [
        ('missing_shard', lambda p: p.pop()),
        ('stale_model', lambda p: p[0].update(model_sha256='0'*64)),
        ('stale_source', lambda p: p[0].update(git_sha='0'*40)),
        ('incomplete_shard', lambda p: p[0].update(complete=False)),
        ('missing_scenario', lambda p: p[0]['simulations'].pop()),
        ('duplicate_scenario', lambda p: p[0]['simulations'].append(copy.deepcopy(p[0]['simulations'][0]))),
        ('changed_parameter', change_parameter),
        ('top_PASS_hides_failed_scenario', lambda p: p[0]['simulations'][0].update(status='FAIL')),
        ('nonfinite_check', lambda p: p[0]['simulations'][0]['checks'][0].update(value=float('nan'))),
        ('inconsistent_check_margin', lambda p: p[0]['simulations'][0]['checks'][0].update(margin=999)),
        ('negative_without_numerical_failure', change_negative)]
    if count > 1:
        controls.extend([
            ('duplicate_shard_index', lambda p: p[1]['shard'].update(index=p[0]['shard']['index'])),
            ('misplaced_scenario', swap_cases)])
    results = []
    for name, mutate in controls:
        parts = copy.deepcopy(reports)
        mutate(parts)
        try:
            validate_results(parts, expected, model, revision, count, suite)
        except RuntimeError as error:
            results.append({'control': name, 'status': 'EXPECTED_REJECTION', 'reason': str(error)})
        else:
            raise RuntimeError('Merge rejection control unexpectedly accepted: '+name)
    return results


def csv_rows(path, header, empty=False):
    require(path.is_file(), 'Missing CSV evidence: '+str(path))
    with path.open(encoding='utf-8', newline='') as stream:
        rows = list(csv.reader(stream))
    if empty and not rows:
        return []
    require(bool(rows) and rows[0] == header and all(len(r) == len(header) for r in rows[1:]),
            'Missing/invalid CSV header or row: '+str(path))
    return rows[1:]


def write_csv(path, header, rows):
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)


def validate_check_csv(source, simulations, suite):
    fields = ['value', 'relation', 'limit', 'unit', 'margin', 'status'] if suite == 'electrical' else ['value', 'relation', 'limit', 'margin', 'unit', 'status']
    header = ['scenario', 'expected', 'check']+fields
    wanted = [[s['scenario']['name'], s['scenario']['expected'], c['check']]+[str(c[k]) for k in fields]
              for s in simulations for c in s['checks']]
    require(csv_rows(source/'checks.csv', header) == wanted, 'CSV numerical evidence differs from its summary')


def validate_traces(source, simulations):
    folder = source/'driver-traces'
    cases = [s['scenario'] for s in simulations if s['scenario']['kind'] == 'bytes' and s['status'] == 'PASS']
    names = [c['name'] for c in cases]
    headers = {
        'manifest.csv': ['name', 'transitions', 'frames', 'expected'],
        'corners.csv': ['name', 'tx_fall_us', 'tx_rise_us', 'rx_fall_us', 'rx_rise_us', 'echo_fall_us', 'echo_rise_us'],
        'edge-paths.csv': ['case', 'polarity', 'd3_s', 'data_s', 'd8_s', 'd3_data_s', 'data_d8_s', 'echo_s']}
    tables = {key: csv_rows(folder/key, header, empty=not cases) for key, header in headers.items()}
    manifest = [[name, name+'-transitions.csv', name+'-frames.csv', 'PASS'] for name in names]
    require(tables['manifest.csv'] == manifest and [r[0] for r in tables['corners.csv']] == names,
            'Missing, duplicate or stale driver trace/calibration case')
    required_files = set(headers) | {r[k] for r in manifest for k in (1, 2)}
    require({p.name for p in folder.iterdir()} == required_files, 'Missing or extra driver trace file')
    paths = tables['edge-paths.csv']
    require(set(r[0] for r in paths) == set(names), 'Missing or extra edge-path case')
    for case, corner in zip(cases, tables['corners.csv']):
        name = case['name']
        frames, _, _ = electrical.byte_stream(case['full'], case['period'], case.get('all_values', False), case.get('byte_block', 0))
        actual = csv_rows(folder/(name+'-frames.csv'), ['direction', 'start_s', 'period_s', 'value'])
        require(len(actual) == len(frames) and all(r[0] == f['direction'] and close(float(r[1]), f['t']) and
                close(float(r[2]), f['period']) and int(r[3]) == f['value'] for r, f in zip(actual, frames)),
                'Driver frame stimulus differs from the current source: '+name)
        events = csv_rows(folder/(name+'-transitions.csv'), ['time_s', 'level', 'valid'])
        require(len(events) > 1, 'Missing digital transitions: '+name)
        times = [float(r[0]) for r in events]
        require(all(math.isfinite(t) and t >= 0 for t in times) and all(a <= b for a, b in zip(times, times[1:])) and
                all(r[1] in ('0', '1') and r[2] in ('0', '1') for r in events), 'Invalid digital transitions: '+name)
        expected_edges = []
        for frame in frames:
            if frame['direction'] != 'tx':
                continue
            last = 1
            for k, bit in enumerate(frame['bits']):
                if bit != last:
                    expected_edges.append(('rise' if bit else 'fall', frame['t']+k*frame['period']))
                last = bit
        edges = [r for r in paths if r[0] == name]
        require(len(edges) == len(expected_edges), 'Missing or duplicated calibrated edge: '+name)
        for row, (polarity, start) in zip(edges, expected_edges):
            d3, data, d8, tx, rx, echo = map(float, row[2:])
            require(row[1] == polarity and close(d3, start) and all(math.isfinite(v) for v in (d3, data, d8, tx, rx, echo)) and
                    0 <= tx and 0 <= rx and close(data-d3, tx) and close(d8-data, rx) and close(tx+rx, echo),
                    'Invalid causal calibrated edge: '+name)
        numbers = list(map(float, corner[1:]))
        require(all(math.isfinite(v) and v >= 0 for v in numbers), 'Invalid frontend calibration: '+name)
        for polarity, offsets in [('fall', (0, 2, 4)), ('rise', (1, 3, 5))]:
            row = max((r for r in edges if r[1] == polarity), key=lambda r: float(r[7]))
            require(all(close(numbers[k], float(row[j])*1e6) for k, j in zip(offsets, (5, 6, 7))),
                    'Calibration is not its checked causal maximum: '+name)
    return headers, tables


def merge(inputs, out, suite, count=4, require_metadata=False):
    out.mkdir(parents=True, exist_ok=True)
    pending = {'status': 'RUNNING', 'complete': False, 'physical_status': 'NOT VERIFIED', 'protection_status': 'OPEN'}
    (out/'summary.json').write_text(json.dumps(pending)+'\n', encoding='utf-8')
    (out/'SUMMARY.md').write_text('# Incomplete shard merge\n\nNo completed model result. Physical hardware: NOT VERIFIED.\n', encoding='utf-8')
    try:
        require(len(set(inputs)) == count, 'Missing or duplicate shard input directory')
        reports = [json.loads((p/'summary.json').read_text(encoding='utf-8')) for p in inputs]
        model = hashlib.sha256((electrical.ROOT/'simulation/models.cir').read_bytes()).hexdigest()
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=electrical.ROOT, text=True).strip()
        expected = electrical.cases() if suite == 'electrical' else failsafe_checks.cases()
        simulations = validate_results(reports, expected, model, revision, count, suite)
        controls = negative_controls(reports, expected, model, revision, count, suite)
        order = sorted(zip(inputs, reports), key=lambda item: item[1]['shard']['index'])
        trace_tables = None
        modules = set()
        for source, report in order:
            if require_metadata:
                metadata = json.loads((source.parent/('metadata-'+source.name+'.json')).read_text(encoding='utf-8'))
                require(metadata.get('git_sha') == revision, 'Missing/stale artifact metadata')
            validate_check_csv(source, report['simulations'], suite)
            names = {s['scenario']['name'] for s in report['simulations']}
            require({p.name for p in source.iterdir() if p.is_dir()} == names | ({'driver-traces'} if suite == 'electrical' else set()),
                    'Missing or extra scenario evidence directory')
            for name in names:
                folder = source/name
                for file in ('scenario.cir', 'frontend.cir', 'engine.log', 'code-module.json'):
                    require((folder/file).is_file() and (folder/file).stat().st_size, 'Missing simulator evidence: '+str(folder/file))
                module = json.loads((folder/'code-module.json').read_text(encoding='utf-8'))
                require(module.get('engine') == 'ngspice42' and module.get('buffer_size') == 8192 and
                        re.fullmatch('[0-9a-f]{64}', module.get('sha256', '')), 'Invalid pinned code module evidence')
                modules.add(module['sha256'])
                require(not re.search(r'timestep too small|fatal error|doAnalyses:|Error on line',
                                      (folder/'engine.log').read_text(encoding='utf-8', errors='replace'), re.I),
                        'Simulator infrastructure error in shard evidence')
            if suite == 'electrical':
                trace_headers, tables = validate_traces(source, report['simulations'])
                if trace_tables is None:
                    trace_tables = {key: [] for key in tables}
                for key in tables:
                    trace_tables[key].extend(tables[key])
        require(len(modules) == 1, 'Mixed simulator code modules')
        for source, report in order:
            for result in report['simulations']:
                name = result['scenario']['name']
                require(not (out/name).exists(), 'Overlapping case output directory')
                shutil.copytree(source/name, out/name)
            if suite == 'electrical':
                traces = out/'driver-traces'
                traces.mkdir(exist_ok=True)
                for path in (source/'driver-traces').glob('*'):
                    if path.name not in trace_headers:
                        require(not (traces/path.name).exists(), 'Overlapping trace output file')
                        shutil.copyfile(path, traces/path.name)
        report = dict(order[0][1])
        report.update(simulations=simulations, shard={'merged': count, 'exact_case_coverage': True},
                      complete=False, status='RUNNING', protection_status='OPEN',
                      merge_controls=controls)
        if suite == 'electrical':
            for key, rows in trace_tables.items():
                write_csv(out/'driver-traces'/key, trace_headers[key], rows)
            require((order[0][0]/'engine.txt').is_file(), 'Missing pinned engine version evidence')
            shutil.copyfile(order[0][0]/'engine.txt', out/'engine.txt')
            electrical.write_reports(out, report)
        else:
            fields = ['value', 'relation', 'limit', 'margin', 'unit', 'status']
            rows = [[s['scenario']['name'], s['scenario']['expected'], c['check']]+[str(c[k]) for k in fields]
                    for s in simulations for c in s['checks']]
            write_csv(out/'checks.csv', ['scenario', 'expected', 'check']+fields, rows)
        report.update(status='PASS', complete=True)
        if suite == 'electrical':
            # write_reports writes JSON first, so use it only with RUNNING.
            # All CSVs are now present; publish the completed JSON atomically last.
            text = (out/'SUMMARY.md').read_text(encoding='utf-8')
            (out/'SUMMARY.md').write_text(text.replace('Result: **RUNNING**.', 'Result: **PASS**.'), encoding='utf-8')
        else:
            (out/'SUMMARY.md').write_text('# Failsafe shard merge\n\nModel criteria: PASS. Physical hardware: NOT VERIFIED. Protection: OPEN.\n', encoding='utf-8')
        pending_path = out/'summary.pending.json'
        pending_path.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n', encoding='utf-8')
        pending_path.replace(out/'summary.json')
        print(suite, 'PASS', len(simulations), 'exactly covered scenarios')
    except Exception as error:
        pending.update(status='ERROR', error=str(error))
        (out/'summary.json').write_text(json.dumps(pending, indent=2)+'\n', encoding='utf-8')
        (out/'SUMMARY.md').write_text('# Incomplete shard merge\n\nERROR; no completed model result. Physical hardware: NOT VERIFIED.\n', encoding='utf-8')
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--suite', choices=['electrical', 'failsafe'], required=True)
    parser.add_argument('--inputs', type=Path, nargs='+', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--expected-shard-count', type=int, default=4)
    parser.add_argument('--require-metadata', action='store_true')
    args = parser.parse_args()
    merge([p.resolve() for p in args.inputs], args.out.resolve(), args.suite,
          args.expected_shard_count, args.require_metadata)


if __name__ == '__main__':
    main()
