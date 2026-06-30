param(
    [string]$Root = (Get-Location).Path,
    [string]$Model = "",
    [double]$Ratio1 = 0.55,
    [double]$Ratio2 = 0.25,
    [double]$Distance1 = 35.0,
    [double]$Distance2 = 70.0,
    [ValidateSet("auto", "blender", "native")]
    [string]$Backend = "auto",
    [string]$BlenderPath = "",
    [switch]$AnalyzeOnly,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$culture = [System.Globalization.CultureInfo]::InvariantCulture

function To-ForwardSlash([string]$Path) {
    return $Path.Replace('\', '/')
}

function Get-RelativePathCompat {
    param(
        [string]$BasePath,
        [string]$TargetPath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)
    $baseUri = [Uri]$baseFull
    $targetUri = [Uri]$targetFull
    $relativeUri = $baseUri.MakeRelativeUri($targetUri)
    return [Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

function Parse-Double([string]$Text) {
    return [double]::Parse($Text, $culture)
}

function Format-Double([double]$Value) {
    return $Value.ToString("0.######", $culture)
}

function Find-ToolExecutable {
    param([string[]]$Names)

    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
            return $command.Source
        }
    }
    return ""
}

function Find-BundledBlenderExecutable {
    param([string]$RootPath)

    $candidatePaths = @(
        (Join-Path $RootPath "tools/blender/blender.exe"),
        (Join-Path $RootPath "tools/Blender/blender.exe"),
        (Join-Path $RootPath "tools/third_party/blender/blender.exe"),
        (Join-Path $RootPath "ExternalTools/Blender/blender.exe"),
        (Join-Path $RootPath "Resources/tools/blender/blender.exe"),
        (Join-Path $RootPath "Resources/tools/Blender/blender.exe"),
        (Join-Path $RootPath "Resources/tools/Blender 4.4/blender.exe")
    )

    foreach ($candidate in $candidatePaths) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    $searchRoots = @(
        (Join-Path $RootPath "Resources/tools/blender"),
        (Join-Path $RootPath "Resources/tools/Blender")
    )
    foreach ($searchRoot in $searchRoots) {
        if (-not (Test-Path -LiteralPath $searchRoot -PathType Container)) {
            continue
        }
        $found = Get-ChildItem -LiteralPath $searchRoot -Recurse -Filter "blender.exe" -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $found) {
            return [System.IO.Path]::GetFullPath($found.FullName)
        }
    }

    $resourcesToolRoot = Join-Path $RootPath "Resources/tools"
    if (Test-Path -LiteralPath $resourcesToolRoot -PathType Container) {
        $blenderFolders = Get-ChildItem -LiteralPath $resourcesToolRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "Blender*" }
        foreach ($folder in $blenderFolders) {
            $found = Get-ChildItem -LiteralPath $folder.FullName -Recurse -Filter "blender.exe" -File -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($null -ne $found) {
                return [System.IO.Path]::GetFullPath($found.FullName)
            }
        }
    }
    return ""
}

function Resolve-LodBackend {
    param(
        [string]$RequestedBackend,
        [bool]$IsAnalyzeOnly,
        [string]$RootPath,
        [string]$ExplicitBlenderPath
    )

    $requested = $RequestedBackend.ToLowerInvariant()
    if ($requested -eq "native") {
        return [ordered]@{
            name = "native"
            executable = ""
            warnings = @("Native grid simplification is selected. It is fast, but visual quality is weaker than Blender Decimate.")
        }
    }

    $configuredPath = ""
    if (-not [string]::IsNullOrWhiteSpace($ExplicitBlenderPath)) {
        $candidate = $ExplicitBlenderPath.Trim().Trim('"')
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $configuredPath = [System.IO.Path]::GetFullPath($candidate)
        }
        elseif ($requested -eq "blender") {
            if ($IsAnalyzeOnly) {
                return [ordered]@{
                    name = "blender"
                    executable = $candidate
                    warnings = @("The configured Blender path was not found. Analysis can continue, but generation will fail until the path is fixed.")
                }
            }
            throw "Configured Blender path was not found: $candidate"
        }
        else {
            return [ordered]@{
                name = "native"
                executable = ""
                warnings = @("Configured Blender path was not found. Auto backend fell back to native grid simplification: $candidate")
            }
        }
    }

    $blenderPath = if ([string]::IsNullOrWhiteSpace($configuredPath)) {
        $bundledPath = Find-BundledBlenderExecutable $RootPath
        if (-not [string]::IsNullOrWhiteSpace($bundledPath)) {
            $bundledPath
        }
        else {
            Find-ToolExecutable @("blender", "blender.exe")
        }
    }
    else {
        $configuredPath
    }
    if (-not [string]::IsNullOrWhiteSpace($blenderPath)) {
        return [ordered]@{
            name = "blender"
            executable = $blenderPath
            warnings = @()
        }
    }

    if ($requested -eq "blender") {
        if ($IsAnalyzeOnly) {
            return [ordered]@{
                name = "blender"
                executable = ""
                warnings = @("Blender backend is selected, but blender.exe was not found in PATH. Analysis can continue, but generation will fail until Blender is installed or PATH is fixed.")
            }
        }
        throw "Blender backend was selected, but blender.exe was not found in PATH."
    }

    return [ordered]@{
        name = "native"
        executable = ""
        warnings = @("Blender CLI was not found in PATH. Auto backend fell back to native grid simplification.")
    }
}

function Resolve-ModelFile {
    param(
        [string]$RootPath,
        [string]$ModelName
    )

    if ([string]::IsNullOrWhiteSpace($ModelName)) {
        throw "Model name is empty."
    }

    $rootFull = [System.IO.Path]::GetFullPath($RootPath)
    $assetRoot = Join-Path $rootFull "Resources/3DModel"

    $directPath = $ModelName
    if (-not [System.IO.Path]::IsPathRooted($directPath)) {
        $directPath = Join-Path $rootFull $directPath
    }
    if (Test-Path -LiteralPath $directPath -PathType Leaf) {
        return [System.IO.Path]::GetFullPath($directPath)
    }

    $modelPath = $ModelName.Replace('\', '/')
    $candidate = Join-Path $assetRoot $modelPath
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return [System.IO.Path]::GetFullPath($candidate)
    }

    $pathObject = [System.IO.Path]::GetFileName($modelPath)
    $extension = [System.IO.Path]::GetExtension($modelPath)
    if (-not [string]::IsNullOrEmpty($extension)) {
        $directAsset = Join-Path $assetRoot $modelPath
        if (Test-Path -LiteralPath $directAsset -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($directAsset)
        }
    }

    $directory = Join-Path $assetRoot $modelPath
    $stem = $pathObject
    if (Test-Path -LiteralPath $directory -PathType Container) {
        foreach ($ext in @(".obj", ".gltf", ".glb")) {
            $file = Join-Path $directory ($stem + $ext)
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                return [System.IO.Path]::GetFullPath($file)
            }
        }

        $fallback = Get-ChildItem -LiteralPath $directory -File |
            Where-Object { $_.Extension -in @(".obj", ".gltf", ".glb") } |
            Select-Object -First 1
        if ($fallback) {
            return $fallback.FullName
        }
    }

    throw "Model file not found: $ModelName"
}

function ConvertTo-ModelName {
    param(
        [string]$RootPath,
        [string]$FullPath,
        [bool]$KeepExtension
    )

    $assetRoot = [System.IO.Path]::GetFullPath((Join-Path $RootPath "Resources/3DModel"))
    $full = [System.IO.Path]::GetFullPath($FullPath)
    if (-not $full.StartsWith($assetRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return (To-ForwardSlash $FullPath)
    }

    $relative = $full.Substring($assetRoot.Length).TrimStart('\', '/')
    $relative = To-ForwardSlash $relative
    if (-not $KeepExtension) {
        $relative = ([System.IO.Path]::ChangeExtension($relative, $null)).TrimEnd('.')
        $parent = To-ForwardSlash ([System.IO.Path]::GetDirectoryName($relative))
        $leaf = [System.IO.Path]::GetFileName($relative)
        if (-not [string]::IsNullOrEmpty($parent) -and [System.IO.Path]::GetFileName($parent) -eq $leaf) {
            return $parent
        }
    }
    return $relative
}

function Analyze-Obj {
    param([string]$Path)

    $vertexCount = 0
    $uvCount = 0
    $normalCount = 0
    $faceCount = 0
    $triangleCount = 0
    $materials = New-Object 'System.Collections.Generic.HashSet[string]'

    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line.StartsWith("v ")) {
            $vertexCount++
        }
        elseif ($line.StartsWith("vt ")) {
            $uvCount++
        }
        elseif ($line.StartsWith("vn ")) {
            $normalCount++
        }
        elseif ($line.StartsWith("usemtl ")) {
            [void]$materials.Add($line.Substring(7).Trim())
        }
        elseif ($line.StartsWith("f ")) {
            $tokens = $line.Substring(2).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($tokens.Count -ge 3) {
                $faceCount++
                $triangleCount += [Math]::Max(1, $tokens.Count - 2)
            }
        }
    }

    return [ordered]@{
        vertices = $vertexCount
        uvs = $uvCount
        normals = $normalCount
        faces = $faceCount
        triangles = $triangleCount
        materials = $materials.Count
    }
}

function Analyze-Gltf {
    param([string]$Path)

    $jsonText = Get-Content -LiteralPath $Path -Raw
    $data = $jsonText | ConvertFrom-Json
    $meshCount = if ($data.PSObject.Properties.Name -contains "meshes") { $data.meshes.Count } else { 0 }
    $primitiveCount = 0
    $positionVertices = 0
    $indexCount = 0

    if ($data.PSObject.Properties.Name -contains "meshes") {
        foreach ($mesh in $data.meshes) {
            if (-not ($mesh.PSObject.Properties.Name -contains "primitives")) { continue }
            foreach ($primitive in $mesh.primitives) {
                $primitiveCount++
                if (($primitive.PSObject.Properties.Name -contains "attributes") -and
                    ($primitive.attributes.PSObject.Properties.Name -contains "POSITION")) {
                    $accessorIndex = [int]$primitive.attributes.POSITION
                    if ($data.PSObject.Properties.Name -contains "accessors") {
                        $positionVertices += [int]$data.accessors[$accessorIndex].count
                    }
                }
                if ($primitive.PSObject.Properties.Name -contains "indices") {
                    $accessorIndex = [int]$primitive.indices
                    if ($data.PSObject.Properties.Name -contains "accessors") {
                        $indexCount += [int]$data.accessors[$accessorIndex].count
                    }
                }
            }
        }
    }

    $skinCount = if ($data.PSObject.Properties.Name -contains "skins") { $data.skins.Count } else { 0 }
    $animationCount = if ($data.PSObject.Properties.Name -contains "animations") { $data.animations.Count } else { 0 }
    $morphTargetCount = 0
    $skinnedPrimitiveCount = 0

    if ($data.PSObject.Properties.Name -contains "meshes") {
        foreach ($mesh in $data.meshes) {
            if (-not ($mesh.PSObject.Properties.Name -contains "primitives")) { continue }
            foreach ($primitive in $mesh.primitives) {
                if ($primitive.PSObject.Properties.Name -contains "targets") {
                    $morphTargetCount += $primitive.targets.Count
                }
                if ($primitive.PSObject.Properties.Name -contains "attributes") {
                    if (($primitive.attributes.PSObject.Properties.Name -contains "JOINTS_0") -or
                        ($primitive.attributes.PSObject.Properties.Name -contains "WEIGHTS_0")) {
                        $skinnedPrimitiveCount++
                    }
                }
            }
        }
    }

    return [ordered]@{
        vertices = $positionVertices
        indices = $indexCount
        triangles = [Math]::Floor($indexCount / 3)
        meshes = $meshCount
        primitives = $primitiveCount
        skins = $skinCount
        animations = $animationCount
        morphTargets = $morphTargetCount
        skinnedPrimitives = $skinnedPrimitiveCount
    }
}

function Get-FileSizeBytes {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return 0
    }
    return [int64](Get-Item -LiteralPath $Path).Length
}

function Get-GltfStorageFiles {
    param([string]$Path)

    $files = New-Object 'System.Collections.Generic.List[string]'
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $files
    }

    [void]$files.Add([System.IO.Path]::GetFullPath($Path))
    $jsonText = Get-Content -LiteralPath $Path -Raw
    $data = $jsonText | ConvertFrom-Json
    $directory = Split-Path -Parent $Path

    foreach ($buffer in @(Get-JsonProperty $data "buffers" @())) {
        $uri = [string](Get-JsonProperty $buffer "uri" "")
        if ([string]::IsNullOrWhiteSpace($uri) -or $uri.StartsWith("data:", [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        $bufferPath = Join-Path $directory ([Uri]::UnescapeDataString($uri))
        if (Test-Path -LiteralPath $bufferPath -PathType Leaf) {
            [void]$files.Add([System.IO.Path]::GetFullPath($bufferPath))
        }
    }

    return @($files | Select-Object -Unique)
}

function Get-ModelStorageBytes {
    param([string]$Path)

    $extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($extension -eq ".gltf") {
        $total = [int64]0
        foreach ($file in Get-GltfStorageFiles $Path) {
            $total += Get-FileSizeBytes $file
        }
        return $total
    }

    return Get-FileSizeBytes $Path
}

function Write-JsonUtf8NoBom {
    param(
        [object]$Value,
        [string]$Path
    )

    $jsonText = $Value | ConvertTo-Json -Depth 12
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $jsonText, $utf8NoBom)
}

function Remove-ModelMeshCacheForFile {
    param(
        [string]$RootPath,
        [string]$SourcePath
    )

    $rootFullPath = [System.IO.Path]::GetFullPath($RootPath)
    $sourceFullPath = [System.IO.Path]::GetFullPath($SourcePath)
    $relativeSource = Get-RelativePathCompat $rootFullPath $sourceFullPath
    $cachePath = Join-Path $rootFullPath (Join-Path "Resources/.cache/model" ($relativeSource + ".meshcache"))

    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        Remove-Item -LiteralPath $cachePath -Force
    }
}

function Invoke-BlenderLod {
    param(
        [string]$RootPath,
        [string]$BlenderPath,
        [string]$InputPath,
        [string]$OutputPath,
        [double]$Ratio
    )

    if ([string]::IsNullOrWhiteSpace($BlenderPath) -or -not (Test-Path -LiteralPath $BlenderPath -PathType Leaf)) {
        throw "Blender executable was not found."
    }

    $scriptPath = Join-Path $RootPath "tools/model_lod/blender_lod.py"
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Blender LOD script was not found: $scriptPath"
    }

    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }

    & $BlenderPath --background --python $scriptPath -- --input $InputPath --output $OutputPath --ratio (Format-Double $Ratio)
    if ($LASTEXITCODE -ne 0) {
        throw "Blender LOD generation failed with exit code $LASTEXITCODE."
    }
}

function Has-JsonProperty {
    param(
        [object]$Object,
        [string]$Name
    )
    return ($null -ne $Object) -and ($Object.PSObject.Properties.Name -contains $Name)
}

function Get-JsonProperty {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Default = $null
    )
    if (Has-JsonProperty $Object $Name) {
        return $Object.PSObject.Properties[$Name].Value
    }
    return $Default
}

function Test-GltfLodSafety {
    param([string]$Path)

    $jsonText = Get-Content -LiteralPath $Path -Raw
    $data = $jsonText | ConvertFrom-Json
    $reasons = New-Object 'System.Collections.Generic.List[string]'

    $skins = @(Get-JsonProperty $data "skins" @())
    $animations = @(Get-JsonProperty $data "animations" @())
    if ($skins.Count -gt 0) {
        [void]$reasons.Add("skins")
    }
    if ($animations.Count -gt 0) {
        [void]$reasons.Add("animations")
    }

    foreach ($node in @(Get-JsonProperty $data "nodes" @())) {
        if (Has-JsonProperty $node "skin") {
            [void]$reasons.Add("node skin")
            break
        }
        $nodeName = [string](Get-JsonProperty $node "name" "")
        if ($nodeName.ToLowerInvariant().Contains("armature")) {
            [void]$reasons.Add("armature node")
            break
        }
    }

    foreach ($mesh in @(Get-JsonProperty $data "meshes" @())) {
        foreach ($primitive in @(Get-JsonProperty $mesh "primitives" @())) {
            if (Has-JsonProperty $primitive "targets") {
                [void]$reasons.Add("morph targets")
            }
            $mode = [int](Get-JsonProperty $primitive "mode" 4)
            if ($mode -ne 4) {
                [void]$reasons.Add("non-triangle primitive")
            }
            $attributes = Get-JsonProperty $primitive "attributes"
            if ($null -ne $attributes) {
                if ((Has-JsonProperty $attributes "JOINTS_0") -or (Has-JsonProperty $attributes "WEIGHTS_0")) {
                    [void]$reasons.Add("skinning attributes")
                }
            }
        }
    }

    $uniqueReasons = @($reasons | Select-Object -Unique)
    return [ordered]@{
        supported = ($uniqueReasons.Count -eq 0)
        reasons = $uniqueReasons
    }
}

function Get-GltfComponentSize {
    param([int]$ComponentType)
    switch ($ComponentType) {
        5120 { return 1 }
        5121 { return 1 }
        5122 { return 2 }
        5123 { return 2 }
        5125 { return 4 }
        5126 { return 4 }
        default { throw "Unsupported glTF component type: $ComponentType" }
    }
}

function Get-GltfTypeCount {
    param([string]$Type)
    switch ($Type) {
        "SCALAR" { return 1 }
        "VEC2" { return 2 }
        "VEC3" { return 3 }
        "VEC4" { return 4 }
        "MAT4" { return 16 }
        default { throw "Unsupported glTF accessor type: $Type" }
    }
}

function Read-GltfComponent {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [int]$ComponentType
    )
    switch ($ComponentType) {
        5120 { return [double]([sbyte]$Bytes[$Offset]) }
        5121 { return [double]$Bytes[$Offset] }
        5122 { return [double][System.BitConverter]::ToInt16($Bytes, $Offset) }
        5123 { return [double][System.BitConverter]::ToUInt16($Bytes, $Offset) }
        5125 { return [double][System.BitConverter]::ToUInt32($Bytes, $Offset) }
        5126 { return [double][System.BitConverter]::ToSingle($Bytes, $Offset) }
        default { throw "Unsupported glTF component type: $ComponentType" }
    }
}

function Read-GltfBuffers {
    param(
        [object]$Data,
        [string]$Directory
    )

    $result = New-Object 'System.Collections.Generic.List[byte[]]'
    foreach ($buffer in @(Get-JsonProperty $Data "buffers" @())) {
        $uri = [string](Get-JsonProperty $buffer "uri" "")
        if ([string]::IsNullOrWhiteSpace($uri)) {
            throw "glTF buffer URI is empty. Binary GLB is not handled by this path."
        }

        if ($uri.StartsWith("data:", [System.StringComparison]::OrdinalIgnoreCase)) {
            $comma = $uri.IndexOf(',')
            if ($comma -lt 0) { throw "Invalid glTF data URI." }
            $base64 = $uri.Substring($comma + 1)
            [void]$result.Add([System.Convert]::FromBase64String($base64))
        }
        else {
            $filePath = Join-Path $Directory ([Uri]::UnescapeDataString($uri))
            if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
                throw "glTF buffer file not found: $filePath"
            }
            [void]$result.Add([System.IO.File]::ReadAllBytes($filePath))
        }
    }
    return $result
}

function Read-GltfAccessorValues {
    param(
        [object]$Data,
        [System.Collections.Generic.List[byte[]]]$Buffers,
        [int]$AccessorIndex
    )

    $accessors = @(Get-JsonProperty $Data "accessors" @())
    $bufferViews = @(Get-JsonProperty $Data "bufferViews" @())
    if ($AccessorIndex -lt 0 -or $AccessorIndex -ge $accessors.Count) {
        throw "glTF accessor index is out of range: $AccessorIndex"
    }

    $accessor = $accessors[$AccessorIndex]
    if (-not (Has-JsonProperty $accessor "bufferView")) {
        throw "Sparse or bufferless accessors are not supported for LOD generation."
    }

    $viewIndex = [int](Get-JsonProperty $accessor "bufferView")
    if ($viewIndex -lt 0 -or $viewIndex -ge $bufferViews.Count) {
        throw "glTF bufferView index is out of range: $viewIndex"
    }

    $view = $bufferViews[$viewIndex]
    $bufferIndex = [int](Get-JsonProperty $view "buffer" 0)
    if ($bufferIndex -lt 0 -or $bufferIndex -ge $Buffers.Count) {
        throw "glTF buffer index is out of range: $bufferIndex"
    }

    $bytes = $Buffers[$bufferIndex]
    $componentType = [int](Get-JsonProperty $accessor "componentType")
    $componentSize = Get-GltfComponentSize $componentType
    $componentCount = Get-GltfTypeCount ([string](Get-JsonProperty $accessor "type"))
    $count = [int](Get-JsonProperty $accessor "count" 0)
    $accessorOffset = [int](Get-JsonProperty $accessor "byteOffset" 0)
    $viewOffset = [int](Get-JsonProperty $view "byteOffset" 0)
    $stride = [int](Get-JsonProperty $view "byteStride" ($componentSize * $componentCount))

    $values = New-Object 'System.Collections.Generic.List[object]'
    for ($i = 0; $i -lt $count; ++$i) {
        $item = New-Object 'double[]' $componentCount
        $base = $viewOffset + $accessorOffset + ($i * $stride)
        for ($c = 0; $c -lt $componentCount; ++$c) {
            $item[$c] = Read-GltfComponent $bytes ($base + ($c * $componentSize)) $componentType
        }
        [void]$values.Add($item)
    }
    return $values
}

function New-GltfIdentityMatrix {
    $m = New-Object 'double[]' 16
    $m[0] = 1.0; $m[5] = 1.0; $m[10] = 1.0; $m[15] = 1.0
    return $m
}

function Convert-GltfNodeToMatrix {
    param([object]$Node)

    if (Has-JsonProperty $Node "matrix") {
        $matrixValues = @(Get-JsonProperty $Node "matrix")
        $m = New-Object 'double[]' 16
        for ($i = 0; $i -lt 16; ++$i) { $m[$i] = [double]$matrixValues[$i] }
        return $m
    }

    $t = @(Get-JsonProperty $Node "translation" @(0.0, 0.0, 0.0))
    $r = @(Get-JsonProperty $Node "rotation" @(0.0, 0.0, 0.0, 1.0))
    $s = @(Get-JsonProperty $Node "scale" @(1.0, 1.0, 1.0))
    $x = [double]$r[0]; $y = [double]$r[1]; $z = [double]$r[2]; $w = [double]$r[3]
    $sx = [double]$s[0]; $sy = [double]$s[1]; $sz = [double]$s[2]

    $m = New-Object 'double[]' 16
    $m[0] = (1.0 - 2.0 * $y * $y - 2.0 * $z * $z) * $sx
    $m[1] = (2.0 * $x * $y + 2.0 * $w * $z) * $sx
    $m[2] = (2.0 * $x * $z - 2.0 * $w * $y) * $sx
    $m[3] = 0.0
    $m[4] = (2.0 * $x * $y - 2.0 * $w * $z) * $sy
    $m[5] = (1.0 - 2.0 * $x * $x - 2.0 * $z * $z) * $sy
    $m[6] = (2.0 * $y * $z + 2.0 * $w * $x) * $sy
    $m[7] = 0.0
    $m[8] = (2.0 * $x * $z + 2.0 * $w * $y) * $sz
    $m[9] = (2.0 * $y * $z - 2.0 * $w * $x) * $sz
    $m[10] = (1.0 - 2.0 * $x * $x - 2.0 * $y * $y) * $sz
    $m[11] = 0.0
    $m[12] = [double]$t[0]
    $m[13] = [double]$t[1]
    $m[14] = [double]$t[2]
    $m[15] = 1.0
    return $m
}

function Multiply-GltfMatrix {
    param(
        [double[]]$A,
        [double[]]$B
    )
    $m = New-Object 'double[]' 16
    for ($col = 0; $col -lt 4; ++$col) {
        for ($row = 0; $row -lt 4; ++$row) {
            $sum = 0.0
            for ($k = 0; $k -lt 4; ++$k) {
                $sum += $A[$k * 4 + $row] * $B[$col * 4 + $k]
            }
            $m[$col * 4 + $row] = $sum
        }
    }
    return $m
}

function Transform-GltfPoint {
    param(
        [double[]]$M,
        [object]$V
    )
    $x = [double]$V[0]; $y = [double]$V[1]; $z = [double]$V[2]
    $tx = ([double]$M[0] * $x) + ([double]$M[4] * $y) + ([double]$M[8] * $z) + [double]$M[12]
    $ty = ([double]$M[1] * $x) + ([double]$M[5] * $y) + ([double]$M[9] * $z) + [double]$M[13]
    $tz = ([double]$M[2] * $x) + ([double]$M[6] * $y) + ([double]$M[10] * $z) + [double]$M[14]
    return @($tx, $ty, $tz)
}

function Transform-GltfVector {
    param(
        [double[]]$M,
        [object]$V
    )
    $x = [double]$V[0]; $y = [double]$V[1]; $z = [double]$V[2]
    $nx = ([double]$M[0] * $x) + ([double]$M[4] * $y) + ([double]$M[8] * $z)
    $ny = ([double]$M[1] * $x) + ([double]$M[5] * $y) + ([double]$M[9] * $z)
    $nz = ([double]$M[2] * $x) + ([double]$M[6] * $y) + ([double]$M[10] * $z)
    $length = [Math]::Sqrt($nx * $nx + $ny * $ny + $nz * $nz)
    if ($length -gt 0.000001) {
        $nx /= $length; $ny /= $length; $nz /= $length
    }
    return @($nx, $ny, $nz)
}

function Export-GltfStaticObj {
    param(
        [string]$GltfPath,
        [string]$OutputPath
    )

    $jsonText = Get-Content -LiteralPath $GltfPath -Raw
    $data = $jsonText | ConvertFrom-Json
    $safety = Test-GltfLodSafety $GltfPath
    if (-not [bool]$safety.supported) {
        throw ("glTF is not safe for automatic LOD: {0}" -f (($safety.reasons) -join ", "))
    }

    $directory = Split-Path -Parent $GltfPath
    $buffers = Read-GltfBuffers $data $directory
    $nodes = @(Get-JsonProperty $data "nodes" @())
    $meshes = @(Get-JsonProperty $data "meshes" @())
    $scenes = @(Get-JsonProperty $data "scenes" @())
    $sceneIndex = [int](Get-JsonProperty $data "scene" 0)
    if ($sceneIndex -lt 0 -or $sceneIndex -ge $scenes.Count) {
        throw "glTF scene index is out of range."
    }

    $vertices = New-Object 'System.Collections.Generic.List[string]'
    $uvs = New-Object 'System.Collections.Generic.List[string]'
    $normals = New-Object 'System.Collections.Generic.List[string]'
    $faces = New-Object 'System.Collections.Generic.List[string]'

    $visitNode = {
        param(
            [int]$NodeIndex,
            [double[]]$ParentMatrix
        )
        if ($NodeIndex -lt 0 -or $NodeIndex -ge $nodes.Count) { return }
        $node = $nodes[$NodeIndex]
        $local = Convert-GltfNodeToMatrix $node
        $world = Multiply-GltfMatrix $ParentMatrix $local

        if (Has-JsonProperty $node "mesh") {
            $meshIndex = [int](Get-JsonProperty $node "mesh")
            if ($meshIndex -ge 0 -and $meshIndex -lt $meshes.Count) {
                $mesh = $meshes[$meshIndex]
                foreach ($primitive in @(Get-JsonProperty $mesh "primitives" @())) {
                    $mode = [int](Get-JsonProperty $primitive "mode" 4)
                    if ($mode -ne 4) { continue }

                    $attributes = Get-JsonProperty $primitive "attributes"
                    if ($null -eq $attributes -or -not (Has-JsonProperty $attributes "POSITION")) { continue }

                    $positions = Read-GltfAccessorValues $data $buffers ([int](Get-JsonProperty $attributes "POSITION"))
                    $hasUv = Has-JsonProperty $attributes "TEXCOORD_0"
                    $hasNormal = Has-JsonProperty $attributes "NORMAL"
                    $texcoords = if ($hasUv) { Read-GltfAccessorValues $data $buffers ([int](Get-JsonProperty $attributes "TEXCOORD_0")) } else { $null }
                    $normalValues = if ($hasNormal) { Read-GltfAccessorValues $data $buffers ([int](Get-JsonProperty $attributes "NORMAL")) } else { $null }

                    $indices = New-Object 'System.Collections.Generic.List[int]'
                    if (Has-JsonProperty $primitive "indices") {
                        $indexValues = Read-GltfAccessorValues $data $buffers ([int](Get-JsonProperty $primitive "indices"))
                        foreach ($indexValue in $indexValues) {
                            [void]$indices.Add([int]$indexValue[0])
                        }
                    }
                    else {
                        for ($i = 0; $i -lt $positions.Count; ++$i) {
                            [void]$indices.Add($i)
                        }
                    }

                    $baseVertex = $vertices.Count + 1
                    $baseUv = $uvs.Count + 1
                    $baseNormal = $normals.Count + 1

                    for ($i = 0; $i -lt $positions.Count; ++$i) {
                        $p = Transform-GltfPoint $world $positions[$i]
                        [void]$vertices.Add(("v {0} {1} {2}" -f (Format-Double $p[0]), (Format-Double $p[1]), (Format-Double $p[2])))

                        if ($hasUv -and $i -lt $texcoords.Count) {
                            $uv = $texcoords[$i]
                            [void]$uvs.Add(("vt {0} {1}" -f (Format-Double ([double]$uv[0])), (Format-Double ([double]$uv[1]))))
                        }
                        if ($hasNormal -and $i -lt $normalValues.Count) {
                            $n = Transform-GltfVector $world $normalValues[$i]
                            [void]$normals.Add(("vn {0} {1} {2}" -f (Format-Double $n[0]), (Format-Double $n[1]), (Format-Double $n[2])))
                        }
                    }

                    for ($i = 0; $i + 2 -lt $indices.Count; $i += 3) {
                        $tokens = New-Object 'System.Collections.Generic.List[string]'
                        for ($j = 0; $j -lt 3; ++$j) {
                            $localIndex = [int]$indices[$i + $j]
                            $vIndex = $baseVertex + $localIndex
                            $vtIndex = $baseUv + $localIndex
                            $vnIndex = $baseNormal + $localIndex
                            if ($hasUv -and $hasNormal) {
                                $token = "{0}/{1}/{2}" -f $vIndex, $vtIndex, $vnIndex
                                [void]$tokens.Add($token)
                            }
                            elseif ($hasUv) {
                                $token = "{0}/{1}" -f $vIndex, $vtIndex
                                [void]$tokens.Add($token)
                            }
                            elseif ($hasNormal) {
                                $token = "{0}//{1}" -f $vIndex, $vnIndex
                                [void]$tokens.Add($token)
                            }
                            else {
                                [void]$tokens.Add([string]$vIndex)
                            }
                        }
                        [void]$faces.Add("f {0}" -f ($tokens -join ' '))
                    }
                }
            }
        }

        foreach ($child in @(Get-JsonProperty $node "children" @())) {
            & $visitNode ([int]$child) $world
        }
    }

    $identity = New-GltfIdentityMatrix
    foreach ($rootNode in @(Get-JsonProperty $scenes[$sceneIndex] "nodes" @())) {
        & $visitNode ([int]$rootNode) $identity
    }

    if ($vertices.Count -lt 4 -or $faces.Count -lt 1) {
        throw "glTF did not contain enough static triangle mesh data for LOD generation."
    }

    $outDir = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    }

    $writer = New-Object System.IO.StreamWriter($OutputPath, $false, [System.Text.UTF8Encoding]::new($false))
    try {
        $writer.WriteLine("# Extracted from static glTF by tools/model_lod/model_lod_builder.ps1")
        $writer.WriteLine("# Source: {0}" -f (Split-Path -Leaf $GltfPath))
        foreach ($line in $vertices) { $writer.WriteLine($line) }
        foreach ($line in $uvs) { $writer.WriteLine($line) }
        foreach ($line in $normals) { $writer.WriteLine($line) }
        foreach ($line in $faces) { $writer.WriteLine($line) }
    }
    finally {
        $writer.Dispose()
    }

    return Analyze-Obj $OutputPath
}

function Get-ObjVertices {
    param([string]$Path)

    $vertices = New-Object 'System.Collections.Generic.List[object]'
    $min = @( [double]::PositiveInfinity, [double]::PositiveInfinity, [double]::PositiveInfinity )
    $max = @( [double]::NegativeInfinity, [double]::NegativeInfinity, [double]::NegativeInfinity )

    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if (-not $line.StartsWith("v ")) { continue }
        $tokens = $line.Substring(2).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
        if ($tokens.Count -lt 3) { continue }
        $v = @((Parse-Double $tokens[0]), (Parse-Double $tokens[1]), (Parse-Double $tokens[2]))
        [void]$vertices.Add($v)
        for ($i = 0; $i -lt 3; ++$i) {
            if ($v[$i] -lt $min[$i]) { $min[$i] = $v[$i] }
            if ($v[$i] -gt $max[$i]) { $max[$i] = $v[$i] }
        }
    }

    return [ordered]@{
        vertices = $vertices
        min = $min
        max = $max
    }
}

function Resolve-ObjIndex {
    param(
        [int]$Index,
        [int]$Count
    )

    if ($Index -gt 0) { return $Index }
    if ($Index -lt 0) { return $Count + $Index + 1 }
    return 0
}

function New-ClusterMap {
    param(
        [System.Collections.Generic.List[object]]$Vertices,
        [object[]]$Min,
        [object[]]$Max,
        [double]$Ratio
    )

    $vertexCount = $Vertices.Count
    $targetClusterCount = [Math]::Max(8.0, [Math]::Ceiling($vertexCount * [Math]::Max(0.02, [Math]::Min(1.0, $Ratio))))
    $grid = [Math]::Max(2, [Math]::Ceiling([Math]::Pow($targetClusterCount, 1.0 / 3.0) * 1.6))
    $extent = @(
        [Math]::Max(0.000001, [double]$Max[0] - [double]$Min[0]),
        [Math]::Max(0.000001, [double]$Max[1] - [double]$Min[1]),
        [Math]::Max(0.000001, [double]$Max[2] - [double]$Min[2])
    )

    $clusters = @{}
    $vertexToCluster = New-Object 'int[]' ($vertexCount + 1)

    for ($i = 0; $i -lt $vertexCount; ++$i) {
        $v = $Vertices[$i]
        $ix = [Math]::Min($grid - 1, [Math]::Max(0, [Math]::Floor((([double]$v[0] - [double]$Min[0]) / $extent[0]) * $grid)))
        $iy = [Math]::Min($grid - 1, [Math]::Max(0, [Math]::Floor((([double]$v[1] - [double]$Min[1]) / $extent[1]) * $grid)))
        $iz = [Math]::Min($grid - 1, [Math]::Max(0, [Math]::Floor((([double]$v[2] - [double]$Min[2]) / $extent[2]) * $grid)))
        $key = "$ix,$iy,$iz"
        if (-not $clusters.ContainsKey($key)) {
            $clusters[$key] = [ordered]@{ x = 0.0; y = 0.0; z = 0.0; count = 0; outIndex = 0 }
        }
        $cluster = $clusters[$key]
        $cluster.x += [double]$v[0]
        $cluster.y += [double]$v[1]
        $cluster.z += [double]$v[2]
        $cluster.count++
        $vertexToCluster[$i + 1] = 0
    }

    $clusterKeys = @($clusters.Keys | Sort-Object)
    $outVertices = New-Object 'System.Collections.Generic.List[string]'
    $clusterIndex = 1
    foreach ($key in $clusterKeys) {
        $cluster = $clusters[$key]
        $cluster.outIndex = $clusterIndex
        $x = [double]$cluster.x / [double]$cluster.count
        $y = [double]$cluster.y / [double]$cluster.count
        $z = [double]$cluster.z / [double]$cluster.count
        [void]$outVertices.Add(("v {0} {1} {2}" -f (Format-Double $x), (Format-Double $y), (Format-Double $z)))
        $clusterIndex++
    }

    for ($i = 0; $i -lt $vertexCount; ++$i) {
        $v = $Vertices[$i]
        $ix = [Math]::Min($grid - 1, [Math]::Max(0, [Math]::Floor((([double]$v[0] - [double]$Min[0]) / $extent[0]) * $grid)))
        $iy = [Math]::Min($grid - 1, [Math]::Max(0, [Math]::Floor((([double]$v[1] - [double]$Min[1]) / $extent[1]) * $grid)))
        $iz = [Math]::Min($grid - 1, [Math]::Max(0, [Math]::Floor((([double]$v[2] - [double]$Min[2]) / $extent[2]) * $grid)))
        $vertexToCluster[$i + 1] = [int]$clusters["$ix,$iy,$iz"].outIndex
    }

    return [ordered]@{
        map = $vertexToCluster
        vertices = $outVertices
        grid = $grid
    }
}

function Rewrite-FaceToken {
    param(
        [string]$Token,
        [int[]]$VertexMap,
        [int]$OriginalVertexCount
    )

    $parts = $Token.Split('/')
    if ($parts.Count -lt 1 -or [string]::IsNullOrWhiteSpace($parts[0])) {
        return $null
    }

    $originalIndex = Resolve-ObjIndex ([int]$parts[0]) $OriginalVertexCount
    if ($originalIndex -le 0 -or $originalIndex -ge $VertexMap.Count) {
        return $null
    }

    $newIndex = $VertexMap[$originalIndex]
    if ($newIndex -le 0) {
        return $null
    }

    $parts[0] = [string]$newIndex
    return ($parts -join '/')
}

function Build-ObjLod {
    param(
        [string]$SourcePath,
        [double]$Ratio,
        [string]$OutputPath
    )

    $vertexInfo = Get-ObjVertices $SourcePath
    $vertices = [System.Collections.Generic.List[object]]$vertexInfo.vertices
    if ($vertices.Count -lt 4) {
        throw "OBJ has too few vertices to build LOD: $SourcePath"
    }

    $cluster = New-ClusterMap $vertices $vertexInfo.min $vertexInfo.max $Ratio
    $vertexMap = [int[]]$cluster.map
    $outVertices = [System.Collections.Generic.List[string]]$cluster.vertices

    $uvLines = New-Object 'System.Collections.Generic.List[string]'
    $normalLines = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in [System.IO.File]::ReadLines($SourcePath)) {
        if ($line.StartsWith("vt ")) {
            [void]$uvLines.Add($line)
        }
        elseif ($line.StartsWith("vn ")) {
            [void]$normalLines.Add($line)
        }
    }

    $writer = New-Object System.IO.StreamWriter($OutputPath, $false, [System.Text.UTF8Encoding]::new($false))
    try {
        $writer.WriteLine("# Generated by tools/model_lod/model_lod_builder.ps1")
        $writer.WriteLine("# Source: {0}" -f (Split-Path -Leaf $SourcePath))
        $writer.WriteLine("# Ratio: {0}" -f (Format-Double $Ratio))

        $wroteMtllib = $false
        foreach ($line in [System.IO.File]::ReadLines($SourcePath)) {
            if ($line.StartsWith("mtllib ")) {
                $writer.WriteLine($line)
                $wroteMtllib = $true
            }
        }
        if (-not $wroteMtllib) {
            $mtl = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFileName($SourcePath), ".mtl")
            if (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $SourcePath) $mtl)) {
                $writer.WriteLine("mtllib {0}" -f $mtl)
            }
        }

        foreach ($line in $outVertices) { $writer.WriteLine($line) }
        foreach ($line in $uvLines) { $writer.WriteLine($line) }
        foreach ($line in $normalLines) { $writer.WriteLine($line) }

        $faceCount = 0
        $triangleCount = 0
        $skippedFaceCount = 0

        foreach ($line in [System.IO.File]::ReadLines($SourcePath)) {
            if ($line.StartsWith("v ") -or $line.StartsWith("vt ") -or $line.StartsWith("vn ") -or $line.StartsWith("mtllib ")) {
                continue
            }

            if ($line.StartsWith("f ")) {
                $tokens = $line.Substring(2).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
                $newTokens = New-Object 'System.Collections.Generic.List[string]'
                $uniqueVertexSet = New-Object 'System.Collections.Generic.HashSet[int]'

                foreach ($token in $tokens) {
                    $rewritten = Rewrite-FaceToken $token $vertexMap $vertices.Count
                    if ($null -eq $rewritten) { continue }
                    $newVertexIndex = [int]($rewritten.Split('/')[0])
                    [void]$uniqueVertexSet.Add($newVertexIndex)
                    [void]$newTokens.Add($rewritten)
                }

                if ($newTokens.Count -ge 3 -and $uniqueVertexSet.Count -ge 3) {
                    $writer.WriteLine("f {0}" -f ($newTokens -join ' '))
                    $faceCount++
                    $triangleCount += [Math]::Max(1, $newTokens.Count - 2)
                }
                else {
                    $skippedFaceCount++
                }
            }
            elseif ($line.StartsWith("usemtl ") -or $line.StartsWith("o ") -or $line.StartsWith("g ") -or $line.StartsWith("s ")) {
                $writer.WriteLine($line)
            }
        }
    }
    finally {
        $writer.Dispose()
    }

    return [ordered]@{
        vertices = $outVertices.Count
        faces = $faceCount
        triangles = $triangleCount
        skippedFaces = $skippedFaceCount
        grid = [int]$cluster.grid
    }
}

function Convert-ObjToStaticGltf {
    param(
        [string]$ObjPath,
        [string]$OutputGltfPath
    )

    $positions = New-Object 'System.Collections.Generic.List[object]'
    $uvs = New-Object 'System.Collections.Generic.List[object]'
    $normals = New-Object 'System.Collections.Generic.List[object]'
    $faceKeys = New-Object 'System.Collections.Generic.List[object]'

    foreach ($line in [System.IO.File]::ReadLines($ObjPath)) {
        if ($line.StartsWith("v ")) {
            $tokens = $line.Substring(2).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($tokens.Count -ge 3) {
                [void]$positions.Add(@((Parse-Double $tokens[0]), (Parse-Double $tokens[1]), (Parse-Double $tokens[2])))
            }
        }
        elseif ($line.StartsWith("vt ")) {
            $tokens = $line.Substring(3).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($tokens.Count -ge 2) {
                [void]$uvs.Add(@((Parse-Double $tokens[0]), (Parse-Double $tokens[1])))
            }
        }
        elseif ($line.StartsWith("vn ")) {
            $tokens = $line.Substring(3).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
            if ($tokens.Count -ge 3) {
                [void]$normals.Add(@((Parse-Double $tokens[0]), (Parse-Double $tokens[1]), (Parse-Double $tokens[2])))
            }
        }
        elseif ($line.StartsWith("f ")) {
            $tokens = $line.Substring(2).Trim().Split(@(' ', "`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
            $keys = New-Object 'System.Collections.Generic.List[string]'
            foreach ($token in $tokens) {
                $parts = $token.Split('/')
                if ($parts.Count -lt 1 -or [string]::IsNullOrWhiteSpace($parts[0])) {
                    continue
                }

                $v = Resolve-ObjIndex ([int]$parts[0]) $positions.Count
                $vt = 0
                $vn = 0
                if ($parts.Count -ge 2 -and -not [string]::IsNullOrWhiteSpace($parts[1])) {
                    $vt = Resolve-ObjIndex ([int]$parts[1]) $uvs.Count
                }
                if ($parts.Count -ge 3 -and -not [string]::IsNullOrWhiteSpace($parts[2])) {
                    $vn = Resolve-ObjIndex ([int]$parts[2]) $normals.Count
                }

                if ($v -gt 0) {
                    [void]$keys.Add(("{0}/{1}/{2}" -f $v, $vt, $vn))
                }
            }

            if ($keys.Count -ge 3) {
                for ($i = 1; $i + 1 -lt $keys.Count; ++$i) {
                    [void]$faceKeys.Add(@($keys[0], $keys[$i], $keys[$i + 1]))
                }
            }
        }
    }

    $outPositions = New-Object 'System.Collections.Generic.List[object]'
    $outNormals = New-Object 'System.Collections.Generic.List[object]'
    $outUvs = New-Object 'System.Collections.Generic.List[object]'
    $outIndices = New-Object 'System.Collections.Generic.List[uint32]'
    $vertexMap = @{}

    foreach ($triangle in $faceKeys) {
        foreach ($key in $triangle) {
            if (-not $vertexMap.ContainsKey($key)) {
                $parts = ([string]$key).Split('/')
                $positionIndex = [int]$parts[0]
                $uvIndex = [int]$parts[1]
                $normalIndex = [int]$parts[2]
                $outIndex = [uint32]$outPositions.Count
                $vertexMap[$key] = $outIndex

                [void]$outPositions.Add($positions[$positionIndex - 1])
                if ($normalIndex -gt 0 -and $normalIndex -le $normals.Count) {
                    [void]$outNormals.Add($normals[$normalIndex - 1])
                }
                else {
                    [void]$outNormals.Add(@(0.0, 1.0, 0.0))
                }

                if ($uvIndex -gt 0 -and $uvIndex -le $uvs.Count) {
                    [void]$outUvs.Add($uvs[$uvIndex - 1])
                }
                else {
                    [void]$outUvs.Add(@(0.0, 0.0))
                }
            }

            [void]$outIndices.Add([uint32]$vertexMap[$key])
        }
    }

    if ($outPositions.Count -lt 3 -or $outIndices.Count -lt 3) {
        throw "OBJ did not contain enough triangle data for glTF export: $ObjPath"
    }

    $min = @([double]::PositiveInfinity, [double]::PositiveInfinity, [double]::PositiveInfinity)
    $max = @([double]::NegativeInfinity, [double]::NegativeInfinity, [double]::NegativeInfinity)
    foreach ($position in $outPositions) {
        for ($i = 0; $i -lt 3; ++$i) {
            $value = [double]$position[$i]
            if ($value -lt $min[$i]) { $min[$i] = $value }
            if ($value -gt $max[$i]) { $max[$i] = $value }
        }
    }

    $outputDirectory = Split-Path -Parent $OutputGltfPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }

    $binPath = [System.IO.Path]::ChangeExtension($OutputGltfPath, ".bin")
    $binaryWriter = [System.IO.BinaryWriter]::new([System.IO.File]::Open($binPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write))

    try {
        $positionOffset = [int]$binaryWriter.BaseStream.Position
        foreach ($position in $outPositions) {
            $binaryWriter.Write([single]$position[0])
            $binaryWriter.Write([single]$position[1])
            $binaryWriter.Write([single]$position[2])
        }
        $positionByteLength = [int]$binaryWriter.BaseStream.Position - $positionOffset
        while (($binaryWriter.BaseStream.Position % 4) -ne 0) { $binaryWriter.Write([byte]0) }

        $normalOffset = [int]$binaryWriter.BaseStream.Position
        foreach ($normal in $outNormals) {
            $binaryWriter.Write([single]$normal[0])
            $binaryWriter.Write([single]$normal[1])
            $binaryWriter.Write([single]$normal[2])
        }
        $normalByteLength = [int]$binaryWriter.BaseStream.Position - $normalOffset
        while (($binaryWriter.BaseStream.Position % 4) -ne 0) { $binaryWriter.Write([byte]0) }

        $uvOffset = [int]$binaryWriter.BaseStream.Position
        foreach ($uv in $outUvs) {
            $binaryWriter.Write([single]$uv[0])
            $binaryWriter.Write([single]$uv[1])
        }
        $uvByteLength = [int]$binaryWriter.BaseStream.Position - $uvOffset
        while (($binaryWriter.BaseStream.Position % 4) -ne 0) { $binaryWriter.Write([byte]0) }

        $indexOffset = [int]$binaryWriter.BaseStream.Position
        foreach ($index in $outIndices) {
            $binaryWriter.Write([uint32]$index)
        }
        $indexByteLength = [int]$binaryWriter.BaseStream.Position - $indexOffset
        while (($binaryWriter.BaseStream.Position % 4) -ne 0) { $binaryWriter.Write([byte]0) }
        $bufferByteLength = [int]$binaryWriter.BaseStream.Position
    }
    finally {
        $binaryWriter.Dispose()
    }

    $binName = [System.IO.Path]::GetFileName($binPath)
    $meshName = [System.IO.Path]::GetFileNameWithoutExtension($OutputGltfPath)
    $gltf = [ordered]@{
        asset = [ordered]@{
            version = "2.0"
            generator = "GE3 Model LOD Builder"
        }
        scene = 0
        scenes = @(
            [ordered]@{
                nodes = @(0)
            }
        )
        nodes = @(
            [ordered]@{
                name = $meshName
                mesh = 0
            }
        )
        meshes = @(
            [ordered]@{
                name = $meshName
                primitives = @(
                    [ordered]@{
                        attributes = [ordered]@{
                            POSITION = 0
                            NORMAL = 1
                            TEXCOORD_0 = 2
                        }
                        indices = 3
                        material = 0
                        mode = 4
                    }
                )
            }
        )
        materials = @(
            [ordered]@{
                name = "LOD_Default"
                doubleSided = $true
                pbrMetallicRoughness = [ordered]@{
                    baseColorFactor = @(1.0, 1.0, 1.0, 1.0)
                    metallicFactor = 0.0
                    roughnessFactor = 0.7
                }
            }
        )
        buffers = @(
            [ordered]@{
                uri = $binName
                byteLength = $bufferByteLength
            }
        )
        bufferViews = @(
            [ordered]@{ buffer = 0; byteOffset = $positionOffset; byteLength = $positionByteLength; target = 34962 },
            [ordered]@{ buffer = 0; byteOffset = $normalOffset; byteLength = $normalByteLength; target = 34962 },
            [ordered]@{ buffer = 0; byteOffset = $uvOffset; byteLength = $uvByteLength; target = 34962 },
            [ordered]@{ buffer = 0; byteOffset = $indexOffset; byteLength = $indexByteLength; target = 34963 }
        )
        accessors = @(
            [ordered]@{
                bufferView = 0
                componentType = 5126
                count = $outPositions.Count
                type = "VEC3"
                min = @($min[0], $min[1], $min[2])
                max = @($max[0], $max[1], $max[2])
            },
            [ordered]@{
                bufferView = 1
                componentType = 5126
                count = $outNormals.Count
                type = "VEC3"
            },
            [ordered]@{
                bufferView = 2
                componentType = 5126
                count = $outUvs.Count
                type = "VEC2"
            },
            [ordered]@{
                bufferView = 3
                componentType = 5125
                count = $outIndices.Count
                type = "SCALAR"
            }
        )
    }

    Write-JsonUtf8NoBom $gltf $OutputGltfPath

    return [ordered]@{
        vertices = $outPositions.Count
        faces = [Math]::Floor($outIndices.Count / 3)
        triangles = [Math]::Floor($outIndices.Count / 3)
        skippedFaces = 0
        format = "gltf"
        fileSizeBytes = (Get-ModelStorageBytes $OutputGltfPath)
    }
}

$rootFull = [System.IO.Path]::GetFullPath($Root)
$sourcePath = Resolve-ModelFile $rootFull $Model
$sourceExtension = [System.IO.Path]::GetExtension($sourcePath).ToLowerInvariant()
if ($sourceExtension -notin @(".obj", ".gltf", ".glb")) {
    throw "Unsupported model file extension for LOD: $sourceExtension"
}
$sourceDirectory = Split-Path -Parent $sourcePath
$sourceStem = [System.IO.Path]::GetFileNameWithoutExtension($sourcePath)
$sourceModelName = ConvertTo-ModelName $rootFull $sourcePath $false
$backendInfo = Resolve-LodBackend $Backend ([bool]$AnalyzeOnly) $rootFull $BlenderPath
$selectedBackend = [string]$backendInfo.name
$reportPath = Join-Path $sourceDirectory ($sourceStem + "_lod_report.json")
$lodConfigPath = Join-Path $sourceDirectory ($sourceStem + "_lod.json")
$cacheReportDirectory = Join-Path $rootFull "Resources/.cache/model_lod"
New-Item -ItemType Directory -Path $cacheReportDirectory -Force | Out-Null

$gltfSafety = $null
if ($sourceExtension -eq ".gltf") {
    $gltfSafety = Test-GltfLodSafety $sourcePath
}

$sourceStats = if ($sourceExtension -eq ".obj") {
    Analyze-Obj $sourcePath
}
elseif ($sourceExtension -eq ".gltf") {
    Analyze-Gltf $sourcePath
}
else {
    [ordered]@{ vertices = 0; triangles = 0; meshes = 0; note = "Binary glb analysis is not supported yet." }
}
$sourceStats["fileSizeBytes"] = Get-ModelStorageBytes $sourcePath

$supportedForGeneration = if ($selectedBackend -eq "blender") {
    $sourceExtension -in @(".obj", ".gltf", ".glb")
}
else {
    ($sourceExtension -eq ".obj") -or
    (($sourceExtension -eq ".gltf") -and ($null -ne $gltfSafety) -and [bool]$gltfSafety.supported)
}

$report = [ordered]@{
    tool = "Model LOD Builder"
    version = 2
    generatedAt = (Get-Date).ToString("s")
    sourceModel = $sourceModelName
    sourceFile = (To-ForwardSlash (Get-RelativePathCompat $rootFull $sourcePath))
    sourceExtension = $sourceExtension
    requestedBackend = $Backend
    selectedBackend = $selectedBackend
    backendExecutable = [string]$backendInfo.executable
    analyzeOnly = [bool]$AnalyzeOnly
    supportedForGeneration = $supportedForGeneration
    source = $sourceStats
    safety = $gltfSafety
    lods = @()
    warnings = @()
}

foreach ($backendWarning in @($backendInfo.warnings)) {
    if (-not [string]::IsNullOrWhiteSpace([string]$backendWarning)) {
        $report["warnings"] = @($report["warnings"]) + [string]$backendWarning
    }
}

$lods = New-Object 'System.Collections.Generic.List[object]'
[void]$lods.Add([ordered]@{
    level = 0
    modelName = $sourceModelName
    file = (To-ForwardSlash (Get-RelativePathCompat $rootFull $sourcePath))
    distance = 0.0
    ratio = 1.0
    generated = $false
    stats = $sourceStats
})

if (-not $supportedForGeneration) {
    if ($sourceExtension -eq ".gltf" -and $null -ne $gltfSafety) {
        $report["warnings"] = @($report["warnings"]) + ("glTF was skipped because automatic LOD could break this model: {0}" -f (($gltfSafety.reasons) -join ", "))
    }
    elseif ($sourceExtension -eq ".glb") {
        $report["warnings"] = @($report["warnings"]) + "Binary glb is excluded from automatic LOD for now. Export as static .gltf or make a manual LOD model."
    }
    else {
        $report["warnings"] = @($report["warnings"]) + "This model format is not supported for LOD generation."
    }
}
elseif (-not $AnalyzeOnly) {
    $useBlenderBackend = ($selectedBackend -eq "blender")
    $lodSourcePath = $sourcePath
    if (-not $useBlenderBackend -and $sourceExtension -eq ".gltf") {
        $extractRoot = Join-Path $cacheReportDirectory "gltf_extract"
        $relativeSource = Get-RelativePathCompat (Join-Path $rootFull "Resources/3DModel") $sourcePath
        $extractPath = Join-Path $extractRoot ([System.IO.Path]::ChangeExtension($relativeSource, ".obj"))
        Export-GltfStaticObj $sourcePath $extractPath | Out-Null
        $lodSourcePath = $extractPath
        $report["warnings"] = @($report["warnings"]) + "Static glTF LOD is generated as glTF+bin for far-distance rendering. Skin, animation, morph and PBR material data are intentionally not copied."
    }
    elseif ($useBlenderBackend) {
        $report["warnings"] = @($report["warnings"]) + "Blender Decimate backend was used. It preserves more source attributes than the native fallback, but generated LODs still need visual review."
    }

    $ratios = @($Ratio1, $Ratio2)
    $distances = @($Distance1, $Distance2)
    for ($i = 0; $i -lt $ratios.Count; ++$i) {
        $level = $i + 1
        $ratio = [Math]::Max(0.02, [Math]::Min(0.98, [double]$ratios[$i]))
        $isGltfOutput = ($sourceExtension -eq ".gltf" -or $sourceExtension -eq ".glb")
        $outputExtension = if ($isGltfOutput) { ".gltf" } else { ".obj" }
        $outputPath = Join-Path $sourceDirectory ("{0}_lod{1}{2}" -f $sourceStem, $level, $outputExtension)

        if ((Test-Path -LiteralPath $outputPath) -and -not $Force) {
            $stats = if ($isGltfOutput) { Analyze-Gltf $outputPath } else { Analyze-Obj $outputPath }
            $stats["fileSizeBytes"] = Get-ModelStorageBytes $outputPath
        }
        else {
            if ($useBlenderBackend) {
                Invoke-BlenderLod $rootFull ([string]$backendInfo.executable) $sourcePath $outputPath $ratio
                $stats = if ($isGltfOutput) { Analyze-Gltf $outputPath } else { Analyze-Obj $outputPath }
                $stats["fileSizeBytes"] = Get-ModelStorageBytes $outputPath
                Remove-ModelMeshCacheForFile $rootFull $outputPath
            }
            elseif ($isGltfOutput) {
                $tempLodObjDirectory = Join-Path $cacheReportDirectory "generated_obj"
                $relativeSource = Get-RelativePathCompat (Join-Path $rootFull "Resources/3DModel") $sourcePath
                $relativeNoExt = [System.IO.Path]::ChangeExtension($relativeSource, $null)
                $tempLodObjPath = Join-Path $tempLodObjDirectory ("{0}_lod{1}.obj" -f $relativeNoExt, $level)
                $tempLodObjParent = Split-Path -Parent $tempLodObjPath
                if (-not [string]::IsNullOrWhiteSpace($tempLodObjParent)) {
                    New-Item -ItemType Directory -Path $tempLodObjParent -Force | Out-Null
                }

                $objStats = Build-ObjLod $lodSourcePath $ratio $tempLodObjPath
                $stats = Convert-ObjToStaticGltf $tempLodObjPath $outputPath
                $stats["grid"] = [int]$objStats.grid
                $stats["skippedFaces"] = [int]$objStats.skippedFaces
                Remove-ModelMeshCacheForFile $rootFull $outputPath

                $staleObjPath = Join-Path $sourceDirectory ("{0}_lod{1}.obj" -f $sourceStem, $level)
                if (Test-Path -LiteralPath $staleObjPath -PathType Leaf) {
                    Remove-Item -LiteralPath $staleObjPath -Force
                }
            }
            else {
                $stats = Build-ObjLod $lodSourcePath $ratio $outputPath
                $stats["fileSizeBytes"] = Get-ModelStorageBytes $outputPath
                Remove-ModelMeshCacheForFile $rootFull $outputPath
            }
        }

        $triReduction = 0.0
        if ([int]$sourceStats.triangles -gt 0) {
            $triReduction = 1.0 - ([double]$stats.triangles / [double]$sourceStats.triangles)
        }
        $storageDelta = [int64]$stats.fileSizeBytes - [int64]$sourceStats.fileSizeBytes
        if ($triReduction -lt 0.1) {
            $report["warnings"] = @($report["warnings"]) + "LOD$level did not reduce triangles much. Try a lower ratio or simplify the source mesh manually."
        }

        [void]$lods.Add([ordered]@{
            level = $level
            modelName = (ConvertTo-ModelName $rootFull $outputPath $true)
            file = (To-ForwardSlash (Get-RelativePathCompat $rootFull $outputPath))
            distance = [double]$distances[$i]
            ratio = $ratio
            generated = $true
            backend = $selectedBackend
            stats = $stats
            triangleReduction = $triReduction
            storageDeltaBytes = $storageDelta
        })
    }
}

$report["lods"] = @($lods.ToArray())

$lodConfig = [ordered]@{
    sourceModel = $sourceModelName
    sourceFile = $report.sourceFile
    generatedAt = $report.generatedAt
    lods = @($lods | ForEach-Object {
        [ordered]@{
            level = $_.level
            modelName = $_.modelName
            distance = $_.distance
            ratio = $_.ratio
            backend = $selectedBackend
            triangles = $_.stats.triangles
            fileSizeBytes = $_.stats.fileSizeBytes
        }
    })
}

Write-JsonUtf8NoBom $report $reportPath
Write-JsonUtf8NoBom $report (Join-Path $cacheReportDirectory "latest_report.json")
Write-JsonUtf8NoBom $lodConfig $lodConfigPath

Write-Host ("Report: {0}" -f (To-ForwardSlash (Get-RelativePathCompat $rootFull $reportPath)))
Write-Host ("LOD config: {0}" -f (To-ForwardSlash (Get-RelativePathCompat $rootFull $lodConfigPath)))
