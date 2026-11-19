# Development only: replace the installed Windows IME with dist\Yuju-Setup-Windows-x64.exe
# without a reboot. Run in an elevated PowerShell (Run as administrator):
#   powershell -ExecutionPolicy Bypass -File Scripts\windows_dev_reinstall.ps1
# Loaded DLLs cannot be overwritten or deleted but can be renamed, so the current files move
# to "old\" first. Programs already running keep their copy until they are restarted; then run
# Scripts\windows_cleanup_old.ps1 (also elevated) to remove the copies.
$ErrorActionPreference = 'Stop'
$dir = 'C:\Program Files\YujuInput'
$setup = Join-Path $PSScriptRoot '..\dist\Yuju-Setup-Windows-x64.exe'
if (-not (Test-Path $setup)) { throw "Build first: $setup not found" }
$old = Join-Path $dir 'old'
New-Item -ItemType Directory -Force $old | Out-Null
Get-ChildItem $old -File | ForEach-Object { try { Remove-Item $_.FullName -Force } catch {} }  # copies no longer in use
$stamp = Get-Date -Format 'yyyyMMddHHmmss'
$moved = @()
foreach ($name in 'YujuIME.dll', 'rime.dll', 'YujuSettings.exe') {
  $path = Join-Path $dir $name
  if (Test-Path $path) {
    Move-Item $path (Join-Path $old "$name.$stamp")
    $moved += $name
  }
}
$p = Start-Process $setup -ArgumentList '/S' -Wait -PassThru
"moved: $($moved -join ', '); installer exit code: $($p.ExitCode)"
if ($p.ExitCode -ne 0) { exit $p.ExitCode }
