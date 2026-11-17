param(
    [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Stop'
$outDir = Join-Path $PSScriptRoot 'build-output'
$partitionTable = Join-Path $outDir 'korvo1-partition-table.bin'
$kernel = Join-Path $outDir 'korvo1-linux-Image'
$kernelManifest = Join-Path $outDir 'korvo1-linux-size.bin'
$rootfs = Join-Path $outDir 'korvo1-rootfs.squashfs'
foreach ($file in @($partitionTable, $kernel, $kernelManifest, $rootfs)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Flash component not found: $file"
    }
}

py -3.12 -m esptool --chip esp32s31 -p $Port -b 921600 write-flash `
    0x8000 $partitionTable `
    0x1F0000 $kernel `
    0x6CF000 $kernelManifest `
    0x6D0000 $rootfs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
