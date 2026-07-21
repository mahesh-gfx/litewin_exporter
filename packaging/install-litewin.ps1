#requires -Version 2.0
<#
.SYNOPSIS
    Installs OR upgrades litewin_exporter (a lightweight Prometheus metrics
    exporter) on a Windows host, and firewalls its metrics port to the
    specified Prometheus scraper(s).

    UPGRADING IS JUST BUMPING THE VERSION. Set -LitewinBaseUrl once (its default
    below), then to move the fleet to a new build re-run with a new
    -LitewinVersion. The script:
      * builds the zip URL from base + version (naming convention),
      * checks the currently-installed version,
      * only downloads + reinstalls when the version differs (or -Force),
      * always reconciles the firewall rules (so scraper changes take effect
        even without a version bump).
    This makes it safe to run repeatedly / on a schedule with no needless
    service restarts.

    Compatible with Windows PowerShell 2.0+ (Windows 7 / Server 2008 R2 and
    every later Windows client and server release).

.PARAMETER ScraperIPs
    IP(s) of the Prometheus server(s) allowed to scrape this host.
    Comma-separated list accepted. Alias: -ScraperIP.

.PARAMETER LitewinVersion
    The version to install or upgrade to, e.g. "0.5.0". THIS IS THE KNOB TO
    BUMP. Combined with -LitewinBaseUrl to form the zip URL:
        {BaseUrl}/litewin_exporter-v{Version}.zip

.PARAMETER LitewinBaseUrl
    Your fileserver directory holding the release zips. Set this once (edit the
    default below), then only -LitewinVersion changes between upgrades.

.PARAMETER LitewinUrl
    Full zip URL override (ignores BaseUrl+Version). Optional.

.PARAMETER LitewinZipPath
    Local/UNC path to a pre-staged zip (ignores URL). Optional.

.PARAMETER VerifyChecksum
    Fetch the ".sha256" sidecar next to the zip and verify integrity before
    install. Recommended over plain HTTP. (Integrity, not authenticity: over
    unauthenticated HTTP a determined attacker could replace both files. For
    tamper protection, rely on the binary's code signature.)

.PARAMETER LitewinSha256
    Pin an explicit expected hash instead of the sidecar. Overrides
    -VerifyChecksum. (Note: pinning means you must bump this too on upgrade,
    which is why -VerifyChecksum is usually the better choice for a bump-only
    workflow.)

.PARAMETER LitewinPort
    TCP /metrics port. Default 9183.

.PARAMETER AllowPing
    Also add a scoped ICMP echo (ping) allow rule for the scraper(s).

.PARAMETER Force
    Reinstall even if the target version is already installed.

.EXAMPLE
    # First install
    powershell -ExecutionPolicy Bypass -File install-litewin.ps1 -ScraperIPs 10.20.30.50 -LitewinVersion 0.4.0 -VerifyChecksum

.EXAMPLE
    # Upgrade the whole fleet later: identical command, bumped version
    powershell -ExecutionPolicy Bypass -File install-litewin.ps1 -ScraperIPs 10.20.30.50 -LitewinVersion 0.5.0 -VerifyChecksum
#>
param(
    [Parameter(Mandatory=$true)]
    [Alias('ScraperIP')]
    [string[]]$ScraperIPs,

    # The version to install/upgrade to. This is the knob to bump.
    [string]$LitewinVersion,

    # Set this ONCE to your fileserver's release directory.
    [string]$LitewinBaseUrl = "http://fileserver.lan/tools",

    # Optional overrides.
    [string]$LitewinUrl,
    [string]$LitewinZipPath,

    # Integrity options.
    [switch]$VerifyChecksum,
    [string]$LitewinSha256,

    [int]$LitewinPort = 9183,
    [switch]$AllowPing,
    [switch]$Force
)

# -- helpers ---------------------------------------------------------------
function Normalize-Version($v) {
    if ($v) { return ($v -replace '^[vV]','').Trim() }
    return $null
}
function Get-Sha256Hex($path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $bytes = [System.IO.File]::ReadAllBytes($path)
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-","").ToLower()
}
function New-WebClientNoProxy {
    # If the URL is HTTPS, Win7 + old .NET default to TLS 1.0 and fail; force
    # TLS 1.2 best-effort (3072 = Tls12). Harmless no-op for http://.
    try {
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    } catch { }
    $wc = New-Object System.Net.WebClient
    $wc.Headers.Add("User-Agent", "litewin-setup")
    # Don't route an internal fileserver fetch through a configured system proxy.
    $wc.Proxy = $null
    return $wc
}

# -- normalize scrapers ----------------------------------------------------
# When invoked via `powershell.exe -File`, commas are NOT split, so
# "1.2.3.4,5.6.7.8" arrives as one element. Split them ourselves.
$ScraperIPs = $ScraperIPs | ForEach-Object { $_ -split '\s*,\s*' } | Where-Object { $_ }
if (-not $ScraperIPs -or $ScraperIPs.Count -eq 0) { throw "-ScraperIPs resolved to an empty list." }
$scraperList = ($ScraperIPs -join ",")

# -- resolve target version and zip source --------------------------------
$targetVer = Normalize-Version $LitewinVersion
$resolvedUrl = $null

if ($LitewinZipPath) {
    if (-not $targetVer) {
        $m = [regex]::Match([System.IO.Path]::GetFileName($LitewinZipPath), '(\d+\.\d+\.\d+)')
        if ($m.Success) { $targetVer = $m.Groups[1].Value }
    }
} elseif ($LitewinUrl) {
    $resolvedUrl = $LitewinUrl
    if (-not $targetVer) {
        $m = [regex]::Match($LitewinUrl, '(\d+\.\d+\.\d+)')
        if ($m.Success) { $targetVer = $m.Groups[1].Value }
    }
} else {
    if (-not $targetVer) { throw "Provide -LitewinVersion (e.g. 0.5.0), or a full -LitewinUrl / -LitewinZipPath." }
    if (-not $LitewinBaseUrl) { throw "Set -LitewinBaseUrl to your fileserver directory (or edit its default at the top of the script)." }
    $base = $LitewinBaseUrl.TrimEnd('/')
    $resolvedUrl = "{0}/litewin_exporter-v{1}.zip" -f $base, $targetVer
}

# -- must be elevated ------------------------------------------------------
$__principal = New-Object System.Security.Principal.WindowsPrincipal([System.Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $__principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "This script must be run as Administrator (elevated PowerShell / cmd)."
}

Write-Host "=== litewin_exporter install / upgrade ==="

# -- what's installed now? -------------------------------------------------
$installDir   = Join-Path ${env:ProgramFiles} "litewin_exporter"
$installedExe = Join-Path $installDir "litewin_exporter.exe"
$installedVer = $null
if (Test-Path $installedExe) {
    try {
        # --version parses args and exits before touching the socket/service,
        # so this is safe to run even while the service is active.
        $out = (& $installedExe --version 2>&1) -join ' '
        $m = [regex]::Match($out, '(\d+\.\d+\.\d+)')
        if ($m.Success) { $installedVer = $m.Groups[1].Value }
    } catch { }
}
$svc = Get-Service -Name "litewin_exporter" -ErrorAction SilentlyContinue

if ($installedVer) { Write-Host ("Installed : {0}" -f $installedVer) } else { Write-Host "Installed : (none)" }
if ($targetVer)    { Write-Host ("Target    : {0}" -f $targetVer) }    else { Write-Host "Target    : (version unknown from source)" }

# -- decide whether to (re)install ----------------------------------------
$needInstall = $true
if (-not $Force -and $targetVer -and ($installedVer -eq $targetVer) -and $svc) {
    $needInstall = $false
}

if (-not $needInstall) {
    Write-Host "Already at target version; skipping download/install (use -Force to reinstall)."
} else {
    if ($installedVer -and $targetVer -and ($installedVer -ne $targetVer)) {
        Write-Host ("Upgrading {0} -> {1}" -f $installedVer, $targetVer)
    }

    # -- stage --------------------------------------------------------------
    $staging = Join-Path $env:TEMP "litewin_stage"
    if (Test-Path $staging) { Remove-Item -Path $staging -Recurse -Force -ErrorAction SilentlyContinue }
    New-Item -Path $staging -ItemType Directory -Force | Out-Null
    $zipPath = Join-Path $staging "litewin.zip"

    # -- fetch (WebClient; Invoke-WebRequest is PS 3.0+) --------------------
    if ($LitewinZipPath) {
        Write-Host ("Using local zip: {0}" -f $LitewinZipPath)
        if (!(Test-Path $LitewinZipPath)) { throw "LitewinZipPath not found: $LitewinZipPath" }
        Copy-Item -Path $LitewinZipPath -Destination $zipPath -Force
    } else {
        Write-Host ("Downloading {0}" -f $resolvedUrl)
        try {
            $wc = New-WebClientNoProxy
            $wc.DownloadFile($resolvedUrl, $zipPath)
        } catch {
            throw ("Download failed from {0}: {1}. Confirm the fileserver serves that exact path over HTTP (open it in a browser), the version exists, and the host resolves. For an unreachable host, pre-stage the zip and use -LitewinZipPath." -f $resolvedUrl, $_.Exception.Message)
        }
        # Guard against a 200 that is really an HTML 404/index page: check the
        # ZIP magic bytes ("PK") before trusting the file.
        $fsr = [System.IO.File]::OpenRead($zipPath)
        try { $b0 = $fsr.ReadByte(); $b1 = $fsr.ReadByte() } finally { $fsr.Close() }
        if ($b0 -ne 0x50 -or $b1 -ne 0x4B) {
            throw ("Downloaded file is not a zip (bad signature). The fileserver likely returned an error/index page instead of {0}." -f $resolvedUrl)
        }
    }

    # -- integrity (System.Security.Cryptography; Get-FileHash is PS 4.0+) --
    $expectedHash = $null
    if ($LitewinSha256) {
        $expectedHash = $LitewinSha256.ToLower()
    } elseif ($VerifyChecksum) {
        $sidecarText = $null
        if ($resolvedUrl) {
            try { $sidecarText = (New-WebClientNoProxy).DownloadString($resolvedUrl + ".sha256") }
            catch { throw ("Could not fetch checksum sidecar {0}.sha256: {1}" -f $resolvedUrl, $_.Exception.Message) }
        } elseif ($LitewinZipPath -and (Test-Path ($LitewinZipPath + ".sha256"))) {
            $sidecarText = (Get-Content ($LitewinZipPath + ".sha256")) -join " "
        }
        if ($sidecarText) {
            $hm = [regex]::Match($sidecarText, '([0-9a-fA-F]{64})')
            if ($hm.Success) { $expectedHash = $hm.Groups[1].Value.ToLower() }
        }
        if (-not $expectedHash) { throw "-VerifyChecksum set but no valid SHA-256 sidecar was found." }
    }
    if ($expectedHash) {
        Write-Host "Verifying SHA-256..."
        $actual = Get-Sha256Hex $zipPath
        if ($actual -ne $expectedHash) { throw ("SHA-256 mismatch. expected {0} got {1}" -f $expectedHash, $actual) }
        Write-Host "  OK"
    }

    # -- extract (Shell.Application; Expand-Archive is PS 5.0+) -------------
    Write-Host "Extracting..."
    $extract = Join-Path $staging "unzipped"
    New-Item -Path $extract -ItemType Directory -Force | Out-Null
    $shell = New-Object -ComObject Shell.Application
    # flags: 0x10 = "Yes to All", 0x04 = no progress dialog
    $shell.NameSpace($extract).CopyHere($shell.NameSpace($zipPath).Items(), 0x14)

    $installBat = $null
    for ($i = 0; $i -lt 30 -and -not $installBat; $i++) {
        $installBat = Get-ChildItem -Path $extract -Recurse -Filter "install.bat" -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $installBat) { Start-Sleep -Seconds 1 }
    }
    if (-not $installBat) { throw "install.bat not found in the zip after extraction" }

    # -- install (stops/replaces/recreates the service; inherits elevation) -
    # Pass ONLY the port; the scraper IP list is applied to the firewall below
    # (cmd.exe batch args mangle comma-separated values).
    Write-Host ("Running installer (service + port {0})..." -f $LitewinPort)
    & $installBat.FullName $LitewinPort
    if ($LASTEXITCODE -ne 0) { throw ("install.bat failed with exit code {0}" -f $LASTEXITCODE) }

    # Confirm the running binary is now the target version.
    if (Test-Path $installedExe) {
        try {
            $out2 = (& $installedExe --version 2>&1) -join ' '
            $vm = [regex]::Match($out2, '(\d+\.\d+\.\d+)')
            if ($vm.Success) { Write-Host ("Now installed: {0}" -f $vm.Groups[1].Value) }
        } catch { }
    }

    Remove-Item -Path $staging -Recurse -Force -ErrorAction SilentlyContinue
}

# -- firewall: always reconcile (idempotent) ------------------------------
Write-Host ("Restricting metrics port {0} to scraper(s): {1}" -f $LitewinPort, $scraperList)
netsh advfirewall firewall delete rule name="litewin_exporter" | Out-Null
netsh advfirewall firewall add rule name="litewin_exporter" dir=in action=allow protocol=TCP localport=$LitewinPort remoteip="$scraperList" | Out-Null

if ($AllowPing) {
    Write-Host ("Allowing ICMP echo (ping) from scraper(s): {0}" -f $scraperList)
    netsh advfirewall firewall delete rule name="litewin_exporter ICMP Echo" | Out-Null
    netsh advfirewall firewall add rule name="litewin_exporter ICMP Echo" dir=in action=allow protocol="icmpv4:8,any" remoteip="$scraperList" | Out-Null
} else {
    # Remove a previously-added ping rule if the operator dropped -AllowPing.
    netsh advfirewall firewall delete rule name="litewin_exporter ICMP Echo" | Out-Null
}

Write-Host ("Done. Test from a scraper: curl http://{0}:{1}/metrics" -f $env:COMPUTERNAME, $LitewinPort)