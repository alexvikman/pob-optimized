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
Compress-Archive -Path $Dir -DestinationPath $Zip
Write-Host "Built $Zip"
