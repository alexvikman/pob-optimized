# Packages the Windows build as a zip: PathOfBuilding.exe next to the PoB files.
# usage: packaging/windows/package.ps1 <build-dir> <out-dir>
param([Parameter(Mandatory)] [string]$Build, [Parameter(Mandatory)] [string]$Out)
$ErrorActionPreference = "Stop"

$Repo = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Exe = Join-Path $Build "host/PathOfBuilding.exe"
$Dir = Join-Path $Out "PathOfBuilding-win64"

if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
New-Item -ItemType Directory -Force $Dir | Out-Null
Copy-Item $Exe $Dir
Copy-Item (Join-Path $Repo "LICENSE") $Dir
Copy-Item (Join-Path $Repo "LICENSE-THIRD-PARTY.md") $Dir

# PoB itself, exactly the files its manifest lists (no Windows SimpleGraphic runtime).
# The exe is a GUI-subsystem program, so PowerShell would not wait for it with "&".
$Stage = Start-Process -FilePath $Exe -Wait -PassThru -NoNewWindow `
    -ArgumentList "--stage-bundle", "`"$(Join-Path $Repo 'PathOfBuilding')`"", "`"$(Join-Path $Dir 'PathOfBuilding')`""
if ($Stage.ExitCode -ne 0) { throw "staging PoB failed with exit code $($Stage.ExitCode)" }

$Zip = Join-Path $Out "PathOfBuilding-win64.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
# Files at the zip's root: Explorer's "Extract All" already makes a folder named
# after the zip, so a top-level folder inside would nest twice.
Compress-Archive -Path (Join-Path $Dir "*") -DestinationPath $Zip
Write-Host "Built $Zip"

# Installer (NSIS, preinstalled on GitHub's Windows runners).
$Makensis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $Makensis) {
    $Candidate = Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
    if (Test-Path $Candidate) { $Makensis = Get-Item $Candidate }
}
if ($Makensis) {
    $Version = (Select-String -Path (Join-Path $Repo "CMakeLists.txt") -Pattern '^\s+VERSION ([0-9.]+)$').Matches[0].Groups[1].Value
    $Setup = Join-Path (Resolve-Path $Out) "PathOfBuilding-Setup-win64.exe"
    & $Makensis.Source /V2 "/DVERSION=$Version" "/DSRC=$((Resolve-Path $Dir).Path)" "/DOUT=$Setup" (Join-Path $PSScriptRoot "installer.nsi")
    if ($LASTEXITCODE -ne 0) { throw "makensis failed" }
    Write-Host "Built $Setup"
} else {
    Write-Warning "NSIS not found; skipping the installer"
}
