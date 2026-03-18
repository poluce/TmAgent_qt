param(
    [ValidateSet("release", "debug")]
    [string]$Config = "release",

    [string]$BuildDir = "build-mingw73_32",

    [string]$QMakePath = "",

    [string]$MakePath = "",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Resolve-CommandPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandName
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    return $null
}

function Resolve-FirstExistingPath {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

$root = $PSScriptRoot
Set-Location $root

if ([string]::IsNullOrWhiteSpace($QMakePath)) {
    $QMakePath = Resolve-CommandPath -CommandName "qmake"
}

if ([string]::IsNullOrWhiteSpace($QMakePath)) {
    $QMakePath = Resolve-FirstExistingPath -Candidates @(
        "D:\Qt5\Qt5.14.2\5.14.2\mingw73_32\bin\qmake.exe",
        "C:\Qt\5.14.2\mingw73_32\bin\qmake.exe"
    )
}

if ([string]::IsNullOrWhiteSpace($QMakePath)) {
    throw "qmake not found. Add Qt bin to PATH or pass -QMakePath explicitly."
}

if ([string]::IsNullOrWhiteSpace($MakePath)) {
    $MakePath = Resolve-CommandPath -CommandName "mingw32-make"
}

if ([string]::IsNullOrWhiteSpace($MakePath)) {
    $MakePath = Resolve-FirstExistingPath -Candidates @(
        "D:\Qt5\Qt5.14.2\Tools\mingw730_32\bin\mingw32-make.exe",
        "C:\Qt\Tools\mingw730_32\bin\mingw32-make.exe"
    )
}

if ([string]::IsNullOrWhiteSpace($MakePath)) {
    throw "mingw32-make not found. Add MinGW bin to PATH or pass -MakePath explicitly."
}

$qmakeDir = Split-Path -Parent $QMakePath
$makeDir = Split-Path -Parent $MakePath
$env:PATH = "$qmakeDir;$makeDir;$env:PATH"

$qtKeychainPath = Join-Path $root "3rdparty\qtkeychain"
$qtKeychainMissing = (-not (Test-Path $qtKeychainPath)) -or
    ($null -eq (Get-ChildItem $qtKeychainPath -Force -ErrorAction SilentlyContinue | Select-Object -First 1))

if ($qtKeychainMissing) {
    Write-Host "Initializing missing submodules..." -ForegroundColor Cyan
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$buildPath = Join-Path $root $BuildDir
if (-not (Test-Path $buildPath)) {
    New-Item -ItemType Directory -Path $buildPath | Out-Null
}

if ($Clean) {
    Write-Host "Cleaning previous build output..." -ForegroundColor Cyan
    Get-ChildItem $buildPath -Force | Remove-Item -Recurse -Force
}

$configArg = "CONFIG+=$Config"

Write-Host "Using qmake: $QMakePath" -ForegroundColor Cyan
Write-Host "Using make: $MakePath" -ForegroundColor Cyan
Write-Host "Build dir: $buildPath" -ForegroundColor Cyan
Write-Host "Build config: $Config" -ForegroundColor Cyan

Push-Location $buildPath
try {
    & $QMakePath -r ..\TmAgent.pro $configArg
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $MakePath -j4
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}

$mainExe = Join-Path $buildPath "$Config\TmAgent.exe"
$cliExe = Join-Path $buildPath "$Config\TmAgentCli.exe"
$logExe = Join-Path $buildPath "tmagent-log\$Config\tmagent-log.exe"

Write-Host "Build finished." -ForegroundColor Green
if (Test-Path $mainExe) {
    Write-Host "TmAgent: $mainExe" -ForegroundColor Green
}
if (Test-Path $cliExe) {
    Write-Host "TmAgentCli: $cliExe" -ForegroundColor Green
}
if (Test-Path $logExe) {
    Write-Host "tmagent-log: $logExe" -ForegroundColor Green
}
