<#
.SYNOPSIS
  Register DMC Native Reader as a file opener for DMC3 resource files, per-user.

.DESCRIPTION
  Sets .scm and .ptx as the default opener (both are unclaimed on a stock
  Windows install) and adds DMC Native Reader to the Explorer "Open with"
  list for every resource family exposed by the Windows v68 shell. Existing
  defaults are preserved for all other extensions. Writes only to
  HKEY_CURRENT_USER, so it never needs admin rights and never affects other
  accounts on the machine.

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
$openWithExtensions = @(
  ".dds", ".mod", ".scm", ".ptx", ".tm2", ".pac", ".mot", ".efm",
  ".shw", ".tsc", ".clt", ".fxbank", ".pnst", ".msc", ".colshape", ".colidx"
)
foreach ($ext in $openWithExtensions) {
  New-Item -Path "HKCU:\Software\Classes\$ext\OpenWithList\DMCNativeReader.exe" -Force | Out-Null
}

Write-Host "Registered DMC Native Reader:"
Write-Host "  .scm, .ptx  -> default opener"
Write-Host "  current DMC resource families -> added to 'Open with' (existing defaults kept)"
Write-Host "Run Unregister-OpenWith.ps1 to undo."
