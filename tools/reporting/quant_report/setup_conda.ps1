param(
    [string]$EnvName = 'Quant'
)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $here

if (-not (Get-Command conda -ErrorAction SilentlyContinue)) {
    Write-Error 'conda not found on PATH. Install Anaconda/Miniconda or add conda to PATH.'
    Pop-Location
    exit 1
}

conda env update -n $EnvName -f environment.yml

Pop-Location
