#!/usr/bin/env python3
"""Conservative path bounds for the pinned ATmega328P bench ELF disassembly.

Build-time analysis only; not an instruction-level simulator or hardware evidence.
All branch alternatives are explored, including infeasible combinations. The only
data-dependent loop accepted in ISR paths is a <=7 bit shift (Driver phase invariant).
Constant copy loops are bounded by their immediate 8-bit loop initializer. Unknown
instructions, indirect calls, unbounded loops, or missing ISR symbols fail visibly.
"""
import argparse
import functools
import hashlib
import json
import pathlib
import re
import sys


ONE = set("adc add and andi asr bclr bld bset bst clc clh cli cln cls clt clv clz com cp cpc cpi dec eor in inc ldi lsr mov movw neg nop or ori out rol ror sbc sbci sec seh sei sen ser ses set sev sez sub subi swap tst wdr".split())
TWO = set("adiw cbi ld ldd lds mul muls mulsu pop push sbi sbiw st std sts".split())
SKIP = {"cpse", "sbic", "sbis", "sbrc", "sbrs"}


class Analysis:
    def __init__(self, source):
        self.instructions = {}
        self.labels = {}
        self.owner = {}
        owner = ""
        for line in source.splitlines():
            match = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
            if match:
                owner = match[2]
                self.labels[owner] = int(match[1], 16)
            match = re.match(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*([a-z][a-z0-9]*)\s*(.*)$", line)
            if match:
                address = int(match[1], 16)
                self.instructions[address] = (len(match[2].split()), match[3], match[4])
                self.owner[address] = owner
        self.addresses = sorted(self.instructions)
        self.previous = {b: a for a, b in zip(self.addresses, self.addresses[1:])}
        self.loop_assumptions = {}
        self.active_calls = set()

    def target(self, operands):
        match = re.search(r"(?:;\s*)?(0x[0-9a-f]+)\s*(?:<|$)", operands)
        if not match:
            raise ValueError("Unresolved branch/call: " + operands)
        return int(match[1], 16)

    def loop_bound(self, address, target, cycle):
        # Backward joins need no loop bound unless the destination is revisited.
        branches = [a for a in cycle if self.instructions[a][1].startswith("br")]
        branch = branches[-1] if branches else address
        _, operation, _ = self.instructions[branch]
        if operation == "brpl":
            loop = [self.instructions[a][1] for a in cycle]
            if set(loop) <= {"add", "adc", "dec", "brpl"} and self.owner[address] in {"__vector_11", "test_shift"}:
                self.loop_assumptions[hex(branch)] = "shift count <=7 from Driver TX/RX phase invariant"
                return 7
        if operation == "brne":
            prev = self.previous[branch]
            _, last_op, operands = self.instructions[prev]
            if last_op == "dec":
                reg = operands.split(";")[0].strip()
                before = [a for a in self.addresses if a < min(cycle)][-12:]
                for start in reversed(before):
                    _, op, args = self.instructions[start]
                    match = re.match(rf"{reg},\s*0x([0-9A-Fa-f]+)", args)
                    if op == "ldi" and match:
                        count = int(match[1], 16)
                        if 0 < count <= 32:
                            self.loop_assumptions[hex(branch)] = f"constant copy/count loop <= {count} iterations"
                            return count - 1
        raise ValueError(f"Unbounded/unrecognized loop at {address:#x} ({self.owner[address]})")

    @functools.lru_cache(maxsize=None)
    def function(self, address):
        if address in self.active_calls:
            raise ValueError("Recursive call graph")
        self.active_calls.add(address)
        try:
            return self.path(address)
        finally:
            self.active_calls.remove(address)

    def path(self, start, stop_register=None, stop_address=None):
        # Only actual cyclic edges need counters. A first walk discovers them;
        # memoization of subsequent branches retains the exact loop counter state.
        loops = {}
        visiting = set()
        active_path = []
        visited = set()

        def successors(address):
            if address == stop_address:
                return []
            size, op, arg = self.instructions[address]
            after = address + size
            if stop_register and op == "out" and re.match(rf"0x3f,\s*{stop_register}(?:\s|$)", arg):
                return []
            if op in {"ret", "reti"}:
                return []
            if op in {"jmp", "rjmp"}:
                return [self.target(arg)]
            if op.startswith("br"):
                return [after, self.target(arg)]
            if op in SKIP:
                return [after, after + self.instructions[after][0]]
            return [after]

        def discover(address):
            if address in visited:
                return
            if address not in self.instructions:
                raise ValueError(f"Missing instruction at {address:#x}")
            visiting.add(address)
            active_path.append(address)
            for dest in successors(address):
                if dest in visiting:
                    loops[(address, dest)] = self.loop_bound(address, dest, active_path[active_path.index(dest):])
                else:
                    discover(dest)
            visiting.remove(address)
            active_path.pop()
            visited.add(address)

        discover(start)
        loop_keys = tuple(sorted(loops))

        @functools.lru_cache(maxsize=None)
        def walk(address, counts):
            if address == stop_address:
                return 0
            size, op, arg = self.instructions[address]
            after = address + size
            if stop_register and op == "out" and re.match(rf"0x3f,\s*{stop_register}(?:\s|$)", arg):
                return 1
            if op in {"ret", "reti"}:
                if stop_register:
                    raise ValueError(f"Atomic path returns without restoring {stop_register}")
                return 4
            if op in {"call", "rcall"}:
                cost = (4 if op == "call" else 3) + self.function(self.target(arg))
                branches = [(after, cost)]
            elif op in {"icall", "eicall", "ijmp", "eijmp", "sleep"}:
                raise ValueError(f"Unsupported dynamic instruction {op} at {address:#x}")
            elif op in {"jmp", "rjmp"}:
                branches = [(self.target(arg), 3 if op == "jmp" else 2)]
            elif op.startswith("br"):
                branches = [(after, 1), (self.target(arg), 2)]
            elif op in SKIP:
                next_size = self.instructions[after][0]
                branches = [(after, 1), (after + next_size, 2 if next_size == 2 else 3)]
            elif op in ONE or op in TWO or op == "lpm":
                branches = [(after, 1 if op in ONE else 3 if op == "lpm" else 2)]
            else:
                raise ValueError(f"Unrecognized instruction {op} at {address:#x}")
            costs = []
            for dest, cost in branches:
                key = (address, dest)
                updated = counts
                if key in loops:
                    index = loop_keys.index(key)
                    if counts[index] == loops[key]:
                        continue
                    changed = list(counts)
                    changed[index] += 1
                    updated = tuple(changed)
                costs.append(cost + walk(dest, updated))
            if not costs:
                return -1000000000 # This branch would violate a source-established loop bound.
            return max(costs)

        result = walk(start, (0,) * len(loop_keys))
        if result < 0:
            raise ValueError("Bounded loop has no exit")
        return result

    def report(self):
        vectors = {}
        names = {10: "TIMER1_CAPT", 11: "TIMER1_COMPA", 13: "TIMER1_OVF", 16: "TIMER0_OVF", 18: "USART_RX", 19: "USART_UDRE"}
        for number, name in names.items():
            symbol = f"__vector_{number}"
            if symbol not in self.labels:
                raise ValueError("Missing " + symbol)
            cycles = self.function(self.labels[symbol]) + 7 # Hardware entry4 + vector JMP3.
            vectors[name] = {"cycles_upper_bound": cycles, "microseconds_at_16mhz": cycles / 16}
        compare = self.labels["__vector_11"]
        clock_calls = [a for a in self.addresses if self.owner[a] == "__vector_11"
                       and self.instructions[a][1] in {"call", "rcall"}
                       and "clockTicks" in self.owner[self.target(self.instructions[a][2])]]
        if not clock_calls:
            raise ValueError("Cannot establish COMPA timer-snapshot prefix")
        # The first clock snapshot is the input sample. A later call anchors the
        # physical TX stop release and must not be included in input lateness.
        clock_end = clock_calls[0] + self.instructions[clock_calls[0]][0]
        through_clock_return = self.path(compare, stop_address=clock_end) + 7
        clock_function = self.target(self.instructions[clock_calls[0]][2])
        clock_owner = self.owner[clock_function]
        high_reads = [a for a in self.addresses if self.owner[a] == clock_owner
                      and self.instructions[a][1] == "lds" and "0x0085" in self.instructions[a][2]]
        if len(high_reads) != 1:
            raise ValueError("Cannot establish actual Timer1 snapshot instruction")
        prefix = self.path(compare, stop_address=clock_calls[0]) + 7 + 4
        prefix += self.path(clock_function, stop_address=high_reads[0] + self.instructions[high_reads[0]][0])
        atomics = []
        exclusions = []
        for address in self.addresses:
            _, op, _ = self.instructions[address]
            if op != "cli":
                continue
            previous = self.instructions[self.previous[address]]
            saved = re.match(r"(r\d+),\s*0x3f", previous[2]) if previous[1] == "in" else None
            if not saved:
                if self.owner[address] != "_exit":
                    raise ValueError(f"Unrecognized CLI region at {address:#x}")
                exclusions.append({"address": hex(address), "function": self.owner[address], "reason": "compiler stack-pointer adjustment or shutdown; not ATOMIC_RESTORESTATE"})
                continue
            cycles = self.path(address, saved[1])
            atomics.append({"address": hex(address), "function": self.owner[address], "cycles_upper_bound": cycles, "microseconds_at_16mhz": cycles / 16})
        return {"analysis": "conservative static instruction-path bounds; not hardware measurement", "cpu": "ATmega328P", "clock_hz": 16000000,
                "isr": vectors, "atomic_restorestate_regions": atomics, "other_cli_sites": exclusions,
                "loop_assumptions": self.loop_assumptions,
                "compare_prefix_through_clock_snapshot": {"cycles_upper_bound": prefix, "microseconds_at_16mhz": prefix / 16,
                    "ends_after": "TCNT1H load; TCNT1L latches the hardware count earlier"},
                "compare_prefix_through_clock_return": {"cycles_upper_bound": through_clock_return, "microseconds_at_16mhz": through_clock_return / 16},
                "limitations": ["Branch choices overapproximate feasible paths.", "Source phase invariant bounds bit shifts by seven; memory corruption is outside this analysis.", "No interrupt arrival/arbitration simulation, oscillator tolerance, wiring or electrical propagation model.", "Physical ISR latency, USART overrun and stack high-water NOT VERIFIED."]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("disassembly", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    content = args.disassembly.read_text(encoding="utf-8-sig")
    result = Analysis(content).report()
    result["analyzer_self_checks"] = self_check()
    result["disassembly_sha256"] = hashlib.sha256(args.disassembly.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    peak = max(x["cycles_upper_bound"] for x in result["isr"].values())
    atomic = max(x["cycles_upper_bound"] for x in result["atomic_restorestate_regions"])
    print(f"AVR static paths: longest ISR <= {peak} cycles ({peak / 16:g} us); atomic region <= {atomic} cycles ({atomic / 16:g} us); physical NOT VERIFIED")


def self_check():
    cases = [
        ("00000000 <test>:\n0: 00 00 nop\n2: 08 95 ret\n", 5),
        ("00000000 <test>:\n0: 00 f0 breq .+2 ; 0x4 <test+4>\n2: 00 00 nop\n4: 08 95 ret\n", 6),
        ("00000000 <test>:\n0: 00 10 cpse r0, r1\n2: 0c 94 04 00 jmp 0x8 <test+8>\n6: 08 95 ret\n8: 08 95 ret\n", 8),
        ("00000000 <test>:\n0: 0e 94 04 00 call 0x8 <helper>\n4: 08 95 ret\n00000008 <helper>:\n8: 08 95 ret\n", 12),
        ("00000000 <test_shift>:\n0: 67 e0 ldi r22, 0x07\n2: 01 c0 rjmp .+2 ; 0x6 <test_shift+6>\n4: 44 0f add r20, r20\n6: 6a 95 dec r22\n8: ea f7 brpl .-6 ; 0x4 <test_shift+4>\na: 08 95 ret\n", 37),
    ]
    for source, expected in cases:
        value = Analysis(source).function(0)
        if value != expected:
            raise ValueError(f"Analyzer self-check {value} != {expected}")
    source = "00000000 <test>:\n0: 9f b6 in r9, 0x3f\n2: f8 94 cli\n4: 2f b7 in r18, 0x3f\n6: f8 94 cli\n8: 00 00 nop\na: 2f bf out 0x3f, r18\nc: 00 00 nop\ne: 9f be out 0x3f, r9\n10: 08 95 ret\n"
    if Analysis(source).path(2, "r9") != 7:
        raise ValueError("Analyzer failed nested atomic restore check")
    for source in ["00000000 <test>:\n0: ff cf rjmp .-2 ; 0x0 <test>\n", "00000000 <test>:\n0: 09 95 icall\n2: 08 95 ret\n"]:
        try:
            Analysis(source).function(0)
        except ValueError:
            continue
        raise ValueError("Analyzer accepted an unbounded/dynamic path")
    return 8


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, RecursionError) as error:
        print(f"AVR timing analysis failed: {error}", file=sys.stderr)
        sys.exit(1)
