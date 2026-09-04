<#
.SYNOPSIS
    Boots an OmniOS ISO in QEMU on Windows.

.DESCRIPTION
    The Linux runner uses KVM; this one uses WHPX, which needs the "Windows
    Hypervisor Platform" Windows feature turned on. Without it QEMU falls back
    to pure emulation, which boots but is slow enough to be tedious.

    Install QEMU first:  winget install --id SoftwareFreedomConservancy.QEMU

.PARAMETER Iso
    Path to the ISO. Defaults to the newest one in out/.

.PARAMETER Uefi
    Boot through OVMF instead of BIOS.

.PARAMETER Disk
    Attach a 40 GB qcow2 disk to install onto.

.EXAMPLE
    .\scripts\run-qemu.ps1
    .\scripts\run-qemu.ps1 -Uefi -Disk
#>
[CmdletBinding()]
param(
    [string]$Iso,
    [switch]$Uefi,
    [switch]$Disk
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $repo 'out'

$qemu = (Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue).Source
if (-not $qemu) {
    $fallback = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
    if (Test-Path $fallback) {
        $qemu = $fallback
    } else {
        throw "qemu-system-x86_64 not found. Install it with: winget install --id SoftwareFreedomConservancy.QEMU"
    }
}

if (-not $Iso) {
    $newest = Get-ChildItem -Path $out -Filter *.iso -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newest) { throw "No ISO in $out. Build one first - see docs/BUILDING.md." }
    $Iso = $newest.FullName
}
if (-not (Test-Path $Iso)) { throw "$Iso does not exist" }

$cpus = [Environment]::ProcessorCount
if ($cpus -gt 8) { $cpus = 8 }

$qemuArgs = @(
    '-name', 'OmniOS'
    '-machine', 'q35'
    '-m', '4G'
    '-smp', "$cpus"
    '-device', 'virtio-vga'
    '-display', 'sdl'
    '-device', 'qemu-xhci'
    '-device', 'usb-tablet'
    '-netdev', 'user,id=net0'
    '-device', 'virtio-net-pci,netdev=net0'
    '-drive', "file=$Iso,media=cdrom,readonly=on"
    '-boot', 'd'
)

# WHPX is the Windows equivalent of KVM. Probe it rather than assume it.
$whpx = Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -ErrorAction SilentlyContinue
if ($whpx -and $whpx.State -eq 'Enabled') {
    $qemuArgs += @('-accel', 'whpx,kernel-irqchip=off', '-cpu', 'max,-hypervisor')
} else {
    Write-Warning "Windows Hypervisor Platform is off - falling back to software emulation, which is slow."
    Write-Warning "Enable it with: Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform"
    $qemuArgs += @('-accel', 'tcg', '-cpu', 'max')
}

if ($Uefi) {
    $ovmf = Join-Path (Split-Path -Parent $qemu) 'share\edk2-x86_64-code.fd'
    if (-not (Test-Path $ovmf)) { throw "OVMF firmware not found next to qemu at $ovmf" }
    $qemuArgs += @('-drive', "if=pflash,format=raw,readonly=on,file=$ovmf")
}

if ($Disk) {
    $diskPath = Join-Path $out 'omnios-test.qcow2'
    if (-not (Test-Path $diskPath)) {
        Write-Host "==> creating a 40G test disk at $diskPath"
        $qemuImg = Join-Path (Split-Path -Parent $qemu) 'qemu-img.exe'
        & $qemuImg create -f qcow2 $diskPath 40G | Out-Null
    }
    $qemuArgs += @('-drive', "file=$diskPath,if=virtio,format=qcow2")
}

Write-Host "==> booting $(Split-Path -Leaf $Iso)"
& $qemu @qemuArgs
