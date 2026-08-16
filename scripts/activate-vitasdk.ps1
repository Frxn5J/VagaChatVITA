param(
    [string]$InstallDirectory = $(if ($env:VITASDK) { $env:VITASDK } else { Join-Path $HOME "vitasdk" })
)

$ErrorActionPreference = "Stop"

$resolvedInstallDirectory = [IO.Path]::GetFullPath($InstallDirectory)
$compiler = Join-Path $resolvedInstallDirectory "bin\arm-vita-eabi-gcc.exe"
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "VitaSDK was not found at $resolvedInstallDirectory"
}

$env:VITASDK = $resolvedInstallDirectory
$sdkBin = Join-Path $resolvedInstallDirectory "bin"
$pathEntries = if ($env:Path) { @($env:Path -split ';' | Where-Object { $_ }) } else { @() }
$alreadyPresent = $pathEntries | Where-Object { $_.TrimEnd('\') -ieq $sdkBin.TrimEnd('\') }

if (-not $alreadyPresent) {
    $env:Path = (($sdkBin) + ';' + ($pathEntries -join ';')).TrimEnd(';')
}

Write-Host "VITASDK=$env:VITASDK"
Write-Host "VitaSDK tools are active in this PowerShell session."
