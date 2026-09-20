<#
.SYNOPSIS
  Register DMC Native Reader as a file opener for DMC3 resource files, per-user.

.DESCRIPTION
  Sets .scm and .ptx as the default opener (both are unclaimed on a stock
  Windows install) and adds DMC Native Reader to the Explorer "Open with"
  list for .dds and .mod without touching their existing default handler --
  both extensions are commonly claimed by an unrelated application (an image
  viewer for .dds, Windows Media Player's camcorder-MOD handler for .mod) and
  silently overriding that default would be a worse experience, not a better
  one. Writes only to HKEY_CURRENT_USER, so it never needs admin rights and
  never affects other accounts on the machine.

.PARAMETER ExePath
  Path to DMC-Native-Reader.exe. Defaults to the copy next to this script,
  which is how it ships inside the release zip.
#>
param(
  [string]$ExePath = (Join-Path $PSScriptRoot "DMC-Native-Reader.exe")
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $ExePath)) {
  throw "DMC-Native-Reader.exe not found at: $ExePath"
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$progId = "DMCNativeReader.Resource"
$openCommand = "`"$ExePath`" `"%1`""

function Set-DefaultValue([string]$Path, [string]$Value) {
  New-Item -Path $Path -Force | Out-Null
  Set-Item -Path $Path -Value $Value
}

Set-DefaultValue "HKCU:\Software\Classes\$progId" "DMC Native Reader resource"
Set-DefaultValue "HKCU:\Software\Classes\$progId\DefaultIcon" "$ExePath,0"
Set-DefaultValue "HKCU:\Software\Classes\$progId\shell\open\command" $openCommand

foreach ($ext in ".scm", ".ptx") {
  Set-DefaultValue "HKCU:\Software\Classes\$ext" $progId
}

Set-DefaultValue "HKCU:\Software\Classes\Applications\DMCNativeReader.exe\shell\open\command" $openCommand
foreach ($ext in ".dds", ".mod", ".scm", ".ptx") {
  New-Item -Path "HKCU:\Software\Classes\$ext\OpenWithList\DMCNativeReader.exe" -Force | Out-Null
}

Write-Host "Registered DMC Native Reader:"
Write-Host "  .scm, .ptx  -> default opener"
Write-Host "  .dds, .mod  -> added to 'Open with' (existing default kept)"
Write-Host "Run Unregister-OpenWith.ps1 to undo."
