# nexchat installer for Windows, in PowerShell:
#
#   irm https://1ay1.github.io/nexchat/install.ps1 | iex
#
# Installs (or updates to) the latest release for this user: no admin
# prompt, Start menu entry, irc:// links, an uninstaller in Settings > Apps.
# The download is checked against the release's SHA256SUMS.txt.
# A version:  $env:NEXCHAT_VERSION = '0.3.1'; irm ... | iex
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is many times faster without the bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repo = 'https://github.com/1ay1/nexchat'

# throw, not exit: under `irm | iex`, exit would close the user's window
function Fail($msg) { throw "nexchat: $msg" }

if (-not [Environment]::Is64BitOperatingSystem) { Fail 'nexchat needs 64-bit Windows.' }

# the latest version: GitHub's API, or else where /releases/latest leads
function Get-Latest {
    try { return (Invoke-RestMethod -UseBasicParsing "https://api.github.com/repos/1ay1/nexchat/releases/latest").tag_name } catch { }
    $r = Invoke-WebRequest -UseBasicParsing "$repo/releases/latest"
    if ($r.BaseResponse.ResponseUri) { return $r.BaseResponse.ResponseUri.AbsoluteUri }      # Windows PowerShell 5
    return $r.BaseResponse.RequestMessage.RequestUri.AbsoluteUri                              # PowerShell 7
}
$version = $env:NEXCHAT_VERSION
if (-not $version) {
    try { $version = (Get-Latest) -split '/v' | Select-Object -Last 1 } catch { Fail "can't reach GitHub: $_" }
}
$version = "$version".TrimStart('v')
if ($version -notmatch '^\d+\.\d+') { Fail "couldn't find the latest release ($version)" }
$dl = "$repo/releases/download/v$version"
$file = "nexchat-$version-windows-x64-setup.exe"
$tmp = Join-Path ([IO.Path]::GetTempPath()) "nexchat-install-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $tmp | Out-Null

try {
    Write-Host "nexchat $version" -ForegroundColor White
    Write-Host "==> Downloading $file"
    Invoke-WebRequest -UseBasicParsing -Uri "$dl/$file" -OutFile "$tmp\$file"
    $sums = (Invoke-WebRequest -UseBasicParsing -Uri "$dl/SHA256SUMS.txt").Content
    $want = ($sums -split "`n" | Where-Object { $_ -match "\s\.?/?$([regex]::Escape($file))\s*$" } |
             ForEach-Object { ($_ -split '\s+')[0] }) | Select-Object -First 1
    if (-not $want) { Fail "$file isn't in SHA256SUMS.txt" }
    $got = (Get-FileHash -Algorithm SHA256 "$tmp\$file").Hash.ToLower()
    if ($got -ne $want.ToLower()) { Fail "checksum mismatch (got $got, want $want)" }

    Write-Host '==> Installing for this user'
    # per user, no wizard; a running nexchat is closed and reopened after
    $p = Start-Process -FilePath "$tmp\$file" -Wait -PassThru -ArgumentList `
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/CLOSEAPPLICATIONS', '/RESTARTAPPLICATIONS', '/SP-'
    if ($p.ExitCode -ne 0) { Fail "the installer stopped (exit code $($p.ExitCode))" }
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "nexchat $version is installed." -ForegroundColor Green
Write-Host 'Start it from the Start menu. Update: run this again.'
Write-Host 'Your HexChat settings are picked up on first start.' -ForegroundColor DarkGray
