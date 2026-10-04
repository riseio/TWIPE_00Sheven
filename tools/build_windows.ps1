[CmdletBinding()]
param([Parameter(Mandatory = $true)][string] $Rom)

$ErrorActionPreference = 'Stop'
$projectPath = Split-Path -Parent $PSScriptRoot
$romPath = (Get-Item -LiteralPath $Rom).FullName
function Invoke-Native {
    param([string] $Command, [Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE." }
}
Push-Location -LiteralPath $projectPath
$previousNodeReuse = $env:MSBUILDDISABLENODEREUSE
try {
    $env:MSBUILDDISABLENODEREUSE = '1'
    $pythonPath = Join-Path $projectPath 'build\python\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $pythonPath)) {
        Invoke-Native py -m venv (Join-Path $projectPath 'build\python')
    }
    Invoke-Native $pythonPath -m pip install -r requirements.txt
    Invoke-Native $pythonPath tools/generate_recomp.py --rom $romPath `
        --build-dir (Join-Path $projectPath 'build\windows') `
        --tools-preset windows-tools --runtime-preset windows-release
    Invoke-Native cmake --build --preset windows-release --parallel 1
}
finally {
    $env:MSBUILDDISABLENODEREUSE = $previousNodeReuse
    Pop-Location
}
