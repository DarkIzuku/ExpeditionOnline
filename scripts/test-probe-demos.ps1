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
if ($helpText -notmatch "--appearance-test" -or $helpText -notmatch "--idle-demo" -or $helpText -notmatch "--char" -or `
    $helpText -notmatch "--customization-skin" -or $helpText -notmatch "--customization-face" -or `
    $helpText -notmatch "--full-exploration-demo" -or $helpText -notmatch "--context" -or `
    $helpText -notmatch "--demo-radius") {
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
$idleOut = Join-Path $root "idle.out.log"
$idleErr = Join-Path $root "idle.err.log"
Remove-Item -Force -ErrorAction SilentlyContinue $serverOut, $serverErr, $movementOut, $movementErr, $jumpOut, $jumpErr, $idleOut, $idleErr

$server = Start-Process -FilePath $serverExe -ArgumentList "--host", "127.0.0.1", "--port", $Port `
    -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru
try {
    Start-Sleep -Milliseconds 500
    $movement = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "MovementDemo", "--zone", "DemoZone", "--context", "exploration", "--snapshot-hz", "15", "--full-exploration-demo", "--crouch-demo" `
        -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $movementOut -RedirectStandardError $movementErr -PassThru
    $jump = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "JumpDemo", "--zone", "DemoZone", "--snapshot-hz", "15", "--jump-demo" `
        -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $jumpOut -RedirectStandardError $jumpErr -PassThru
    $idle = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "IdleDemo", "--zone", "DemoZone", "--snapshot-hz", "4", "--idle-demo", "--duration", "2", "--x", "123", "--y", "456", "--z", "789" `
        -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $idleOut -RedirectStandardError $idleErr -PassThru
    Wait-Process -Id $movement.Id, $jump.Id, $idle.Id -Timeout 40
    $movement.Refresh()
    $jump.Refresh()
    $idle.Refresh()
    if ($movement.ExitCode -ne 0 -or $jump.ExitCode -ne 0 -or $idle.ExitCode -ne 0) { throw "Probe demo failed" }
    $movementText = Get-Content -Raw -LiteralPath $movementOut
    $jumpText = Get-Content -Raw -LiteralPath $jumpOut
    foreach ($phase in @("IDLE", "WALK", "RUN", "SPRINT", "STOP")) {
        if ($movementText -notmatch "DEMO_PHASE movement=$phase") { throw "Movement demo is missing $phase" }
    }
    foreach ($gait in @("gait=1", "gait=2", "gait=3")) {
        if ($movementText -notmatch [regex]::Escape($gait)) { throw "Full demo is missing $gait" }
    }
    if ($movementText -notmatch 'PROBE_SYNTHETIC_GROUND z_is_fixed=true terrain_following=false demo_radius=250') {
        throw "Full demo is missing the fixed-Z terrain warning and conservative radius"
    }
    if ($movementText -notmatch 'stance=1[^\r\n]*crouching=true') {
        throw "Full demo is missing the crouch locomotion state"
    }
    foreach ($phase in @("GROUND", "ASCEND", "APEX", "DESCEND", "LAND")) {
        if ($jumpText -notmatch "jump=$phase") { throw "Jump demo is missing $phase" }
    }
    $movementModes = [regex]::Matches($jumpText, 'DEMO_MOVEMENT_STATE mode=(\d+)') | ForEach-Object { $_.Groups[1].Value }
    if (($movementModes -join ',') -ne '1,3,1') {
        throw "Jump demo MovementState transition must be exactly 1,3,1; got $($movementModes -join ',')"
    }
    $jumpEvents = [regex]::Matches($jumpText, 'DEMO_JUMP_EVENT sequence=(\d+)')
    if ($jumpEvents.Count -ne 1 -or $jumpEvents[0].Groups[1].Value -ne '1') {
        throw "Jump demo must produce exactly one JumpEvent with sequence 1"
    }
    if ($jumpText.IndexOf('DEMO_JUMP_EVENT sequence=1') -gt $jumpText.IndexOf('DEMO_MOVEMENT_STATE mode=3')) {
        throw "JumpEvent must precede airborne MovementState"
    }
    $idleText = Get-Content -Raw -LiteralPath $idleOut
    if ($idleText -notmatch 'idle_demo=true' -or $idleText -notmatch 'radius=0') {
        throw "Idle demo did not force a constant transform"
    }
    Write-Host "PASS: Probe generated Idle, Walk, Run, Stop and one JumpEvent before the airborne phase; idle mode kept a constant transform."
}
finally {
    if ($idle -and -not $idle.HasExited) { Stop-Process -Id $idle.Id -Force }
    if ($jump -and -not $jump.HasExited) { Stop-Process -Id $jump.Id -Force }
    if ($movement -and -not $movement.HasExited) { Stop-Process -Id $movement.Id -Force }
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}
