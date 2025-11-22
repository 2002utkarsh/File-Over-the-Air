param (
    [Parameter(Mandatory = $false)]
    [string]$Port = "COM5",
    [int]$BaudRate = 115200
)

Write-Host "Opening serial port $Port at $BaudRate baud..." -ForegroundColor Cyan
Write-Host "Commands: 'imu print' to view data, 'imu delete' to clear flash" -ForegroundColor Yellow
Write-Host "Press Ctrl+C to exit" -ForegroundColor Yellow
Write-Host ""

try {
    # Create and open serial port
    $serialPort = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, None, 8, one
    $serialPort.Open()
    
    Write-Host "[Connected to $Port]" -ForegroundColor Green
    Write-Host ""
    
    # Read continuously
    while ($true) {
        if ($serialPort.BytesToRead -gt 0) {
            $data = $serialPort.ReadExisting()
            Write-Host $data -NoNewline
        }
        Start-Sleep -Milliseconds 50
        
        # Check if user wants to type something (Ctrl+C to exit)
    }
}
catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
finally {
    if ($serialPort -and $serialPort.IsOpen) {
        $serialPort.Close()
        Write-Host "`n[Disconnected]" -ForegroundColor Yellow
    }
}
