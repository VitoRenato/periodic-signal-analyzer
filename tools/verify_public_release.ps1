$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# Test harnesses may point the same checker at an isolated synthetic release tree.
# Normal invocations leave this unset and retain the repository-root default above.
if (-not [string]::IsNullOrWhiteSpace($env:VERIFY_PUBLIC_RELEASE_ROOT)) {
    $repoRoot = (Resolve-Path -LiteralPath $env:VERIFY_PUBLIC_RELEASE_ROOT -ErrorAction Stop).Path
}

$requiredFiles = @(
    'README.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    '.gitignore',
    'periodic-signal-analyzer.code-workspace',
    'components/ad9226_f407/README.md',
    'components/ad9226_f407/LICENSE'
)
$skipPattern = '\\(build[^\\]*|work|\.git|\.planning|Drivers|Middlewares|CMSIS|docs\\superpowers)\\'
$skipNames = @('findings.md', 'progress.md', 'task_plan.md')
$errors = [System.Collections.Generic.List[string]]::new()

$forbiddenText = @(
    'wxid_', 'xwechat_files', 'file:///', '/home/',
    '哈尔滨工程大学', '田力睿', '郭翰璋', '梅俊杰'
)
$vendorExtensions = @('.SchDoc', '.PcbDoc', '.PrjPcb', '.rar', '.7z')
$absolutePathPattern = '(?<![A-Za-z0-9_:])(?:[A-Za-z]:[\\/]|\\\\[^\\/\s]+[\\/][^\\/\s]+|//[^\\/\s]+[\\/][^\\/\s]+|~[\\/]|/(?:home|Users)/)'
$probeDefaultPattern = 'STLINK_SERIAL_(?:F4|H7)\s*\?=\s*[0-9A-Fa-f]{16,}'
$probeEmbeddedValuePattern = '(?i)(?:adapter\s+serial|hla_serial|(?:STLINK|JLINK|PROBE|ADAPTER)(?:_SERIAL(?:_[A-Za-z0-9_]+)?|_SERIAL_NUMBER|_SN)?|serialNumber)\s*["'']?\s*(?:=|:|\s)\s*["'']?[0-9A-F][0-9A-F:_-]{5,}'
$privateDocumentExtensions = @('.docx', '.pdf')
$probeAssignmentPattern = '(?i)^\s*(?:STLINK(?:_SERIAL(?:_[A-Za-z0-9_]+)?)?|JLINK(?:_SERIAL(?:_NUMBER)?|_SN)?|PROBE(?:_SERIAL|_SN)?|ADAPTER_SERIAL)\s*(?:\?|\+|:)?=\s*(?![$(])"?[0-9A-F][0-9A-F:_-]{5,}"?\s*$'

$textExtensions = @(
    '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.s', '.S', '.ld', '.md', '.txt',
    '.json', '.code-workspace', '.yml', '.yaml', '.cmake', '.ps1', '.psd1', '.py',
    '.sh', '.bat', '.cmd', '.mk', '.make', '.ioc', '.project', '.cproject', '.cfg',
    '.ini', '.properties', '.xml', '.csv'
)
$textNames = @('CMakeLists.txt', 'Makefile', 'makefile', 'GNUmakefile', 'LICENSE', 'NOTICE')

function Add-Violation {
    param([string]$Rule, [string]$Path, [int]$Line = 0)

    if ($Line -gt 0) {
        $errors.Add(('{0}: {1}:{2}' -f $Rule, $Path, $Line))
    }
    else {
        $errors.Add(('{0}: {1}' -f $Rule, $Path))
    }
}

function Get-RelativePath {
    param([string]$FullPath)

    return $FullPath.Substring($repoRoot.Length).TrimStart('\', '/') -replace '\\', '/'
}

foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $relativePath) -PathType Leaf)) {
        Add-Violation 'required-file' $relativePath
    }
}

$workspaceRelativePath = 'periodic-signal-analyzer.code-workspace'
$workspacePath = Join-Path $repoRoot $workspaceRelativePath
if (Test-Path -LiteralPath $workspacePath -PathType Leaf) {
    try {
        $workspace = Get-Content -LiteralPath $workspacePath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $requiredWorkspaceTasks = @(
            'Build F407',
            'Flash F407 - ST-Link',
            'Flash F407 - CMSIS-DAP',
            'Build H743 Firmware',
            'Run H743 Host Tests',
            'Flash H743 - ST-Link'
        )
        $workspaceTaskLabels = @($workspace.tasks.tasks | ForEach-Object { $_.label })
        foreach ($label in $requiredWorkspaceTasks) {
            if ($label -notin $workspaceTaskLabels) {
                Add-Violation ('workspace-task-label:{0}' -f $label) $workspaceRelativePath
            }
        }

        $expectedH743FirmwareElf = 'H743_Signal_Processor_Firmware.elf'
        $legacyH743FirmwareElf = 'H743_Signal_Processor.elf'
        $h743FlashTask = @($workspace.tasks.tasks | Where-Object { $_.label -eq 'Flash H743 - ST-Link' }) | Select-Object -First 1
        $h743FlashTaskText = if ($null -ne $h743FlashTask) {
            $h743FlashTask | ConvertTo-Json -Depth 10 -Compress
        }
        else {
            ''
        }
        if (-not $h743FlashTaskText.Contains($expectedH743FirmwareElf) -or
            $h743FlashTaskText.Contains($legacyH743FirmwareElf)) {
            Add-Violation 'workspace-h743-firmware-elf' $workspaceRelativePath
        }
    }
    catch {
        Add-Violation 'json-parse' $workspaceRelativePath
    }
}

$readmeRelativePath = 'README.md'
$readmePath = Join-Path $repoRoot $readmeRelativePath
if (Test-Path -LiteralPath $readmePath -PathType Leaf) {
    $readmeText = Get-Content -LiteralPath $readmePath -Raw -Encoding UTF8
    if (-not $readmeText.Contains('H743_Signal_Processor_Firmware.elf') -or
        $readmeText.Contains('H743_Signal_Processor.elf')) {
        Add-Violation 'readme-h743-firmware-elf' $readmeRelativePath
    }
}

$f407TasksRelativePath = 'AD9226_driver3/AD9226_driver/.vscode/tasks.json'
$f407TasksPath = Join-Path $repoRoot ($f407TasksRelativePath -replace '/', '\')
if (Test-Path -LiteralPath $f407TasksPath -PathType Leaf) {
    try {
        $f407Tasks = Get-Content -LiteralPath $f407TasksPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $requiredF407Tasks = @(
            'Build F407',
            'Clean F407',
            'Flash F407 - ST-Link',
            'Flash F407 - CMSIS-DAP'
        )
        $f407TaskLabels = @($f407Tasks.tasks | ForEach-Object { $_.label })
        foreach ($label in $requiredF407Tasks) {
            if ($label -notin $f407TaskLabels) {
                Add-Violation ('f407-task-label:{0}' -f $label) $f407TasksRelativePath
            }
        }
    }
    catch {
        Add-Violation 'json-parse' $f407TasksRelativePath
    }
}

$f407LaunchRelativePath = 'AD9226_driver3/AD9226_driver/.vscode/launch.json'
$f407LaunchPath = Join-Path $repoRoot ($f407LaunchRelativePath -replace '/', '\')
if (Test-Path -LiteralPath $f407LaunchPath -PathType Leaf) {
    try {
        $f407Launch = Get-Content -LiteralPath $f407LaunchPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $requiredF407Launches = @('Debug F407 - ST-Link', 'Debug F407 - CMSIS-DAP')
        $f407LaunchNames = @($f407Launch.configurations | ForEach-Object { $_.name })
        foreach ($name in $requiredF407Launches) {
            if ($name -notin $f407LaunchNames) {
                Add-Violation ('f407-launch-name:{0}' -f $name) $f407LaunchRelativePath
            }
        }

        $stlinkLaunch = @($f407Launch.configurations | Where-Object { $_.name -eq 'Debug F407 - ST-Link' }) | Select-Object -First 1
        if ($null -ne $stlinkLaunch) {
            $serialSelectionText = @(
                [string]$stlinkLaunch.serialNumber
                @($stlinkLaunch.serverArgs)
                @($stlinkLaunch.openOCDPreConfigLaunchCommands)
                @($stlinkLaunch.openOCDLaunchCommands)
            ) -join ' '
            $inputMatch = [regex]::Match($serialSelectionText, '\$\{input:(?<id>[^}]+)\}')
            $hasSerialCommand = $serialSelectionText -match '(?i)(?:adapter\s+serial|hla_serial)' -or
                (($stlinkLaunch.PSObject.Properties.Name -contains 'serialNumber') -and $stlinkLaunch.serialNumber)
            if (-not $hasSerialCommand -or -not $inputMatch.Success) {
                Add-Violation 'f407-launch-probe-select' $f407LaunchRelativePath
            }
            else {
                $inputId = $inputMatch.Groups['id'].Value
                $inputDefinition = @($f407Launch.inputs | Where-Object { $_.id -eq $inputId }) | Select-Object -First 1
                if ($null -eq $inputDefinition -or
                    $inputDefinition.type -ne 'promptString' -or
                    [string]$inputDefinition.description -notmatch '多个\s*ST-Link' -or
                    [string]$inputDefinition.description -notmatch '必须') {
                    Add-Violation 'f407-launch-probe-input' $f407LaunchRelativePath
                }
            }
        }

        $f407ProjectPath = Split-Path (Split-Path $f407LaunchPath -Parent) -Parent
        foreach ($configuration in @($f407Launch.configurations)) {
            $svdReference = [string]$configuration.svdFile
            if (-not [string]::IsNullOrWhiteSpace($svdReference)) {
                $portableSvdReference = $svdReference -replace '^\$\{workspaceFolder\}[\\/]*', ''
                if ($portableSvdReference -match '\$\{' -or [System.IO.Path]::IsPathRooted($portableSvdReference)) {
                    Add-Violation ('f407-launch-svd:{0}' -f $configuration.name) $f407LaunchRelativePath
                    continue
                }
                $svdPath = Join-Path $f407ProjectPath ($portableSvdReference -replace '/', '\')
                if (-not (Test-Path -LiteralPath $svdPath -PathType Leaf)) {
                    Add-Violation ('f407-launch-svd:{0}' -f $configuration.name) $f407LaunchRelativePath
                }
            }
        }
    }
    catch {
        Add-Violation 'json-parse' $f407LaunchRelativePath
    }
}

$files = Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Force | Where-Object {
    $_.FullName -ne $PSCommandPath -and
    $_.Name -notin $skipNames -and
    $_.FullName -notmatch $skipPattern -and
    $_.FullName -notmatch '\\.superpowers\\'
}

foreach ($file in $files) {
    $relativePath = Get-RelativePath $file.FullName
    foreach ($forbidden in $forbiddenText) {
        if ($relativePath.IndexOf($forbidden, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Add-Violation 'privacy-path' $relativePath
        }
    }
    if ($relativePath -match $absolutePathPattern) {
        Add-Violation 'privacy-path' $relativePath
    }
    if ($file.Extension -in $privateDocumentExtensions) {
        Add-Violation 'private-document' $relativePath
    }
    if ($file.Extension -in $vendorExtensions) {
        Add-Violation 'vendor-artifact' $relativePath
    }

    if ($file.Extension -notin $textExtensions -and $file.Name -notin $textNames) {
        continue
    }

    $lines = @(Get-Content -LiteralPath $file.FullName -Encoding UTF8)
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
        $line = [string]$lines[$lineIndex]
        $lineNumber = $lineIndex + 1
        foreach ($forbidden in $forbiddenText) {
            if ($line.IndexOf($forbidden, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                Add-Violation 'privacy-path' $relativePath $lineNumber
            }
        }
        if ($line -match $absolutePathPattern) {
            Add-Violation 'privacy-path' $relativePath $lineNumber
        }
        if ($line -match $probeDefaultPattern -or $line -match $probeAssignmentPattern -or $line -match $probeEmbeddedValuePattern) {
            Add-Violation 'fixed-probe' $relativePath $lineNumber
        }
    }

    if ($file.Extension -in @('.json', '.code-workspace')) {
        try {
            Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop | Out-Null
        }
        catch {
            Add-Violation 'json-parse' $relativePath
        }
    }

    if ($file.Name -eq 'README.md') {
        for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
            foreach ($match in [regex]::Matches([string]$lines[$lineIndex], '(?<!!)\[[^\]]*\]\((?<target>[^)\s]+)')) {
                $target = $match.Groups['target'].Value
                if ($target -match '^(?:https?:|mailto:|#)' -or $target.StartsWith('/')) {
                    continue
                }
                $target = ($target -split '[#?]', 2)[0]
                if (-not $target) {
                    continue
                }
                $targetPath = Join-Path $file.DirectoryName ($target -replace '/', '\\')
                $normalizedTargetPath = [System.IO.Path]::GetFullPath($targetPath)
                $repoRootPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
                if ($normalizedTargetPath -ne $repoRoot -and -not $normalizedTargetPath.StartsWith($repoRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                    Add-Violation 'readme-link-escape' $relativePath ($lineIndex + 1)
                }
                elseif (-not (Test-Path -LiteralPath $normalizedTargetPath)) {
                    Add-Violation 'readme-link' $relativePath ($lineIndex + 1)
                }
            }
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { [Console]::Error.WriteLine($_) }
    exit 1
}
Write-Output 'PUBLIC_RELEASE_CHECK=PASS'
