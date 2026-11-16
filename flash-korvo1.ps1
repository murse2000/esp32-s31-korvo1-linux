param(
    [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Stop'
$image = Join-Path $PSScriptRoot 'build-output\korvo1-flash.bin'
if (-not (Test-Path -LiteralPath $image)) {
    throw "Flash image not found: $image"
}

py -3.12 -m esptool --chip esp32s31 -p $Port -b 921600 write-flash 0x0 $image
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
