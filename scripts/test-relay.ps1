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

$help = (& $probeExe --help 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "Probe --help failed with exit code $LASTEXITCODE" }
foreach ($option in "--x", "--y", "--z", "--yaw", "--radius") {
    if ($help -notmatch [regex]::Escape($option)) { throw "Probe --help is missing $option" }
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
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "ProbeA", "--zone", "SharedZone", "--duration", "3", "--x", "100", "--y", "200", "--z", "300", "--yaw", "45", "--radius", "25" `
        -WorkingDirectory $logRoot -WindowStyle Hidden `
        -RedirectStandardOutput $probeAOut -RedirectStandardError $probeAErr -PassThru
    Start-Sleep -Milliseconds 400
    $probeB = Start-Process -FilePath $probeExe `
        -ArgumentList "--host", "127.0.0.1", "--port", $Port, "--name", "ProbeB", "--zone", "SharedZone", "--duration", "5", "--x", "-400", "--y", "-500", "--z", "-600", "--yaw", "90", "--radius", "40" `
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

    function Assert-RemoteTransform {
        param(
            [string]$Text,
            [string]$PlayerId,
            [double]$BaseX,
            [double]$BaseY,
            [double]$BaseZ,
            [double]$Yaw,
            [double]$Radius,
            [string]$Name
        )
        $match = [regex]::Match($Text, "RECV TransformSnapshot[^`r`n]*player=$PlayerId[^`r`n]*xyz=([-+0-9.eE]+),([-+0-9.eE]+),([-+0-9.eE]+)[^`r`n]*yaw=([-+0-9.eE]+)")
        if (-not $match.Success) { throw "Missing positional snapshot: $Name" }
        $culture = [System.Globalization.CultureInfo]::InvariantCulture
        $x = [double]::Parse($match.Groups[1].Value, $culture)
        $y = [double]::Parse($match.Groups[2].Value, $culture)
        $z = [double]::Parse($match.Groups[3].Value, $culture)
        $actualYaw = [double]::Parse($match.Groups[4].Value, $culture)
        if ([Math]::Abs($x - $BaseX) -gt ($Radius + 0.1) -or
            [Math]::Abs($y - $BaseY) -gt ($Radius + 0.1) -or
            [Math]::Abs($z - $BaseZ) -gt 0.01 -or
            [Math]::Abs($actualYaw - $Yaw) -gt 0.01) {
            throw "Unexpected positional snapshot for ${Name}: x=$x y=$y z=$z yaw=$actualYaw"
        }
    }

    Assert-RemoteTransform -Text $a -PlayerId $idB -BaseX -400 -BaseY -500 -BaseZ -600 -Yaw 90 -Radius 40 -Name "Probe A receiving Probe B"
    Assert-RemoteTransform -Text $b -PlayerId $idA -BaseX 100 -BaseY 200 -BaseZ 300 -Yaw 45 -Radius 25 -Name "Probe B receiving Probe A"

    if ($a -match 'FATAL|timeout' -or $b -match 'FATAL|timeout') { throw "Unexpected probe error or timeout" }
    Write-Host "PASS: IDs $idA/$idB; help, positional options, relay state and PlayerLeft verified."
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host "Integration logs: $logRoot"
}
