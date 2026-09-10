$ErrorActionPreference = 'Stop'

$exe = Join-Path $PSScriptRoot 'DMC-Native-Reader.exe'
if (-not (Test-Path $exe)) {
    throw "DMC-Native-Reader.exe must be beside this script."
}

$progId = 'DMCNativeReader.Resource'
$progRoot = "HKCU:\Software\Classes\$progId"
$command = '"' + $exe + '" "%1"'

New-Item -Path $progRoot -Force | Out-Null
Set-Item -Path $progRoot -Value 'DMC Native Reader resource'
New-Item -Path "$progRoot\shell\open\command" -Force | Out-Null
Set-Item -Path "$progRoot\shell\open\command" -Value $command

# Add the Reader to Windows "Open with" without claiming ownership or changing
# the user's current default application for any extension (especially DDS).
foreach ($ext in '.mod', '.scm', '.dds', '.ptx') {
    $openWith = "HKCU:\Software\Classes\$ext\OpenWithProgids"
    New-Item -Path $openWith -Force | Out-Null
    New-ItemProperty -Path $openWith -Name $progId -PropertyType String -Value '' -Force | Out-Null
}

Write-Host 'DMC Native Reader registered in Open with for MOD / SCM / DDS / PTX.'
Write-Host 'No default file association was changed.'
