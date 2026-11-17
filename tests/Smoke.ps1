param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$appRoot = [System.IO.Path]::GetFullPath("$PSScriptRoot/..")
$profile = $Configuration.ToLowerInvariant()
$directory = "$appRoot/build/$profile/x86_64-pc-windows-msvc"
$project = "$appRoot/build/tests/$profile-$([guid]::NewGuid().ToString('N'))"
$previousProject = $env:OXYGEN_EDITOR_PROJECT
$previousEditorSmoke = $env:OXYGEN_EDITOR_SMOKE
$previousSmoke = $env:OXYGEN_SMOKE
try
{
    $env:OXYGEN_EDITOR_PROJECT = $project
    $env:OXYGEN_EDITOR_SMOKE = '1'
    $env:OXYGEN_SMOKE = '1'
    Push-Location $directory
    try
    {
        & './Oxygen.Editor.exe' *> './editor-smoke.log'
        $result = $LASTEXITCODE
        Get-Content './editor-smoke.log' | Select-String 'PASS|FAIL|ERROR|WARNING'
        if ($result -ne 0) { throw "Editor checks failed: $result" }
    }
    finally { Pop-Location }
}
finally
{
    $env:OXYGEN_EDITOR_PROJECT = $previousProject
    $env:OXYGEN_EDITOR_SMOKE = $previousEditorSmoke
    $env:OXYGEN_SMOKE = $previousSmoke
}
