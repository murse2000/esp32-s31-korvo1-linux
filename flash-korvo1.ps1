param(
    [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Stop'
$outDir = Join-Path $PSScriptRoot 'build-output'
$kernel = Join-Path $outDir 'korvo1-linux-Image'
$kernelManifest = Join-Path $outDir 'korvo1-linux-size.bin'
foreach ($file in @($kernel, $kernelManifest)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Flash component not found: $file"
    }
}

$arguments = @(
    '-3.12', '-m', 'esptool', '--chip', 'esp32s31', '-p', $Port,
    '-b', '921600', 'write-flash',
    '0x1F0000', $kernel,
    '0x6AF000', $kernelManifest
)
py @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
