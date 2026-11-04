# SPDX-License-Identifier: GPL-2.0-or-later
# Install, check and uninstall the NSIS and MSI installers on a disposable
# Windows machine. Each installed copy must report the release version, load
# the required modules and contain every file of the portable package.
param(
    [Parameter(Mandatory)] [string] $Downloads,
    [Parameter(Mandatory)] [string] $Tag,
    # The extracted hakone-win64 folder of the same release
    [Parameter(Mandatory)] [string] $Reference
)
$ErrorActionPreference = 'Stop'
$installDir = Join-Path $env:ProgramFiles 'Hakone'
$uninstallKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Hakone Media Player'
$version = Join-Path $PSScriptRoot 'version.py'

# Files an installed copy must have: the portable package without its
# portable profile, the SDK and link libraries.
$reference = (Resolve-Path $Reference).Path
$expected = Get-ChildItem $reference -Recurse -File |
    ForEach-Object { $_.FullName.Substring($reference.Length + 1) } |
    Where-Object { $_ -notmatch '^(portable|sdk)\\' -and $_ -notmatch '\.(pdb|dll\.a)$' }
if (-not ($expected -contains 'vlc-addon-worker.exe')) { throw 'The reference package has no add-on worker' }

function Invoke-Checked([string] $File, [string[]] $Arguments, [string] $What) {
    $process = Start-Process $File -ArgumentList $Arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "$What exited $($process.ExitCode)" }
}

function Assert-Installed([string] $Kind) {
    $hakone = Join-Path $installDir 'hakone.exe'
    if (-not (Test-Path $hakone)) { throw "$Kind did not install $hakone" }
    $missing = $expected | Where-Object { -not (Test-Path -LiteralPath (Join-Path $installDir $_)) }
    if ($missing) { throw "$Kind installed copy is missing: $($missing -join ', ')" }
    if (Test-Path (Join-Path $installDir 'portable')) { throw "$Kind installed a portable profile" }
    python $version $Tag --executable $hakone --require-module qt --require-module avcodec --require-module medialibrary
    if ($LASTEXITCODE -ne 0) { throw "$Kind installed copy failed its version or module check" }
    Write-Host "$Kind installed $($expected.Count) package files and runs as $Tag"
}

function Assert-Removed([string] $Kind) {
    foreach ($name in 'hakone.exe', 'libvlccore.dll', 'vlc-addon-worker.exe', 'plugins') {
        if (Test-Path (Join-Path $installDir $name)) { throw "$Kind uninstall left $name behind" }
    }
}

# NSIS. Without _? the uninstaller copies itself away and returns at once.
Invoke-Checked (Join-Path $Downloads "hakone-$Tag-win64.exe") @('/S') 'The NSIS installer'
Assert-Installed 'NSIS'
if (-not (Test-Path $uninstallKey)) { throw 'The NSIS installer did not register its uninstaller' }
Invoke-Checked (Join-Path $installDir 'uninstall.exe') @('/S', "_?=$installDir") 'The NSIS uninstaller'
Assert-Removed 'NSIS'
if (Test-Path $uninstallKey) { throw 'The NSIS uninstaller left its uninstall entry' }
Remove-Item $installDir -Recurse -Force -ErrorAction SilentlyContinue

# MSI
$msi = Join-Path $Downloads "hakone-$Tag-win64.msi"
$logs = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
Invoke-Checked 'msiexec.exe' @('/i', "`"$msi`"", '/qn', '/norestart', '/l*v', "`"$logs\hakone-msi-install.log`"") 'The MSI install'
Assert-Installed 'MSI'
Invoke-Checked 'msiexec.exe' @('/x', "`"$msi`"", '/qn', '/norestart', '/l*v', "`"$logs\hakone-msi-uninstall.log`"") 'The MSI uninstall'
Assert-Removed 'MSI'
Write-Host 'Both installers installed, ran and uninstalled cleanly'
