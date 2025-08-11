$ErrorActionPreference = "Stop"

$ext = "build\release\extension\geotiff\geotiff.duckdb_extension"
$out = "dist\windows"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $ext $out\

# vcpkg dynamic DLLs (default triplet in CI: x64-windows-dynamic)
$vcpkgBin = Join-Path (Get-Location) "build\release\vcpkg_installed\x64-windows-dynamic\bin"

# Find extension's DLL deps
$dumpbin = "${env:VSINSTALLDIR}VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path $dumpbin)) { $dumpbin = "dumpbin.exe" } # GitHub Runner has it on PATH
$deps = & $dumpbin /DEPENDENTS $ext | Select-String -Pattern '\.dll' | ForEach-Object { ($_ -split '\s+')[-1].Trim() } | Sort-Object -Unique

foreach ($d in $deps) {
  $src = Join-Path $vcpkgBin $d
  if (Test-Path $src) { Copy-Item $src $out\ -ErrorAction SilentlyContinue }
}

Write-Host "Windows package at: $out"
