$ErrorActionPreference = 'Stop'

$progId = 'DMCNativeReader.Resource'

foreach ($ext in '.mod', '.scm', '.dds', '.ptx') {
    $openWith = "HKCU:\Software\Classes\$ext\OpenWithProgids"
    if (Test-Path $openWith) {
        Remove-ItemProperty -Path $openWith -Name $progId -ErrorAction SilentlyContinue
    }
}

$progRoot = "HKCU:\Software\Classes\$progId"
if (Test-Path $progRoot) {
    Remove-Item -Path $progRoot -Recurse -Force
}

Write-Host 'DMC Native Reader Open with registration removed.'
