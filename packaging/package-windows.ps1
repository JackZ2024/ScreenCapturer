param(
    [ValidateSet('x64', 'arm64', 'x86')]
    [string] $Architecture = 'x64',
    [string] $BuildOutput = (Join-Path $PSScriptRoot '..\app'),
    [string] $OutputDirectory = (Join-Path $PSScriptRoot '..\build\release'),
    [string] $WinDeployQt = 'windeployqt'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$output = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$archive = Join-Path $output "ScreenCapturer-windows-$Architecture.zip"
if (Test-Path -LiteralPath $archive) {
    throw "Release archive already exists: $archive"
}

$stage = (New-Item -ItemType Directory -Path (Join-Path $output ([Guid]::NewGuid().ToString('N')))).FullName
$temporaryArchive = "$stage.zip"
try {
    foreach ($file in @('ScreenCapturer.exe', 'ScreenCapturerUpdater.exe', 'ng-log.dll')) {
        Copy-Item -LiteralPath (Join-Path $BuildOutput $file) -Destination $stage
    }
    & $WinDeployQt --release --no-compiler-runtime (Join-Path $stage 'ScreenCapturer.exe') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

    $manifestName = 'ScreenCapturer-files.txt'
    $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        if ($relative -cnotmatch '^[a-zA-Z0-9_-][a-zA-Z0-9_.-]*(/[a-zA-Z0-9_-][a-zA-Z0-9_.-]*)*$') {
            throw "Unsupported file name in the Windows package: $relative"
        }
        $relative
    })
    $files += $manifestName
    [IO.File]::WriteAllLines((Join-Path $stage $manifestName), [string[]]($files | Sort-Object), [Text.Encoding]::ASCII)
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $temporaryArchive)
    [IO.File]::Move($temporaryArchive, $archive)
    Get-Item -LiteralPath $archive
} finally {
    if ([IO.Path]::GetDirectoryName($stage) -ne $output) {
        throw "Unexpected staging directory: $stage"
    }
    Remove-Item -LiteralPath $stage -Recurse -Force
    if (Test-Path -LiteralPath $temporaryArchive) {
        Remove-Item -LiteralPath $temporaryArchive -Force
    }
}
