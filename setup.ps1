# FoundryVision — install/setup.ps1
# Run once: powershell -ExecutionPolicy Bypass -File install\setup.ps1
# Requires: Windows 10/11, internet access

$ErrorActionPreference = "Stop"

function step  { Write-Host "`n$($args[0])" -ForegroundColor Cyan }
function ok    { Write-Host "  ok  $($args[0])" -ForegroundColor Green }
function warn  { Write-Host "  --  $($args[0])" -ForegroundColor Yellow }
function fail  { Write-Host "  !!  $($args[0])" -ForegroundColor Red }

Write-Host @"

  FoundryVision — setup
  ─────────────────────
"@ -ForegroundColor Cyan

# ─── Python ──────────────────────────────────────────────────────────────────
step "Python"
$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) {
    ok (python --version)
} else {
    warn "Not found. Installing Python 3.12 via winget..."
    winget install Python.Python.3.12 --silent --accept-package-agreements
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH","Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("PATH","User")
    ok "Python installed"
}

# ─── Python packages ──────────────────────────────────────────────────────────
step "Python packages"
python -m pip install --upgrade pip --quiet
python -m pip install websockets requests pyserial rich --quiet
ok "websockets requests pyserial rich"

# ─── Foundry Local ────────────────────────────────────────────────────────────
step "Microsoft Foundry Local"
$foundry = Get-Command foundry -ErrorAction SilentlyContinue
if ($foundry) {
    ok "Already installed"
} else {
    warn "Installing via winget (this may take a minute)..."
    winget install Microsoft.FoundryLocal --silent --accept-package-agreements
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH","Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("PATH","User")
    ok "Foundry Local installed"
}

# ─── LLM model ────────────────────────────────────────────────────────────────
step "LLM model (phi3.5-mini-instruct, ~2 GB — downloads once)"
try {
    foundry model run phi3.5-mini-instruct --pull-only
    ok "Model cached"
} catch {
    warn "Auto-pull failed. Run manually after setup:"
    warn "  foundry model run phi3.5-mini-instruct"
}

# ─── Local IP ─────────────────────────────────────────────────────────────────
step "Network"
$ip = (
    Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notmatch "^127\." -and $_.PrefixOrigin -ne "WellKnown" } |
    Select-Object -First 1
).IPAddress

if ($ip) {
    ok "Local IP: $ip"
} else {
    $ip = "?.?.?.?"
    warn "Could not detect IP — run ipconfig to find it"
}

# ─── Launch scripts ───────────────────────────────────────────────────────────
step "Creating launch scripts"

@"
# Start Foundry LLM server
foundry model run phi3.5-mini-instruct
"@ | Out-File "start_foundry.ps1" -Encoding UTF8

@"
# Start FoundryVision host (WiFi UDP)
# Dashboard → http://localhost:8080
python -m host.server
"@ | Out-File "start_server.ps1" -Encoding UTF8

@"
# Start FoundryVision host (Serial/USB)
# Change COM3 to your port (Device Manager > Ports)
python -m host.server --serial COM3
"@ | Out-File "start_server_serial.ps1" -Encoding UTF8

ok "start_foundry.ps1  start_server.ps1  start_server_serial.ps1"

# ─── Logs dir ─────────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force -Path "host\logs" | Out-Null

# ─── Done ─────────────────────────────────────────────────────────────────────
Write-Host @"

  ─────────────────────────────────────────────
  Setup complete.

  Before flashing the ESP32-S3, edit:
    esp\main\fv_config.h

  Set:
    FV_WIFI_SSID      your network name
    FV_WIFI_PASSWORD  your password
    FV_HOST_IP        $ip

  Then:
    Terminal 1  .\start_foundry.ps1
    Terminal 2  .\start_server.ps1
    Browser     http://localhost:8080
  ─────────────────────────────────────────────

"@ -ForegroundColor Green
