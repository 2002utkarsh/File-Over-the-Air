param (
    [string]$Port = "COM5"
)

Write-Host "Attempting to connect to $Port..." -ForegroundColor Cyan
Write-Host "If you see output, the board is working!" -ForegroundColor Yellow
Write-Host "Try typing: imu print" -ForegroundColor Green
Write-Host ""

try {
    $serialPort = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
    $serialPort.DtrEnable = $true
    $serialPort.RtsEnable = $true
    $serialPort.Open()
    
    Write-Host "[Connected to $Port]" -ForegroundColor Green
    Write-Host "Reading for 10 seconds..." -ForegroundColor Yellow
    Write-Host ""
    
    # Read for 10 seconds
    $endTime = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $endTime) {
        if ($serialPort.BytesToRead -gt 0) {
            $data = $serialPort.ReadExisting()
            Write-Host $data -NoNewline -ForegroundColor White
        }
        Start-Sleep -Milliseconds 50
    }
    
    Write-Host "`n`n[Timeout - closing connection]" -ForegroundColor Yellow
}
catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
finally {
    if ($serialPort -and $serialPort.IsOpen) {
        $serialPort.Close()
    }
}
