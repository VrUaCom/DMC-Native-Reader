<#
.SYNOPSIS
  Remove the per-user file-type registration created by Register-OpenWith.ps1.

.DESCRIPTION
  Reverses every key Register-OpenWith.ps1 wrote under HKEY_CURRENT_USER.
  .scm and .ptx return to "no association" (their state before registration)
  rather than being pointed at some other default -- this script only ever
  owned that specific assignment, never a prior one. .dds and .mod keep
  whatever default they already had; only the "Open with" list entry this
  reader added is removed.
#>

$ErrorActionPreference = "SilentlyContinue"
$progId = "DMCNativeReader.Resource"

foreach ($ext in ".scm", ".ptx") {
  $key = "HKCU:\Software\Classes\$ext"
  if ((Test-Path $key) -and ((Get-Item $key).GetValue("") -eq $progId)) {
    Remove-Item -Path $key -Force -Recurse
  }
}

foreach ($ext in ".dds", ".mod", ".scm", ".ptx") {
  Remove-Item -Path "HKCU:\Software\Classes\$ext\OpenWithList\DMCNativeReader.exe" -Force -Recurse
}

Remove-Item -Path "HKCU:\Software\Classes\$progId" -Force -Recurse
Remove-Item -Path "HKCU:\Software\Classes\Applications\DMCNativeReader.exe" -Force -Recurse

Write-Host "DMC Native Reader file-type registration removed."
