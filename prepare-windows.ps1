#requires -Version 5.1
<#
.SYNOPSIS
Prepares a Windows 11 x64 computer to build Aseprite KARN Mods.
.DESCRIPTION
Run from ordinary PowerShell. Only Microsoft's installer needs administrator
access. Existing clones are kept on their current revision; standalone copies
of these scripts clone karn-mods into the source subfolder.
#>
[CmdletBinding()]
param()

function Assert-KarnWindows {
    $architecture = $env:PROCESSOR_ARCHITECTURE
    if ($env:PROCESSOR_ARCHITEW6432) { $architecture = $env:PROCESSOR_ARCHITEW6432 }
    if ($env:OS -ne 'Windows_NT' -or $architecture -ne 'AMD64' -or
        [Environment]::OSVersion.Version.Build -lt 22000 -or
        -not [Environment]::Is64BitProcess) {
        throw 'Use 64-bit Windows PowerShell on Windows 11 with an Intel/AMD x64 processor. ARM64 is not supported by these scripts.'
    }
}

function Invoke-KarnCommand {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Executable $($Arguments -join ' ')"
    }
}

function Get-KarnDownload {
    param([string]$Url, [string]$Destination, [string]$Sha256)
    if (Test-Path -LiteralPath $Destination) {
        if (-not $Sha256 -or (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -eq $Sha256) {
            Write-Host "Using cached download: $(Split-Path $Destination -Leaf)"
            return
        }
        throw "Checksum mismatch: $Destination. Remove this file and rerun setup to download it again."
    }
    Write-Host "Downloading $(Split-Path $Destination -Leaf)..."
    # A failed download must never be mistaken for a complete cached archive.
    $partial = "$Destination.partial"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $partial
    if ($Sha256 -and (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Downloaded file failed SHA-256 verification: $Url"
    }
    Move-Item -LiteralPath $partial -Destination $Destination -Force
}

function Find-KarnVisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $found = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires `
        Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        Microsoft.VisualStudio.Component.VC.CMake.Project `
        Microsoft.VisualStudio.Component.Windows11SDK.26100 -property installationPath
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio detection failed.' }
    if ($found) { return ([string]$found).Trim() }
    return $null
}

function Install-KarnVisualStudio {
    param([string]$Downloads)
    $installer = Join-Path $Downloads 'vs_buildtools.exe'
    Get-KarnDownload 'https://aka.ms/vs/17/release/vs_buildtools.exe' $installer
    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation[,\s]') {
        throw 'The Visual Studio installer does not have a valid Microsoft signature. Setup stopped.'
    }
    Write-Host 'Opening Microsoft Visual Studio 2022 Build Tools setup.'
    Write-Host 'Approve its Windows administrator prompt, review the Microsoft terms, and click Install/Modify.'
    Write-Host 'The required C++ compiler, Windows SDK, CMake, and Ninja components are preselected.'
    # Keep the installer interactive so the user can review its license and progress.
    $installArgs = '--wait --add Microsoft.VisualStudio.Workload.VCTools ' +
        '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
        '--add Microsoft.VisualStudio.Component.Windows11SDK.26100 ' +
        '--add Microsoft.VisualStudio.Component.VC.CMake.Project'
    # If Build Tools already exists but is incomplete, modify that installation.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $existing = & $vswhere -latest -products Microsoft.VisualStudio.Product.BuildTools `
            -version '[17.0,18.0)' -property installationPath
        if ($LASTEXITCODE -ne 0) { throw 'Visual Studio detection failed.' }
        if ($existing) {
            $installArgs = 'modify --channelId VisualStudio.17.Release --installPath "' +
                ([string]$existing).Trim() + '" ' + $installArgs
        }
    }
    $process = Start-Process -FilePath $installer -ArgumentList $installArgs -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -in @(3010, 1641)) {
        throw 'Microsoft setup requires a restart. Restart Windows, then run prepare-windows.ps1 again.'
    }
    if ($process.ExitCode -ne 0) {
        throw "Microsoft setup exited with code $($process.ExitCode). Finish the C++ tools installation, then rerun this script."
    }
    $result = Find-KarnVisualStudio
    if (-not $result) { throw 'The required Visual Studio 2022 components are still missing. Rerun setup and finish the Microsoft installer.' }
    return $result
}

function Get-KarnGit {
    param([string]$Dependencies, [string]$Downloads)
    # Multiple Git installations can be on PATH. Keep its first match as one
    # executable path instead of passing an array that becomes a joined string.
    $installed = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($installed) { return $installed.Source }
    $portable = Join-Path $Dependencies 'mingit/cmd/git.exe'
    $ready = Join-Path $Dependencies 'mingit/.karn-ready'
    if ((Test-Path -LiteralPath $portable) -and (Test-Path -LiteralPath $ready)) { return $portable }
    Write-Host 'Getting portable Git for Windows (no system-wide installation needed)...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/git-for-windows/git/releases/latest' `
        -Headers @{ 'User-Agent' = 'KARN-Mods-Windows-Setup' }
    $assets = @($release.assets | Where-Object { $_.name -match '^MinGit-[0-9.]+-64-bit\.zip$' })
    if ($assets.Count -ne 1 -or $assets[0].digest -notmatch '^sha256:[0-9a-fA-F]{64}$') {
        throw 'Could not find a verified x64 MinGit release. Install Git from https://git-scm.com/download/win and rerun setup.'
    }
    $asset = $assets[0]
    $archive = Join-Path $Downloads $asset.name
    Get-KarnDownload $asset.browser_download_url $archive $asset.digest.Substring(7)
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $Dependencies 'mingit') -Force
    if (-not (Test-Path -LiteralPath $portable)) { throw 'Portable Git extraction failed.' }
    Set-Content -LiteralPath $ready -Value $asset.name
    return $portable
}

function Initialize-KarnSource {
    param([string]$Root, [string]$Git)
    $source = $Root
    if (-not (Test-Path -LiteralPath (Join-Path $Root '.git'))) {
        $source = Join-Path $Root 'source'
        if (-not (Test-Path -LiteralPath $source)) {
            Write-Host "Downloading the karn-mods branch into $source..."
            Invoke-KarnCommand $Git @('clone', '--branch', 'karn-mods', '--single-branch',
                'https://github.com/FearLabsAudio/aseprite.git', $source)
        }
        elseif (-not (Test-Path -LiteralPath (Join-Path $source '.git'))) {
            throw "$source already exists but is not a Git clone. Use an empty folder for the two scripts."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt')) -or
        -not (Test-Path -LiteralPath (Join-Path $source '.gitmodules'))) {
        throw "Not an Aseprite source checkout: $source"
    }
    Write-Host "Preparing submodules in $source (keeping the current branch and local changes)..."
    Invoke-KarnCommand $Git @('-C', $source, 'submodule', 'update', '--init', '--recursive')
    return $source
}

function Get-KarnSkia {
    param([string]$Source, [string]$Dependencies, [string]$Downloads)
    $tag = (Get-Content -LiteralPath (Join-Path $Source 'laf/misc/skia-tag.txt') -Raw).Trim()
    if ($tag -ne 'm124-08a5439a6b') {
        throw "This checkout requires Skia $tag. These scripts support m124-08a5439a6b; update the scripts before continuing."
    }
    $skia = Join-Path $Dependencies 'skia'
    $marker = Join-Path $skia 'include/core/SkMilestone.h'
    $library = Join-Path $skia 'out/Release-x64/skia.lib'
    $extracting = Join-Path $Dependencies 'skia-extraction-in-progress'
    if (-not (Test-Path -LiteralPath $extracting) -and
        (Test-Path -LiteralPath $marker) -and (Test-Path -LiteralPath $library) -and
        (Get-Content -LiteralPath $marker -Raw) -match '#define SK_MILESTONE 124\b') {
        Write-Host 'Using existing Skia m124 x64.'
        return $skia
    }
    if ((Test-Path -LiteralPath $library) -and -not (Test-Path -LiteralPath $extracting)) {
        throw "A different Skia build exists at $skia. Move it aside and rerun setup."
    }
    $archive = Join-Path $Dependencies 'Skia-Windows-Release-x64.zip'
    Get-KarnDownload "https://github.com/aseprite/skia/releases/download/$tag/Skia-Windows-Release-x64.zip" `
        $archive '5A371A4B2819BB4EB96E36CD75FA623585E1D5477E253A970302B6F2471B6934'
    Write-Host 'Extracting Skia (this can take several minutes)...'
    Set-Content -LiteralPath $extracting -Value $tag
    Expand-Archive -LiteralPath $archive -DestinationPath $skia -Force
    if (-not (Test-Path -LiteralPath $library) -or -not (Test-Path -LiteralPath $marker)) {
        throw 'Skia extraction was incomplete. Rerun setup.'
    }
    Remove-Item -LiteralPath $extracting
    return $skia
}

function Invoke-KarnPrepare {
    param([string]$Root)
    Assert-KarnWindows
    $dependencies = Join-Path $Root '.deps'
    $downloads = Join-Path $dependencies 'downloads'
    New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    $visualStudio = Find-KarnVisualStudio
    if (-not $visualStudio) { $visualStudio = Install-KarnVisualStudio $downloads }
    Write-Host "Using Visual Studio: $visualStudio"
    $cmakeRoot = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    $cmake = Join-Path $cmakeRoot 'CMake/bin/cmake.exe'
    $ninja = Join-Path $cmakeRoot 'Ninja/ninja.exe'
    foreach ($tool in @($cmake, $ninja)) {
        if (-not (Test-Path -LiteralPath $tool)) { throw "Missing $tool. Repair the Visual Studio C++ CMake tools component." }
    }
    $git = Get-KarnGit $dependencies $downloads
    $source = Initialize-KarnSource $Root $git
    $skia = Get-KarnSkia $source $dependencies $downloads
    $configuration = [ordered]@{
        version = 1; source = $source; visualStudio = $visualStudio
        cmake = $cmake; ninja = $ninja; git = $git; skia = $skia
    }
    $configuration | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dependencies 'windows-build.json') -Encoding UTF8
    Write-Host ''
    Write-Host 'Dependencies are ready. Next, double-click build-windows.cmd.' -ForegroundColor Green
}

# Dot-sourcing exposes helpers for the build script and isolated tests.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    $logStarted = $false
    try {
        New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot '.build') -Force | Out-Null
        Start-Transcript -Path (Join-Path $PSScriptRoot '.build/windows-setup.log') -Force | Out-Null
        $logStarted = $true
        Invoke-KarnPrepare $PSScriptRoot
    }
    catch {
        Write-Host "SETUP FAILED: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host 'See .build/windows-setup.log. Fix the problem and rerun the same command.'
        exit 1
    }
    finally { if ($logStarted) { Stop-Transcript | Out-Null } }
}
