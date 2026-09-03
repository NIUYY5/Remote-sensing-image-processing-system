# =============================================================================
#  Remote Sensing Analysis System - Release Package Script
#  VS 2022 + Qt 5.15.2 + GDAL
#
#  Usage: Run from "x64 Native Tools Command Prompt for VS 2022":
#    cd d:\zuizhongruanjian4\secondjiemian
#    powershell -ExecutionPolicy Bypass -File package_release.ps1
# =============================================================================

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# ---- Configuration ----
$ProjectFile  = Join-Path $ScriptDir "moban.vcxproj"
$ReleaseDir   = Join-Path $ScriptDir "x64\Release"
$OutputDir    = Join-Path $ScriptDir "..\release_package"   # d:\zuizhongruanjian4\release_package
$GDAL_BINDIR  = "D:\gdl\x64-windows\bin"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Release Package Builder" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---- Step 1: Locate MSBuild ----
Write-Host "[1/6] Locating MSBuild..." -ForegroundColor Yellow

$MSBuild = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPaths = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
)

if (Test-Path $vswhere) {
    Write-Host "  Using vswhere to locate VS 2022..."
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($vsPath) {
        $MSBuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
    }
}

if (-not $MSBuild -or -not (Test-Path $MSBuild)) {
    foreach ($p in $vsPaths) {
        if (Test-Path $p) { $MSBuild = $p; break }
    }
}

if (-not $MSBuild -or -not (Test-Path $MSBuild)) {
    Write-Host "  ERROR: MSBuild not found. Please run this script from 'x64 Native Tools Command Prompt for VS 2022'." -ForegroundColor Red
    Write-Host "  Start Menu -> Visual Studio 2022 -> x64 Native Tools Command Prompt for VS 2022" -ForegroundColor Red
    exit 1
}
Write-Host "  Found: $MSBuild" -ForegroundColor Green

# ---- Step 2: Build Release ----
Write-Host ""
Write-Host "[2/6] Building Release..." -ForegroundColor Yellow

Write-Host "  Project: $ProjectFile"
& $MSBuild $ProjectFile /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Build failed (exit code $LASTEXITCODE). Check errors above." -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "  Build succeeded." -ForegroundColor Green

# Verify output
$ExePath = Join-Path $ReleaseDir "moban.exe"
if (-not (Test-Path $ExePath)) {
    Write-Host "  ERROR: Output exe not found: $ExePath" -ForegroundColor Red
    exit 1
}
Write-Host "  Output: $ExePath ($([math]::Round((Get-Item $ExePath).Length/1MB, 2)) MB)" -ForegroundColor Green

# ---- Step 3: Locate Qt & windeployqt ----
Write-Host ""
Write-Host "[3/6] Locating Qt & windeployqt..." -ForegroundColor Yellow

$QtDirs = @(
    "D:\qt5.15.2\5.15.2\msvc2019_64",
    "C:\Qt\5.15.2\msvc2019_64",
    "D:\Qt\5.15.2\msvc2019_64"
)
$Windep = $null
foreach ($qd in $QtDirs) {
    $test = Join-Path $qd "bin\windeployqt.exe"
    if (Test-Path $test) { $Windep = $test; $QtDir = $qd; break }
}

if (-not $Windep) {
    Write-Host "  ERROR: windeployqt not found. Searched:" -ForegroundColor Red
    foreach ($qd in $QtDirs) { Write-Host "    $qd" -ForegroundColor Red }
    exit 1
}
Write-Host "  Found: $QtDir" -ForegroundColor Green

# ---- Step 4: Deploy Qt DLLs ----
Write-Host ""
Write-Host "[4/6] Deploying Qt runtime (windeployqt)..." -ForegroundColor Yellow

# Prepare output directory
if (Test-Path $OutputDir) {
    Write-Host "  Cleaning existing output directory..."
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# Copy exe first
Copy-Item $ExePath $OutputDir -Force

# Run windeployqt
$DeployExe = Join-Path $OutputDir "moban.exe"
Write-Host "  Running windeployqt..."
& $Windep --release --no-compiler-runtime --no-translations $DeployExe
if ($LASTEXITCODE -ne 0) {
    Write-Host "  WARNING: windeployqt returned exit code $LASTEXITCODE" -ForegroundColor Yellow
} else {
    Write-Host "  Qt deployment done." -ForegroundColor Green
}

# windeployqt may miss some Qt modules - explicitly copy them
$extraQtDlls = @("Qt5PrintSupport.dll", "Qt5Svg.dll", "Qt5Charts.dll")
foreach ($dll in $extraQtDlls) {
    $src = Join-Path (Join-Path $QtDir "bin") $dll
    if (Test-Path $src) {
        Copy-Item $src $OutputDir -Force
        Write-Host "  Explicitly copied $dll" -ForegroundColor Green
    } else {
        Write-Host "  WARNING: $dll not found in Qt bin" -ForegroundColor Yellow
    }
}

# Copy Qt plugins explicitly (sometimes windeployqt misses some)
$QtPlugins = Join-Path $QtDir "plugins"
$DestPlugins = Join-Path $OutputDir "plugins"
if (Test-Path "$QtPlugins\imageformats") {
    New-Item -ItemType Directory -Path "$DestPlugins\imageformats" -Force | Out-Null
    Copy-Item "$QtPlugins\imageformats\*.dll" "$DestPlugins\imageformats\" -Force
    Write-Host "  Copied imageformats plugins." -ForegroundColor Green
}
if (Test-Path "$QtPlugins\platforms") {
    New-Item -ItemType Directory -Path "$DestPlugins\platforms" -Force | Out-Null
    Copy-Item "$QtPlugins\platforms\qwindows.dll" "$DestPlugins\platforms\" -Force
    Write-Host "  Copied platforms plugin." -ForegroundColor Green
}
if (Test-Path "$QtPlugins\sqldrivers") {
    New-Item -ItemType Directory -Path "$DestPlugins\sqldrivers" -Force | Out-Null
    Copy-Item "$QtPlugins\sqldrivers\*.dll" "$DestPlugins\sqldrivers\" -Force -ErrorAction SilentlyContinue
}

# Copy VCRuntime (if not already deployed by windeployqt)
$VCRedistFiles = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
foreach ($f in $VCRedistFiles) {
    $dest = Join-Path $OutputDir $f
    if (-not (Test-Path $dest)) {
        $src1 = Join-Path "$env:SystemRoot\System32" $f
        $src2 = Join-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" $f
        if (Test-Path $src1) {
            Copy-Item $src1 $OutputDir -Force
            Write-Host "  Copied $f from System32." -ForegroundColor Green
        } else {
            # Try glob for VS redist
            $found = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT\$f" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($found) {
                Copy-Item $found.FullName $OutputDir -Force
                Write-Host "  Copied $f from VS redist." -ForegroundColor Green
            }
        }
    }
}

# ---- Step 5: Deploy GDAL ----
Write-Host ""
Write-Host "[5/6] Deploying GDAL runtime..." -ForegroundColor Yellow

if (Test-Path $GDAL_BINDIR) {
    $gdalCount = 0
    Get-ChildItem "$GDAL_BINDIR\*.dll" | ForEach-Object {
        $dest = Join-Path $OutputDir $_.Name
        if (-not (Test-Path $dest)) {
            Copy-Item $_.FullName $OutputDir -Force
            $gdalCount++
        }
    }
    # Also copy GDAL data directory if it exists
    $gdalData = "$GDAL_BINDIR\..\share\gdal"
    if (Test-Path $gdalData) {
        Copy-Item $gdalData (Join-Path $OutputDir "gdal-data") -Recurse -Force
        Write-Host "  Copied GDAL data files." -ForegroundColor Green
    }
    # Copy PROJ data if exists
    $projData = "$GDAL_BINDIR\..\share\proj"
    if (Test-Path $projData) {
        Copy-Item $projData (Join-Path $OutputDir "proj-data") -Recurse -Force
        Write-Host "  Copied PROJ data files." -ForegroundColor Green
    }
    Write-Host "  Copied $gdalCount GDAL DLLs from $GDAL_BINDIR" -ForegroundColor Green
} else {
    Write-Host "  WARNING: GDAL bin directory not found: $GDAL_BINDIR" -ForegroundColor Yellow
    Write-Host "  Please manually copy gdal.dll and its dependencies." -ForegroundColor Yellow
}

# ---- Step 6: Cleanup and verify ----
Write-Host ""
Write-Host "[6/6] Verifying package..." -ForegroundColor Yellow

# Remove debug/pdb files to reduce size
Get-ChildItem $OutputDir -Recurse -Filter "*.pdb" | Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $OutputDir -Recurse -Filter "*.ilk" | Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $OutputDir -Recurse -Filter "*.exp" | Remove-Item -Force -ErrorAction SilentlyContinue

# Count files
$totalFiles = (Get-ChildItem $OutputDir -Recurse -File).Count
$totalSize  = [math]::Round((Get-ChildItem $OutputDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB, 2)

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Package created successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Location : $OutputDir" -ForegroundColor White
Write-Host "  Files    : $totalFiles" -ForegroundColor White
Write-Host "  Size     : $totalSize MB" -ForegroundColor White
Write-Host "  Binary   : moban.exe" -ForegroundColor White
Write-Host ""
Write-Host "  To distribute, copy the entire folder:" -ForegroundColor White
Write-Host "    $OutputDir" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Run the following to test:" -ForegroundColor White
Write-Host "    $OutputDir\moban.exe" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
