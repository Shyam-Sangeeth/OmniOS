# Building and booting the OmniOS ISO

The profile in [iso/](../iso/) is complete and syntax-checked. Turning it into
a bootable `.iso` requires an Arch Linux userspace, because `mkarchiso`
pacstraps a real system and needs loop devices and root. **It cannot run on
Windows**, and neither can the ISO build inside plain WSL without an Arch
distribution.

Pick whichever route matches what you already have.

## Route A — Docker Desktop (verified: this is how the shipped ISO was built)

Docker Desktop on Windows runs its engine inside a Linux VM, so it needs
**WSL2 or Hyper-V** — enabling either is a Windows optional feature and takes a
reboot. Docker alone is not enough on a machine that has neither. Check first:

```powershell
wsl --status            # "Default Version: 2" means WSL2 is live
Get-Service vmcompute   # Running means Hyper-V is live
```

Nothing works until those features are enabled *and the machine has rebooted* —
a pending servicing operation reports as "not installed" and Docker's engine
returns 500 with no VM behind it.

The Arch image is ~800 MB and the build pulls a few GB of packages.

```powershell
winget install --id Docker.DockerDesktop
```

Then, from the repo root:

```powershell
docker run --rm --privileged `
  -v "${PWD}:/repo" -w /repo `
  archlinux:latest `
  bash -c "pacman -Syu --noconfirm archiso cmake ninja gcc git && ./scripts/build-iso.sh"
```

`--privileged` is required: `mkarchiso` mounts loop devices, which a default
container cannot do.

In practice use the wrapper rather than that raw command. It mounts a Docker
volume for the work tree, which is not optional: `mkarchiso` builds a real root
filesystem, and a Windows bind mount cannot carry Unix ownership, setuid bits
or device nodes — pacstrap dies partway through if you try. It also refreshes
`archlinux-keyring` first, since a stale keyring in the base image makes every
package fail signature verification for no obvious reason.

```powershell
.\scripts\build-iso-docker.ps1 -KeepCache
```

`-KeepCache` persists pacman's package cache in a named volume, so a second
build does not re-download several GB.

The ISO lands in `out/`. Boot it from Windows:

```powershell
winget install --id SoftwareFreedomConservancy.QEMU
.\scripts\run-qemu.ps1
```

## Route B — WSL2 with Arch

Needs a reboot to install WSL, and gives you a persistent Linux environment
that is useful beyond this one build.

```powershell
wsl --install --no-distribution   # reboot when it asks
wsl --install archlinux
```

Then inside the Arch shell:

```bash
sudo pacman -Syu --noconfirm archiso cmake ninja gcc
cd /mnt/c/Users/shyam/OneDrive/Documents/Projects/OmniOS
sudo ./scripts/build-iso.sh
```

WSL2 runs a real kernel with loop device support, so `mkarchiso` works. QEMU
can then run either inside WSL (`./scripts/run-qemu.sh`, using WSLg for the
window) or on Windows against the same file (`.\scripts\run-qemu.ps1`).

## Route C — an actual Arch machine

```bash
sudo pacman -S archiso qemu-full edk2-ovmf
sudo ./scripts/build-iso.sh
./scripts/run-qemu.sh --uefi
```

This is the only route where the VM gets KVM and OVMF without extra setup, so
it is much faster than the other two.

---

## What the build script does

`scripts/build-iso.sh` does three things before calling `mkarchiso`, all of
them consequences of the profile being edited on Windows:

1. **Compiles `omnictl`** and installs it into `iso/airootfs/usr/local/bin/`,
   so the image ships the actual compatibility engine rather than a stub.
2. **Creates the systemd `.wants` symlinks** listed in `iso/services.enable`.
   Git on Windows checks symlinks out as plain text files, so they cannot be
   committed; the list is text and the links are made on the Linux host.
3. **Stages `packages.aur.txt`** at `/usr/share/omnios/`, where
   `omni-first-boot` reads it.

## What a QEMU boot does and does not prove

A VM boot validates the half of the system that is about *being an OS*:

- the `linux-zen` kernel boots and the archiso initramfs finds the squashfs
- systemd-boot (UEFI) and syslinux (BIOS) entries are correct
- the sysctl drop-in, zram device and cpupower governor apply
- autologin reaches tty1, Hyprland starts, and `omni-launcher` runs
- `omnictl scan` finds games on an attached disk and routes them correctly

It cannot validate the half that is about *running games*. There is no GPU
passthrough, so Vulkan falls back to software rendering (`llvmpipe`): Proton
and every emulator will either crawl or refuse to start. That is expected and
is exactly why OmniOS.md Phase 13.4 is "test on real hardware" — the VM proves
the plumbing, hardware proves the performance.

## The boot handoff, and why it is fragile

Boot passes the display through three owners: plymouth (from the initramfs),
then Hyprland, then the launcher inside it. Two rules make that work, and both
were learned by breaking them:

1. **plymouth must be gone, not merely asked to go.** `plymouth quit` returns
   when the request is sent. `plymouth --ping` then reports the daemon gone
   while it is still alive holding DRM master. Poll for the `plymouthd`
   *process*. Until that was right the compositor could not allocate a single
   framebuffer — `DRM_IOCTL_MODE_CREATE_DUMB failed: Permission denied`, 482
   times — and every display mode was refused as a downstream symptom.
2. **Do not pass `--retain-splash`.** Retaining the splash means plymouth keeps
   the display instead of handing it back, which is the same failure.

The symptom of getting either wrong is a compositor that starts, stays running,
logs nothing alarming, and never draws. `ps` shows Hyprland and Xwayland alive;
the screen shows the console underneath. Nothing fails, so nothing points at
the cause. Read `/tmp/omnios-session.log` over the serial port
(`run-qemu.ps1 -SerialLog`) rather than inferring from screenshots.

A VM also needs a display device with a render node. `-Vga std` (bochs-drm)
exposes `card0` but no `renderD128`, so aquamarine reports "Can't create
renderer, no matching devices found" and nothing renders at all; it is useful
only for capturing the splash, because QEMU's `screendump` can read its scanout
at every stage of boot where virtio's cannot.

## Running the VM on Windows

`run-qemu.ps1` passes `kernel-irqchip=off,hpet=off`. Both are required and they
fix different things:

- **`kernel-irqchip=off`** — without it the VM never leaves SeaBIOS under WHPX:
  it starts, burns under two CPU-seconds and sits paused.
- **`hpet=off`** — with the emulated HPET present, the guest kernel's IO-APIC
  timer check fails and it panics during early boot:

      Kernel panic - not syncing: IO-APIC + timer doesn't work!

  The check is timing-sensitive, so the panic is intermittent. That makes it
  look like a flaky hypervisor rather than a guest that died on the first
  screenful, especially since the serial log then holds only the ISOLINUX
  banner and `screendump` appears to do nothing.

If a VM looks stuck, screendump it before theorising: `(Get-Process
qemu-system-x86_64).CPU` frozen near five seconds almost always means the guest
panicked, and the panic text is on screen.

The QEMU monitor also serves exactly one client and leaves the socket in
CloseWait afterwards, so a second connection is refused for the life of the VM.
Do all monitor work — every screendump, every sendkey — over one held
connection.

## Known gaps in the profile

Honest list of what has not been verified, because it cannot be without a
build:

- **Proton is not detected.** It shows as "not installed" on a booted image
  even though `steam` is present, and that is correct: Proton is not a binary
  on `PATH`. Steam fetches it into `compatibilitytools.d` and runs it through
  its own runtime, so both detecting and launching it have to go via Steam.
- **AUR package names are unverified.** The ISO build never sees them — pacman
  has no AUR support — so `pcsx2-git`, `shadps4-bin`, `ryubing` and the rest
  are only proven when `omni-first-boot` runs on a live system.
- **Not an installer yet.** This is a live image. Phase 13.2's auto-installer —
  boot, confirm once, install to disk — is not written. `archinstall` is on the
  image as a manual fallback.
- **No GPU in a VM.** Mesa falls back to `llvmpipe`, which is enough for the
  compositor and the launcher but not for any game. Phase 13.4 still means
  real hardware.
- **Branding is partial.** The Plymouth splash is in and working; the boot menu
  is silent. A GRUB theme and launcher sound effects are not done, and the
  Hyprland config still uses the .conf format that Hyprland 0.57 removes.
