
$ErrorActionPreference = "Stop"

Write-Host "Starting Driver Test..."

# 1. Start Named Pipe Server (Background Job)
$job = Start-Job -ScriptBlock {
    try {
        $pipe = New-Object System.IO.Pipes.NamedPipeServerStream("VirtualComPipe", [System.IO.Pipes.PipeDirection]::InOut)
        Write-Output "Waiting for connection..."
        $pipe.WaitForConnection()
        Write-Output "Connected!"
        
        $reader = New-Object System.IO.StreamReader($pipe)
        $data = $reader.ReadLine()
        $pipe.Close()
        return $data
    } catch {
        return "ERROR: $_"
    }
}

Write-Host "Pipe Server started in background."
Start-Sleep -Seconds 5

# 2. Find the COM Port
$maxRetries = 5
$portName = $null

for ($i=0; $i -lt $maxRetries; $i++) {
    $dev = Get-CimInstance Win32_SerialPort | Where-Object { $_.Description -match "Virtual Serial Port" }
    if ($dev) {
        $portName = $dev.DeviceID
        break
    }
    Write-Host "Waiting for device enumeration..."
    Start-Sleep -Seconds 2
}

if (!$portName) {
    Write-Host "Available Serial Ports:"
    [System.IO.Ports.SerialPort]::GetPortNames()
    Write-Error "Could not find Virtual COM Port device."
}

Write-Host "Found Device on Port: $portName"

# 3. Open Port and Send Data
try {
    $port = New-Object System.IO.Ports.SerialPort($portName, 9600)
    $port.Open()
    Write-Host "Port Opened. Sending data..."
    $port.WriteLine("HELLO_DRIVER")
    $port.Close()
    Write-Host "Data sent."
} catch {
    Write-Error "Failed to communicate with COM port: $_"
}

# 4. Verify Data Received
$result = Wait-Job $job -Timeout 10
if (!$result) {
    Stop-Job $job
    Write-Error "Timeout waiting for pipe data."
}

$output = Receive-Job $job
Write-Host "Pipe Output: $output"

if ($output -match "HELLO_DRIVER") {
    Write-Host "TEST PASSED: Data successfully forwarded from Driver to Pipe."
    Exit 0
} else {
    Write-Error "TEST FAILED: Data mismatch or empty."
}
