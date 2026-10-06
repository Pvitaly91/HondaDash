#!/usr/bin/env python3
"""Constrained normal TX GPIO paths of the final pinned AVR disassembly.

Reuses the strict instruction/loop counter from analyze-avr-isr.py. The source
constraints are explicit: active normal TX, due compare <=40ticks late, even data
phase2..16 or stop phase18, and the matching post-increment phase. Unknown compiler
structure fails; this is a static bound, not an AVR/hardware timing measurement.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('avr_paths', ROOT / 'scripts/analyze-avr-isr.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def require(value, reason):
    if not value:
        raise ValueError(reason)


def unique(values, reason):
    require(len(values) == 1, reason + ': expected one site, found ' + str(len(values)))
    return values[0]


def bound(text, stop_phase):
    a = module.Analysis(text)
    vec = a.labels['__vector_11']
    addresses = [pc for pc in a.addresses if a.owner[pc] == '__vector_11']
    clocks = [(name, pc) for name, pc in a.labels.items() if 'clockTicks()' in name]
    clock_name, clock = unique(clocks, 'clock function')
    updates = [(name, pc) for name, pc in a.labels.items() if 'updateHardware()' in name]
    update_name, update = unique(updates, 'GPIO update function')
    calls = [pc for pc in addresses if a.instructions[pc][1] in ('call', 'rcall')
             and a.target(a.instructions[pc][2]) == clock]
    require(calls, 'COMPA must snapshot time')
    first_clock = calls[0]
    after_clock = first_clock + a.instructions[first_clock][0]
    stop_pin = unique([pc for pc in addresses if a.instructions[pc][1] == 'cbi'
                       and re.match(r'0x0b,\s*3(?:\s|$)',a.instructions[pc][2],re.I)], 'actual stop PORTD')

    def cpi(immediate, before=None):
        return unique([pc for pc in addresses if a.instructions[pc][1] == 'cpi'
                       and (before is None or pc < before)
                       and re.match(r'r\d+,\s*0x' + f'{immediate:02X}' + r'(?:\s|$)', a.instructions[pc][2], re.I)],
                      f'phase/deadline comparison {immediate}')

    phase20, phase18, phase19, late41 = cpi(20), cpi(18), cpi(19, stop_pin), cpi(41)
    phase_register = a.instructions[phase20][2].split(',')[0].strip()
    phase1 = cpi(1, stop_pin)
    # The compiler may use a different register for the post-timer phase reload.
    if phase1 > phase20:
        raise ValueError('unexpected start/normal-TX CFG layout')

    forced = {}

    def force_equality_after(pc, equal):
        following = pc + a.instructions[pc][0]
        size, op, arg = a.instructions[following]
        require(op in ('breq','brne'), f'branch after {pc:#x}: expected equality branch, got {op}')
        taken = equal if op == 'breq' else not equal
        forced[following] = a.target(arg) if taken else following + size

    force_equality_after(phase20, False)
    force_equality_after(phase18, stop_phase)
    force_equality_after(phase1, False)
    force_equality_after(phase19, stop_phase)

    # Normal, due TX; exclude paths that cannot reach this phase transition.
    event_branch = unique([pc for pc in addresses if after_clock < pc < late41
                           and a.instructions[pc][1] == 'breq'], 'hasEvent branch')
    forced[event_branch] = event_branch + a.instructions[event_branch][0]
    signed_skip = unique([pc for pc in addresses if after_clock < pc < late41
                          and a.instructions[pc][1] == 'sbrc'], 'due-time signed skip')
    after = signed_skip + a.instructions[signed_skip][0]
    forced[signed_skip] = after + a.instructions[after][0]
    late_branch = unique([pc for pc in addresses if late41 < pc < late41 + 16
                          and a.instructions[pc][1] == 'brcc'], 'unchanged <=40ticks guard')
    forced[late_branch] = late_branch + a.instructions[late_branch][0]

    odd_skip = unique([pc for pc in addresses if phase20 - 100 < pc < phase20
                       and a.instructions[pc][1] == 'sbrs'
                       and re.match(re.escape(phase_register) + r',\s*0(?:\s|$)', a.instructions[pc][2])], 'TX even-phase skip')
    forced[odd_skip] = odd_skip + a.instructions[odd_skip][0]
    # Two active-TX guards: before normal state handling and before HAL handling.
    active_branches = [pc for pc in addresses if a.instructions[pc][1] == 'brne'
                       and (phase1 - 18 < pc < phase1 or odd_skip - 20 < pc < odd_skip - 4)]
    require(len(active_branches) == 2, 'two active TX guards are required')
    for pc in active_branches:
        forced[pc] = a.target(a.instructions[pc][2])

    if stop_phase:
        # Unanchored phase19: skip the jump for a previously anchored stop.
        anchored_skip = unique([pc for pc in addresses if phase19 < pc < stop_pin
                                and a.instructions[pc][1] == 'cpse'], 'stop-anchor flag skip')
        after = anchored_skip + a.instructions[anchored_skip][0]
        forced[anchored_skip] = after + a.instructions[after][0]
        target = stop_pin
    else:
        target = unique([pc for pc in addresses if pc > phase19 and a.instructions[pc][1] in ('call', 'rcall')
                         and a.target(a.instructions[pc][2]) == update], 'ordinary TX GPIO call')

    # A forced rjmp charges2cycles, conservatively >=the chosen branch/skip cost.
    # None of the chosen skip sites skips a4-byte instruction.
    for pc, dest in forced.items():
        size, _, _ = a.instructions[pc]
        a.instructions[pc] = (size, 'rjmp', f'0x{dest:x}')
    low_read = unique([pc for pc in a.addresses if a.owner[pc] == clock_name
                       and a.instructions[pc][1] == 'lds' and ', 0x0084' in a.instructions[pc][2]], 'TCNT1L timestamp')
    clock_tail = a.path(low_read + a.instructions[low_read][0])
    core = a.path(after_clock, stop_address=target)
    gpio = 2
    if not stop_phase:
        # Bound each possible ordinary TX GPIO polarity independently.
        options = []
        for operation in ('sbi', 'cbi'):
            b = module.Analysis(text)
            sites = [pc for pc in b.addresses if b.owner[pc] == update_name and b.instructions[pc][1] == operation
                     and re.match(r'0x0b,\s*3(?:\s|$)', b.instructions[pc][2], re.I)]
            pin = unique(sites, 'ordinary TX PORTD ' + operation)
            branch = unique([pc for pc in b.addresses if b.owner[pc] == update_name
                             and update < pc < min(sites + [pc2 for pc2 in b.addresses if b.owner[pc2] == update_name
                                   and b.instructions[pc2][1] in ('sbi','cbi')]) and b.instructions[pc][1] == 'breq'], 'GPIO polarity branch')
            size, _, arg = b.instructions[branch]
            dest = b.target(arg) if operation == 'cbi' else branch + size
            b.instructions[branch] = (size, 'rjmp', f'0x{dest:x}')
            options.append(b.path(update, stop_address=pin) + 2)
        gpio = 4 + max(options)  # CALL andactual GPIO instruction; no laterHALwork.
    return {'path': 'normal stop phase18' if stop_phase else 'normal data phase2..16',
            'clock_return_cycles': clock_tail, 'core_cycles': core, 'gpio_cycles': gpio,
            'cycles_upper_bound': clock_tail + core + gpio,
            'microseconds_at_16mhz': (clock_tail + core + gpio) / 16,
            'forced_source_invariants': {hex(pc): hex(dest) for pc, dest in forced.items()},
            'loop_assumptions': a.loop_assumptions}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('disassembly', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'status':'RUNNING','complete':False})+'\n')
    text = args.disassembly.read_text()
    a = module.Analysis(text)
    start_name, start = unique([(name, pc) for name, pc in a.labels.items() if 'applyTxStart()' in name], 'start HAL')
    clock_name, clock = unique([(name, pc) for name, pc in a.labels.items() if 'clockTicks()' in name], 'clock function')
    pin = unique([pc for pc in a.addresses if a.owner[pc] == start_name and a.instructions[pc][1] == 'sbi'
                  and re.match(r'0x0b,\s*3(?:\s|$)',a.instructions[pc][2],re.I)], 'actual start PORTD')
    low_read = unique([pc for pc in a.addresses if a.owner[pc] == start_name and a.instructions[pc][1] == 'lds'
                       and ', 0x0084' in a.instructions[pc][2]], 'TCNT1L timestamp')
    stamp_cycles = a.path(pin+a.instructions[pin][0],stop_address=low_read)+2
    report = {'status': 'PASS', 'complete':True, 'analysis': 'constrained static instruction paths, not hardware measurement',
              'disassembly_sha256': hashlib.sha256(args.disassembly.read_bytes()).hexdigest(),
              'data': bound(text, False), 'stop': bound(text, True),
              'gpio_to_start_timestamp': {'cycles_upper_bound': stamp_cycles, 'microseconds_at_16mhz': stamp_cycles/16},
              'limits': 'normal TX only; <=40ticks lateness; no corruption/fault branches; clocks/interruption/electrical checked separately'}
    report['checks'] = []
    for name,value,limit in [
            ('normal_data_gpio_tail_us',report['data']['microseconds_at_16mhz'],10),
            ('normal_stop_gpio_tail_us',report['stop']['microseconds_at_16mhz'],9),
            ('gpio_timestamp_lag_us',stamp_cycles/16,.25)]:
        row = {'criterion':name,'measured':value,'relation':'<=','limit':limit,
               'margin':limit-value,'status':'PASS' if value<=limit else 'FAIL'}
        report['checks'].append(row)
    if any(row['status']=='FAIL' for row in report['checks']):
        report['status'] = 'FAIL'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('AVR GPIO', report['status'],
          'data', report['data']['cycles_upper_bound'], 'cycles;',
          'stop', report['stop']['cycles_upper_bound'], 'cycles;',
          'stamp', stamp_cycles, 'cycles; physical NOT VERIFIED')
    if report['status'] != 'PASS':
        raise ValueError('compiled GPIO timing exceeds paired-model acceptance allocation')


if __name__ == '__main__':
    main()
