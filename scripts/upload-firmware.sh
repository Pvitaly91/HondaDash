#!/usr/bin/env bash
set -euo pipefail
repository="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
firmware=''; port=''; fqbn=''; dry_run=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --firmware|--port|--fqbn)
            [[ $# -ge 2 ]] || { echo "$1 requires a value" >&2; exit 2; }
            case "$1" in --firmware) firmware="$2" ;; --port) port="$2" ;; --fqbn) fqbn="$2" ;; esac
            shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        *) echo 'Required: --firmware synthetic|bridge-lab|bridge-bench|responder-bench --port PORT --fqbn arduino:avr:nano:cpu=atmega328[old] [--dry-run]' >&2; exit 2 ;;
    esac
done
[[ "$firmware" == synthetic || "$firmware" == bridge-lab || "$firmware" == bridge-bench || "$firmware" == responder-bench ]] || { echo 'Explicit firmware selection is required.' >&2; exit 2; }
[[ -n "$port" ]] || { echo 'Explicit port is required.' >&2; exit 2; }
[[ "$fqbn" == arduino:avr:nano:cpu=atmega328 || "$fqbn" == arduino:avr:nano:cpu=atmega328old ]] || { echo 'Explicit supported Nano FQBN is required.' >&2; exit 2; }
tools_directory="${ARDUINO_TOOLS_DIR:-$repository/.tools/arduino-linux}"
build_directory="${FIRMWARE_BUILD_DIR:-$repository/build/firmware}"
sketch_name=nano_synthetic
if [[ "$firmware" != synthetic ]]; then build_directory="${build_directory}-$firmware"; sketch_name="nano_dlc_${firmware//-/_}"; fi
hex="$build_directory/${fqbn##*=}/$sketch_name.ino.hex"
cli="${ARDUINO_CLI:-$tools_directory/cli-1.2.2/arduino-cli}"
config="$tools_directory/arduino-cli.yaml"
[[ -x "$cli" && -s "$hex" && -s "$config" ]] || { echo 'CLI/config/selected firmware missing; build first.' >&2; exit 1; }
version_text="$("$cli" version)"
[[ "$version_text" =~ Version:[[:space:]]+1\.2\.2[[:space:]] ]] || { echo 'Arduino CLI 1.2.2 is required.' >&2; exit 1; }
if $dry_run; then
    printf 'Dry run: firmware=%s port=%s fqbn=%s input=%s\n' "$firmware" "$port" "$fqbn" "$hex"
    exit 0
fi
"$cli" --config-file "$config" upload --fqbn "$fqbn" --port "$port" --input-file "$hex"
