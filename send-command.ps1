param (
    [Parameter(Mandatory = $true)]
    [string]$Command,
    [Parameter(Mandatory = $false)]
    [string]$Port = "COM3",
    [int]$BaudRate = 115200
)

try {
    $serialPort = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, None, 8, one
    $serialPort.Open()
    $serialPort.WriteLine($Command)
    Write-Host "Sent: $Command" -ForegroundColor Green
    Start-Sleep -Milliseconds 500
    $serialPort.Close()
}
catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
