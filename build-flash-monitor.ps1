# Complete Build, Flash & Monitor Script for ESP32 E-Drum Trigger System
# Run this script in PowerShell with ESP-IDF environment loaded

Write-Host "=== ESP32 E-Drum Complete Build Script ===" -ForegroundColor Green
Write-Host "Setting up ESP-IDF v5.5.1 environment..." -ForegroundColor Yellow

# Set ESP-IDF path
$env:IDF_PATH = "I:\esp32\v5.5.1\esp-idf"

# Load ESP-IDF environment
try {
    & "$env:IDF_PATH\export.ps1"
    Write-Host "ESP-IDF environment loaded successfully" -ForegroundColor Green
}
catch {
    Write-Host "Error loading ESP-IDF environment" -ForegroundColor Red
    Write-Host "Make sure to run this in ESP-IDF PowerShell or use export.ps1 first" -ForegroundColor Yellow
    exit 1
}

# Navigate to project directory
Set-Location "I:\esp32\esp32-drumpad"
Write-Host "Current directory: $(Get-Location)" -ForegroundColor Cyan

# Set target
Write-Host "Setting target to ESP32-S3..." -ForegroundColor Yellow
idf.py set-target esp32s3

# Build project
Write-Host "Building project..." -ForegroundColor Yellow
idf.py build

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit 1
}

Write-Host "Build completed successfully!" -ForegroundColor Green

# Flash to device
Write-Host "Flashing to ESP32-S3 (COM9)..." -ForegroundColor Yellow
idf.py flash

if ($LASTEXITCODE -ne 0) {
    Write-Host "Flash failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit 1
}

Write-Host "Flash completed successfully!" -ForegroundColor Green

# Start monitor
Write-Host "Starting serial monitor (115200 baud)..." -ForegroundColor Yellow
Write-Host "Use Ctrl+] to exit monitor" -ForegroundColor Cyan
idf.py monitor

Write-Host "=== Complete Process Finished ===" -ForegroundColor Green