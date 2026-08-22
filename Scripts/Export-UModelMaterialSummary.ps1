[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-TextureSlotGuess {
    param(
        [string]$Key,
        [string]$Value
    )

    $text = (($Key + " " + $Value).ToLowerInvariant())

    if ($text -match "basecolor|albedo|diffuse") { return "BaseColor" }
    if ($text -match "normal") { return "Normal" }
    if ($text -match "orm|occlusionroughnessmetallic|roughness|metallic|specular|ao") { return "SurfacePacked" }
    if ($text -match "opacity|alpha") { return "Opacity" }
    if ($text -match "emissive|glow") { return "Emissive" }
    if ($text -match "mask") { return "Mask" }
    if ($text -match "ramp|lut|gradient") { return "RampOrLUT" }
    if ($text -match "sdf") { return "SDF" }
    if ($text -match "hair") { return "Hair" }
    if ($text -match "eye|iris|pupil") { return "Eye" }

    return "Unknown"
}

function Test-LooksLikeTextureRef {
    param(
        [string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }

    $trimmed = $Value.Trim()

    if ($trimmed -match "\.(tga|png|dds|bmp|jpg|jpeg|tif|tiff|exr|hdr)$") {
        return $true
    }

    if ($trimmed -match "(^|[/\\._-])(tex|texture|diff|diffuse|albedo|basecolor|normal|orm|mask|ramp|lut|sdf)($|[/\\._-])") {
        return $true
    }

    return $false
}

$resolvedRoot = (Resolve-Path -LiteralPath $RootDir).Path

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$resolvedOutput = (Resolve-Path -LiteralPath $OutputDir).Path
$matFiles = Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter *.mat

$materialFiles = New-Object System.Collections.Generic.List[object]
$properties = New-Object System.Collections.Generic.List[object]
$textureRefs = New-Object System.Collections.Generic.List[object]

foreach ($file in $matFiles) {
    $relativePath = $file.FullName.Substring($resolvedRoot.Length).TrimStart('\')
    $materialName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)

    $materialFiles.Add([PSCustomObject]@{
        MaterialName = $materialName
        FileName = $file.Name
        FullPath = $file.FullName
        RelativePath = $relativePath
        Directory = $file.DirectoryName
    })

    $lineIndex = 0
    foreach ($rawLine in Get-Content -LiteralPath $file.FullName) {
        $lineIndex += 1
        $line = $rawLine.Trim()

        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        if ($line.StartsWith("#") -or $line.StartsWith("//") -or $line.StartsWith(";")) {
            continue
        }

        $key = $null
        $value = $null

        if ($line -match '^\s*([^=:]+?)\s*[:=]\s*(.+?)\s*$') {
            $key = $matches[1].Trim()
            $value = $matches[2].Trim()
        }
        else {
            $key = "RawLine"
            $value = $line
        }

        $properties.Add([PSCustomObject]@{
            MaterialName = $materialName
            RelativePath = $relativePath
            LineNumber = $lineIndex
            Property = $key
            Value = $value
        })

        if (Test-LooksLikeTextureRef -Value $value) {
            $textureRefs.Add([PSCustomObject]@{
                MaterialName = $materialName
                RelativePath = $relativePath
                LineNumber = $lineIndex
                Property = $key
                TextureRef = $value
                SlotGuess = Get-TextureSlotGuess -Key $key -Value $value
            })
        }
    }
}

$materialFiles | Sort-Object RelativePath | Export-Csv -LiteralPath (Join-Path $resolvedOutput "UModel_MaterialFiles.csv") -NoTypeInformation -Encoding UTF8

$properties | Sort-Object MaterialName, LineNumber | Export-Csv -LiteralPath (Join-Path $resolvedOutput "UModel_MaterialProperties.csv") -NoTypeInformation -Encoding UTF8

$textureRefs | Sort-Object MaterialName, LineNumber | Export-Csv -LiteralPath (Join-Path $resolvedOutput "UModel_TextureRefs.csv") -NoTypeInformation -Encoding UTF8

Write-Host ("Scanned {0} .mat files from {1}" -f $matFiles.Count, $resolvedRoot)
Write-Host ("Exported CSV files to {0}" -f $resolvedOutput)
