# Updated: Generate-Diagrams.ps1

#Execution:
#Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#.\generate-diagrams.ps1


$ErrorActionPreference = "Stop"

$basePath = "$PSScriptRoot/docs"
$pumlPath = Join-Path $basePath "diagrams"
$pngPath  = Join-Path $basePath "images"
$svgPath  = Join-Path $basePath "svg"
$plantumlJar = "plantuml.jar"

New-Item -ItemType Directory -Force -Path $pngPath | Out-Null
New-Item -ItemType Directory -Force -Path $svgPath | Out-Null

if (-not (Test-Path $plantumlJar)) {
    Write-Error "Missing plantuml.jar. Download from: https://plantuml.com/download"
    exit 1
}

if (-not (Get-Command java -ErrorAction SilentlyContinue)) {
    Write-Error "Java is not installed or not in PATH."
    exit 1
}

# Process each .puml file
Get-ChildItem -Path $pumlPath -Filter "*.puml" -Recurse | ForEach-Object {
    $file = $_.FullName
    $filename = $_.BaseName

    Write-Host "Rendering $filename..."

    java -jar $plantumlJar -tpng $file -o $pngPath
    java -jar $plantumlJar -tsvg $file -o $svgPath
}

Write-Host "`n✅ Done. Check 'docs/images' and 'docs/svg'."
