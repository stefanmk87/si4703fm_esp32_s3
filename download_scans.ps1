param(
    [string]$PortName = "COM4",
    [string]$OutputDirectory = [Environment]::GetFolderPath("Desktop")
)

$ErrorActionPreference = "Stop"
$serial = New-Object System.IO.Ports.SerialPort
$serial.PortName = $PortName
$serial.BaudRate = 115200
$serial.DataBits = 8
$serial.Parity = [System.IO.Ports.Parity]::None
$serial.StopBits = [System.IO.Ports.StopBits]::One
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.ReadTimeout = 1000
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    Write-Host "Connecting to $PortName... Close PlatformIO Serial Monitor first."
    try {
        $serial.Open()
    }
    catch {
        throw "Could not open $PortName. Close Serial Monitor and check the COM port. $($_.Exception.Message)"
    }

    $serial.DiscardInBuffer()
    Write-Host "Connected. Requesting saved scan tables..."

    $files = @{}
    $activeFile = $null
    $activeLines = [System.Collections.Generic.List[string]]::new()
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    $nextRequest = [DateTime]::UtcNow

    while ([DateTime]::UtcNow -lt $deadline -and $files.Count -lt 2) {
        if ([DateTime]::UtcNow -ge $nextRequest) {
            $serial.WriteLine("L")
            Write-Host "Sent L; waiting for scan files..."
            $nextRequest = [DateTime]::UtcNow.AddSeconds(5)
        }

        try {
            $line = $serial.ReadLine().TrimEnd([char[]]@(13))
        }
        catch [System.TimeoutException] {
            continue
        }

        if ($line -match '^--- /(fm_filter_on|fm_filter_off)\.txt ---$') {
            $activeFile = "$($Matches[1]).txt"
            $activeLines = [System.Collections.Generic.List[string]]::new()
            continue
        }

        if ($line -eq "--- end ---" -and $null -ne $activeFile) {
            $files[$activeFile] = $activeLines.ToArray()
            $activeFile = $null
            continue
        }

        if ($null -ne $activeFile) {
            $activeLines.Add($line)
        }
    }

    if ($files.Count -eq 0) {
        throw "No scan files received. Run F and N in Serial Monitor, close it, and retry."
    }

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    foreach ($fileName in @("fm_filter_on.txt", "fm_filter_off.txt")) {
        if (-not $files.ContainsKey($fileName)) {
            Write-Host "No saved $fileName found on the ESP32."
            continue
        }

        $destination = Join-Path $OutputDirectory $fileName
        Set-Content -Path $destination -Value $files[$fileName] -Encoding Ascii
        Write-Host "Saved $destination"
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}