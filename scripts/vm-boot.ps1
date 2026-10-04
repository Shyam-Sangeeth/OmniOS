# Boots the VM (retrying past the WHPX IO-APIC panic), starts the monitor
# relay (vm-relay.ps1), and returns once the session log has come out on
# serial. Then drive it with vm-job.ps1.
#
#   powershell.exe -File scripts\vm-boot.ps1     live ISO, Try OmniOS
#   ... -Game | -Install                         another boot menu entry
#   ... -Disk                                    live ISO + the test disk
#   ... -FromDisk                                the installed test disk
#
# Run it through powershell.exe from Bash, not the PowerShell tool: inside
# that tool Get-CimInstance cannot see other powershell.exe processes, so the
# relay cleanup below silently finds nothing and old relays pile up, each
# grabbing jobs meant for the new VM.
param([switch]$FromDisk, [switch]$Disk, [switch]$Game, [switch]$Install, [switch]$Fresh, [int]$Tries = 6, [string]$Memory = "8G")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$serial = "$env:TEMP\omnios-serial.log"
$dir = "$env:TEMP\omnimon"
foreach ($try in 1..$Tries) {
    Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like '*vm-relay.ps1*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    Start-Sleep 2
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    & "$here\run-qemu.ps1" -Detach -Fresh:($Fresh -and $try -eq 1) -Disk:$Disk -FromDisk:$FromDisk -SerialLog $serial -MonitorPort 24444 -Memory $Memory | Out-Null
    Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$here\vm-relay.ps1"
    foreach ($i in 1..30) { if (Test-Path "$dir\relay.status") { break }; Start-Sleep 1 }
    if (-not $FromDisk) {
        if ($Install) { & "$here\vm-job.ps1" -Name menu '<WAIT:3>' '<KEY:down>' '<ENTER>' | Out-Null }
        elseif ($Game) { & "$here\vm-job.ps1" -Name menu '<WAIT:3>' '<KEY:down>' '<KEY:down>' '<ENTER>' | Out-Null }
        else { & "$here\vm-job.ps1" -Name menu '<WAIT:3>' '<ENTER>' | Out-Null }
    }
    Start-Sleep 25
    # A frozen QEMU (seen 2026-09-27: window not responding, CPU stuck near
    # 7 s, screendumps never written; cause not found) looks like a slow boot
    # forever. Two samples tell them apart.
    $q = Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Select-Object -First 1
    $cpu = $q.CPU; Start-Sleep 5; $q.Refresh()
    if (-not $q.Responding -and ($q.CPU - $cpu) -lt 0.5) { Write-Host "try $try : QEMU frozen at $([int]$q.CPU) CPU-s, retrying"; continue }
    # The IO-APIC panic leaves a 720x400 text screen. It comes the moment the
    # kernel starts, but loading the kernel from the ISO can take well past
    # 25 s here, so one look was often too early and the panic went unseen
    # (then five minutes waiting for a session that would never come). Looked
    # at every few seconds for a minute and a half instead.
    $panicked = $false
    $shots = 0
    foreach ($look in 1..18) {
        & "$here\vm-job.ps1" -Name probe '<WAIT:0>' | Out-Null
        if (Test-Path "$dir\job-probe.png") {
            $shots++
            $bytes = [IO.File]::ReadAllBytes("$dir\job-probe.png")
            # PNG IHDR width at offset 16.
            $width = ($bytes[16] -shl 24) -bor ($bytes[17] -shl 16) -bor ($bytes[18] -shl 8) -bor $bytes[19]
            if ($width -eq 720) { $panicked = $true; break }
        }
        Start-Sleep 3
    }
    if ($shots -eq 0) { Write-Host "try $try : no screenshot, retrying"; continue }
    if ($panicked) { Write-Host "try $try : panic (text mode), retrying"; continue }
    $deadline = (Get-Date).AddSeconds(300)
    # An install with a sign-in screen never starts the session by itself, so
    # there is no marker to wait for: give it time and look instead.
    if ($FromDisk) { Start-Sleep 60; Write-Host "disk booted on try $try (no session marker to wait for)"; exit 0 }
    $up = $false
    while ((Get-Date) -lt $deadline) {
        if ((Get-Content $serial -Raw -ErrorAction SilentlyContinue) -match 'END =====') { $up = $true; break }
        Start-Sleep 5
    }
    if (-not $up) { Write-Host "try $try : no session after 300 s, retrying"; continue }
    Start-Sleep 15
    Write-Host "up on try $try"
    exit 0
}
throw "no clean boot in $Tries tries"
