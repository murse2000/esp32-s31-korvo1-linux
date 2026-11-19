param(
    [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Stop'
$outDir = Join-Path $PSScriptRoot 'build-output'
$kernel = Join-Path $outDir 'korvo1-linux-Image'
$kernelManifest = Join-Path $outDir 'korvo1-linux-size.bin'
$partitionTable = Join-Path $outDir 'korvo1-partition-table.bin'
$partitionCsv = Join-Path $outDir 'korvo1-partitions.csv'
foreach ($file in @($kernel, $kernelManifest, $partitionTable, $partitionCsv)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Flash component not found: $file"
    }
}
$row = Get-Content -LiteralPath $partitionCsv | Where-Object { $_ -match '^linux\s*,' } | Select-Object -First 1
if (-not $row) { throw 'Linux partition is missing from partition CSV' }
$fields = $row.Split(',').Trim()
$kernelOffset = [Convert]::ToInt64($fields[3].Replace('0x',''),16)
$kernelSize = [Convert]::ToInt64($fields[4].Replace('0x',''),16)
$manifestOffset = $kernelOffset + $kernelSize - 4096
if ((Get-Item -LiteralPath $kernel).Length -gt $kernelSize - 4096) { throw 'Kernel exceeds its partition payload area' }
if ((Get-Item -LiteralPath $kernelManifest).Length -ne 4096) { throw 'Kernel manifest must occupy one 4096-byte sector' }

$arguments = @(
    '-3.12', '-m', 'esptool', '--chip', 'esp32s31', '-p', $Port,
    '-b', '921600', 'write-flash',
    '0x8000', $partitionTable,
    ('0x{0:X}' -f $kernelOffset), $kernel,
    ('0x{0:X}' -f $manifestOffset), $kernelManifest
)
py @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
