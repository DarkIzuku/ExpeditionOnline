param(
    [Parameter(Mandatory = $true)][string]$BinDirectory,
    [Parameter(Mandatory = $true)][string]$ClientPackageRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$ExpeditionCommit,
    [Parameter(Mandatory = $true)][string]$UE4SSRevision,
    [Parameter(Mandatory = $true)][string]$BuildDate
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$fullStage = Join-Path $OutputDirectory "ExpeditionOnline-Windows-x64"
$hostStage = Join-Path $OutputDirectory "ExpeditionOnline-Host-Windows-x64"
$clientStage = Join-Path $OutputDirectory "ExpeditionOnline-Client-Windows-x64"
$fullZip = "$fullStage.zip"
$hostZip = "$hostStage.zip"
$clientZip = "$clientStage.zip"

foreach ($path in @($fullStage, $hostStage, $clientStage, $fullZip, $hostZip, $clientZip)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

$serverExe = Join-Path $BinDirectory "ExpeditionOnlineServer.exe"
$probeExe = Join-Path $BinDirectory "ExpeditionOnlineProbe.exe"
$selfTestExe = Join-Path $BinDirectory "ExpeditionOnlineSelfTest.exe"
$doctorExe = Join-Path $BinDirectory "ExpeditionOnlineDoctor.exe"
$mainDll = Join-Path $ClientPackageRoot "dlls\main.dll"
foreach ($required in @($serverExe, $probeExe, $selfTestExe, $doctorExe, $mainDll)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required build output missing: $required" }
    if ((Get-Item -LiteralPath $required).Length -lt 4096) { throw "Build output is unexpectedly small: $required" }
}

$version = @(
    "ExpeditionOnline version: 0.4.0-rc1"
    "ExpeditionOnline commit: $ExpeditionCommit"
    "UE4SS build revision: $UE4SSRevision"
    "Build date (UTC): $BuildDate"
    "Protocol version: 3"
    "Configuration: Release x64"
    "UE4SS configuration: Game__Shipping__Win64 x64"
)

function Write-Version([string]$Directory) {
    Set-Content -LiteralPath (Join-Path $Directory "VERSION.txt") -Value $version -Encoding utf8NoBOM
}

function Write-Hashes([string]$Directory) {
    $targets = Get-ChildItem -LiteralPath $Directory -Recurse -File | Where-Object { $_.Extension -in @('.exe', '.dll') }
    $lines = foreach ($target in $targets) {
        $hash = (Get-FileHash -LiteralPath $target.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $relative = [System.IO.Path]::GetRelativePath($Directory, $target.FullName).Replace('\', '/')
        "$hash  $relative"
    }
    Set-Content -LiteralPath (Join-Path $Directory "SHA256SUMS.txt") -Value $lines -Encoding ascii
}

# Host package: double-click server plus an all-in-one local network self-test.
New-Item -ItemType Directory -Force -Path $hostStage | Out-Null
Copy-Item -LiteralPath $serverExe -Destination $hostStage
Copy-Item -LiteralPath $selfTestExe -Destination $hostStage
Copy-Item -LiteralPath (Join-Path $projectRoot "config\server.example.ini") -Destination (Join-Path $hostStage "server.ini")
Copy-Item -LiteralPath (Join-Path $projectRoot "host\Start-Server.bat") -Destination $hostStage
Copy-Item -LiteralPath (Join-Path $projectRoot "host\README_HOST.txt") -Destination $hostStage
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\QUICK_START_HOST.md") -Destination $hostStage
Write-Version $hostStage
Write-Hashes $hostStage

# Client package: mod payload, guided installer/configurator and native Doctor.
$clientMod = Join-Path $clientStage "Mod\ExpeditionOnline"
New-Item -ItemType Directory -Force -Path $clientMod | Out-Null
Copy-Item -LiteralPath (Join-Path $ClientPackageRoot "dlls") -Destination $clientMod -Recurse
Copy-Item -LiteralPath (Join-Path $ClientPackageRoot "config") -Destination $clientMod -Recurse
if (Test-Path -LiteralPath (Join-Path $ClientPackageRoot "enabled.txt")) {
    Copy-Item -LiteralPath (Join-Path $ClientPackageRoot "enabled.txt") -Destination $clientMod
} else {
    New-Item -ItemType File -Path (Join-Path $clientMod "enabled.txt") | Out-Null
}
Set-Content -LiteralPath (Join-Path $clientMod "VERSION.txt") -Value $version -Encoding utf8NoBOM
Copy-Item -LiteralPath (Join-Path $projectRoot "installer\Install-ExpeditionOnline.bat") -Destination $clientStage
Copy-Item -LiteralPath (Join-Path $projectRoot "installer\Install-ExpeditionOnline.ps1") -Destination $clientStage
Copy-Item -LiteralPath (Join-Path $projectRoot "installer\Configure-ExpeditionOnline.bat") -Destination $clientStage
Copy-Item -LiteralPath (Join-Path $projectRoot "installer\Configure-ExpeditionOnline.ps1") -Destination $clientStage
Copy-Item -LiteralPath $doctorExe -Destination $clientStage
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\QUICK_START_CLIENT.md") -Destination $clientStage
Copy-Item -LiteralPath (Join-Path $projectRoot "UE4SS_BUILD_REVISION.txt") -Destination $clientStage
Write-Version $clientStage
Write-Hashes $clientStage

# Development/full package contains the exact Host and Client payloads plus Probe and technical docs.
$fullHost = Join-Path $fullStage "Host"
$fullClient = Join-Path $fullStage "Client"
$fullProbe = Join-Path $fullStage "Probe"
$fullDocs = Join-Path $fullStage "docs"
New-Item -ItemType Directory -Force -Path $fullHost, $fullClient, $fullProbe, $fullDocs | Out-Null
Copy-Item -Path (Join-Path $hostStage "*") -Destination $fullHost -Recurse
Copy-Item -Path (Join-Path $clientStage "*") -Destination $fullClient -Recurse
Copy-Item -LiteralPath $probeExe -Destination $fullProbe
Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination $fullStage
Copy-Item -Path (Join-Path $projectRoot "docs\*") -Destination $fullDocs -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "UE4SS_BUILD_REVISION.txt") -Destination $fullStage
Write-Version $fullStage
Write-Hashes $fullStage

Compress-Archive -LiteralPath $fullStage -DestinationPath $fullZip -CompressionLevel Optimal
Compress-Archive -LiteralPath $hostStage -DestinationPath $hostZip -CompressionLevel Optimal
Compress-Archive -LiteralPath $clientStage -DestinationPath $clientZip -CompressionLevel Optimal
Write-Host "PACKAGE_READY $fullZip"
Write-Host "PACKAGE_READY $hostZip"
Write-Host "PACKAGE_READY $clientZip"
