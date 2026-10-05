#!/usr/bin/env bash
set -euo pipefail
repository="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tools_directory="${ARDUINO_TOOLS_DIR:-$repository/.tools/arduino-linux}"
cli_version=1.2.2
core_version=1.8.6
compiler_version=7.3.0-atmel3.6.1-arduino7
cli_checksum=a5ad085d1b02fd4010df18b1bbf20453276efe6c6e2ac323baa0cac4e96a85b4
bootstrap=false
if [[ "${1:-}" == --bootstrap ]]; then bootstrap=true; shift; fi
if [[ $# != 0 ]]; then echo 'Usage: bash scripts/build-firmware.sh [--bootstrap]' >&2; exit 2; fi
mkdir -p "$tools_directory"
cli="${ARDUINO_CLI:-$tools_directory/cli-$cli_version/arduino-cli}"
if $bootstrap && [[ ! -f "$cli" ]]; then
    archive="$tools_directory/arduino-cli_${cli_version}_Linux_64bit.tar.gz"
    curl --fail --location --retry 3 "https://downloads.arduino.cc/arduino-cli/arduino-cli_${cli_version}_Linux_64bit.tar.gz" --output "$archive"
    printf '%s  %s\n' "$cli_checksum" "$archive" | sha256sum --check --status
    mkdir -p "$(dirname -- "$cli")"
    tar -xzf "$archive" -C "$(dirname -- "$cli")"
fi
[[ -x "$cli" ]] || { echo 'Arduino CLI missing; pass --bootstrap or ARDUINO_CLI.' >&2; exit 1; }
version_text="$("$cli" version)"
[[ "$version_text" =~ Version:[[:space:]]+1\.2\.2[[:space:]] ]] || { echo "Expected CLI $cli_version: $version_text" >&2; exit 1; }
config="$tools_directory/arduino-cli.yaml"
printf "directories:\n  data: '%s/data'\n  downloads: '%s/downloads'\n  user: '%s/user'\n" "$tools_directory" "$tools_directory" "$tools_directory" > "$config"
if $bootstrap; then
    "$cli" --config-file "$config" core update-index
    "$cli" --config-file "$config" core install "arduino:avr@$core_version"
fi
cores="$("$cli" --config-file "$config" core list)"
[[ "$cores" =~ arduino:avr[[:space:]]+1\.8\.6[[:space:]] ]] || { echo 'Arduino AVR Boards 1.8.6 missing; use --bootstrap.' >&2; exit 1; }
compiler_bin="$tools_directory/data/packages/arduino/tools/avr-gcc/$compiler_version/bin"
compiler="$compiler_bin/avr-g++"
sketch="$repository/firmware/nano_synthetic"
build_directory="${FIRMWARE_BUILD_DIR:-$repository/build/firmware}"
for cpu in atmega328 atmega328old; do
    output="$build_directory/$cpu"
    mkdir -p "$output/stack"
    mkdir -p "$output/licenses"
    cp "$repository"/docs/licenses/firmware/* "$output/licenses/"
    fqbn="arduino:avr:nano:cpu=$cpu"
    "$cli" --config-file "$config" compile --fqbn "$fqbn" --warnings all --build-path "$output" \
        --build-property "compiler.c.elf.extra_flags=\"-Wl,-Map=$output/nano_synthetic.map\"" "$sketch" 2>&1 | tee "$output/compile.txt"
    elf="$output/nano_synthetic.ino.elf"
    [[ -s "$elf" && -s "$output/nano_synthetic.ino.hex" && -s "$output/nano_synthetic.map" ]]
    "$compiler_bin/avr-size" -A "$elf" > "$output/size.txt"
    "$compiler_bin/avr-nm" --print-size --size-sort --radix=d "$elf" > "$output/symbols.txt"
    if grep -Eq '[[:space:]](malloc|calloc|realloc|_Zn[^[:space:]]*)$' "$output/symbols.txt"; then
        echo 'Heap allocator linked into firmware.' >&2; exit 1
    fi
    "$compiler" -mmcu=atmega328p -DF_CPU=16000000UL -std=gnu++11 -Os -fno-exceptions -fno-threadsafe-statics \
        -fstack-usage -c "$sketch/endpoint.cpp" -o "$output/stack/endpoint.o"
    "$compiler" --version > "$output/compiler.txt"
    python3 - "$output" "$fqbn" <<'PY'
import json, pathlib, re, sys
output, fqbn = pathlib.Path(sys.argv[1]), sys.argv[2]
sections = {name:int(size) for name,size in re.findall(r'^\s*(\.\w+)\s+(\d+)\s+', (output/'size.txt').read_text(), re.M)}
sram = sections['.data'] + sections['.bss']
flash = sections['.text'] + sections['.data']
report = dict(fqbn=fqbn, arduino_cli='1.2.2', arduino_avr_boards='1.8.6',
    avr_gcc_package='7.3.0-atmel3.6.1-arduino7', compiler=(output/'compiler.txt').read_text().splitlines()[0],
    protocol_baud=115200, flash_bytes=flash, data_bytes=sections['.data'], bss_bytes=sections['.bss'],
    static_sram_bytes=sram, static_sram_budget=1536, sram_remaining_for_stack=2048-sram,
    serial_rx_buffer_bytes=64, serial_tx_buffer_bytes=64,
    stack_status='Static estimate only; physical stack high-water NOT VERIFIED', hardware_status='NOT VERIFIED')
(output/'size-report.json').write_text(json.dumps(report, indent=2)+'\n')
if sram > 1536 or flash > 30720:
    raise SystemExit(f'Firmware memory budget exceeded: SRAM={sram}; Flash={flash}')
print(f'{fqbn}: Flash {flash}/30720; .data+.bss {sram}/1536; stack reserve {2048-sram}.')
PY
done
echo 'Both Nano builds completed. No port was opened and no upload was performed.'
