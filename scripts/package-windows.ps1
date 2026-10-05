[CmdletBinding()]
param(
    [string]$QtPath = $env:QT_ROOT_DIR,
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [string]$BuildDir = 'build/windows',
    [string]$OutputDir = 'dist'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path -Parent $PSScriptRoot
function Repository-Path([string]$Value) {
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath((Join-Path $repository $Value))
}
function Assert-NativeExit([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed with exit code $LASTEXITCODE." }
}
if ([string]::IsNullOrWhiteSpace($QtPath)) { throw 'Pass -QtPath or set QT_ROOT_DIR.' }
$qtRoot = (Resolve-Path -LiteralPath $QtPath).Path
$deployTool = Join-Path $qtRoot 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool)) { throw "windeployqt missing: $deployTool" }
$buildRoot = Repository-Path $BuildDir
$outputRoot = Repository-Path $OutputDir
$builtExecutable = Join-Path $buildRoot "$Configuration/HondaDash.exe"
if (-not (Test-Path -LiteralPath $builtExecutable)) { throw "Build Release first: $builtExecutable" }
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
# Fresh paths avoid merging a previous package with newly deployed dependencies.
$runId = [Guid]::NewGuid().ToString('N')
$stage = Join-Path $outputRoot "stage-$runId"
$clean = Join-Path $outputRoot "unpacked-$runId"
$reports = Join-Path $outputRoot 'reports'
New-Item -ItemType Directory -Path $stage, $clean, $reports -Force | Out-Null
Copy-Item -LiteralPath $builtExecutable -Destination (Join-Path $stage 'HondaDash.exe')
$vsRoot = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Assert-NativeExit 'Visual Studio discovery'
}
$savedPath = $env:PATH
$savedVCInstallDir = $env:VCINSTALLDIR
try {
    $env:PATH = (Join-Path $qtRoot 'bin') + ';' + $savedPath
    if ($vsRoot) { $env:VCINSTALLDIR = (Join-Path $vsRoot 'VC') + '/' }
    & $deployTool --release --compiler-runtime --no-translations --no-opengl-sw --skip-plugin-types generic,networkinformation,tls --dir $stage (Join-Path $stage 'HondaDash.exe')
    Assert-NativeExit 'windeployqt'
} finally { $env:PATH = $savedPath; $env:VCINSTALLDIR = $savedVCInstallDir }

# App-local redistribution of the release CRT files designated by VS 2022.
# The newest installed VC143 redistributable can satisfy older Qt MSVC builds.
if ($vsRoot) {
    $redistRoot = Join-Path $vsRoot 'VC/Redist/MSVC'
    if (Test-Path -LiteralPath $redistRoot) {
        $crtDirectory = Get-ChildItem -LiteralPath $redistRoot -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [Version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if ($crtDirectory) {
            Get-ChildItem -LiteralPath $crtDirectory -Filter '*.dll' -File | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $stage -Force
            }
        }
    }
}
foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6SerialPort.dll', 'platforms/qwindows.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $required))) { throw "Incomplete deployment: $required missing." }
}

# windeployqt may supply the redistributable installer rather than app-local DLLs.
# Inspect the compiler imports separately and make the prerequisite explicit.
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (-not $dumpbin) {
    if ($vsRoot) {
        $dumpbinCandidate = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Tools/MSVC') -Directory |
            Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin/Hostx64/x64/dumpbin.exe' } |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if ($dumpbinCandidate) { $dumpbin = Get-Command $dumpbinCandidate }
    }
}
if (-not $dumpbin) { throw 'dumpbin is required to inspect MSVC runtime dependencies. Run from a VS 2022 developer PowerShell.' }
$dependencyReport = Join-Path $reports 'windows-dependencies.txt'
$imports = @(foreach ($binary in (Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Extension -in @('.exe', '.dll') })) {
    Write-Output "Dependency inspection: $($binary.Name)"
    & $dumpbin.Source /dependents $binary.FullName
    Assert-NativeExit "Runtime dependency inspection: $($binary.Name)"
})
$imports | Set-Content -LiteralPath $dependencyReport -Encoding utf8
$runtimeDlls = @($imports | Select-String -Pattern '(?i)\b(?:msvcp|vcruntime|concrt)[a-z0-9_]*\.dll\b' -AllMatches |
    ForEach-Object { $_.Matches.Value.Trim() } | Where-Object { $_ -ne 'msvcp_win.dll' } | Sort-Object -Unique)
$missingRuntime = @($runtimeDlls | Where-Object { -not (Test-Path -LiteralPath (Join-Path $stage $_)) })
$runtimeStatement = if ($missingRuntime.Count -eq 0) {
    'Перевірені прямі DLL залежності MSVC додано поруч із програмою; системна Universal CRT входить до Windows 10/11.'
} else {
    'Перед запуском потрібен Microsoft Visual C++ Redistributable 2015–2022 x64 (версія не старіша за використаний MSVC 2022). Відсутні app-local DLL: ' + ($missingRuntime -join ', ') + '. Інсталятор vc_redist.x64.exe, якщо доданий windeployqt, треба встановити окремо.'
}
@"
HondaDash M2a — запуск під Windows x64

M1: вбудована емуляція або синтетична Nano через USB.
M2a: окремий Honda DLC reference-профіль, тільки програмний відповідач.
Фізичні Nano/USB, електричний DLC та реальний ECU: NOT VERIFIED.
Реальних captures немає; Valid у лабораторії не підтверджує сумісність ECU.

Розпакуйте весь ZIP в одну папку та запустіть HondaDash.exe.
Не переносіть EXE окремо від DLL і папки platforms.
$runtimeStatement

Натисніть «Старт», виберіть сценарій демонстрації або ручного керування.
У ручному режимі змініть оберти: панель оновиться після байтової відповіді.
Вимкніть відповіді: застарілі дані позначаються після 1 с, числа зникають
після 3 с. Відновіть відповіді та дочекайтеся нового вимірювання.
Запис: виберіть папку через кнопку запису, потім зупиніть запис.
F11 — повний екран; Esc — вихід із повного екрана.
USB: окремо завантажте firmware, виберіть джерело USB та конкретний порт.
Інструкція і hardware-чекліст: NANO_USB_TESTING.md. Build і GUI не роблять auto-upload.

Honda DLC: оберіть «Honda DLC — лабораторна емуляція», натисніть «Старт».
Набір A: 750 RPM, 61 °C, 32 %. Набір B: 1500 RPM, 89 °C, 75 %.
HEX-інспектор показує справжні байти reference-формату й джерело формули.
Інші чотири канали не визначені для цього профілю. Пошкодження/тайм-аут
зупиняє опитування; повторний «Старт» створює новий offline-експеримент.
Recording v3 зберігає часткові оновлення, давність і всі пошкоджені RX.
Докази, профіль, невизначеності й чекліст: HONDA_DLC_*.md поруч із програмою.

Автоматична перевірка з PowerShell:
.\HondaDash.exe --smoke-test --report smoke.json --screenshot dashboard.png
Успіх підтверджує report із passed=true і код завершення 0.

Qt 6.8.3 використано як динамічні бібліотеки. Ліцензії та copyright:
THIRD_PARTY_NOTICES.md і папка licenses. Ліцензію HondaDash власник
репозиторію поки не обрав.
"@ | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $repository 'docs/THIRD_PARTY_NOTICES.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repository 'docs/NANO_USB_TESTING.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repository 'docs/SYNTHETIC_DEVICE_EXTENSION.md') -Destination $stage
foreach ($document in @('HONDA_DLC_EVIDENCE.md', 'HONDA_DLC_PROTOCOL.md', 'HONDA_DLC_REFERENCE_PROFILE.md', 'HONDA_DLC_OFFLINE_TESTING.md')) {
    Copy-Item -LiteralPath (Join-Path $repository "docs/$document") -Destination $stage
}
$licenses = Join-Path $repository 'docs/licenses'
if (-not (Test-Path -LiteralPath $licenses)) { throw 'Third-party license texts are missing from docs/licenses.' }
Copy-Item -LiteralPath $licenses -Destination (Join-Path $stage 'licenses') -Recurse
# Force platform/image plugins to resolve relative to this executable.
"[Paths]`nPlugins=.`n" | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding ascii
$archive = Join-Path $outputRoot 'HondaDash-windows-x64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Expand-Archive -LiteralPath $archive -DestinationPath $clean

# Test the extracted archive, with every Qt/developer path removed.
$environmentNames = @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QT_ROOT_DIR', 'QTDIR', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QT_SCALE_FACTOR')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $env:PATH = "$clean;$env:SystemRoot\System32;$env:SystemRoot"
    $report = Join-Path $reports 'package-smoke.json'
    $screenshot = Join-Path $reports 'package-windows.png'
    $arguments = @('--smoke-test', '--report', "`"$report`"", '--screenshot', "`"$screenshot`"")
    $process = Start-Process -FilePath (Join-Path $clean 'HondaDash.exe') -ArgumentList $arguments -WorkingDirectory $clean -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Extracted package smoke test timed out.' }
    if ($process.ExitCode -ne 0) { throw "Extracted package smoke failed with exit code $($process.ExitCode)." }
    if (-not (Test-Path -LiteralPath $report)) { throw 'Extracted package did not write its smoke report.' }
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if ($result.passed -ne $true) { throw 'Extracted package smoke report did not indicate passed=true.' }
    if (-not (Test-Path -LiteralPath $screenshot)) { throw 'Extracted package did not save a dashboard screenshot.' }
    if ($result.platform -ne 'windows' -or $result.offscreen -ne $false) { throw 'Package smoke must use native Windows platform.' }
    $env:QT_SCALE_FACTOR = '1.5'
    $scaleReport = Join-Path $reports 'package-scale-150.json'
    $scaleScreenshot = Join-Path $reports 'package-scale-150.png'
    $scaleArguments = @('--smoke-test', '--report', "`"$scaleReport`"", '--screenshot', "`"$scaleScreenshot`"")
    $scaledProcess = Start-Process -FilePath (Join-Path $clean 'HondaDash.exe') -ArgumentList $scaleArguments -WorkingDirectory $clean -WindowStyle Hidden -PassThru
    if (-not $scaledProcess.WaitForExit(30000)) { $scaledProcess.Kill(); throw '150% package smoke timed out.' }
    if ($scaledProcess.ExitCode -ne 0) { throw "150% package smoke failed with exit code $($scaledProcess.ExitCode)." }
    $scaled = Get-Content -LiteralPath $scaleReport -Raw | ConvertFrom-Json
    if ($scaled.passed -ne $true -or $scaled.platform -ne 'windows' -or $scaled.offscreen -ne $false) { throw '150% package smoke failed.' }
    if (-not (Test-Path -LiteralPath $scaleScreenshot)) { throw '150% package screenshot missing.' }
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
Write-Output "Windows package and extracted-package check completed: $archive"
