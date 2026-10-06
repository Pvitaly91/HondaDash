[CmdletBinding()]
param([switch]$Bootstrap, [string]$ArduinoCli = '', [int]$SramBudget = 1536, [string]$Python = 'python',
      [ValidateSet('synthetic', 'bridge-lab', 'bridge-bench', 'responder-bench', 'all')][string]$Firmware = 'synthetic')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path -Parent $PSScriptRoot
$toolsDirectory = Join-Path $repository '.tools/arduino'
$cliVersion = '1.2.2'
$coreVersion = '1.8.6'
$compilerVersion = '7.3.0-atmel3.6.1-arduino7'
$cliChecksum = 'bdd3ed88a361af8539e51a1cc0bf831b269be155ddfdd90cb96a900ce78723b7'
if ($SramBudget -le 0 -or $SramBudget -gt 1536) { throw 'SRAM budget must be 1..1536 bytes.' }
function Assert-NativeExit([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed with exit code $LASTEXITCODE." }
}
New-Item -ItemType Directory -Path $toolsDirectory -Force | Out-Null
if ([string]::IsNullOrWhiteSpace($ArduinoCli)) {
    $ArduinoCli = Join-Path $toolsDirectory "cli-$cliVersion/arduino-cli.exe"
}
if ($Bootstrap -and -not (Test-Path -LiteralPath $ArduinoCli)) {
    $archive = Join-Path $toolsDirectory "arduino-cli_${cliVersion}_Windows_64bit.zip"
    Invoke-WebRequest -Uri "https://downloads.arduino.cc/arduino-cli/arduino-cli_${cliVersion}_Windows_64bit.zip" -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $cliChecksum) {
        throw 'Arduino CLI archive SHA256 mismatch.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath (Split-Path -Parent $ArduinoCli) -Force
}
if (-not (Test-Path -LiteralPath $ArduinoCli)) { throw 'Arduino CLI missing; run with -Bootstrap or pass -ArduinoCli.' }
$versionText = & $ArduinoCli version
Assert-NativeExit 'Arduino CLI version'
if ($versionText -notmatch 'Version:\s+1\.2\.2\s') { throw "Expected Arduino CLI $cliVersion, got: $versionText" }
$configPath = Join-Path $toolsDirectory 'arduino-cli.yaml'
$portableTools = $toolsDirectory.Replace('\', '/')
@"
directories:
  data: '$portableTools/data'
  downloads: '$portableTools/downloads'
  user: '$portableTools/user'
"@ | Set-Content -LiteralPath $configPath -Encoding utf8
if ($Bootstrap) {
    & $ArduinoCli --config-file $configPath core update-index
    Assert-NativeExit 'Arduino indexes'
    & $ArduinoCli --config-file $configPath core install "arduino:avr@$coreVersion"
    Assert-NativeExit 'Arduino AVR Boards install'
}
$cores = & $ArduinoCli --config-file $configPath core list
Assert-NativeExit 'Arduino core list'
if (($cores -join "`n") -notmatch 'arduino:avr\s+1\.8\.6\s') { throw 'Arduino AVR Boards 1.8.6 missing; run with -Bootstrap.' }
$compilerBin = Join-Path $toolsDirectory "data/packages/arduino/tools/avr-gcc/$compilerVersion/bin"
$compiler = Join-Path $compilerBin 'avr-g++.exe'
$sizeTool = Join-Path $compilerBin 'avr-size.exe'
$nmTool = Join-Path $compilerBin 'avr-nm.exe'
$variants = if ($Firmware -eq 'all') { @('synthetic', 'bridge-lab', 'bridge-bench', 'responder-bench') } else { @($Firmware) }
foreach ($variant in $variants) {
$sketchName = @{ synthetic='nano_synthetic'; 'bridge-lab'='nano_dlc_bridge_lab'; 'bridge-bench'='nano_dlc_bridge_bench'; 'responder-bench'='nano_dlc_responder_bench' }[$variant]
$buildName = if ($variant -eq 'synthetic') { 'firmware' } else { "firmware-$variant" }
$sketch = Join-Path $repository "firmware/$sketchName"
foreach ($cpu in @('atmega328', 'atmega328old')) {
    $output = Join-Path $repository "build/$buildName/$cpu"
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $outputLicenses = Join-Path $output 'licenses'
    New-Item -ItemType Directory -Path $outputLicenses -Force | Out-Null
    $map = (Join-Path $output "$sketchName.map").Replace('\', '/')
    $fqbn = "arduino:avr:nano:cpu=$cpu"
    & $ArduinoCli --config-file $configPath compile --fqbn $fqbn --warnings all --build-path $output `
        --build-property "compiler.cpp.extra_flags=`"-I$($sketch.Replace('\','/'))`"" `
        --build-property "compiler.c.elf.extra_flags=`"-Wl,-Map=$map`"" $sketch 2>&1 | Tee-Object -FilePath (Join-Path $output 'compile.txt')
    Assert-NativeExit "AVR $cpu compile"
    # Arduino CLI may clean the build directory when cached properties change.
    New-Item -ItemType Directory -Path $outputLicenses -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $repository 'docs/licenses/firmware') -File | Copy-Item -Destination $outputLicenses -Force
    $elf = Join-Path $output "$sketchName.ino.elf"
    $hex = Join-Path $output "$sketchName.ino.hex"
    foreach ($required in @($elf, $hex, $map)) {
        if (-not (Test-Path -LiteralPath $required)) { throw "Missing firmware output: $required" }
    }
    $sizeText = & $sizeTool -A $elf
    Assert-NativeExit 'AVR size'
    $sizeText | Set-Content -LiteralPath (Join-Path $output 'size.txt') -Encoding utf8
    $sections = @{}
    foreach ($line in $sizeText) {
        if ($line -match '^\s*(\.\w+)\s+(\d+)\s+') { $sections[$Matches[1]] = [int]$Matches[2] }
    }
    foreach ($required in @('.data', '.bss', '.text')) {
        if (-not $sections.ContainsKey($required)) { throw "Missing ELF section $required" }
    }
    $sram = $sections['.data'] + $sections['.bss']
    $flash = $sections['.text'] + $sections['.data']
    $symbols = & $nmTool --print-size --size-sort --radix=d $elf
    Assert-NativeExit 'AVR symbols'
    $symbols | Set-Content -LiteralPath (Join-Path $output 'symbols.txt') -Encoding utf8
    & (Join-Path $compilerBin 'avr-objdump.exe') -d -C $elf | Set-Content -LiteralPath (Join-Path $output 'disassembly.txt') -Encoding utf8
    Assert-NativeExit 'AVR disassembly'
    if ($variant.EndsWith('-bench')) {
        $pythonCommand = Get-Command $Python -ErrorAction Stop
        if ($pythonCommand.Source -match '\\WindowsApps\\') {
            throw 'AVR static analysis needs Python 3; pass -Python <python.exe> or put Python on PATH (Windows Store alias is not a runtime).'
        }
        & $Python (Join-Path $repository 'scripts/analyze-avr-isr.py') (Join-Path $output 'disassembly.txt') --output (Join-Path $output 'isr-timing.json')
        Assert-NativeExit 'AVR static ISR analysis'
        & $Python (Join-Path $repository 'tests/protected_frontend/avr_gpio_timing.py') (Join-Path $output 'disassembly.txt') --out (Join-Path $output 'gpio-timing.json')
        Assert-NativeExit 'AVR constrained GPIO timing analysis'
    }
    if (($symbols -join "`n") -cmatch '(?m)\s(?:malloc|calloc|realloc|_Zn\w*)\s*$') { throw 'Heap allocator linked into firmware.' }
    $compilerText = & $compiler --version
    Assert-NativeExit 'AVR compiler version'
    # Diagnostic non-LTO compilation provides individual stack frames; it is not
    # a measurement of the deployed LTO image's maximum stack depth.
    $stackDirectory = Join-Path $output 'stack'
    New-Item -ItemType Directory -Path $stackDirectory -Force | Out-Null
    foreach ($source in (Get-ChildItem -LiteralPath $sketch -Filter '*.cpp' -File)) {
        & $compiler -mmcu=atmega328p -DF_CPU=16000000UL -std=gnu++11 -Os -fno-exceptions -fno-threadsafe-statics `
            -I $sketch -I (Join-Path $toolsDirectory "data/packages/arduino/hardware/avr/$coreVersion/cores/arduino") `
            -I (Join-Path $toolsDirectory "data/packages/arduino/hardware/avr/$coreVersion/variants/eightanaloginputs") `
            -fstack-usage -c $source.FullName -o (Join-Path $stackDirectory ($source.BaseName + '.o'))
        Assert-NativeExit "AVR stack diagnostic compile: $($source.Name)"
    }
    $report = [ordered]@{
        fqbn=$fqbn; firmware=$variant;
        arduino_cli=$cliVersion; arduino_avr_boards=$coreVersion;
        avr_gcc_package=$compilerVersion; compiler=$compilerText[0]; protocol_baud=115200;
        flash_bytes=$flash; data_bytes=$sections['.data']; bss_bytes=$sections['.bss'];
        static_sram_bytes=$sram; static_sram_budget=$SramBudget; sram_remaining_for_stack=2048-$sram;
        serial_rx_buffer_bytes=64; serial_tx_buffer_bytes=64;
        stack_status='Static estimate only; physical stack high-water NOT VERIFIED';
        hardware_status='NOT VERIFIED'
    }
    if ($variant.EndsWith('-bench')) { $report.bench_schema_version=1; $report.bench_io_enabled=$true; $report.vehicle_connection_allowed=$false; $report.line_baud=9600 }
    else { $report.physical_dlc_enabled=$false }
    $report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'size-report.json') -Encoding utf8
    if ($sram -gt $SramBudget) { throw "Static SRAM budget exceeded: $sram > $SramBudget" }
    if ($flash -gt 30720) { throw "Flash budget exceeded: $flash > 30720" }
    Write-Host "$variant / $fqbn : Flash $flash/30720; .data+.bss $sram/$SramBudget; stack reserve $(2048-$sram)."
}
}
Write-Host 'Selected Nano firmware builds completed. No port was opened and no upload was performed.'
