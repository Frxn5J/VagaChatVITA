param(
    [string]$InstallDirectory = $(if ($env:VITASDK) { $env:VITASDK } else { Join-Path $HOME "vitasdk" }),
    [string]$BootstrapArchive,
    [string]$BootstrapSha256 = "9413c6ceddb3e836c279993549a1da7e800cd53e9863c5bfbf8ba1cf10215132",
    [switch]$PersistEnvironment
)

$ErrorActionPreference = "Stop"

if (-not ([IO.Path]::IsPathRooted($InstallDirectory) -and (
        $InstallDirectory -match '^[A-Za-z]:[\\/]' -or
        $InstallDirectory -match '^\\\\'
    ))) {
    throw "InstallDirectory must be an absolute path."
}

$resolvedInstallDirectory = [IO.Path]::GetFullPath($InstallDirectory)
if (Test-Path -LiteralPath $resolvedInstallDirectory) {
    throw "The install directory already exists: $resolvedInstallDirectory"
}

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ("vdpm-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try {
    Write-Host "Downloading the official VitaSDK installer..."
    $bootstrap = Join-Path $temporaryDirectory "bootstrap-vitasdk.ps1"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) {
        throw "curl.exe is required to download the VitaSDK bootstrap."
    }
    & $curl.Source `
        --fail `
        --location `
        --silent `
        --show-error `
        --connect-timeout 15 `
        --max-time 60 `
        --output $bootstrap `
        "https://raw.githubusercontent.com/vitasdk/vdpm/v0.1.1/bootstrap-vitasdk.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "Could not download the VitaSDK bootstrap."
    }

    # Windows PowerShell 5.1 does not provide IsPathFullyQualified(). Keep
    # the official bootstrap validation while replacing only that API call.
    $compatibilityBootstrap = Join-Path $temporaryDirectory "bootstrap-vitasdk-compatible.ps1"
    $bootstrapContent = Get-Content -LiteralPath $bootstrap -Raw
    $installPathCheck = '([IO.Path]::IsPathRooted($InstallDirectory) -and ($InstallDirectory -match ''^[A-Za-z]:[\\/]'' -or $InstallDirectory -match ''^\\\\''))'
    $targetPathCheck = '([IO.Path]::IsPathRooted($target) -and ($target -match ''^[A-Za-z]:[\\/]'' -or $target -match ''^\\\\''))'
    $bootstrapContent = $bootstrapContent.Replace(
        '[IO.Path]::IsPathFullyQualified($InstallDirectory)',
        $installPathCheck
    )
    $bootstrapContent = $bootstrapContent.Replace(
        '[IO.Path]::IsPathFullyQualified($target)',
        $targetPathCheck
    )
    $bootstrapContent = $bootstrapContent.Replace(
        'Invoke-WebRequest -Uri $Url -OutFile $downloadedArchive',
        (
            '& curl.exe --fail --location --silent --show-error --connect-timeout 15 --max-time 1800 --output $downloadedArchive $Url' +
            [Environment]::NewLine +
            '    if ($LASTEXITCODE -ne 0) { throw "Could not download the VitaSDK archive." }'
        )
    )
    $bootstrapContent = $bootstrapContent.Replace(
        'Invoke-WebRequest -Uri "$manifestBase/index.json" -OutFile $indexPath',
        (
            '& curl.exe --fail --location --silent --show-error --connect-timeout 15 --max-time 60 --output $indexPath "$manifestBase/index.json"' +
            [Environment]::NewLine +
            '            if ($LASTEXITCODE -ne 0) { throw "Could not download the VitaSDK release index." }'
        )
    )
    $bootstrapContent = $bootstrapContent.Replace(
        'Invoke-WebRequest -Uri "$manifestBase/index.json.sig" -OutFile "${indexPath}.sig"',
        (
            '& curl.exe --fail --location --silent --show-error --connect-timeout 15 --max-time 60 --output "${indexPath}.sig" "$manifestBase/index.json.sig"' +
            [Environment]::NewLine +
            '            if ($LASTEXITCODE -ne 0) { throw "Could not download the VitaSDK release signature." }'
        )
    )
    if ($bootstrapContent.Contains('IsPathFullyQualified')) {
        throw "The downloaded VitaSDK bootstrap uses an unsupported path API."
    }
    if ($bootstrapContent.Contains('Invoke-WebRequest')) {
        throw "The downloaded VitaSDK bootstrap uses an unsupported network API."
    }
    Set-Content -LiteralPath $compatibilityBootstrap -Value $bootstrapContent -Encoding UTF8

    $bootstrapArguments = @('-InstallDirectory', $resolvedInstallDirectory)
    $seedArchive = $null
    if ($BootstrapArchive) {
        $resolvedBootstrapArchive = [IO.Path]::GetFullPath($BootstrapArchive)
        if (-not (Test-Path -LiteralPath $resolvedBootstrapArchive -PathType Leaf)) {
            throw "The bootstrap archive was not found: $resolvedBootstrapArchive"
        }

        # The downloaded vdpm archive is a seed package. Expose it through
        # the official seed environment variables so it follows the signed
        # package installation path instead of being treated as a full SDK.
        $seedArchive = Join-Path $temporaryDirectory "vdpm-seed.tar.bz2"
        Copy-Item -LiteralPath $resolvedBootstrapArchive -Destination $seedArchive
        Set-Content -LiteralPath "${seedArchive}.sha256" -Value "$BootstrapSha256  $seedArchive" -Encoding ASCII
        $env:VITASDK_SEED_ARCHIVE = $seedArchive
        $env:VITASDK_SEED_SHA256 = $BootstrapSha256
    }
    else {
        # Pin the small bootstrap archive so PowerShell does not wait on the
        # checksum sidecar endpoint and the downloaded bytes remain verified.
        $bootstrapUrl = "https://github.com/vitasdk/vdpm/releases/download/v0.1.1/vdpm-0.1.1-x86_64-w64-mingw32.tar.bz2"
        $bootstrapArguments += @('-Url', $bootstrapUrl, '-Sha256', $BootstrapSha256)
    }

    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $compatibilityBootstrap @bootstrapArguments
    }
    finally {
        if ($BootstrapArchive) {
            Remove-Item Env:VITASDK_SEED_ARCHIVE -ErrorAction SilentlyContinue
            Remove-Item Env:VITASDK_SEED_SHA256 -ErrorAction SilentlyContinue
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "VitaSDK installation failed."
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$env:VITASDK = $resolvedInstallDirectory

if ($PersistEnvironment) {
    [Environment]::SetEnvironmentVariable("VITASDK", $resolvedInstallDirectory, "User")

    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $pathEntries = if ($userPath) { @($userPath -split ';' | Where-Object { $_ }) } else { @() }
    $sdkBin = Join-Path $resolvedInstallDirectory "bin"
    $alreadyPresent = $pathEntries | Where-Object { $_.TrimEnd('\') -ieq $sdkBin.TrimEnd('\') }

    if (-not $alreadyPresent) {
        [Environment]::SetEnvironmentVariable("Path", (($pathEntries + $sdkBin) -join ';'), "User")
    }

    Write-Host "VITASDK and PATH were saved for future user sessions."
}

Write-Host "VitaSDK installed at: $resolvedInstallDirectory"
Write-Host "Activate it in this PowerShell session with:"
Write-Host ". .\scripts\activate-vitasdk.ps1 -InstallDirectory `"$resolvedInstallDirectory`""
