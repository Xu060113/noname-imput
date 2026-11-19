# Development only: remove the copies Scripts\windows_dev_reinstall.ps1 moved into
# "C:\Program Files\YujuInput\old". Run in an elevated PowerShell. Files still loaded by a
# running program are scheduled for deletion at the next restart.
$ErrorActionPreference = 'Stop'
$old = 'C:\Program Files\YujuInput\old'
Add-Type -Namespace Win32 -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern bool MoveFileEx(string existing, string replacement, int flags);
'@
# PowerShell turns $null into "" for string arguments; MoveFileEx needs a real null as the
# new name to mean "delete at restart" (flag 4).
$none = [NullString]::Value
$deleted = @(); $scheduled = @(); $failed = @()
if (Test-Path $old) {
  foreach ($f in Get-ChildItem $old -File) {
    try { Remove-Item $f.FullName -Force; $deleted += $f.Name }
    catch {
      if ([Win32.Native]::MoveFileEx($f.FullName, $none, 4)) { $scheduled += $f.Name }
      else { $failed += "$($f.Name) (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
    }
  }
  if ($scheduled.Count -eq 0 -and $failed.Count -eq 0) { Remove-Item $old -Force }
  elseif ($failed.Count -eq 0 -and -not [Win32.Native]::MoveFileEx($old, $none, 4)) { $failed += 'old folder' }
}
"deleted: $($deleted -join ', ')"
"scheduled for restart: $($scheduled -join ', ')"
"failed: $($failed -join ', ')"
