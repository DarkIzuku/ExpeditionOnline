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
$stage = Join-Path $OutputDirectory "ExpeditionOnline-Windows-x64"
$zip = Join-Path $OutputDirectory "ExpeditionOnline-Windows-x64.zip"

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

$serverDir = Join-Path $stage "Server"
$probeDir = Join-Path $stage "Probe"
$docsDir = Join-Path $stage "docs"
$modDir = Join-Path $stage "UE4SS\Mods\ExpeditionOnline"
New-Item -ItemType Directory -Force -Path $serverDir, $probeDir, $docsDir, $modDir | Out-Null

$serverExe = Join-Path $BinDirectory "ExpeditionOnlineServer.exe"
$probeExe = Join-Path $BinDirectory "ExpeditionOnlineProbe.exe"
$mainDll = Join-Path $ClientPackageRoot "dlls\main.dll"
foreach ($required in @($serverExe, $probeExe, $mainDll)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required build output missing: $required" }
    if ((Get-Item -LiteralPath $required).Length -lt 4096) { throw "Build output is unexpectedly small: $required" }
}

Copy-Item -LiteralPath $serverExe -Destination $serverDir
Copy-Item -LiteralPath (Join-Path $projectRoot "config\server.example.ini") -Destination (Join-Path $serverDir "server.ini")
Copy-Item -LiteralPath $probeExe -Destination $probeDir
Copy-Item -LiteralPath (Join-Path $ClientPackageRoot "dlls") -Destination $modDir -Recurse
Copy-Item -LiteralPath (Join-Path $ClientPackageRoot "config") -Destination $modDir -Recurse
if (Test-Path -LiteralPath (Join-Path $ClientPackageRoot "enabled.txt")) {
    Copy-Item -LiteralPath (Join-Path $ClientPackageRoot "enabled.txt") -Destination $modDir
} else {
    New-Item -ItemType File -Path (Join-Path $modDir "enabled.txt") | Out-Null
}
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\PROTOCOL.md") -Destination $docsDir
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\QUICK_TEST.md") -Destination $docsDir
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\BUILDING.md") -Destination $docsDir
Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination $stage

$version = @(
    "ExpeditionOnline commit: $ExpeditionCommit"
    "UE4SS build revision: $UE4SSRevision"
    "Build date (UTC): $BuildDate"
    "Protocol version: 2"
    "Project version: 0.2.0"
    "Configuration: Release x64"
    "UE4SS configuration: Game__Shipping__Win64 x64"
)
Set-Content -LiteralPath (Join-Path $stage "VERSION.txt") -Value $version -Encoding utf8NoBOM

$hashTargets = @(
    (Join-Path $serverDir "ExpeditionOnlineServer.exe"),
    (Join-Path $probeDir "ExpeditionOnlineProbe.exe"),
    (Join-Path $modDir "dlls\main.dll")
)
$hashLines = foreach ($path in $hashTargets) {
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = [System.IO.Path]::GetRelativePath($stage, $path).Replace('\', '/')
    "$hash  $relative"
}
Set-Content -LiteralPath (Join-Path $stage "SHA256SUMS.txt") -Value $hashLines -Encoding ascii

Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "PACKAGE_READY $zip"
