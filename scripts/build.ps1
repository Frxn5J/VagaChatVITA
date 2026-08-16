param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not $env:VITASDK) {
    throw "VITASDK is not defined. Run . .\scripts\activate-vitasdk.ps1 first."
}

$toolchain = Join-Path $env:VITASDK "share\vita.toolchain.cmake"
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "The VitaSDK toolchain was not found at $toolchain"
}

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmake) {
    $candidates = @(
        (Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "CMake\bin\cmake.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\CMake\bin\cmake.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $cmake = $candidate
            break
        }
    }
}
if (-not $cmake) {
    throw "CMake is required. Install CMake and open a new PowerShell session."
}

$ninjaCommand = Get-Command ninja.exe -ErrorAction SilentlyContinue
$ninja = if ($ninjaCommand) { $ninjaCommand.Source } else { $null }
if (-not $ninja) {
    $ninjaCandidates = @(
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
    )
    foreach ($candidate in $ninjaCandidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $ninja = $candidate
            break
        }
    }
}
if (-not $ninja) {
    throw "Ninja is required for the Vita cross-compiler. Install it with Visual Studio or add ninja.exe to PATH."
}

$ninjaDirectory = Split-Path -Parent $ninja
$env:Path = "$ninjaDirectory;$env:Path"

$buildDirectory = Join-Path $projectRoot "build"

Write-Host "Configuring Vita project..."
& $cmake -G Ninja -S $projectRoot -B $buildDirectory "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Building $Configuration..."
& $cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$package = Join-Path $buildDirectory "VagaChatVITA.vpk"
if (Test-Path -LiteralPath $package) {
    Write-Host "VPK created at: $package"
}
