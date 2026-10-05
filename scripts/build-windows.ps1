[CmdletBinding()]
param(
    [string]$QtPath = $env:QT_ROOT_DIR,
    [ValidateSet('Debug', 'Release')][string[]]$Configuration = @('Debug', 'Release')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($QtPath)) {
    throw 'Pass -QtPath <Qt 6.8.3 msvc2022_64 folder> or set QT_ROOT_DIR.'
}
$qtRoot = (Resolve-Path -LiteralPath $QtPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin/qmake.exe'))) {
    throw 'QtPath must be the Qt 6.8.3 msvc2022_64 installation root.'
}
function Assert-NativeExit([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed with exit code $LASTEXITCODE." }
}
$savedPath = $env:PATH
Push-Location -LiteralPath $repository
try {
    $env:PATH = (Join-Path $qtRoot 'bin') + ';' + $savedPath
    & cmake --version
    Assert-NativeExit 'CMake version check'
    & (Join-Path $qtRoot 'bin/qmake.exe') -query QT_VERSION
    Assert-NativeExit 'Qt version check'
    & cmake --preset windows "-DCMAKE_PREFIX_PATH=$qtRoot"
    Assert-NativeExit 'Windows configuration'
    $reports = Join-Path $repository 'build/windows/reports'
    New-Item -ItemType Directory -Path $reports -Force | Out-Null
    foreach ($buildConfiguration in $Configuration) {
        & cmake --build build/windows --config $buildConfiguration --parallel
        Assert-NativeExit "$buildConfiguration build"
        & ctest --test-dir build/windows -C $buildConfiguration --output-on-failure --output-junit (Join-Path $reports "ctest-$buildConfiguration.xml")
        Assert-NativeExit "$buildConfiguration tests"
    }
} finally {
    $env:PATH = $savedPath
    Pop-Location
}
