# Flash Script for ESP32 E-Drum Trigger System
# Run this script in PowerShell with ESP-IDF environment loaded

Write-Host "=== ESP32 E-Drum Flash Script ===" -ForegroundColor Green
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

# Flash to device
Write-Host "Flashing to ESP32-S3 (COM9)..." -ForegroundColor Yellow
idf.py flash

if ($LASTEXITCODE -eq 0) {
    Write-Host "Flash completed successfully!" -ForegroundColor Green
} else {
    Write-Host "Flash failed with exit code $LASTEXITCODE" -ForegroundColor Red
}

Write-Host "=== Flash Complete ===" -ForegroundColor Cyan