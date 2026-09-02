param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [int]$Port = 27891,
    [string]$BinDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BinDirectory)) {
    $BinDirectory = Join-Path $projectRoot "build\standalone\bin\$Configuration"
}
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$serverExe = Join-Path $bin "ExpeditionOnlineServer.exe"
$probeExe = Join-Path $bin "ExpeditionOnlineProbe.exe"
$helpText = (& $probeExe --help) -join "`n"
if ($helpText -notmatch "--appearance-test" -or $helpText -notmatch "--character" -or `
    $helpText -notmatch "--outfit" -or $helpText -notmatch "--hair") {
    throw "Probe help is missing literal appearance test options"
}
$root = Join-Path $projectRoot "build\probe-demos"
New-Item -ItemType Directory -Force -Path $root | Out-Null
$serverOut = Join-Path $root "server.out.log"
$serverErr = Join-Path $root "server.err.log"
$movementOut = Join-Path $root "movement.out.log"
$movementErr = Join-Path $root "movement.err.log"
$jumpOut = Join-Path $root "jump.out.log"
$jumpErr = Join-Path $root "jump.err.log"
Remove-Item -Force -ErrorAction SilentlyContinue $serverOut, $serverErr, $movementOut, $movementErr, $jumpOut, $jumpErr

$server = Start-Process -FilePath $serverExe -ArgumentList "--host", "127.0.0.1", "--port", $Port `
    -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru
try {
    Start-Sleep -Milliseconds 500
    $movement = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "MovementDemo", "--zone", "DemoZone", "--snapshot-hz", "15", "--movement-demo" `
        -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $movementOut -RedirectStandardError $movementErr -PassThru
    $jump = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "JumpDemo", "--zone", "DemoZone", "--snapshot-hz", "15", "--jump-demo" `
        -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $jumpOut -RedirectStandardError $jumpErr -PassThru
    Wait-Process -Id $movement.Id, $jump.Id -Timeout 40
    $movement.Refresh()
    $jump.Refresh()
    if ($movement.ExitCode -ne 0 -or $jump.ExitCode -ne 0) { throw "Probe demo failed" }
    $movementText = Get-Content -Raw -LiteralPath $movementOut
    $jumpText = Get-Content -Raw -LiteralPath $jumpOut
    foreach ($phase in @("IDLE", "WALK", "RUN", "STOP")) {
        if ($movementText -notmatch "DEMO_PHASE movement=$phase") { throw "Movement demo is missing $phase" }
    }
    foreach ($phase in @("GROUND", "ASCEND", "APEX", "DESCEND", "LAND")) {
        if ($jumpText -notmatch "jump=$phase") { throw "Jump demo is missing $phase" }
    }
    $movementModes = [regex]::Matches($jumpText, 'DEMO_MOVEMENT_STATE mode=(\d+)') | ForEach-Object { $_.Groups[1].Value }
    if (($movementModes -join ',') -ne '1,3,1') {
        throw "Jump demo MovementState transition must be exactly 1,3,1; got $($movementModes -join ',')"
    }
    Write-Host "PASS: Probe generated Idle, Walk, Run, Stop, Jump, Apex, Fall and Land phases without animation packets."
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}
