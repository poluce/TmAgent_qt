param(
    [string]$TestName = "HistoryFormattersTest",

    [ValidateSet("release", "debug")]
    [string]$Config = "release",

    [string]$BuildDir = "",

    [string]$QMakePath = "",

    [string]$MakePath = "",

    [int]$Jobs = 4,

    [switch]$Clean,

    [switch]$Run
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

function Resolve-RepoPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,

        [Parameter(Mandatory = $true)]
        [string]$PathValue
    )

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }

    return (Join-Path $BasePath $PathValue)
}

function Invoke-NativeOrThrow {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandPath,

        [string[]]$Arguments = @(),

        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory = $true)]
        [string]$FailurePrefix
    )

    $startArgs = @{
        FilePath = $CommandPath
        WorkingDirectory = $WorkingDirectory
        NoNewWindow = $true
        Wait = $true
        PassThru = $true
    }

    if ($Arguments.Count -gt 0) {
        $startArgs.ArgumentList = $Arguments
    }

    $process = Start-Process @startArgs

    $exitCode = if ($null -eq $process) { 0 } else { [int]$process.ExitCode }

    if ($exitCode -ne 0) {
        throw "$FailurePrefix (exit code: $exitCode)"
    }
}

function Resolve-UiProjects {
    param(
        [Parameter(Mandatory = $true)]
        [string]$UiRoot,

        [Parameter(Mandatory = $true)]
        [string]$RequestedTest
    )

    if ($RequestedTest -eq "all") {
        $projects = Get-ChildItem -Path $UiRoot -Filter "*.pro" | Sort-Object Name
        if ($projects.Count -eq 0) {
            throw "No UI test project files were found under $UiRoot."
        }
        return @($projects | ForEach-Object { $_.FullName })
    }

    $candidates = @()
    if ([System.IO.Path]::IsPathRooted($RequestedTest)) {
        $candidates += $RequestedTest
    } elseif ($RequestedTest.EndsWith(".pro")) {
        $candidates += (Join-Path $UiRoot $RequestedTest)
    } else {
        $candidates += (Join-Path $UiRoot "$RequestedTest.pro")
        $candidates += (Join-Path $UiRoot $RequestedTest)
    }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return @((Resolve-Path $candidate).Path)
        }
    }

    throw "UI test project '$RequestedTest' was not found under $UiRoot."
}

function Resolve-BuildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootPath,

        [Parameter(Mandatory = $true)]
        [string]$UiRoot,

        [AllowEmptyString()]
        [string]$RequestedBuildDir,

        [Parameter(Mandatory = $true)]
        [string]$ProjectName,

        [Parameter(Mandatory = $true)]
        [bool]$MultipleProjects
    )

    if ([string]::IsNullOrWhiteSpace($RequestedBuildDir)) {
        return (Join-Path $UiRoot (Join-Path "build" $ProjectName))
    }

    $basePath = Resolve-RepoPath -BasePath $RootPath -PathValue $RequestedBuildDir
    if ($MultipleProjects) {
        return (Join-Path $basePath $ProjectName)
    }

    return $basePath
}

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$uiRoot = (Resolve-Path (Join-Path $root "tests\ui")).Path
Set-Location $root

if ($Jobs -lt 1) {
    throw "-Jobs must be at least 1."
}

if ([string]::IsNullOrWhiteSpace($QMakePath)) {
    $QMakePath = Resolve-CommandPath -CommandName "qmake"
}

if ([string]::IsNullOrWhiteSpace($QMakePath)) {
    $QMakePath = Resolve-FirstExistingPath -Candidates @(
        "E:\Qt\Qt5.14.2\5.14.2\mingw73_32\bin\qmake.exe",
        "D:\Qt5\Qt5.14.2\5.14.2\mingw73_32\bin\qmake.exe",
        "C:\Qt\Qt5.14.2\5.14.2\mingw73_32\bin\qmake.exe",
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
        "E:\Qt\Qt5.14.2\Tools\mingw730_32\bin\mingw32-make.exe",
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

$projects = Resolve-UiProjects -UiRoot $uiRoot -RequestedTest $TestName
$multipleProjects = $projects.Count -gt 1
$builtExecutables = New-Object System.Collections.Generic.List[string]

Write-Host "Using qmake: $QMakePath" -ForegroundColor Cyan
Write-Host "Using make: $MakePath" -ForegroundColor Cyan
Write-Host "UI tests root: $uiRoot" -ForegroundColor Cyan
Write-Host "Build config: $Config" -ForegroundColor Cyan
Write-Host "Requested test: $TestName" -ForegroundColor Cyan

foreach ($projectPath in $projects) {
    $projectName = [System.IO.Path]::GetFileNameWithoutExtension($projectPath)
    $buildPath = Resolve-BuildPath `
        -RootPath $root `
        -UiRoot $uiRoot `
        -RequestedBuildDir $BuildDir `
        -ProjectName $projectName `
        -MultipleProjects $multipleProjects

    if (-not (Test-Path $buildPath)) {
        New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
    }

    if ($Clean) {
        Write-Host "Cleaning previous output for $projectName ..." -ForegroundColor Cyan
        Get-ChildItem $buildPath -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    }

    Write-Host "Building UI test: $projectName" -ForegroundColor Yellow
    Write-Host "Project file: $projectPath" -ForegroundColor Yellow
    Write-Host "Build dir: $buildPath" -ForegroundColor Yellow

    Push-Location $buildPath
    try {
        Invoke-NativeOrThrow -CommandPath $QMakePath `
            -Arguments @($projectPath, "CONFIG+=$Config") `
            -WorkingDirectory $buildPath `
            -FailurePrefix "qmake failed for $projectName"

        Invoke-NativeOrThrow -CommandPath $MakePath `
            -Arguments @("-j$Jobs") `
            -WorkingDirectory $buildPath `
            -FailurePrefix "mingw32-make failed for $projectName"
    }
    finally {
        Pop-Location
    }

    $exePath = Join-Path $buildPath "$Config\$projectName.exe"
    if (-not (Test-Path $exePath)) {
        throw "Build finished but executable was not found: $exePath"
    }

    $builtExecutables.Add($exePath) | Out-Null
    Write-Host "Built executable: $exePath" -ForegroundColor Green

    if ($Run) {
        Write-Host "Running UI test: $projectName" -ForegroundColor Yellow
        Invoke-NativeOrThrow `
            -CommandPath $exePath `
            -WorkingDirectory $buildPath `
            -FailurePrefix "UI test failed: $projectName"
    }
}

Write-Host "UI test build finished." -ForegroundColor Green
foreach ($exePath in $builtExecutables) {
    Write-Host "Artifact: $exePath" -ForegroundColor Green
}
