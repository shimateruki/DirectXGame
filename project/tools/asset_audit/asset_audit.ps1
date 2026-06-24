param(
    [string]$Root = "Resources",
    [int]$Top = 120,
    [double]$HeavyTextureMB = 4.0,
    [double]$HeavyModelMB = 2.0,
    [double]$HeavyAudioMB = 3.0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$script:ProjectRoot = [System.IO.Path]::GetFullPath((Get-Location).Path)
if ([System.IO.Path]::IsPathRooted($Root)) {
    $script:ResourceRoot = [System.IO.Path]::GetFullPath($Root)
} else {
    $script:ResourceRoot = [System.IO.Path]::GetFullPath((Join-Path $script:ProjectRoot $Root))
}

if (-not (Test-Path -LiteralPath $script:ResourceRoot -PathType Container)) {
    throw "Resource root was not found: $script:ResourceRoot"
}

$script:OutputDir = Join-Path $script:ResourceRoot ".cache\asset_audit"
New-Item -ItemType Directory -Path $script:OutputDir -Force | Out-Null

try {
    Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
} catch {
}

function New-StringSet {
    return New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
}

function ConvertTo-RelativeSlash {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }

    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full.StartsWith($script:ProjectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $relative = $full.Substring($script:ProjectRoot.Length).TrimStart('\', '/')
        return $relative.Replace('\', '/')
    }

    return $full.Replace('\', '/')
}

function ConvertTo-FullPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $script:ProjectRoot $Path))
}

function Normalize-ReferenceText {
    param([string]$Text)
    if ($null -eq $Text) {
        return ""
    }

    $normalized = $Text.Trim().Trim('"').Trim("'").Replace('\', '/')
    while ($normalized.StartsWith("./", [System.StringComparison]::Ordinal)) {
        $normalized = $normalized.Substring(2)
    }
    return $normalized
}

$script:AudioKindByRelativePath = @{}

function Register-AudioSettingKinds {
    $settingsPath = Join-Path $script:ProjectRoot "Resources/json/audio/audio_settings.json"
    if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
        return
    }

    try {
        $json = Get-Content -LiteralPath $settingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not ($json.PSObject.Properties.Name -contains "entries")) {
            return
        }

        foreach ($entry in @($json.entries)) {
            if (-not ($entry.PSObject.Properties.Name -contains "path")) {
                continue
            }

            $rawPath = Normalize-ReferenceText ([string]$entry.path)
            if ([string]::IsNullOrWhiteSpace($rawPath)) {
                continue
            }

            if (-not $rawPath.StartsWith("Resources/", [System.StringComparison]::OrdinalIgnoreCase)) {
                $rawPath = "Resources/audio/" + $rawPath.TrimStart("/", "\")
            }

            $fullPath = ConvertTo-FullPath $rawPath
            $relative = (ConvertTo-RelativeSlash $fullPath).ToLowerInvariant()
            $kind = "Audio"
            if ($entry.PSObject.Properties.Name -contains "type") {
                $type = ([string]$entry.type).ToLowerInvariant()
                if ($type.Contains("bgm")) {
                    $kind = "Audio-BGM"
                } elseif ($type.Contains("se")) {
                    $kind = "Audio-SE"
                }
            }

            if ($kind -eq "Audio") {
                $lowerPath = $relative.Replace('\', '/')
                if ($lowerPath.Contains("/bgm/") -or $lowerPath.Contains("bgm")) {
                    $kind = "Audio-BGM"
                } elseif ($lowerPath.Contains("/se/") -or $lowerPath.Contains("se")) {
                    $kind = "Audio-SE"
                }
            }

            $script:AudioKindByRelativePath[$relative] = $kind
        }
    } catch {
    }
}

function Get-AudioCategory {
    param([System.IO.FileInfo]$File)

    $relative = (ConvertTo-RelativeSlash $File.FullName).ToLowerInvariant()
    if ($script:AudioKindByRelativePath.ContainsKey($relative)) {
        return $script:AudioKindByRelativePath[$relative]
    }

    $lower = $relative.Replace('\', '/')
    if ($lower.Contains("/bgm/") -or $lower.Contains("bgm")) {
        return "Audio-BGM"
    }
    if ($lower.Contains("/se/") -or $lower.Contains("se")) {
        return "Audio-SE"
    }
    return "Audio"
}

function Get-CleanReferencePath {
    param([string]$Text)

    $value = Normalize-ReferenceText $Text
    if ([string]::IsNullOrWhiteSpace($value)) {
        return ""
    }

    if ($value.StartsWith("data:", [System.StringComparison]::OrdinalIgnoreCase)) {
        return ""
    }

    $pattern = "[A-Za-z0-9_\-./\\()\[\]+:]+"
    if ($value -match "^(Resources[\\/]$pattern)") {
        return $matches[1]
    }
    if ($value -match "^($pattern\.(png|dds|jpg|jpeg|bmp|tga|gltf|glb|obj|fbx|bin|mtl|wav|mp3|ogg|json))($|[^A-Za-z0-9_./\\-])") {
        return $matches[1]
    }
    if ($value -match "^($pattern[\\/]$pattern)$") {
        return $matches[1]
    }
    if ($value -match "^[A-Za-z]:[\\/]$pattern$") {
        return $value
    }

    return ""
}

function Get-SizeText {
    param([Int64]$Bytes)

    $units = @("B", "KB", "MB", "GB")
    $value = [double][Math]::Max(0, $Bytes)
    $unitIndex = 0
    while ($value -ge 1024.0 -and $unitIndex -lt ($units.Count - 1)) {
        $value = $value / 1024.0
        $unitIndex++
    }

    if ($unitIndex -eq 0) {
        return ("{0} {1}" -f $Bytes, $units[$unitIndex])
    }
    return ("{0:N1} {1}" -f $value, $units[$unitIndex])
}

function Get-AssetCategory {
    param([System.IO.FileInfo]$File)

    $ext = $File.Extension.ToLowerInvariant()
    switch ($ext) {
        ".png"  { return "Texture" }
        ".dds"  { return "Texture" }
        ".jpg"  { return "Texture" }
        ".jpeg" { return "Texture" }
        ".bmp"  { return "Texture" }
        ".tga"  { return "Texture" }
        ".gltf" { return "Model" }
        ".glb"  { return "Model" }
        ".obj"  { return "Model" }
        ".fbx"  { return "Model" }
        ".bin"  { return "ModelData" }
        ".mtl"  { return "ModelData" }
        ".wav"  { return Get-AudioCategory $File }
        ".mp3"  { return Get-AudioCategory $File }
        ".ogg"  { return Get-AudioCategory $File }
        default { return "Other" }
    }
}

function Test-IsAssetFile {
    param([System.IO.FileInfo]$File)

    $category = Get-AssetCategory $File
    return $category -ne "Other"
}

function Test-IsGeneratedOrCache {
    param([string]$RelativePath)

    $path = $RelativePath.Replace('\', '/')
    $lower = $path.ToLowerInvariant()
    if ($lower.StartsWith("resources/.cache/")) { return $true }
    if ($lower.StartsWith("resources/.trash/")) { return $true }
    if ($lower.Contains("/bakedshader/")) { return $true }
    if ($lower -match "_lod[0-9]*\.(obj|gltf|glb)$") { return $true }
    if ($lower -match "_lod(_report)?\.json$") { return $true }
    if ($lower -match "_lod_report\.json$") { return $true }
    return $false
}

function Get-TextureInfo {
    param([System.IO.FileInfo]$File)

    $result = [ordered]@{
        width = 0
        height = 0
        format = ""
    }

    $ext = $File.Extension.ToLowerInvariant()
    if ($ext -eq ".dds") {
        try {
            $bytes = [System.IO.File]::ReadAllBytes($File.FullName)
            if ($bytes.Length -ge 20 -and [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -eq "DDS ") {
                $result.height = [BitConverter]::ToInt32($bytes, 12)
                $result.width = [BitConverter]::ToInt32($bytes, 16)
                $result.format = "DDS"
            }
        } catch {
        }
        return [pscustomobject]$result
    }

    try {
        $image = [System.Drawing.Image]::FromFile($File.FullName)
        try {
            $result.width = [int]$image.Width
            $result.height = [int]$image.Height
            $result.format = $image.RawFormat.ToString()
        } finally {
            $image.Dispose()
        }
    } catch {
    }

    return [pscustomobject]$result
}

function Get-ObjInfo {
    param([System.IO.FileInfo]$File)

    $vertices = 0
    $triangles = 0
    try {
        foreach ($line in [System.IO.File]::ReadLines($File.FullName)) {
            if ($line.StartsWith("v ")) {
                $vertices++
            } elseif ($line.StartsWith("f ")) {
                $parts = $line.Trim().Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
                if ($parts.Length -ge 4) {
                    $triangles += ($parts.Length - 3)
                }
            }
        }
    } catch {
    }

    return [pscustomobject]@{
        vertices = $vertices
        triangles = $triangles
        hasSkin = $false
        hasAnimation = $false
    }
}

function Get-GltfInfo {
    param([System.IO.FileInfo]$File)

    $vertices = 0
    $triangles = 0
    $hasSkin = $false
    $hasAnimation = $false

    try {
        $json = Get-Content -LiteralPath $File.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($json.PSObject.Properties.Name -contains "skins" -and $null -ne $json.skins) {
            $hasSkin = @($json.skins).Count -gt 0
        }
        if ($json.PSObject.Properties.Name -contains "animations" -and $null -ne $json.animations) {
            $hasAnimation = @($json.animations).Count -gt 0
        }

        if ($json.PSObject.Properties.Name -contains "meshes" -and $json.PSObject.Properties.Name -contains "accessors") {
            $accessors = @($json.accessors)
            foreach ($mesh in @($json.meshes)) {
                if (-not ($mesh.PSObject.Properties.Name -contains "primitives")) {
                    continue
                }
                foreach ($primitive in @($mesh.primitives)) {
                    if ($primitive.PSObject.Properties.Name -contains "attributes" -and
                        $primitive.attributes.PSObject.Properties.Name -contains "POSITION") {
                        $positionIndex = [int]$primitive.attributes.POSITION
                        if ($positionIndex -ge 0 -and $positionIndex -lt $accessors.Count) {
                            $vertices += [int]$accessors[$positionIndex].count
                        }
                    }

                    if ($primitive.PSObject.Properties.Name -contains "indices") {
                        $index = [int]$primitive.indices
                        if ($index -ge 0 -and $index -lt $accessors.Count) {
                            $triangles += [Math]::Floor([double]$accessors[$index].count / 3.0)
                        }
                    } elseif ($primitive.PSObject.Properties.Name -contains "attributes" -and
                        $primitive.attributes.PSObject.Properties.Name -contains "POSITION") {
                        $positionIndex = [int]$primitive.attributes.POSITION
                        if ($positionIndex -ge 0 -and $positionIndex -lt $accessors.Count) {
                            $triangles += [Math]::Floor([double]$accessors[$positionIndex].count / 3.0)
                        }
                    }
                }
            }
        }
    } catch {
    }

    return [pscustomobject]@{
        vertices = $vertices
        triangles = [int]$triangles
        hasSkin = $hasSkin
        hasAnimation = $hasAnimation
    }
}

function Add-UsedFile {
    param(
        [System.Collections.Generic.HashSet[string]]$UsedFiles,
        [string]$FullPath
    )

    if ([string]::IsNullOrWhiteSpace($FullPath)) {
        return
    }

    if (-not (Test-Path -LiteralPath $FullPath -PathType Leaf)) {
        return
    }

    $relative = ConvertTo-RelativeSlash $FullPath
    [void]$UsedFiles.Add($relative)

    $ext = [System.IO.Path]::GetExtension($FullPath).ToLowerInvariant()
    if ($ext -eq ".png") {
        $dds = [System.IO.Path]::ChangeExtension($FullPath, ".dds")
        if (Test-Path -LiteralPath $dds -PathType Leaf) {
            [void]$UsedFiles.Add((ConvertTo-RelativeSlash $dds))
        }
    } elseif ($ext -eq ".dds") {
        $png = [System.IO.Path]::ChangeExtension($FullPath, ".png")
        if (Test-Path -LiteralPath $png -PathType Leaf) {
            [void]$UsedFiles.Add((ConvertTo-RelativeSlash $png))
        }
    }
}

function Add-GltfDependencies {
    param(
        [System.Collections.Generic.HashSet[string]]$UsedFiles,
        [string]$GltfPath
    )

    if (-not (Test-Path -LiteralPath $GltfPath -PathType Leaf)) {
        return
    }

    try {
        $json = Get-Content -LiteralPath $GltfPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $baseDir = Split-Path -Parent $GltfPath

        foreach ($collectionName in @("buffers", "images")) {
            if (-not ($json.PSObject.Properties.Name -contains $collectionName)) {
                continue
            }
            foreach ($entry in @($json.$collectionName)) {
                if ($entry.PSObject.Properties.Name -contains "uri") {
                    $uri = Normalize-ReferenceText ([string]$entry.uri)
                    if ($uri -and -not $uri.StartsWith("data:", [System.StringComparison]::OrdinalIgnoreCase)) {
                        $dep = [System.IO.Path]::GetFullPath((Join-Path $baseDir $uri))
                        Add-UsedFile $UsedFiles $dep
                    }
                }
            }
        }
    } catch {
    }
}

function Add-DirectoryReference {
    param(
        [System.Collections.Generic.HashSet[string]]$UsedFiles,
        [string]$DirectoryPath
    )

    if (-not (Test-Path -LiteralPath $DirectoryPath -PathType Container)) {
        return $false
    }

    $sourceFiles = Get-ChildItem -LiteralPath $DirectoryPath -File -ErrorAction SilentlyContinue |
        Where-Object { @(".gltf", ".glb", ".obj") -contains $_.Extension.ToLowerInvariant() }

    if (@($sourceFiles).Count -gt 0) {
        foreach ($file in $sourceFiles) {
            Add-UsedFile $UsedFiles $file.FullName
            if ($file.Extension.ToLowerInvariant() -eq ".gltf") {
                Add-GltfDependencies $UsedFiles $file.FullName
            }
        }
    } else {
        foreach ($file in Get-ChildItem -LiteralPath $DirectoryPath -File -ErrorAction SilentlyContinue) {
            Add-UsedFile $UsedFiles $file.FullName
        }
    }

    return $true
}

function Test-ReferenceLooksLikePath {
    param([string]$Text)

    $value = Get-CleanReferencePath $Text
    if ([string]::IsNullOrWhiteSpace($value)) { return $false }
    if ($value -match "^[A-Za-z]:/") { return $true }
    if ($value.StartsWith("Resources/", [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    if ($value.Contains("/") -or $value.Contains("\")) { return $true }
    if ($value -match "\.(png|dds|jpg|jpeg|bmp|tga|gltf|glb|obj|fbx|bin|mtl|wav|mp3|ogg|json)$") { return $true }
    return $false
}

function Resolve-Reference {
    param([string]$Value)

    $value = Get-CleanReferencePath $Value
    $candidates = New-Object "System.Collections.Generic.List[string]"
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $candidates
    }

    if ($value.StartsWith("Resources/", [System.StringComparison]::OrdinalIgnoreCase)) {
        $candidates.Add((ConvertTo-FullPath $value))
        return $candidates
    }

    if ([System.IO.Path]::IsPathRooted($value)) {
        $candidates.Add([System.IO.Path]::GetFullPath($value))
        return $candidates
    }

    $ext = [System.IO.Path]::GetExtension($value).ToLowerInvariant()
    if ($ext) {
        foreach ($prefix in @("Resources", "Resources/sprite", "Resources/texture", "Resources/3DModel", "Resources/json")) {
            $candidates.Add((ConvertTo-FullPath (Join-Path $prefix $value)))
        }
    } else {
        $candidates.Add((ConvertTo-FullPath (Join-Path "Resources/3DModel" $value)))
        $leaf = Split-Path -Leaf $value
        if ($leaf) {
            foreach ($extName in @(".gltf", ".glb", ".obj")) {
                $candidates.Add((ConvertTo-FullPath (Join-Path (Join-Path "Resources/3DModel" $value) ($leaf + $extName))))
                $candidates.Add((ConvertTo-FullPath (Join-Path "Resources/3DModel" ($value + $extName))))
            }
        }
        $candidates.Add((ConvertTo-FullPath (Join-Path "Resources/json" ($value + ".json"))))
    }

    return $candidates
}

function Add-Reference {
    param(
        [System.Collections.Generic.HashSet[string]]$UsedFiles,
        [System.Collections.Generic.List[object]]$MissingReferences,
        [string]$Source,
        [string]$Value
    )

    if (-not (Test-ReferenceLooksLikePath $Value)) {
        return
    }

    $candidates = Resolve-Reference $Value
    $matched = $false
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Add-UsedFile $UsedFiles $candidate
            if ([System.IO.Path]::GetExtension($candidate).ToLowerInvariant() -eq ".gltf") {
                Add-GltfDependencies $UsedFiles $candidate
            }
            $matched = $true
        } elseif (Test-Path -LiteralPath $candidate -PathType Container) {
            if (Add-DirectoryReference $UsedFiles $candidate) {
                $matched = $true
            }
        }
    }

    $cleanValue = Get-CleanReferencePath $Value
    if (-not $matched -and $cleanValue.StartsWith("Resources/", [System.StringComparison]::OrdinalIgnoreCase)) {
        $MissingReferences.Add([pscustomobject]@{
            source = $Source
            value = $cleanValue
            expectedCandidates = @($candidates | ForEach-Object { ConvertTo-RelativeSlash $_ })
        })
    }
}

function Add-JsonStringReferences {
    param(
        [object]$Node,
        [System.Collections.Generic.HashSet[string]]$UsedFiles,
        [System.Collections.Generic.List[object]]$MissingReferences,
        [string]$Source
    )

    if ($null -eq $Node) {
        return
    }

    if ($Node -is [string]) {
        Add-Reference $UsedFiles $MissingReferences $Source $Node
        return
    }

    if ($Node -is [System.Collections.IDictionary]) {
        foreach ($key in $Node.Keys) {
            Add-JsonStringReferences $Node[$key] $UsedFiles $MissingReferences $Source
        }
        return
    }

    if ($Node -is [System.Collections.IEnumerable] -and -not ($Node -is [string])) {
        foreach ($item in $Node) {
            Add-JsonStringReferences $item $UsedFiles $MissingReferences $Source
        }
        return
    }

    if ($Node.PSObject -and $Node.PSObject.Properties) {
        foreach ($property in $Node.PSObject.Properties) {
            Add-JsonStringReferences $property.Value $UsedFiles $MissingReferences $Source
        }
    }
}

function Add-CodeReferences {
    param(
        [System.Collections.Generic.HashSet[string]]$UsedFiles,
        [System.Collections.Generic.List[object]]$MissingReferences
    )

    $scanRoots = @("engine", "game", "application", "Resources/json")
    $extensions = @(".cpp", ".h", ".hpp", ".c", ".cs", ".json", ".ps1", ".bat")
    foreach ($scanRoot in $scanRoots) {
        $fullRoot = ConvertTo-FullPath $scanRoot
        if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
            continue
        }

        foreach ($file in Get-ChildItem -LiteralPath $fullRoot -Recurse -File -ErrorAction SilentlyContinue) {
            if (-not ($extensions -contains $file.Extension.ToLowerInvariant())) {
                continue
            }

            $source = ConvertTo-RelativeSlash $file.FullName
            try {
                if ($file.Extension.ToLowerInvariant() -eq ".json") {
                    $json = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
                    Add-JsonStringReferences $json $UsedFiles $MissingReferences $source
                }

                $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
                $matches = [regex]::Matches($text, '"([^"]*Resources[\\/][^"]+)"')
                foreach ($match in $matches) {
                    Add-Reference $UsedFiles $MissingReferences $source $match.Groups[1].Value
                }
            } catch {
            }
        }
    }
}

Register-AudioSettingKinds

$allFiles = @(Get-ChildItem -LiteralPath $script:ResourceRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {
        $relative = ConvertTo-RelativeSlash $_.FullName
        $lower = $relative.Replace('\', '/').ToLowerInvariant()
        -not $lower.StartsWith("resources/.cache/") -and -not $lower.StartsWith("resources/.trash/")
    })

$usedFiles = New-StringSet
$missingReferences = New-Object "System.Collections.Generic.List[object]"
Add-CodeReferences $usedFiles $missingReferences

$heavyAssets = New-Object "System.Collections.Generic.List[object]"
foreach ($file in $allFiles) {
    if (-not (Test-IsAssetFile $file)) {
        continue
    }

    $relative = ConvertTo-RelativeSlash $file.FullName
    $category = Get-AssetCategory $file
    $thresholdBytes = 0
    switch ($category) {
        "Texture" { $thresholdBytes = [int64]($HeavyTextureMB * 1024 * 1024) }
        "Model" { $thresholdBytes = [int64]($HeavyModelMB * 1024 * 1024) }
        "ModelData" { $thresholdBytes = [int64]($HeavyModelMB * 1024 * 1024) }
        "Audio" { $thresholdBytes = [int64]($HeavyAudioMB * 1024 * 1024) }
        "Audio-BGM" { $thresholdBytes = [int64]($HeavyAudioMB * 1024 * 1024) }
        "Audio-SE" { $thresholdBytes = [int64]($HeavyAudioMB * 1024 * 1024) }
        default { $thresholdBytes = [int64](8 * 1024 * 1024) }
    }

    $info = [ordered]@{
        category = $category
        path = $relative
        sizeBytes = [int64]$file.Length
        sizeText = Get-SizeText $file.Length
        width = 0
        height = 0
        vertices = 0
        triangles = 0
        notes = @()
        severity = "info"
    }

    if ($category -eq "Texture") {
        $texture = Get-TextureInfo $file
        $info.width = [int]$texture.width
        $info.height = [int]$texture.height
        if ($info.width -gt 2048 -or $info.height -gt 2048) {
            $info.notes += "Large texture resolution"
        }
    } elseif ($file.Extension.ToLowerInvariant() -eq ".gltf") {
        $gltf = Get-GltfInfo $file
        $info.vertices = [int]$gltf.vertices
        $info.triangles = [int]$gltf.triangles
        if ($gltf.hasSkin) { $info.notes += "Skinned model" }
        if ($gltf.hasAnimation) { $info.notes += "Animation model" }
    } elseif ($file.Extension.ToLowerInvariant() -eq ".obj") {
        $obj = Get-ObjInfo $file
        $info.vertices = [int]$obj.vertices
        $info.triangles = [int]$obj.triangles
    }

    if ($file.Length -ge $thresholdBytes) {
        $info.severity = "warning"
        $info.notes += "File size is over threshold"
    }
    if ($info.triangles -ge 20000) {
        $info.severity = "warning"
        $info.notes += "High triangle count"
    }

    if ($info.severity -eq "warning" -or $heavyAssets.Count -lt $Top) {
        $heavyAssets.Add([pscustomobject]$info)
    }
}

$heavyAssetsSorted = @($heavyAssets | Sort-Object -Property @{ Expression = { $_.severity -eq "warning" }; Descending = $true }, @{ Expression = { $_.sizeBytes }; Descending = $true } | Select-Object -First $Top)

$unusedAssets = New-Object "System.Collections.Generic.List[object]"
foreach ($file in $allFiles) {
    if (-not (Test-IsAssetFile $file)) {
        continue
    }

    $relative = ConvertTo-RelativeSlash $file.FullName
    if (Test-IsGeneratedOrCache $relative) {
        continue
    }

    if (-not $usedFiles.Contains($relative)) {
        $unusedAssets.Add([pscustomobject]@{
            category = Get-AssetCategory $file
            path = $relative
            sizeBytes = [int64]$file.Length
            sizeText = Get-SizeText $file.Length
            reason = "No direct reference was found in JSON/code scan"
        })
    }
}

$unusedAssetsSorted = @($unusedAssets | Sort-Object -Property @{ Expression = { $_.sizeBytes }; Descending = $true } | Select-Object -First $Top)
$totalBytes = [int64](($allFiles | Measure-Object -Property Length -Sum).Sum)
$unusedBytes = [int64](($unusedAssetsSorted | Measure-Object -Property sizeBytes -Sum).Sum)
$warningCount = @($heavyAssetsSorted | Where-Object { $_.severity -eq "warning" }).Count
$totalSizeText = Get-SizeText ([int64]$totalBytes)
$unusedSizeText = Get-SizeText ([int64]$unusedBytes)
$heavyAssetsArray = @($heavyAssetsSorted)
$unusedAssetsArray = @($unusedAssetsSorted)
$missingReferencesArray = @($missingReferences.ToArray())

$report = [ordered]@{
    schemaVersion = 1
    generatedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    root = (ConvertTo-RelativeSlash $script:ResourceRoot)
    summary = [ordered]@{
        totalFiles = @($allFiles).Count
        totalBytes = $totalBytes
        totalSizeText = $totalSizeText
        usedFiles = $usedFiles.Count
        unusedFiles = $unusedAssetsArray.Count
        unusedBytes = $unusedBytes
        unusedSizeText = $unusedSizeText
        heavyWarningCount = $warningCount
        missingReferenceCount = $missingReferencesArray.Count
    }
    thresholds = [ordered]@{
        heavyTextureMB = $HeavyTextureMB
        heavyModelMB = $HeavyModelMB
        heavyAudioMB = $HeavyAudioMB
        top = $Top
    }
    heavyAssets = $heavyAssetsArray
    unusedAssets = $unusedAssetsArray
    missingReferences = $missingReferencesArray
}

$jsonPath = Join-Path $script:OutputDir "latest_report.json"
$mdPath = Join-Path $script:OutputDir "asset_audit_report.md"
$jsonText = $report | ConvertTo-Json -Depth 16
[System.IO.File]::WriteAllText($jsonPath, $jsonText, [System.Text.UTF8Encoding]::new($false))

$markdown = New-Object "System.Collections.Generic.List[string]"
$markdown.Add("# Asset Audit Report")
$markdown.Add("")
$markdown.Add("- Generated: $($report.generatedAt)")
$markdown.Add("- Root: $($report.root)")
$markdown.Add("- Total: $($report.summary.totalFiles) files / $($report.summary.totalSizeText)")
$markdown.Add("- Heavy warnings: $($report.summary.heavyWarningCount)")
$markdown.Add("- Unused candidates: $($report.summary.unusedFiles) / $($report.summary.unusedSizeText)")
$markdown.Add("- Missing references: $($report.summary.missingReferenceCount)")
$markdown.Add("")
$markdown.Add("## Heavy Assets")
foreach ($item in $heavyAssetsSorted | Select-Object -First 30) {
    $markdown.Add("- [$($item.category)] $($item.path) - $($item.sizeText)")
}
$markdown.Add("")
$markdown.Add("## Unused Asset Candidates")
foreach ($item in $unusedAssetsSorted | Select-Object -First 50) {
    $markdown.Add("- [$($item.category)] $($item.path) - $($item.sizeText)")
}
[System.IO.File]::WriteAllLines($mdPath, $markdown, [System.Text.UTF8Encoding]::new($false))

Write-Host ("Asset audit completed: {0}" -f (ConvertTo-RelativeSlash $jsonPath))
