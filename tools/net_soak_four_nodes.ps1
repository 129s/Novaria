param(
    [string]$BinaryPath = ".\build\Debug\novaria_net_soak.exe",
    [int]$Ticks = 108000,
    [int]$PayloadInterval = 30
)

$ErrorActionPreference = "Stop"

function Start-SoakProcess {
    param(
        [string]$Role,
        [string]$LocalHost,
        [int]$LocalPort,
        [string]$RemoteHost,
        [int]$RemotePort,
        [string]$LogPrefix
    )

    $arguments = @(
        "--role", $Role,
        "--local-host", $LocalHost,
        "--local-port", "$LocalPort",
        "--remote-host", $RemoteHost,
        "--remote-port", "$RemotePort",
        "--ticks", "$Ticks",
        "--payload-interval", "$PayloadInterval"
    )

    $stdoutLog = "$LogPrefix.out.log"
    $stderrLog = "$LogPrefix.err.log"

    $process = Start-Process -FilePath $BinaryPath -ArgumentList $arguments `
        -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog `
        -PassThru -WindowStyle Hidden

    return [PSCustomObject]@{
        Process = $process
        StdoutLog = $stdoutLog
        StderrLog = $stderrLog
    }
}

if (-not (Test-Path $BinaryPath)) {
    throw "Soak binary not found: $BinaryPath"
}

$logRoot = Join-Path $env:TEMP "novaria-net-soak-four-nodes"
if (Test-Path $logRoot) {
    Remove-Item -Recurse -Force $logRoot
}
New-Item -ItemType Directory -Force $logRoot | Out-Null

Write-Host "[INFO] Starting four-node soak (2 independent pairs)."

$processes = @()
$processes += Start-SoakProcess -Role host -LocalHost "127.0.0.1" -LocalPort 27000 -RemoteHost "127.0.0.1" -RemotePort 27001 -LogPrefix (Join-Path $logRoot "pair_a_host")
$processes += Start-SoakProcess -Role client -LocalHost "127.0.0.1" -LocalPort 27001 -RemoteHost "127.0.0.1" -RemotePort 27000 -LogPrefix (Join-Path $logRoot "pair_a_client")
$processes += Start-SoakProcess -Role host -LocalHost "127.0.0.1" -LocalPort 27100 -RemoteHost "127.0.0.1" -RemotePort 27101 -LogPrefix (Join-Path $logRoot "pair_b_host")
$processes += Start-SoakProcess -Role client -LocalHost "127.0.0.1" -LocalPort 27101 -RemoteHost "127.0.0.1" -RemotePort 27100 -LogPrefix (Join-Path $logRoot "pair_b_client")

$failed = $false
foreach ($proc in $processes) {
    $proc.Process.WaitForExit()
    $stdoutContent = Get-Content -Raw $proc.StdoutLog
    if ($stdoutContent -notmatch "\[PASS\] novaria_net_soak") {
        $failed = $true
        Write-Host "[ERROR] Process failed: pid=$($proc.Process.Id)"
    } else {
        Write-Host "[INFO] Process passed: pid=$($proc.Process.Id)"
    }
}

if ($failed) {
    Write-Host "[FAIL] Four-node soak failed. Logs: $logRoot"
    exit 1
}

Write-Host "[PASS] Four-node soak passed. Logs: $logRoot"
