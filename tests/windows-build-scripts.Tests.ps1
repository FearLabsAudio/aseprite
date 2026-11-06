#requires -Version 5.1
# No Pester or administrator rights required. Downloads/installers use fixtures.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tests/windows-build-scripts.Tests.ps1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
. (Join-Path $repository 'build-windows.ps1')
$fixtures = Join-Path $repository ('.build/windows-script-tests/' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $fixtures -Force | Out-Null
$script:passed = 0

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    try { & $Action }
    catch {
        if ($_.Exception.Message -match $Pattern) { return }
        throw "Expected '$Pattern', got: $($_.Exception.Message)"
    }
    throw "Expected an error matching '$Pattern'."
}
function Test-Case([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Host "PASS: $Name"
}
function New-Fixture([string]$Name) {
    $path = Join-Path $fixtures $Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}
function Write-Fixture([string]$Path, [string]$Content = 'fixture') {
    New-Item -ItemType Directory -Path (Split-Path $Path) -Force | Out-Null
    Set-Content -LiteralPath $Path -Value $Content
}

Test-Case 'Both entry points parse in Windows PowerShell' {
    foreach ($name in @('prepare-windows.ps1', 'build-windows.ps1')) {
        $tokens = $null; $errors = $null
        [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repository $name), [ref]$tokens, [ref]$errors) | Out-Null
        Assert-True ($errors.Count -eq 0) "$name has syntax errors."
    }
}
Test-Case 'Failed native commands stop the script' {
    Assert-Throws { Invoke-KarnCommand $env:ComSpec @('/d', '/c', 'exit', '7') } 'exit 7'
}
Test-Case 'Downloads verify checksums, cache successes, and reject corruption' {
    $root = New-Fixture 'download folder with spaces'
    $sample = Join-Path $root 'sample'
    Set-Content -LiteralPath $sample -Value 'test download'
    $digest = (Get-FileHash -LiteralPath $sample).Hash
    $script:requests = 0
    function Invoke-WebRequest {
        param([switch]$UseBasicParsing, $Uri, $OutFile)
        $script:requests++
        Copy-Item -LiteralPath $sample -Destination $OutFile
    }
    $download = Join-Path $root 'cached.zip'
    Get-KarnDownload 'https://example.invalid/file' $download $digest
    Get-KarnDownload 'https://example.invalid/file' $download $digest
    Assert-True ($script:requests -eq 1) 'A cached valid download was fetched again.'
    Set-Content -LiteralPath $download -Value 'corrupt'
    Assert-Throws { Get-KarnDownload 'https://example.invalid/file' $download $digest } 'Checksum mismatch'
    Assert-Throws { Get-KarnDownload 'https://example.invalid/file' (Join-Path $root 'bad.zip') ('0' * 64) } 'SHA-256 verification'
    Assert-True (-not (Test-Path (Join-Path $root 'bad.zip'))) 'An invalid download was published as complete.'
}
Test-Case 'Interrupted downloads are retried instead of reused' {
    $root = New-Fixture 'interrupted'
    $target = Join-Path $root 'download.zip'
    function Invoke-WebRequest {
        param([switch]$UseBasicParsing, $Uri, $OutFile)
        Set-Content -LiteralPath $OutFile -Value 'partial'
        throw 'Connection interrupted'
    }
    Assert-Throws { Get-KarnDownload 'https://example.invalid/file' $target } 'Connection interrupted'
    Assert-True (-not (Test-Path $target)) 'A partial download was cached.'
    function Invoke-WebRequest {
        param([switch]$UseBasicParsing, $Uri, $OutFile)
        Set-Content -LiteralPath $OutFile -Value 'complete'
    }
    Get-KarnDownload 'https://example.invalid/file' $target
    Assert-True ((Get-Content $target) -eq 'complete') 'The retry did not complete.'
}
Test-Case 'Multiple Git installations select only the first executable on PATH' {
    function Get-Command {
        param($Name, $CommandType, $ErrorAction)
        return @(
            @{ Source = 'C:\Program Files\Git\cmd\git.exe' },
            @{ Source = 'C:\msys64\usr\bin\git.exe' }
        )
    }
    function Invoke-RestMethod { throw 'Installed Git must not trigger a download.' }
    $git = Get-KarnGit $fixtures $fixtures
    Assert-True ($git -is [string]) 'Git selection returned more than one executable.'
    Assert-True ($git -eq 'C:\Program Files\Git\cmd\git.exe') 'Git selection did not preserve the first PATH match.'
}
Test-Case 'Missing Git uses a verified portable package and survives a repeat run' {
    $root = New-Fixture 'portable git'
    $payload = New-Fixture 'git zip contents'
    Write-Fixture (Join-Path $payload 'cmd/git.exe')
    $gitFixtureArchive = Join-Path $root 'fixture.zip'
    Compress-Archive -Path (Join-Path $payload 'cmd') -DestinationPath $gitFixtureArchive
    $digest = (Get-FileHash -LiteralPath $gitFixtureArchive).Hash
    function Get-Command { param($Name, $CommandType, $ErrorAction) return $null }
    function Invoke-RestMethod {
        param($Uri, $Headers)
        return @{ assets = @(@{ name = 'MinGit-2.99.0-64-bit.zip'; digest = "sha256:$digest"; browser_download_url = 'https://example.invalid/git.zip' }) }
    }
    function Get-KarnDownload {
        param($Url, $Destination, $Sha256)
        Assert-True ($Sha256 -eq $digest) 'Git checksum was not passed through.'
        Copy-Item -LiteralPath $gitFixtureArchive -Destination $Destination
    }
    $git = Get-KarnGit $root $root
    Assert-True (Test-Path $git) 'Portable Git was not extracted.'
    function Invoke-RestMethod { throw 'Repeat setup must use the cached portable Git.' }
    Assert-True ((Get-KarnGit $root $root) -eq $git) 'Repeat setup changed Git.'
}
Test-Case 'Installer rejects untrusted signatures and reports cancellation/reboot' {
    $root = New-Fixture 'installer'
    function Get-KarnDownload { param($Url, $Destination, $Sha256) }
    function Get-AuthenticodeSignature { param($LiteralPath) return @{ Status = 'NotSigned' } }
    function Start-Process { throw 'An untrusted installer must not be launched.' }
    Assert-Throws { Install-KarnVisualStudio $root } 'valid Microsoft signature'
    function Get-AuthenticodeSignature {
        param($LiteralPath)
        return @{ Status = 'Valid'; SignerCertificate = @{ Subject = 'CN=Microsoft Corporation, O=Microsoft Corporation, C=US' } }
    }
    function Start-Process {
        param($FilePath, $ArgumentList, $Verb, [switch]$Wait, [switch]$PassThru)
        Assert-True ($Verb -eq 'RunAs') 'Installer did not request elevation.'
        Assert-True ($ArgumentList -match 'Windows11SDK.26100') 'SDK not selected.'
        Assert-True ($ArgumentList -notmatch '--quiet|--passive|--norestart') 'Installer must allow interactive license review.'
        return @{ ExitCode = 3010 }
    }
    Assert-Throws { Install-KarnVisualStudio $root } 'Restart Windows'
    function Start-Process {
        param($FilePath, $ArgumentList, $Verb, [switch]$Wait, [switch]$PassThru)
        return @{ ExitCode = 1602 }
    }
    Assert-Throws { Install-KarnVisualStudio $root } '1602'
}
Test-Case 'Standalone setup clones once; existing clones only prepare submodules' {
    $root = New-Fixture 'standalone folder'
    $script:commands = @()
    function Invoke-KarnCommand {
        param($Executable, $Arguments)
        $script:commands += ,$Arguments
        if ($Arguments[0] -eq 'clone') {
            $destination = $Arguments[-1]
            New-Item -ItemType Directory -Path (Join-Path $destination '.git') -Force | Out-Null
            Write-Fixture (Join-Path $destination 'CMakeLists.txt')
            Write-Fixture (Join-Path $destination '.gitmodules')
        }
    }
    $source = Initialize-KarnSource $root 'fake-git'
    Assert-True ($source -eq (Join-Path $root 'source')) 'Standalone source location is wrong.'
    Initialize-KarnSource $root 'fake-git' | Out-Null
    Initialize-KarnSource $source 'fake-git' | Out-Null
    Assert-True ($script:commands.Count -eq 4) 'Unexpected clone/update count.'
    Assert-True (($script:commands[0] -join ' ') -match 'clone --branch karn-mods') 'Wrong clone branch.'
    foreach ($command in $script:commands[1..3]) {
        Assert-True (($command -join ' ') -match 'submodule update --init --recursive$') 'An existing clone was unexpectedly changed.'
    }
    $occupied = New-Fixture 'occupied'
    New-Item -ItemType Directory -Path (Join-Path $occupied 'source') | Out-Null
    Assert-Throws { Initialize-KarnSource $occupied 'fake-git' } 'not a Git clone'
}
Test-Case 'Skia checks the source requirement and reuses the matching library' {
    $root = New-Fixture 'skia'
    Write-Fixture (Join-Path $root 'laf/misc/skia-tag.txt') 'm999'
    Assert-Throws { Get-KarnSkia $root $root $root } 'requires Skia m999'
    Write-Fixture (Join-Path $root 'laf/misc/skia-tag.txt') 'm124-08a5439a6b'
    Write-Fixture (Join-Path $root 'skia/include/core/SkMilestone.h') '#define SK_MILESTONE 124'
    Write-Fixture (Join-Path $root 'skia/out/Release-x64/skia.lib')
    function Get-KarnDownload { throw 'Matching Skia should be reused.' }
    Assert-True ((Get-KarnSkia $root $root $root) -eq (Join-Path $root 'skia')) 'Matching Skia was not reused.'
}
Test-Case 'Successful preparation writes configuration; failed setup does not' {
    $root = New-Fixture 'full setup with spaces'
    $vs = Join-Path $root 'Visual Studio'
    Write-Fixture (Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    Write-Fixture (Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')
    function Find-KarnVisualStudio { return $null }
    function Install-KarnVisualStudio { param($Downloads) return $vs }
    function Get-KarnGit { param($Dependencies, $Downloads) return (Join-Path $root 'git.exe') }
    function Initialize-KarnSource { param($Root, $Git) return (Join-Path $Root 'source') }
    function Get-KarnSkia { param($Source, $Dependencies, $Downloads) return (Join-Path $Dependencies 'skia') }
    Invoke-KarnPrepare $root
    $config = Get-Content (Join-Path $root '.deps/windows-build.json') -Raw | ConvertFrom-Json
    Assert-True ($config.source -eq (Join-Path $root 'source')) 'Source path was not saved.'
    Assert-True ($config.visualStudio -eq $vs) 'Tool path with spaces was not saved.'
    function Initialize-KarnSource { throw 'submodule failure' }
    $failed = New-Fixture 'failed setup'
    Assert-Throws { Invoke-KarnPrepare $failed } 'submodule failure'
    Assert-True (-not (Test-Path (Join-Path $failed '.deps/windows-build.json'))) 'Failed setup produced a success manifest.'
}
Test-Case 'Build rejects missing setup and incompatible build caches' {
    $root = New-Fixture 'bad builds'
    Assert-Throws { Invoke-KarnBuild $root 2 '' } 'Run prepare-windows.ps1'
    $source = Join-Path $root 'source'
    $tool = Join-Path $root 'tool.exe'
    Write-Fixture $tool
    Write-Fixture (Join-Path $source 'CMakeLists.txt')
    Write-Fixture (Join-Path $source 'laf/clip/CMakeLists.txt')
    Write-Fixture (Join-Path $root 'skia/out/Release-x64/skia.lib')
    $config = @{ version = 1; source = $source; cmake = $tool; ninja = $tool; git = $tool; skia = (Join-Path $root 'skia') }
    Write-Fixture (Join-Path $root '.deps/windows-build.json') ($config | ConvertTo-Json)
    Assert-Throws { Invoke-KarnBuild $root 2 $source } 'source folder itself'
    $build = Join-Path $root 'build'
    Write-Fixture (Join-Path $build 'CMakeCache.txt') "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022`nCMAKE_HOME_DIRECTORY:INTERNAL=$source"
    Assert-Throws { Invoke-KarnBuild $root 2 $build } 'different source folder or generator'
    Write-Fixture (Join-Path $build 'CMakeCache.txt') "CMAKE_GENERATOR:INTERNAL=Ninja`nCMAKE_HOME_DIRECTORY:INTERNAL=C:/elsewhere"
    Assert-Throws { Invoke-KarnBuild $root 2 $build } 'different source folder or generator'
    Write-Fixture (Join-Path $build 'CMakeCache.txt') "CMAKE_GENERATOR:INTERNAL=Ninja`nCMAKE_HOME_DIRECTORY:INTERNAL=$source"
    function Get-Process {
        param($Name, $ErrorAction)
        return @{ Path = (Join-Path $build 'bin/aseprite.exe') }
    }
    Assert-Throws { Invoke-KarnBuild $root 2 $build } 'Save your artwork and close'
    function Get-Process { param($Name, $ErrorAction) }
    $lockedExe = Join-Path $build 'bin/aseprite.exe'
    Write-Fixture $lockedExe 'do not change this file'
    $lock = [IO.File]::Open($lockedExe, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        Assert-Throws { Invoke-KarnBuild $root 2 $build } 'Windows cannot replace'
    }
    finally { $lock.Dispose() }
    Assert-True ((Get-Content -LiteralPath $lockedExe) -eq 'do not change this file') 'The lock probe altered the executable.'
}
Test-Case 'Build keeps paths intact, selects x64, and stops after a configure failure' {
    $root = New-Fixture 'build args with spaces'
    $source = Join-Path $root 'source'
    $tool = Join-Path $root 'tool.exe'
    $fakeCompiler = Join-Path $root 'MSVC/bin/cl.exe'
    Write-Fixture $tool
    Write-Fixture (Join-Path $source 'CMakeLists.txt')
    Write-Fixture (Join-Path $source 'laf/clip/CMakeLists.txt')
    Write-Fixture (Join-Path $root 'skia/out/Release-x64/skia.lib')
    $config = @{ version = 1; source = $source; visualStudio = $root; cmake = $tool; ninja = $tool; git = $tool; skia = (Join-Path $root 'skia') }
    Write-Fixture (Join-Path $root '.deps/windows-build.json') ($config | ConvertTo-Json)
    function Get-Process { param($Name, $ErrorAction) }
    function Import-Module { param($Name) }
    function Enter-VsDevShell {
        param($VsInstallPath, [switch]$SkipAutomaticLocation, $DevCmdArguments)
        Assert-True ($DevCmdArguments -eq '-arch=x64 -host_arch=x64') 'Wrong compiler architecture.'
    }
    function Get-Command {
        param($Name, $CommandType, $ErrorAction)
        return @(@{ Source = $fakeCompiler }, @{ Source = 'C:\other compiler\cl.exe' })
    }
    $script:buildCalls = @()
    function Invoke-KarnCommand {
        param($Executable, $Arguments)
        $script:buildCalls += ,$Arguments
        if ($Arguments[0] -eq '--build') {
            Write-Fixture (Join-Path $Arguments[1] 'bin/aseprite.exe')
        }
    }
    Invoke-KarnBuild $root 2 '' -CheckOnly
    Assert-True ($script:buildCalls.Count -eq 2) 'CheckOnly tried to build.'
    $script:buildCalls = @()
    Invoke-KarnBuild $root 2 ''
    Assert-True ($script:buildCalls.Count -eq 4) 'Expected version checks, configure, and build.'
    Assert-True ($script:buildCalls[2][1] -eq $source) 'Source path with spaces was split.'
    Assert-True ($script:buildCalls[2] -contains '-DCMAKE_BUILD_TYPE=Release') 'Expected an optimized end-user build.'
    Assert-True ($script:buildCalls[2] -contains '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release') 'Compiler checks must use the same configuration.'
    Assert-True ($script:buildCalls[2] -contains '-DAWK:FILEPATH=OFF') 'An unrelated MSYS installation could affect the build.'
    Assert-True ($script:buildCalls[2] -contains "-DCMAKE_CXX_COMPILER=$fakeCompiler") 'Compiler full path was not used.'
    Assert-True ($script:buildCalls[3][-1] -eq '2') 'Job limit was not honored.'
    Assert-True ($script:buildCalls[3][1] -eq (Join-Path $source 'build/windows-karn')) 'Unexpected default output folder.'
    function Invoke-KarnCommand {
        param($Executable, $Arguments)
        if ($Arguments[0] -eq '-S') { throw 'configure failed' }
        if ($Arguments[0] -eq '--build') { throw 'must not build after failure' }
    }
    Assert-Throws { Invoke-KarnBuild $root 2 '' } 'configure failed'
}
Write-Host "$script:passed Windows build-script tests passed. Fixtures: $fixtures" -ForegroundColor Green
