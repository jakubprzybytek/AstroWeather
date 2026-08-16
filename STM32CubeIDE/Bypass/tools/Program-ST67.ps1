[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [ValidateSet('MissionT01', 'MissionT02', 'Manufacturing')]
    [string]$Profile,

    [string]$Version,
    [string]$SdkRoot,
    [string]$ConfigPath,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    $SdkRoot = Join-Path $scriptRoot 'External\x-cube-st67w61'
}
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$binariesRoot = Join-Path $SdkRoot 'Projects\ST67W6X_Scripts\Binaries'
$ncpBinaryRoot = Join-Path $binariesRoot 'NCP_Binaries'
$qconnRoot = Join-Path $binariesRoot 'QConn_Flash'

$profileInfo = @{
    MissionT01 = @{ Prefix = 'st67w611m_mission_t01'; Config = 'mission_t01_flash_prog_cfg.ini' }
    MissionT02 = @{ Prefix = 'st67w611m_mission_t02'; Config = 'mission_t02_flash_prog_cfg.ini' }
    Manufacturing = @{ Prefix = 'st67w611m_mfg'; Config = 'mfg_flash_prog_cfg.ini' }
}[$Profile]

$generatedConfig = $null
$result = 1
try {
    $availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($availablePorts -notcontains $Port) {
        $portList = if ($availablePorts) { $availablePorts -join ', ' } else { '(none)' }
        throw "Port $Port was not found. Available ports: $portList"
    }

    if (-not (Test-Path -LiteralPath $binariesRoot -PathType Container)) {
        throw "X-CUBE-ST67 Binaries directory was not found: $binariesRoot"
    }

    $qconnCandidates = @(Get-ChildItem -LiteralPath $qconnRoot -Filter 'QConn_Flash_Cmd.exe' -File -Recurse)
    if ($qconnCandidates.Count -ne 1) {
        throw "Expected exactly one QConn_Flash_Cmd.exe under $qconnRoot, found $($qconnCandidates.Count)."
    }
    $qconn = $qconnCandidates[0].FullName

    if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
        $templatePath = Join-Path $ncpBinaryRoot $profileInfo.Config
    } else {
        $templatePath = (Resolve-Path -LiteralPath $ConfigPath).Path
    }
    if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf)) {
        throw "Flash configuration was not found: $templatePath"
    }

    if ([string]::IsNullOrWhiteSpace($Version)) {
        $versionFiles = @(Get-ChildItem -LiteralPath $ncpBinaryRoot -Filter "$($profileInfo.Prefix)_v*.bin" -File |
            Where-Object { $_.BaseName -match "^$([regex]::Escape($profileInfo.Prefix))_v\d+\.\d+\.\d+$" })
        if ($versionFiles.Count -eq 0) {
            throw "No versioned $Profile firmware image was found in $ncpBinaryRoot"
        }
        $selectedFile = $versionFiles |
            Sort-Object { [version]($_.BaseName.Substring($profileInfo.Prefix.Length + 2)) } -Descending |
            Select-Object -First 1
        $Version = $selectedFile.BaseName.Substring($profileInfo.Prefix.Length + 2)
    }
    if ($Version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must use the form major.minor.patch, for example 2.0.106."
    }

    $selectedImage = Join-Path $ncpBinaryRoot "$($profileInfo.Prefix)_v$Version.bin"
    if (-not (Test-Path -LiteralPath $selectedImage -PathType Leaf)) {
        throw "Selected $Profile image was not found: $selectedImage"
    }

    $efuseImage = Join-Path $ncpBinaryRoot 'efusedata.bin'
    if (-not (Test-Path -LiteralPath $efuseImage -PathType Leaf)) {
        throw "Required efuse image was not found: $efuseImage"
    }

    if (-not $Force) {
        Write-Warning 'This operation may lock an unlocked ST67 device. Use -Force only after confirming the profile and image.'
        throw 'Programming was not started. Re-run with -Force after reviewing the warning.'
    }

    $templateDirectory = Split-Path -Parent $templatePath
    $templateLines = @(Get-Content -LiteralPath $templatePath)
    $firmwareMatches = 0
    $resolvedLines = foreach ($line in $templateLines) {
        if ($line -match '^\s*filedir\s*=\s*(?<file>[^;#]+?)\s*$') {
            $configuredPath = $Matches.file.Trim()
            if ($configuredPath -match "^\./$([regex]::Escape($profileInfo.Prefix))_v(?:xxx|\d+\.\d+\.\d+)\.bin$") {
                $firmwareMatches++
                "filedir = $selectedImage"
            } else {
                $referencePath = if ([IO.Path]::IsPathRooted($configuredPath)) {
                    $configuredPath
                } else {
                    Join-Path $templateDirectory $configuredPath
                }
                $resolvedReference = [IO.Path]::GetFullPath($referencePath)
                if (-not (Test-Path -LiteralPath $resolvedReference -PathType Leaf)) {
                    throw "Configuration references a missing image: $resolvedReference"
                }
                "filedir = $resolvedReference"
            }
        } else {
            $line
        }
    }
    if ($firmwareMatches -ne 1) {
        throw "Expected exactly one $Profile firmware filedir entry in $templatePath, found $firmwareMatches."
    }

    $generatedConfig = Join-Path ([IO.Path]::GetTempPath()) "st67-bypass-$([guid]::NewGuid().ToString('N')).ini"
    [IO.File]::WriteAllLines($generatedConfig, $resolvedLines, [Text.ASCIIEncoding]::new())

    Write-Host "Programming $Profile version $Version through $Port"
    Write-Host "Using QConn: $qconn"
    & $qconn '--port' $Port '--config' $generatedConfig "--efuse=$efuseImage" 2>&1 | Tee-Object -Variable qconnOutput
    $result = $LASTEXITCODE
    if ($result -ne 0) {
        throw "QConn_Flash_Cmd failed with exit code $result. Re-enter bootloader mode before any retry."
    }
    Write-Host 'QConn programming completed successfully.'
}
catch {
    Write-Error $_
    if ($result -eq 0) {
        $result = 1
    }
}
finally {
    if ($null -ne $generatedConfig -and (Test-Path -LiteralPath $generatedConfig)) {
        try {
            Remove-Item -LiteralPath $generatedConfig -Force
        }
        catch {
            Write-Warning "Could not remove generated temporary configuration $generatedConfig : $_"
            if ($result -eq 0) {
                $result = 1
            }
        }
    }
}

exit $result
