[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$checkerPath = Join-Path $repoRoot 'tools\verify_public_release.ps1'

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Write-Utf8Text {
    param(
        [string]$Path,
        [string]$Text
    )

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Set-Content -LiteralPath $Path -Value $Text -Encoding UTF8
}

function New-CheckerFixture {
    param(
        [string]$Name
    )

    $fixtureBase = Join-Path $env:TEMP ('verify-public-release-fixtures-' + $PID)
    $fixtureRoot = Join-Path $fixtureBase $Name
    if (Test-Path -LiteralPath $fixtureRoot) {
        throw "Fixture already exists: $fixtureRoot"
    }

    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    foreach ($relativePath in @(
        'LICENSE',
        'THIRD_PARTY_NOTICES.md',
        '.gitignore',
        'components/ad9226_f407/README.md',
        'components/ad9226_f407/LICENSE'
    )) {
        Write-Utf8Text (Join-Path $fixtureRoot $relativePath) 'fixture'
    }

    $firmwareElf = 'H743_Signal_Processor_' + 'Firmware.elf'
    Write-Utf8Text (Join-Path $fixtureRoot 'README.md') $firmwareElf
    $taskLabels = @(
        'Build F407',
        'Flash F407 - ST-Link',
        'Flash F407 - CMSIS-DAP',
        'Build H743 Firmware',
        'Run H743 Host Tests',
        'Flash H743 - ST-Link'
    )
    $workspace = @{
        folders = @()
        tasks = @{
            version = '2.0.0'
            tasks = @(
                foreach ($label in $taskLabels) {
                    @{
                        label = $label
                        args = @($firmwareElf)
                    }
                }
            )
        }
    }
    Write-Utf8Text (Join-Path $fixtureRoot 'periodic-signal-analyzer.code-workspace') ($workspace | ConvertTo-Json -Depth 8)
    return $fixtureRoot
}

function Assert-CheckerViolation {
    param(
        [string]$FixtureRoot,
        [string]$ExpectedRule
    )

    $previousFixtureRoot = $env:VERIFY_PUBLIC_RELEASE_ROOT
    # The checker writes violations to stderr. Redirect stderr to a temp file
    # rather than 2>&1: on Windows PowerShell 5.1 the inline redirect wraps native
    # stderr lines in ErrorRecords and throws under $ErrorActionPreference='Stop'.
    # Relax the preference for the native call and restore it afterwards.
    $stderrFile = Join-Path $env:TEMP ('checker-stderr-' + [guid]::NewGuid().ToString('N') + '.txt')
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $env:VERIFY_PUBLIC_RELEASE_ROOT = $FixtureRoot
        $ErrorActionPreference = 'Continue'
        $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $checkerPath 2>$stderrFile
        $exitCode = $LASTEXITCODE
    }
    finally {
        $env:VERIFY_PUBLIC_RELEASE_ROOT = $previousFixtureRoot
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $stderrText = if (Test-Path -LiteralPath $stderrFile) { Get-Content -LiteralPath $stderrFile -Raw -Encoding UTF8 } else { '' }
    Remove-Item -LiteralPath $stderrFile -Force -ErrorAction SilentlyContinue
    Assert-Condition ($exitCode -eq 1) "Checker unexpectedly passed fixture $FixtureRoot"
    $outputText = ($output -join [Environment]::NewLine) + [Environment]::NewLine + $stderrText
    Assert-Condition ($outputText -match [regex]::Escape($ExpectedRule + ':')) "Expected $ExpectedRule in checker output for $FixtureRoot. Output: $outputText"
}

# Static I3 regression: simulate VS Code expansion in a clone path containing spaces.
$workspacePath = Join-Path $repoRoot 'periodic-signal-analyzer.code-workspace'
$workspace = Get-Content -LiteralPath $workspacePath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
$flashTask = @($workspace.tasks.tasks | Where-Object { $_.label -eq 'Flash H743 - ST-Link' }) | Select-Object -First 1
Assert-Condition ($null -ne $flashTask) 'Missing Flash H743 - ST-Link task.'
$flashCommand = [string]$flashTask.args[-1]
$workspaceToken = '${workspaceFolder:H743}'
$expectedTclTemplate = 'program {' + $workspaceToken + '/Firmware_H743/build/vscode-release/H743_Signal_Processor_Firmware.elf} verify reset exit'
Assert-Condition ($flashCommand.Contains($expectedTclTemplate)) 'H743 OpenOCD command must brace the ELF path as one Tcl token.'
$drivePrefix = 'Q' + ':'
$syntheticWorkspace = $drivePrefix + '/Synthetic Clone With Spaces/H743_Signal_Processor/H743_Signal_Processor'
$expandedCommand = $flashCommand.Replace($workspaceToken, $syntheticWorkspace)
$programMatch = [regex]::Match($expandedCommand, 'program \{(?<elf>[^}]+)\} verify reset exit')
Assert-Condition ($programMatch.Success) 'Synthetic space-path command did not retain a braced Tcl program argument.'
Assert-Condition ($programMatch.Groups['elf'].Value.Contains('Synthetic Clone With Spaces')) 'Synthetic workspace path was not retained as one Tcl ELF token.'

# The checker fixtures are generated outside the repository so their deliberate
# violations never become inputs to the real repository release gate.
$driveSlashFixture = New-CheckerFixture 'drive-slash'
$allDrivePrefix = 'Z' + ':'
Write-Utf8Text (Join-Path $driveSlashFixture 'paths.txt') ($allDrivePrefix + '/portable-violation')
Assert-CheckerViolation $driveSlashFixture 'privacy-path'

$driveBackslashFixture = New-CheckerFixture 'drive-backslash'
$otherDrivePrefix = 'A' + ':'
Write-Utf8Text (Join-Path $driveBackslashFixture 'paths.txt') ($otherDrivePrefix + '\portable-violation')
Assert-CheckerViolation $driveBackslashFixture 'privacy-path'

$uncFixture = New-CheckerFixture 'unc'
Write-Utf8Text (Join-Path $uncFixture 'paths.txt') ('\' + '\server\share\release')
Assert-CheckerViolation $uncFixture 'privacy-path'

$homeFixture = New-CheckerFixture 'home'
Write-Utf8Text (Join-Path $homeFixture 'paths.txt') ('~' + '/private; ' + '/' + 'home/private; ' + '/' + 'Users/private')
Assert-CheckerViolation $homeFixture 'privacy-path'

$pdfFixture = New-CheckerFixture 'pdf'
$pdfFileName = 'vendor.' + 'pdf'
Set-Content -LiteralPath (Join-Path $pdfFixture $pdfFileName) -Value 'fixture' -Encoding UTF8
Assert-CheckerViolation $pdfFixture 'private-document'

$probeFixture = New-CheckerFixture 'probe-json'
$probeSerial = '0123456789ABCDEF'
$probeCommand = 'openocd -c "adapter serial ' + $probeSerial + '"'
Write-Utf8Text (Join-Path $probeFixture 'probe.json') ((@{ command = $probeCommand } | ConvertTo-Json -Compress))
Assert-CheckerViolation $probeFixture 'fixed-probe'

Write-Output 'VERIFY_PUBLIC_RELEASE_TESTS=PASS'
