param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [int]$Port = 27889,
    [string]$BinDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BinDirectory)) {
    $BinDirectory = Join-Path $projectRoot "build\standalone\bin\$Configuration"
}
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$serverExe = Join-Path $bin "ExpeditionOnlineServer.exe"
$selfTestExe = Join-Path $bin "ExpeditionOnlineSelfTest.exe"
foreach ($required in @($serverExe, $selfTestExe)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing self-test output: $required" }
}

$logRoot = Join-Path $projectRoot "build\self-test"
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$serverOut = Join-Path $logRoot "server.out.log"
$serverErr = Join-Path $logRoot "server.err.log"
Remove-Item -Force -ErrorAction SilentlyContinue $serverOut, $serverErr

$server = Start-Process -FilePath $serverExe `
    -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--heartbeat", "1", "--timeout", "3" `
    -WorkingDirectory $logRoot -WindowStyle Hidden `
    -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru
try {
    Start-Sleep -Milliseconds 500
    if ($server.HasExited) { throw "Server exited before SelfTest connected" }
    $output = (& $selfTestExe --host 127.0.0.1 --port $Port --server-timeout 3 2>&1) -join "`n"
    Write-Host $output
    if ($LASTEXITCODE -ne 0 -or $output -notmatch 'SELF_TEST PASS') {
        throw "ExpeditionOnlineSelfTest failed"
    }
    Start-Sleep -Milliseconds 250
    $serverLog = Get-Content -Raw -LiteralPath $serverOut
    foreach ($marker in @(
        "SERVER_READY", "PLAYER_CONNECTED", "PLAYER_READY", "PLAYER_ZONE_CHANGE",
        "PLAYER_DISCONNECTED", "PROTOCOL_MISMATCH", "CLIENT_TIMEOUT"
    )) {
        if ($serverLog -notmatch $marker) { throw "Server log is missing $marker" }
    }
    Write-Host "PASS: exploration self-test and friendly server diagnostics verified."
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host "Self-test logs: $logRoot"
}
