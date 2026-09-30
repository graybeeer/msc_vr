# Download official CC0 source models and 2K PBR maps, validating each checksum.
$ErrorActionPreference='Stop'
$assetRoot=Join-Path (Split-Path $PSScriptRoot -Parent) 'SourceAssets/Cargo/PolyHaven'
foreach ($asset in @('cardboard_box_01','plastic_crate_01')) {
    $folder=Join-Path $assetRoot $asset
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $info=Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$asset"
    $selected=@{mesh=$info.fbx.'2k'.fbx;albedo=$info.Diffuse.'2k'.jpg;normal=$info.nor_dx.'2k'.png;roughness=$info.Rough.'2k'.jpg}
    $manifest=@{asset=$asset;license='CC0';source="https://polyhaven.com/a/$asset";files=@{}}
    foreach ($kind in $selected.Keys) {
        $item=$selected[$kind]
        $filename=([Uri]$item.url).Segments[-1]
        $target=Join-Path $folder $filename
        if (-not (Test-Path $target) -or (Get-FileHash -LiteralPath $target -Algorithm MD5).Hash -ne $item.md5) {
            Invoke-WebRequest -Uri $item.url -OutFile $target
            if ((Get-FileHash -LiteralPath $target -Algorithm MD5).Hash -ne $item.md5) {throw "Checksum failed: $filename"}
        }
        $manifest.files[$kind]=@{filename=$filename;url=$item.url;md5=$item.md5}
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $folder 'source.json')
    Write-Output "CARGO_SOURCE_DOWNLOADED $asset"
}
