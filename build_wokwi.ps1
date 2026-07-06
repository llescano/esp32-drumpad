# Build firmware for Wokwi simulator (PSRAM disabled)
Write-Host "Building for Wokwi (no PSRAM)..." -ForegroundColor Cyan
cd I:\esp32\esp32-drumpad

# Save current sdkconfig
Copy-Item sdkconfig sdkconfig.hw_backup -Force

# Disable PSRAM for Wokwi
$content = Get-Content sdkconfig -Raw
$content = $content -replace 'CONFIG_ESP32S3_SPIRAM_SUPPORT=y', 'CONFIG_ESP32S3_SPIRAM_SUPPORT=n'
$content = $content -replace "CONFIG_SPIRAM=y`r`n", "# CONFIG_SPIRAM is not set`r`n"
Set-Content sdkconfig -Value $content

# Clean and build
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
idf.py build

# Restore original sdkconfig
Copy-Item sdkconfig.hw_backup sdkconfig -Force
Remove-Item sdkconfig.hw_backup -Force

Write-Host "Done! Use build/esp32-edrumulus.bin in Wokwi." -ForegroundColor Green
