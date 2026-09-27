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
    Attach a 40 GB qcow2 disk to install onto. It lives in
    %LOCALAPPDATA%\OmniOS\vm, not in the repo: a disk image that changes by
    gigabytes on every install has no business in the working tree.

.PARAMETER BlankDisk
    Start from an empty test disk, throwing away whatever the last install left
    on it. Implies -Disk.

.PARAMETER FromDisk
    Boot the test disk with no ISO attached: the installed system, as it would
    start on a machine with the USB stick pulled out. Implies -Disk.

.EXAMPLE
    .\scripts\run-qemu.ps1
    .\scripts\run-qemu.ps1 -Uefi -Disk
#>
[CmdletBinding()]
param(
    [string]$Iso,
    [switch]$Uefi,
    [switch]$Disk,
    [switch]$FromDisk,
    [switch]$BlankDisk,
    # Display device. stdvga dumps reliably at every stage of boot, which is
    # what makes the VM debuggable; virtio is closer to what a compositor
    # expects. Being able to switch isolates "did the compositor break" from
    # "can the harness see it".
    # Default virtio: stdvga (bochs-drm) exposes no render node, so aquamarine
    # cannot create a renderer at all and the compositor never presents. std is
    # kept only because QEMU's screendump can read its scanout at every stage
    # of boot, which virtio's cannot once something holds the device — useful
    # for capturing the splash, useless for testing the launcher.
    [ValidateSet('std','virtio')]
    [string]$Vga = 'virtio',
    # Capture the guest's serial port to a file on the host. The image writes
    # its session log there, which is the only reliable way to see inside a
    # compositor that runs without producing output.
    [string]$SerialLog,
    # Expose QEMU's monitor on this TCP port, so the boot can be screenshotted
    # with "screendump" without anyone having to watch the window.
    [int]$MonitorPort = 0,

    # Start QEMU and return, printing its process id, instead of blocking until
    # the VM exits. Everything after boot is then driven through the monitor
    # socket, which is the only way an agent can use this at all: a foreground
    # QEMU holds the shell for as long as the machine runs.
    [switch]$Detach,

    # Stop any QEMU already running, and clear the hypervisor first.
    #
    # WHPX is shared with WSL2, and a running WSL backend can leave it in a
    # state where QEMU either dies mid-boot with "failed to get xsave state" or
    # hangs at the ISOLINUX banner with QEMU itself no longer servicing its
    # monitor — a screendump that returns nothing is how the two are told
    # apart, because a merely panicked guest still screenshots.
    #
    # "wsl --shutdown" alone was not enough: Docker Desktop's own processes go
    # on holding the hypervisor after the WSL backend is down, and three boots
    # in a row wedged until they were stopped too. So this stops them, and a
    # build afterwards has to start Docker Desktop again and wait for
    # "docker info" to answer.
    [switch]$Fresh,

    # No window at all. screendump over the monitor still works, so the VM can
    # be driven and photographed exactly as before.
    #
    # Use it whenever the point is to verify something rather than to watch.
    # The SDL window is a live input device wired to the host: it takes the
    # host pointer position the moment it opens, and stray clicks and keys land
    # in the guest as if someone had typed them. That produced a launcher that
    # had opened Files by itself, one sitting on the wrong tab, and one that had
    # picked Sleep out of the power menu — none of which the shell can do on its
    # own, and all of which look like real bugs until the cursor position gives
    # it away.
    [switch]$Headless,

    # Host port forwarded to the guest's ssh. The image already runs sshd; with
    # no forward there was simply no way to reach it, which is why every
    # diagnosis so far went through the console — and a console visit costs the
    # session its keyboard until the next reboot.
    #
    # Log in as omni, password omnios.
    #
    # 12222 rather than the obvious 2222: Windows reserves ranges of ports for
    # Hyper-V, and QEMU refuses the whole netdev with "Could not set up host
    # forwarding rule" when it lands in one — with nothing listening on the port
    # to explain why. Pass -SshPort 0 to turn the forward off.
    [int]$SshPort = 12222,

    # Guest RAM. Half of it becomes the live session's writable overlay, so
    # this is also how much room there is to install anything: at 4G the
    # overlay was 2G, and opening Steam once filled it completely.
    [string]$Memory = '8G',

    # CPU model handed to the accelerator.
    #
    # Not "max". WHPX asks the host for every feature it advertises, and on some
    # hosts reading the xsave state of the resulting vCPU fails outright:
    #
    #   qemu-system-x86_64.exe: failed to get xsave state: No error
    #
    # It dies mid-boot, after ISOLINUX has already drawn, so it reads like a
    # broken image rather than a hypervisor limit. A named model asks for a
    # fixed, older feature set and sidesteps it. Pass -Cpu max,-hypervisor to
    # get the old behaviour back on a host where it works.
    #
    # -tsc-deadline because WHPX does not offer that timer; leaving it in only
    # produces a warning, but the warning is written to stderr and PowerShell
    # turns native stderr into a terminating NativeCommandError when this
    # script is called from a pipeline.
    [string]$Cpu = 'Skylake-Client,-hypervisor,-tsc-deadline',

    # Leave the sound card out, to tell a problem that comes with it from one
    # that does not.
    [switch]$NoSound,

    # Anything else to hand QEMU as it is, for a one-off experiment — for
    # example -ExtraArgs '-d','cpu_reset,guest_errors','-D','C:\tmp\qemu.log'
    # to see why a guest reset.
    [string[]]$ExtraArgs = @()
)

# Before anything else. The VM being replaced still holds the serial log open,
# so deleting that file while it runs fails with "used by another process" —
# which is what happened the first time this ran in the other order.
if ($Fresh) {
    Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host "==> clearing the hypervisor (this stops Docker Desktop as well)"
    Get-Process 'Docker Desktop', 'com.docker.backend', 'com.docker.build' `
        -ErrorAction SilentlyContinue | Stop-Process -Force
    & wsl --shutdown
    # Long enough for the WSL utility VM to actually be gone. Six seconds was
    # not, and the boot that followed hung at ISOLINUX.
    Start-Sleep -Seconds 20
}

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
    # accel=whpx:tcg is QEMU's own fallback chain: it uses WHPX when the
    # Windows Hypervisor Platform is available and drops to software emulation
    # when it is not. Probing from PowerShell is not worth it — QEMU prints
    # -accel help on stderr, and capturing that reliably in 5.1 is a trap.
    # kernel-irqchip=off is mandatory for WHPX: with the in-kernel irqchip the
    # VM starts and then sits paused in SeaBIOS, burning no CPU and never
    # bringing up a display. It is ignored by the tcg fallback.
    # hpet=off is not optional here. With WHPX and kernel-irqchip=off the
    # emulated HPET makes the guest kernel's IO-APIC timer check fail, and it
    # panics during early boot:
    #
    #   Kernel panic - not syncing: IO-APIC + timer doesn't work!
    #
    # It is intermittent, because the check is timing-sensitive — which made it
    # look like a flaky hypervisor rather than a guest panic. Dropping
    # kernel-irqchip=off is not an alternative: without it the VM never leaves
    # SeaBIOS.
    '-machine', 'q35,accel=whpx:tcg,kernel-irqchip=off,hpet=off'
    # Let the guest reach S3, so Sleep can be tested at all. Without it
    # "systemctl suspend" is refused by the firmware and the shell can only be
    # checked as far as "it asked". Wake it again from the monitor with
    # "system_wakeup".
    '-global', 'ICH9-LPC.disable_s3=0'
    # And do not let the chipset's watchdog reboot it on the way back. Waking
    # from S3 resets the ICH9 TCO timer to running, Linux's iTCO_wdt only stops
    # it again if something was using it, and since QEMU made "noreboot" default
    # to off its second timeout resets the machine — so the guest resumed
    # completely, then cold-booted half a second later with nothing in any log.
    # That is the VM's chipset, not the image: on a real board the firmware
    # sets the TCO timer up again on resume, which SeaBIOS does not do.
    '-global', 'ICH9-LPC.noreboot=on'
    '-cpu', $Cpu
    '-m', $Memory
    '-smp', "$cpus"
    # See the -Vga parameter. There is no GPU acceleration in this VM either
    # way; Mesa falls back to llvmpipe regardless.
    '-device', $(if ($Vga -eq 'virtio') { 'virtio-vga' } else { 'VGA,vgamem_mb=64' })
    '-display', $(if ($Headless) { 'none' } else { 'sdl' })
    # A sound card the guest can see. audiodev=none means nothing is played on
    # the host — this is headless most of the time — but PipeWire finds a real
    # sink, so the shell's volume and output controls can be exercised at all.
    # Without it the machine has no audio device and every audio path is
    # untestable rather than merely silent.
    $(if (-not $NoSound) { '-audiodev', 'none,id=omnisnd', '-device', 'intel-hda', '-device', 'hda-output,audiodev=omnisnd' })
    '-device', 'qemu-xhci'
    '-device', 'usb-tablet'
    '-netdev', $(if ($SshPort -gt 0) { "user,id=net0,hostfwd=tcp::${SshPort}-:22" } else { 'user,id=net0' })
    '-device', 'virtio-net-pci,netdev=net0'
)

# The ISO, unless the point is to prove the installed disk starts without it.
if ($FromDisk) {
    $Disk = $true
    $qemuArgs += @('-boot', 'c')
} else {
    $qemuArgs += @('-drive', "file=$Iso,media=cdrom,readonly=on", '-boot', 'd')
}


if ($SerialLog) {
    $dir = Split-Path -Parent $SerialLog
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    if (Test-Path $SerialLog) { Remove-Item $SerialLog -Force }
    $qemuArgs += @('-serial', "file:$SerialLog")
}

if ($MonitorPort -gt 0) {
    $qemuArgs += @('-monitor', "tcp:127.0.0.1:$MonitorPort,server,nowait")
}

if ($Uefi) {
    $ovmf = Join-Path (Split-Path -Parent $qemu) 'share\edk2-x86_64-code.fd'
    if (-not (Test-Path $ovmf)) { throw "OVMF firmware not found next to qemu at $ovmf" }
    $qemuArgs += @('-drive', "if=pflash,format=raw,readonly=on,file=$ovmf")
}

if ($BlankDisk) { $Disk = $true }

if ($Disk) {
    $diskDir = Join-Path $env:LOCALAPPDATA 'OmniOS\vm'
    $diskPath = Join-Path $diskDir 'omnios-test.qcow2'
    if (-not (Test-Path $diskDir)) { New-Item -ItemType Directory -Path $diskDir | Out-Null }

    # Where it used to be. Moved rather than left, so an existing test install
    # is not silently traded for an empty disk.
    $oldPath = Join-Path $out 'omnios-test.qcow2'
    if ((Test-Path $oldPath) -and -not (Test-Path $diskPath)) {
        Write-Host "==> moving the test disk out of the repo to $diskPath"
        Move-Item $oldPath $diskPath
    }

    if ($BlankDisk -and (Test-Path $diskPath)) {
        Write-Host "==> discarding the old test disk"
        Remove-Item $diskPath -Force
    }
    if (-not (Test-Path $diskPath)) {
        Write-Host "==> creating a 40G test disk at $diskPath"
        $qemuImg = Join-Path (Split-Path -Parent $qemu) 'qemu-img.exe'
        & $qemuImg create -f qcow2 $diskPath 40G | Out-Null
    }
    # discard=unmap lets the guest hand space back. A qcow2 only ever grows:
    # every install writes ~8 GB of new blocks, and wiping the disk inside the
    # VM freed nothing on the host, so the file crept from empty to 16.5 GB
    # over a few test installs. With discard passed through, the installer's
    # own wipefs and mkfs mark those clusters free and the next install reuses
    # them instead of growing the file. (QEMU on Windows cannot punch holes in
    # NTFS, so the file does not shrink either; -BlankDisk starts it again.)
    $qemuArgs += @('-drive', "file=$diskPath,if=virtio,format=qcow2,discard=unmap,detect-zeroes=unmap")
}

$qemuArgs += $ExtraArgs

Write-Host "==> booting $(if ($FromDisk) { 'the installed test disk' } else { Split-Path -Leaf $Iso })"

if ($Detach) {
    # Quoted by hand. Windows PowerShell's Start-Process joins an argument
    # array with spaces and quotes nothing, so the UEFI firmware at
    # "C:\Program Files\qemu\share\..." reached QEMU as "file=C:\Program" and
    # -Uefi -Detach died on the spot. Nothing else here had a space in it, which
    # is why it went unnoticed until the installer needed a UEFI boot.
    $argLine = ($qemuArgs | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
    }) -join ' '
    $process = Start-Process -FilePath $qemu -ArgumentList $argLine -PassThru
    Start-Sleep -Seconds 6
    if ($process.HasExited) {
        throw "QEMU exited immediately with code $($process.ExitCode)"
    }
    Write-Host "==> running as pid $($process.Id)"
    if ($SerialLog) {
        Write-Host "==> watch $SerialLog for the boot; the session is up once it contains 'END ====='"
    }
    exit 0
}

& $qemu @qemuArgs
