# Build and Test Script for FOTA Application
# Handles environment setup automatically

param(
    [string]$Board = "native_sim",
    [switch]$Test,
    [switch]$Clean
)

Write-Host "=== FOTA Application Build Script ===" -ForegroundColor Cyan

# Set environment variables
$env:ZEPHYR_BASE = "C:\UG\Uni\FOTO_Project\zephyrproject\zephyr"
Write-Host "ZEPHYR_BASE set to: $env:ZEPHYR_BASE" -ForegroundColor Green

# Change to zephyrproject directory
Set-Location "C:\UG\Uni\FOTO_Project\zephyrproject"

if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path "build") {
        Remove-Item -Recurse -Force "build"
    }
}

if ($Test) {
    Write-Host "Running tests with Twister..." -ForegroundColor Yellow
    & "C:\UG\Uni\FOTO_Project\venv\Scripts\west.exe" twister -p $Board -T fota_app --inline-logs
} else {
    Write-Host "Building for board: $Board" -ForegroundColor Yellow
    & "C:\UG\Uni\FOTO_Project\venv\Scripts\west.exe" build -b $Board fota_app -p
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "`nBuild successful!" -ForegroundColor Green
        if ($Board -eq "native_sim") {
            Write-Host "To run: .\build\zephyr\zephyr.exe" -ForegroundColor Cyan
        }
    } else {
        Write-Host "`nBuild failed!" -ForegroundColor Red
    }
}
