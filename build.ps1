param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$library = [System.IO.Path]::GetFullPath("$PSScriptRoot/../oxygen.engine.lib")
$profile = $Configuration.ToLowerInvariant()
$cache = "$library/build/dependency-directory.txt"
if (!(Test-Path -LiteralPath $cache))
{
    & "$library/build.ps1" -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Oxygen dependency build failed.' }
}
$dependencies = (Get-Content -LiteralPath $cache -Raw).Trim()
$xenon = (Get-Command xenon -ErrorAction Stop).Source
$cmake = (Get-Command cmake -ErrorAction Stop).Source
& $cmake -S $library -B "$library/build/cmake/$profile" -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$dependencies" "-DXENON_EXECUTABLE=$xenon" "-DOXYGEN_PROFILE=$profile"
if ($LASTEXITCODE -ne 0) { throw 'Editor configuration failed.' }
& $cmake --build "$library/build/cmake/$profile" --config Release --target oxygen_editor
if ($LASTEXITCODE -ne 0) { throw 'Editor build failed.' }
