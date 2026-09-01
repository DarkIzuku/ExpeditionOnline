param(
    [Parameter(Mandatory = $true)][string]$BinDirectory,
    [Parameter(Mandatory = $true)][string]$ClientPackageRoot,
    [int]$Port = 27890
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$clientMod = (Resolve-Path -LiteralPath $ClientPackageRoot).Path
$serverExe = Join-Path $bin "ExpeditionOnlineServer.exe"
$doctorExe = Join-Path $bin "ExpeditionOnlineDoctor.exe"
$root = Join-Path $projectRoot "build\doctor-test"
$game = Join-Path $root "game"
$win64 = Join-Path $game "Sandfall\Binaries\Win64"
$ue4ss = Join-Path $win64 "ue4ss"
$installedMod = Join-Path $ue4ss "Mods\ExpeditionOnline"
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Split-Path $installedMod -Parent) | Out-Null
New-Item -ItemType File -Path (Join-Path $ue4ss "UE4SS.dll") -Force | Out-Null
Copy-Item -LiteralPath $clientMod -Destination $installedMod -Recurse

$serverOut = Join-Path $root "server.out.log"
$serverErr = Join-Path $root "server.err.log"
$server = Start-Process -FilePath $serverExe `
    -ArgumentList "--host", "127.0.0.1", "--port", $Port `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru
try {
    Start-Sleep -Milliseconds 500
    if ($server.HasExited) { throw "Doctor test server exited early" }
    $output = (& $doctorExe --game-path $game --host 127.0.0.1 --port $Port 2>&1) -join "`n"
    Write-Host $output
    if ($LASTEXITCODE -ne 0 -or $output -notmatch 'READY TO PLAY') { throw "Doctor did not report READY TO PLAY" }
    foreach ($marker in @("UE4SS detected", "ExpeditionOnline main.dll", "config.ini", "Server reachable", "Protocol compatible")) {
        if ($output -notmatch [regex]::Escape($marker)) { throw "Doctor output is missing $marker" }
    }
    Write-Host "PASS: Doctor detects a complete installation and compatible server."
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}
