$ErrorActionPreference = 'Stop'

$workspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$environmentFile = Join-Path $workspaceRoot '.env'
$runtimeDirectory = Join-Path $PSScriptRoot 'runtime'
$flowFile = Join-Path $PSScriptRoot 'flows.json'
$settingsFile = Join-Path $PSScriptRoot 'settings.js'

if (-not (Test-Path -LiteralPath $environmentFile)) {
    throw "Missing $environmentFile"
}

Get-Content -LiteralPath $environmentFile | ForEach-Object {
    if ($_ -match '^([^#=]+)=(.*)$') {
        $name = $matches[1].Trim()
        $value = $matches[2].Trim()
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

if (-not (Get-Command node-red -ErrorAction SilentlyContinue)) {
    throw 'Node-RED is not installed. Run: npm install -g --unsafe-perm node-red'
}

if (-not (Test-Path -LiteralPath $runtimeDirectory)) {
    New-Item -ItemType Directory -Path $runtimeDirectory | Out-Null
}

Write-Host 'Starting Smart Farm Node-RED at http://127.0.0.1:1880'
Write-Host 'Press Ctrl+C to stop it.'

node-red --userDir $runtimeDirectory --settings $settingsFile $flowFile
