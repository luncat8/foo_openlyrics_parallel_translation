<#
.SYNOPSIS
    Builds foo_openlyrics for Windows with MSBuild (MSVC) and logs the result.

.DESCRIPTION
    Wraps the same MSBuild invocation used by CI:

        msbuild /r /m /p:Configuration=<cfg> /p:Platform=<plat> build\foo_openlyrics.sln

    Every build writes a timestamped log under build\logs. Compiler and linker
    diagnostics are also extracted into a *-errors.txt file and printed to the
    console, so a failed build is easy to diagnose.

.PARAMETER Configuration
    Debug or Release. Defaults to Release.

.PARAMETER Platform
    x86, x64 or all. Defaults to x86.

.PARAMETER Clean
    Remove build\intermediate and the selected output directories before building.

.PARAMETER LogDirectory
    Where to write logs. Defaults to <repository>\build\logs.

.PARAMETER MSBuildPath
    Explicit path to MSBuild.exe. When omitted, MSBuild is located with vswhere.

.EXAMPLE
    .\build.bat

.EXAMPLE
    .\build.ps1 -Configuration Debug -Platform all

.EXAMPLE
    .\build.ps1 -Clean -Configuration Release -Platform x64
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x86', 'x64', 'all')]
    [string]$Platform = 'x86',

    [switch]$Clean,

    [string]$LogDirectory,

    [string]$MSBuildPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot
$solution = Join-Path $repoRoot 'build\foo_openlyrics.sln'
$platforms = if ($Platform -eq 'all') { @('x86', 'x64') } else { @($Platform) }

$script:logWriter = $null
$script:logFile = $null

function Write-Log {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,

        [string]$Level = 'INFO'
    )

    $line = '{0} [{1}] {2}' -f (Get-Date -Format 'HH:mm:ss'), $Level, $Message
    Write-Host $line
    if ($null -ne $script:logWriter) {
        $script:logWriter.WriteLine($line)
    }
}

function Write-BuildOutput {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line
    )

    Write-Host $Line
    if ($null -ne $script:logWriter) {
        $script:logWriter.WriteLine($Line)
    }
}

function Close-Log {
    if ($null -ne $script:logWriter) {
        try {
            $script:logWriter.Flush()
            $script:logWriter.Dispose()
        }
        catch {
            # Nothing useful can be done if the log cannot be flushed.
        }
        $script:logWriter = $null
    }
}

function Resolve-MSBuild {
    param(
        [string]$ExplicitPath
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "MSBuild was not found at the path supplied with -MSBuildPath: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    # Prefer vswhere: a plain `msbuild` on PATH can be the old .NET Framework
    # MSBuild, which cannot build the v143 toolset used by this project.
    $vswhereCandidates = @()
    $programFilesX86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vswhereCandidates += (Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $vswhereCandidates += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    }

    $vswhere = $vswhereCandidates |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1

    if ($null -ne $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installPath)) {
            $msbuild = Join-Path $installPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $msbuild -PathType Leaf) {
                return (Resolve-Path -LiteralPath $msbuild).Path
            }
        }
    }

    $fromPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $fromPath) {
        return $fromPath.Source
    }

    throw "MSBuild.exe could not be found. Pass its full path with -MSBuildPath."
}

function Get-BuildOutputDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PlatformName
    )

    if ($PlatformName -eq 'x64') {
        return (Join-Path $repoRoot "build\x64\$Configuration")
    }
    return (Join-Path $repoRoot "build\$Configuration")
}

function Remove-BuildOutputs {
    $targets = @(Join-Path $repoRoot 'build\intermediate')
    foreach ($platformName in $platforms) {
        $targets += (Get-BuildOutputDirectory -PlatformName $platformName)
    }

    foreach ($target in $targets) {
        if (Test-Path -LiteralPath $target) {
            Write-Log "Removing $target"
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
}

function Invoke-Build {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PlatformName,

        [Parameter(Mandatory = $true)]
        [string]$MSBuild
    )

    Write-Log "Building $Configuration|$PlatformName ..."
    Write-Log "  MSBuild: $MSBuild"
    Write-Log "  Solution: $solution"

    $msbuildArgs = @(
        $solution,
        '/r',
        '/m',
        '/nologo',
        "/p:Configuration=$Configuration",
        "/p:Platform=$PlatformName",
        '/v:minimal'
    )

    & $MSBuild $msbuildArgs 2>&1 | ForEach-Object { Write-BuildOutput ([string]$_) }

    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "MSBuild exited with code $exitCode."
    }
}

function Get-DiagnosticLines {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return @()
    }

    return @(Select-String -LiteralPath $Path -Pattern $Pattern |
        Select-Object -ExpandProperty Line |
        Sort-Object -Unique)
}

function Write-Diagnostics {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Pattern,

        [Parameter(Mandatory = $true)]
        [string]$OutputFile
    )

    $lines = @(Get-DiagnosticLines -Path $Path -Pattern $Pattern)
    if ($lines.Count -eq 0) {
        return $lines
    }

    Set-Content -LiteralPath $OutputFile -Value $lines -Encoding UTF8
    return $lines
}

function Write-ArtifactInfo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PlatformName
    )

    $outputDir = Get-BuildOutputDirectory -PlatformName $PlatformName
    $dll = Join-Path $outputDir 'foo_openlyrics.dll'

    if (Test-Path -LiteralPath $dll -PathType Leaf) {
        $item = Get-Item -LiteralPath $dll
        Write-Log ("Artifact: {0} ({1:N0} bytes, modified {2})" -f $item.FullName, $item.Length, $item.LastWriteTime) 'OK'
    }
    else {
        Write-Log "Expected artifact was not produced: $dll" 'WARN'
    }
}

$exitCode = 0

try {
    if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
        throw "Solution was not found: $solution"
    }

    if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
        $LogDirectory = Join-Path $repoRoot 'build\logs'
    }
    $null = New-Item -ItemType Directory -Force -Path $LogDirectory

    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $platformTag = if ($Platform -eq 'all') { 'all' } else { $Platform }
    $logFile = Join-Path $LogDirectory ("build-{0}-{1}-{2}.log" -f $Configuration, $platformTag, $timestamp)
    $script:logFile = $logFile

    $script:logWriter = New-Object System.IO.StreamWriter(
        $logFile,
        $false,
        (New-Object System.Text.UTF8Encoding($false)))
    $script:logWriter.AutoFlush = $true

    Write-Log "foo_openlyrics build"
    Write-Log "  Configuration: $Configuration"
    Write-Log "  Platform(s): $($platforms -join ', ')"
    Write-Log "  Log file: $logFile"

    $msbuild = Resolve-MSBuild -ExplicitPath $MSBuildPath
    Write-Log "Using MSBuild: $msbuild"

    if ($Clean) {
        Write-Log 'Cleaning previous build outputs ...'
        Remove-BuildOutputs
    }

    foreach ($platformName in $platforms) {
        Invoke-Build -PlatformName $platformName -MSBuild $msbuild
    }

    # Flush before reading the log back for diagnostics.
    if ($null -ne $script:logWriter) {
        $script:logWriter.Flush()
    }

    $errorPattern = '(?i)(^|[\s(])(fatal\s+)?error\s+([A-Za-z]+\d+)?\s*:'
    $warningPattern = '(?i)(^|[\s(])warning\s+([A-Za-z]+\d+)?\s*:'
    $errorFile = [System.IO.Path]::ChangeExtension($logFile, '.errors.txt')
    $warningFile = [System.IO.Path]::ChangeExtension($logFile, '.warnings.txt')

    $errors = @(Write-Diagnostics -Path $logFile -Pattern $errorPattern -OutputFile $errorFile)
    $warnings = @(Write-Diagnostics -Path $logFile -Pattern $warningPattern -OutputFile $warningFile)

    foreach ($platformName in $platforms) {
        Write-ArtifactInfo -PlatformName $platformName
    }

    if ($errors.Count -gt 0) {
        $exitCode = 1
        Write-Log "BUILD FAILED: $($errors.Count) unique error line(s)." 'ERROR'
        $errors | Select-Object -First 50 | ForEach-Object { Write-Log "  $_" 'ERROR' }
        if ($errors.Count -gt 50) {
            Write-Log "  ... and $($errors.Count - 50) more. Full list: $errorFile" 'ERROR'
        }
    }
    else {
        Write-Log 'Build completed without compiler or linker errors.' 'OK'
    }

    if ($warnings.Count -gt 0) {
        Write-Log "$($warnings.Count) unique warning line(s). Full list: $warningFile" 'WARN'
    }

}
catch {
    Write-Log "FATAL: $($_.Exception.Message)" 'ERROR'
    if (-not [string]::IsNullOrWhiteSpace($_.ScriptStackTrace)) {
        Write-Log $_.ScriptStackTrace 'ERROR'
    }
    $exitCode = 1
}
finally {
    Close-Log
    Write-Host ''
    if ($exitCode -eq 0) {
        Write-Host "Build finished successfully. Log: $script:logFile"
    }
    else {
        Write-Host "Build failed. Log: $script:logFile"
    }
}

exit $exitCode