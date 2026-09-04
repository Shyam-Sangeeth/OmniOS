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
- **No branding.** Phase 11's Plymouth theme, GRUB theme and boot splash are
  not in the profile; the boot is plain text.
