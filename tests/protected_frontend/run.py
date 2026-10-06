#!/usr/bin/env python3
"""Build real production Driver twice: pinned old anchor and current revision.

Electrical build tool only. Python and a host compiler are not app dependencies.
No captures are invented, no serial port is opened, and no firmware is uploaded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASELINE = 'b2cf4ea12a783d4eb81736383f51790b361d9b04'


def command(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cxx', default='g++')
    ap.add_argument('--out', type=Path, default=Path('build/electrical/driver'))
    ap.add_argument('--trace-dir', type=Path)
    ap.add_argument('--calibration', type=Path)
    ap.add_argument('--gpio-timing', type=Path)
    ap.add_argument('--skip-baseline', action='store_true')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'summary.json').write_text(json.dumps({'status': 'RUNNING', 'complete': False}) + '\n')
    extra = []
    if args.trace_dir:
        extra += ['--trace-dir', args.trace_dir.resolve()]
    if args.calibration:
        extra += ['--calibration', args.calibration.resolve()]
    gpio = None
    if args.gpio_timing:
        gpio = json.loads(args.gpio_timing.read_text())
        if gpio['status'] != 'PASS':
            raise RuntimeError('constrained AVR GPIO proof must pass')
        extra += ['--data-tail-us', gpio['data']['microseconds_at_16mhz'],
                  '--stop-tail-us', gpio['stop']['microseconds_at_16mhz'],
                  '--gpio-stamp-us', gpio['gpio_to_start_timestamp']['microseconds_at_16mhz']]
    executions = []
    compiler = command([args.cxx, '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    for baseline in ([False] if args.skip_baseline else [True, False]):
        folder = out / ('baseline-A' if baseline else 'revision-B')
        folder.mkdir(exist_ok=True)
        source = ROOT / 'firmware/shared'
        if baseline:
            source = folder / 'production-source'
            source.mkdir(exist_ok=True)
            for filename in ['one_wire.cpp', 'one_wire.hpp']:
                raw = command(['git', 'show', f'{BASELINE}:firmware/shared/{filename}'], cwd=ROOT, capture_output=True).stdout
                (source / filename).write_bytes(raw)
        executable = folder / ('driver-integration.exe' if sys.platform == 'win32' else 'driver-integration')
        command([args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', source,
                 *(['-DHD_LEGACY_DRIVER=1'] if baseline else []),
                 ROOT / 'tests/protected_frontend/driver_integration.cpp', source / 'one_wire.cpp', '-o', executable])
        command([executable, '--out', folder] + extra + (['--anchor-baseline', 'true'] if baseline else []))
        report = json.loads((folder / 'driver-integration.json').read_text())
        if report['status'] != 'PASS':
            raise RuntimeError('driver numerical/logical criterion failed')
        executions.append({'revision': 'A original stop anchor' if baseline else 'B stop settling',
                           'frontend_scope': 'same revision-B/sensitivity frontend for both driver anchors; not a full revision-A electrical rerun',
                           'source_sha': BASELINE if baseline else command(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
                           'source_sha_is_worktree_base': not baseline,
                           'source_sha256': {filename: hashlib.sha256((source / filename).read_bytes()).hexdigest()
                                             for filename in ['one_wire.cpp', 'one_wire.hpp']},
                           'compiler': compiler,
                           'report': str(folder / 'driver-integration.json'),
                           'checks': len(report['checks']), 'status': report['status']})
    (out / 'summary.json').write_text(json.dumps({'status': 'PASS', 'complete': True, 'runs': executions,
                                                'scope': 'checked SPICE replay + calibrated feedback + final constrained AVR proof'
                                                         if args.trace_dir and args.calibration and args.gpio_timing else
                                                         'checked SPICE replay + calibrated feedback; GPIO bounds are sensitivity allocations'
                                                         if args.trace_dir and args.calibration else
                                                         'standalone sensitivity model; no complete SPICE integration claim',
                                                'trace_directory': str(args.trace_dir.resolve()) if args.trace_dir else None,
                                                'calibration': str(args.calibration.resolve()) if args.calibration else None,
                                                'avr_gpio_proof': gpio,
                                                'physical_status': 'NOT VERIFIED'}, indent=2) + '\n')
    print('driver integration PASS; no instruction-level or physical evidence generated')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print('driver integration ERROR; never expected rejection:', error, file=sys.stderr)
        sys.exit(3)
