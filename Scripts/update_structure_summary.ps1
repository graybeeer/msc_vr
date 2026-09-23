# Refresh only the path inventory in PROJECT_STRUCTURE.md.
$projectRoot = Split-Path -Parent $PSScriptRoot
$summary = Join-Path $projectRoot 'PROJECT_STRUCTURE.md'
$start = '<!-- STRUCTURE:START -->'
$end = '<!-- STRUCTURE:END -->'
$lines = [System.Collections.Generic.List[string]]::new()

function Add-Files($directory, $extensions, $indent) {
    Get-ChildItem -LiteralPath $directory -File |
        Where-Object { $extensions -contains $_.Extension } |
        Sort-Object Name |
        ForEach-Object { $lines.Add((' ' * $indent) + $_.Name) }
}

$lines.Add('```text')
$lines.Add('msc_vr/')
Add-Files $projectRoot @('.uproject', '.md') 2
foreach ($entry in @(@('Config', '.ini'), @('Scripts', '.ps1', '.py'))) {
    $name = $entry[0]
    $lines.Add("  $name/")
    Add-Files (Join-Path $projectRoot $name) $entry[1..($entry.Length - 1)] 4
}
$source = Join-Path $projectRoot 'Source'
$lines.Add('  Source/')
Add-Files $source @('.cs') 4
$module = Join-Path $source 'msc_vr'
$lines.Add('    msc_vr/')
Add-Files $module @('.h', '.cpp', '.cs') 6
Get-ChildItem -LiteralPath $module -Directory | Sort-Object Name | ForEach-Object {
    $lines.Add('      ' + $_.Name + '/')
    Get-ChildItem -LiteralPath $_.FullName -Directory | Sort-Object Name |
        ForEach-Object { $lines.Add('        ' + $_.Name + '/') }
}

$content = Join-Path $projectRoot 'Content'
$lines.Add('  Content/')
Get-ChildItem -LiteralPath $content -Directory | Sort-Object Name | ForEach-Object {
    $lines.Add('    ' + $_.Name + '/')
    if ($_.Name -in @('FirstPerson', 'Variant_Horror', 'Variant_Shooter')) {
        Add-Files $_.FullName @('.umap') 6
    }
    if ($_.Name -notin @('__ExternalActors__', '__ExternalObjects__', 'Scene_Warehouse', 'Developers')) {
        Get-ChildItem -LiteralPath $_.FullName -Directory | Sort-Object Name |
            ForEach-Object { $lines.Add('      ' + $_.Name + '/') }
    }
}
Get-ChildItem -LiteralPath $projectRoot -Directory | Sort-Object Name | Where-Object {
    $_.Name -notin @('.git', '.vs', 'Binaries', 'Config', 'Content', 'DerivedDataCache', 'Intermediate', 'Saved', 'Scripts', 'Source')
} | ForEach-Object { $lines.Add('  ' + $_.Name + '/') }
$lines.Add('```')

$document = Get-Content -LiteralPath $summary -Raw -Encoding UTF8
$startIndex = $document.IndexOf($start)
$endIndex = $document.IndexOf($end)
if ($startIndex -lt 0 -or $endIndex -le $startIndex -or
    $document.LastIndexOf($start) -ne $startIndex -or $document.LastIndexOf($end) -ne $endIndex) {
    throw 'PROJECT_STRUCTURE.md must contain one ordered STRUCTURE marker pair.'
}
$updated = $document.Substring(0, $startIndex + $start.Length) + "`n" +
    ($lines -join "`n") + "`n" + $document.Substring($endIndex)
if ($updated -ne $document) {
    [System.IO.File]::WriteAllText($summary, $updated, [System.Text.UTF8Encoding]::new($false))
}
Write-Output "Updated $summary"
