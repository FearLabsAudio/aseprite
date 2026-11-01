#requires -Version 5.1
<#
.SYNOPSIS
Builds Aseprite KARN Mods after prepare-windows.ps1 has completed.
.PARAMETER Jobs
Maximum parallel compiler jobs. Defaults to at most four to limit memory use.
.PARAMETER BuildDirectory
Optional build folder. Defaults to build/windows-karn inside the source checkout.
.PARAMETER CheckOnly
Checks the prepared tools and imports the compiler environment without building.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 128)][int]$Jobs = [Math]::Max(1, [Math]::Min(4, [Environment]::ProcessorCount)),
    [string]$BuildDirectory,
    [switch]$CheckOnly
)

. (Join-Path $PSScriptRoot 'prepare-windows.ps1')

function Invoke-KarnBuild {
    param([string]$Root, [int]$Jobs, [string]$BuildDirectory, [switch]$CheckOnly)
    Assert-KarnWindows
    $manifest = Join-Path $Root '.deps/windows-build.json'
    if (-not (Test-Path -LiteralPath $manifest)) { throw 'Run prepare-windows.ps1 successfully before building.' }
    $config = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($config.version -ne 1) { throw 'Setup information is outdated. Rerun prepare-windows.ps1.' }
    foreach ($path in @($config.cmake, $config.ninja, $config.git,
        (Join-Path $config.source 'CMakeLists.txt'),
        (Join-Path $config.source 'laf/clip/CMakeLists.txt'),
        (Join-Path $config.skia 'out/Release-x64/skia.lib'))) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path. Rerun prepare-windows.ps1." }
    }
    if (-not $BuildDirectory) { $BuildDirectory = Join-Path $config.source 'build/windows-karn' }
    $BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
    if ($BuildDirectory.TrimEnd('\', '/') -eq ([string]$config.source).TrimEnd('\', '/')) {
        throw 'Choose a build subfolder, not the source folder itself.'
    }
    $cache = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $cacheText = Get-Content -LiteralPath $cache -Raw
        $generator = [regex]::Match($cacheText, '(?m)^CMAKE_GENERATOR:INTERNAL=(.+)\r?$').Groups[1].Value.Trim()
        $cachedSource = [regex]::Match($cacheText, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$').Groups[1].Value.Trim()
        if ($generator -ne 'Ninja' -or -not $cachedSource -or
            [IO.Path]::GetFullPath($cachedSource) -ne [IO.Path]::GetFullPath($config.source)) {
            throw 'This build folder belongs to a different source folder or generator. Use -BuildDirectory with a new empty folder.'
        }
    }
    $executable = Join-Path $BuildDirectory 'bin/aseprite.exe'
    foreach ($process in @(Get-Process aseprite -ErrorAction SilentlyContinue)) {
        if ($process.Path -eq $executable) { throw 'Save your artwork and close this Aseprite build, then rerun build-windows.ps1.' }
    }
    if (-not $CheckOnly -and (Test-Path -LiteralPath $executable)) {
        # Opening an existing file without writing or truncating catches locks
        # and permission problems before spending time compiling the project.
        try {
            $probe = [IO.File]::Open($executable, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $probe.Dispose()
        }
        catch {
            throw "Windows cannot replace $executable. Close applications using it and check folder permissions, or choose a new -BuildDirectory."
        }
    }
    Write-Host 'Loading the Visual Studio x64 compiler environment...'
    Import-Module (Join-Path $config.visualStudio 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $config.visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Host
    $env:Path = (Split-Path $config.git) + ';' + $env:Path
    $compiler = Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue
    if (-not $compiler) {
        throw 'The Microsoft C++ compiler could not be loaded. Rerun prepare-windows.ps1.'
    }
    Invoke-KarnCommand $config.cmake @('--version')
    Invoke-KarnCommand $config.ninja @('--version')
    if ($CheckOnly) { Write-Host 'Build prerequisites passed.' -ForegroundColor Green; return }
    Write-Host "Configuring $BuildDirectory..."
    # Use libpng's supplied configuration header. An unrelated MSYS/Cygwin awk
    # on PATH must not change this native Windows build's dependencies.
    Invoke-KarnCommand $config.cmake @('-S', $config.source, '-B', $BuildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',
        "-DCMAKE_C_COMPILER=$($compiler.Source)", "-DCMAKE_CXX_COMPILER=$($compiler.Source)",
        "-DCMAKE_MAKE_PROGRAM=$($config.ninja)", '-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DAWK:FILEPATH=OFF',
        '-DLAF_BACKEND=skia', "-DSKIA_DIR=$($config.skia)",
        "-DSKIA_LIBRARY_DIR=$($config.skia)/out/Release-x64",
        "-DSKIA_LIBRARY=$($config.skia)/out/Release-x64/skia.lib")
    Write-Host "Building Aseprite with $Jobs compiler jobs. The first build can take a while..."
    Invoke-KarnCommand $config.cmake @('--build', $BuildDirectory, '--target', 'aseprite', '--parallel', "$Jobs")
    if (-not (Test-Path -LiteralPath $executable)) { throw 'The build finished without producing aseprite.exe.' }
    Write-Host ''
    Write-Host 'Build complete. Start Aseprite here:' -ForegroundColor Green
    Write-Host $executable
    Write-Host 'Keep the entire bin folder together; it includes the editor resources.'
}

if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    $logStarted = $false
    try {
        New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot '.build') -Force | Out-Null
        Start-Transcript -Path (Join-Path $PSScriptRoot '.build/windows-build.log') -Force | Out-Null
        $logStarted = $true
        Invoke-KarnBuild $PSScriptRoot $Jobs $BuildDirectory -CheckOnly:$CheckOnly
    }
    catch {
        Write-Host "BUILD FAILED: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host 'See .build/windows-build.log. If tools moved or were removed, rerun prepare-windows.ps1.'
        exit 1
    }
    finally { if ($logStarted) { Stop-Transcript | Out-Null } }
}
