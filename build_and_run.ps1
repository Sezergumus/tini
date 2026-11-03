$env:VSLANG = "1033"

$exePath = "build\Tini_artefacts\Release\Standalone\Tini.exe"
$processName = "Tini"

# Auto close
Stop-Process -Name $processName -Force -ErrorAction SilentlyContinue

Clear-Host
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "  [///]     TINI BUILD SYSTEM     [///]" -ForegroundColor Cyan
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host ""

# Timestamp Check
$needsBuild = $false

if (-not (Test-Path $exePath)) {
    $needsBuild = $true
} else {
    $exeTime = (Get-Item $exePath).LastWriteTime
    
    # Check if source files or CMakeLists.txt have been modified
    $filesToCheck = Get-ChildItem -Path "Source" -Recurse -Include *.cpp, *.h, *.hpp -ErrorAction SilentlyContinue
    $filesToCheck += Get-Item "CMakeLists.txt" -ErrorAction SilentlyContinue

    foreach ($file in $filesToCheck) {
        if ($file -and $file.LastWriteTime -gt $exeTime) {
            $needsBuild = $true
            break
        }
    }
}

if ($needsBuild) {
    Write-Host "[1/2] Configuring CMake (Release)..." -ForegroundColor Yellow
    Write-Host "[==============            ] 50%" -ForegroundColor DarkGray
    cmd.exe /c "cmake -B build 2>&1" | ForEach-Object { Write-Host $_ -ForegroundColor White }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERROR] Configuration failed! Check logs." -ForegroundColor Red
        Exit
    }

    Write-Host "`n[2/2] Building Project..." -ForegroundColor Yellow
    Write-Host "[==========================] 100%" -ForegroundColor DarkGray
    cmd.exe /c "cmake --build build --config Release 2>&1" | ForEach-Object { Write-Host $_ -ForegroundColor White }

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERROR] Build failed! Check logs." -ForegroundColor Red
        Exit
    }
} else {
    Write-Host "[*] No code changes detected. Skipping build..." -ForegroundColor DarkGray
}

# Run
if (Test-Path $exePath) {
    Write-Host "`n[SUCCESS] Launching Engine..." -ForegroundColor Green
    Start-Process $exePath
} else {
    Write-Host "`n[ERROR] Executable not found at $exePath!" -ForegroundColor Red
}