param(
    [string]$Generator = "Visual Studio 17 2022",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\standalone"

cmake -S $projectRoot -B $buildDir -G $Generator -A x64 `
    -DEXPEDITION_BUILD_CLIENT=OFF `
    -DEXPEDITION_BUILD_SERVER=ON `
    -DEXPEDITION_BUILD_PROBE=ON `
    -DEXPEDITION_BUILD_TESTS=ON

cmake --build $buildDir --config $Configuration --parallel
ctest --test-dir $buildDir -C $Configuration --output-on-failure

Write-Host "Server: $buildDir\bin\$Configuration\ExpeditionOnlineServer.exe"
Write-Host "Probe:  $buildDir\bin\$Configuration\ExpeditionOnlineProbe.exe"
