# Build and Test Script for File Update System
# Handles environment setup automatically

param(
    [string]$Board = "native_sim",
    [switch]$Test,
    [switch]$Clean
)

Write-Host "=== File Update System Build Script ===" -ForegroundColor Cyan

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
    & west twister -p $Board -T ..\file_update_system --inline-logs
} else {
    Write-Host "Building for board: $Board" -ForegroundColor Yellow
    & west build -b $Board ..\file_update_system -p
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "`nBuild successful!" -ForegroundColor Green
        Write-Host "Build artifacts location:" -ForegroundColor Cyan
        Write-Host "  - Firmware binary: .\build\zephyr\zephyr.hex" -ForegroundColor Cyan
        Write-Host "  - Binary file:     .\build\zephyr\zephyr.bin" -ForegroundColor Cyan
        Write-Host "  - ELF file:        .\build\zephyr\zephyr.elf" -ForegroundColor Cyan
        
        # Display binary size
        if (Test-Path ".\build\zephyr\zephyr.bin") {
            $binSize = (Get-Item ".\build\zephyr\zephyr.bin").Length
            $binSizeKB = [math]::Round($binSize / 1024, 2)
            Write-Host "`nBinary size: $binSizeKB KB (max: 464 KB)" -ForegroundColor $(if ($binSizeKB -lt 464) { "Green" } else { "Red" })
        }
    } else {
        Write-Host "`nBuild failed!" -ForegroundColor Red
    }
}
