[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][ValidateSet('synthetic', 'bridge-lab')][string]$Firmware,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Port,
    [Parameter(Mandatory)][ValidateSet('arduino:avr:nano:cpu=atmega328', 'arduino:avr:nano:cpu=atmega328old')][string]$Fqbn
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path -Parent $PSScriptRoot
$cpu = $Fqbn.Split('=')[-1]
$sketchName = if ($Firmware -eq 'synthetic') { 'nano_synthetic' } else { 'nano_dlc_bridge_lab' }
$buildName = if ($Firmware -eq 'synthetic') { 'firmware' } else { 'firmware-bridge-lab' }
$hex = Join-Path $repository "build/$buildName/$cpu/$sketchName.ino.hex"
$cli = Join-Path $repository '.tools/arduino/cli-1.2.2/arduino-cli.exe'
$config = Join-Path $repository '.tools/arduino/arduino-cli.yaml'
foreach ($file in @($hex, $cli, $config)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file missing: $file. Build the selected firmware first." }
}
$version = & $cli version
if ($LASTEXITCODE -or ($version -join "`n") -notmatch 'Version:\s+1\.2\.2\s') { throw 'Arduino CLI 1.2.2 is required.' }
if ($PSCmdlet.ShouldProcess("$Port / $Fqbn", "Upload explicitly selected $Firmware firmware (USB only)")) {
    & $cli --config-file $config upload --fqbn $Fqbn --port $Port --input-file $hex
    if ($LASTEXITCODE) { throw "Upload failed with exit code $LASTEXITCODE." }
}
