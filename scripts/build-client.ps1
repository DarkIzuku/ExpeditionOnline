param(
    [Parameter(Mandatory = $true)]
    [string]$UE4SSRoot,
    [string]$Generator = "Visual Studio 17 2022",
    [ValidateSet("Game__Shipping__Win64", "Game__Debug__Win64")]
    [string]$Configuration = "Game__Shipping__Win64"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\client"
$resolvedUE4SS = (Resolve-Path -LiteralPath $UE4SSRoot).Path

cmake -S $projectRoot -B $buildDir -G $Generator -A x64 `
    -DEXPEDITION_BUILD_CLIENT=ON `
    -DEXPEDITION_BUILD_SERVER=OFF `
    -DEXPEDITION_BUILD_PROBE=OFF `
    -DEXPEDITION_BUILD_TESTS=OFF `
    -DUE4SS_ROOT=$resolvedUE4SS

cmake --build $buildDir --target ExpeditionOnline --config $Configuration --parallel

$package = Join-Path $buildDir "package\UE4SS\Mods\ExpeditionOnline"
Write-Host "UE4SS package: $package"
Write-Host "DLL: $package\dlls\main.dll"
