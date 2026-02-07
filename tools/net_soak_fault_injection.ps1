param(
    [string]$BinaryPath = ".\build\Debug\novaria_net_soak.exe",
    [int]$Ticks = 6000
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BinaryPath)) {
    throw "Soak binary not found: $BinaryPath"
}

$logRoot = Join-Path $env:TEMP "novaria-net-soak-fault"
if (Test-Path $logRoot) {
    Remove-Item -Recurse -Force $logRoot
}
New-Item -ItemType Directory -Force $logRoot | Out-Null

Write-Host "[INFO] Starting fault injection soak (pause injection on client)."

$hostOutLog = Join-Path $logRoot "host.out.log"
$hostErrLog = Join-Path $logRoot "host.err.log"
$clientOutLog = Join-Path $logRoot "client.out.log"
$clientErrLog = Join-Path $logRoot "client.err.log"

$hostArgs = @(
    "--role", "host",
    "--local-host", "127.0.0.1",
    "--local-port", "27200",
    "--remote-host", "127.0.0.1",
    "--remote-port", "27201",
    "--ticks", "$Ticks",
    "--payload-interval", "30",
    "--allow-timeout-disconnects", "1"
)

$clientArgs = @(
    "--role", "client",
    "--local-host", "127.0.0.1",
    "--local-port", "27201",
    "--remote-host", "127.0.0.1",
    "--remote-port", "27200",
    "--ticks", "$Ticks",
    "--payload-interval", "30",
    "--allow-timeout-disconnects", "1",
    "--inject-pause-tick", "2000",
    "--inject-pause-ms", "4000"
)

$hostProc = Start-Process -FilePath $BinaryPath -ArgumentList $hostArgs `
    -RedirectStandardOutput $hostOutLog -RedirectStandardError $hostErrLog `
    -PassThru -WindowStyle Hidden
$clientProc = Start-Process -FilePath $BinaryPath -ArgumentList $clientArgs `
    -RedirectStandardOutput $clientOutLog -RedirectStandardError $clientErrLog `
    -PassThru -WindowStyle Hidden

$hostProc.WaitForExit()
$clientProc.WaitForExit()

$hostOut = Get-Content -Raw $hostOutLog
$clientOut = Get-Content -Raw $clientOutLog
if (($hostOut -notmatch "\[PASS\] novaria_net_soak") -or
    ($clientOut -notmatch "\[PASS\] novaria_net_soak")) {
    Write-Host "[FAIL] Fault injection soak failed. Logs: $logRoot"
    exit 1
}

Write-Host "[PASS] Fault injection soak passed. Logs: $logRoot"
