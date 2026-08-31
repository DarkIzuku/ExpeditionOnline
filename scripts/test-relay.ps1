param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [int]$Port = 27888,
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

if (-not (Test-Path -LiteralPath $serverExe) -or -not (Test-Path -LiteralPath $probeExe)) {
    throw "Missing server or probe in $bin"
}

$logRoot = Join-Path $projectRoot "build\integration"
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$serverOut = Join-Path $logRoot "server.out.log"
$serverErr = Join-Path $logRoot "server.err.log"
$probeAOut = Join-Path $logRoot "probe-a.out.log"
$probeAErr = Join-Path $logRoot "probe-a.err.log"
$probeBOut = Join-Path $logRoot "probe-b.out.log"
$probeBErr = Join-Path $logRoot "probe-b.err.log"
Remove-Item -Force -ErrorAction SilentlyContinue $serverOut, $serverErr, $probeAOut, $probeAErr, $probeBOut, $probeBErr

$server = Start-Process -FilePath $serverExe `
    -ArgumentList "--host", "127.0.0.1", "--port", $Port `
    -WorkingDirectory $logRoot -WindowStyle Hidden `
    -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru

try {
    Start-Sleep -Milliseconds 750
    if ($server.HasExited) { throw "Server exited before probes connected" }

    $probeA = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "ProbeA", "--zone", "SharedZone", "--duration", "3" `
        -WorkingDirectory $logRoot -WindowStyle Hidden `
        -RedirectStandardOutput $probeAOut -RedirectStandardError $probeAErr -PassThru
    Start-Sleep -Milliseconds 400
    $probeB = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "ProbeB", "--zone", "SharedZone", "--duration", "5" `
        -WorkingDirectory $logRoot -WindowStyle Hidden `
        -RedirectStandardOutput $probeBOut -RedirectStandardError $probeBErr -PassThru

    Wait-Process -Id $probeA.Id, $probeB.Id -Timeout 20
    $probeA.Refresh()
    $probeB.Refresh()
    if ($probeA.ExitCode -ne 0 -or $probeB.ExitCode -ne 0) {
        throw "A probe failed: A=$($probeA.ExitCode), B=$($probeB.ExitCode)"
    }

    $a = Get-Content -Raw -LiteralPath $probeAOut
    $b = Get-Content -Raw -LiteralPath $probeBOut
    $aWelcome = [regex]::Match($a, 'RECV Welcome[^\r\n]*player=(\d+)')
    $bWelcome = [regex]::Match($b, 'RECV Welcome[^\r\n]*player=(\d+)')
    if (-not $aWelcome.Success -or -not $bWelcome.Success) { throw "Both probes must receive Welcome" }
    $idA = $aWelcome.Groups[1].Value
    $idB = $bWelcome.Groups[1].Value
    if ($idA -eq $idB) { throw "Probes received duplicate player IDs" }

    $checks = @(
        @{ Text = $a; Pattern = "RECV PlayerJoined[^`r`n]*player=$idB"; Name = "Probe A PlayerJoined" },
        @{ Text = $b; Pattern = "RECV PlayerJoined[^`r`n]*player=$idA"; Name = "Probe B PlayerJoined" },
        @{ Text = $a; Pattern = "RECV ZoneState[^`r`n]*player=$idB[^`r`n]*zone=SharedZone"; Name = "Probe A ZoneState" },
        @{ Text = $b; Pattern = "RECV ZoneState[^`r`n]*player=$idA[^`r`n]*zone=SharedZone"; Name = "Probe B ZoneState" },
        @{ Text = $a; Pattern = "RECV AppearanceState[^`r`n]*player=$idB"; Name = "Probe A AppearanceState" },
        @{ Text = $b; Pattern = "RECV AppearanceState[^`r`n]*player=$idA"; Name = "Probe B AppearanceState" },
        @{ Text = $a; Pattern = "RECV TransformSnapshot[^`r`n]*player=$idB"; Name = "Probe A TransformSnapshot" },
        @{ Text = $b; Pattern = "RECV TransformSnapshot[^`r`n]*player=$idA"; Name = "Probe B TransformSnapshot" },
        @{ Text = $b; Pattern = "RECV PlayerLeft[^`r`n]*player=$idA"; Name = "Probe B PlayerLeft" }
    )
    foreach ($check in $checks) {
        if ($check.Text -notmatch $check.Pattern) { throw "Missing integration event: $($check.Name)" }
    }

    if ($a -match 'FATAL|timeout' -or $b -match 'FATAL|timeout') { throw "Unexpected probe error or timeout" }
    Write-Host "PASS: IDs $idA/$idB; PlayerJoined, zone, appearance, transforms and PlayerLeft verified."
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host "Integration logs: $logRoot"
}
