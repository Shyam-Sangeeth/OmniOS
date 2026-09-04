#!/usr/bin/env bash
# Boots an OmniOS ISO in QEMU — Phase 13.4, minus the real hardware.
#
#   ./scripts/run-qemu.sh                     # newest ISO in out/, BIOS boot
#   ./scripts/run-qemu.sh path/to.iso         # a specific image
#   ./scripts/run-qemu.sh --uefi              # boot through OVMF instead
#   ./scripts/run-qemu.sh --disk              # attach a 40G disk to install onto
#
# A VM exercises the kernel, initramfs, boot path, systemd units and the
# scanner. It cannot exercise the compatibility layers: there is no passthrough
# GPU, so Proton and every Vulkan emulator will fall back to software or fail.
# That is expected, and is why Phase 13.4 says "test on real hardware".
set -euo pipefail

readonly REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DISK="$REPO/out/omnios-test.qcow2"

iso=""
uefi=0
with_disk=0

for arg in "$@"; do
    case "$arg" in
        --uefi) uefi=1 ;;
        --disk) with_disk=1 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) iso="$arg" ;;
    esac
done

die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

command -v qemu-system-x86_64 >/dev/null || die "qemu not found — pacman -S qemu-full"

if [[ -z "$iso" ]]; then
    iso="$(ls -t "$REPO"/out/*.iso 2>/dev/null | head -1 || true)"
    [[ -n "$iso" ]] || die "no ISO in out/ — run scripts/build-iso.sh first"
fi
[[ -f "$iso" ]] || die "$iso does not exist"

args=(
    -name "OmniOS"
    -machine q35
    -m 4G
    -smp "$(nproc)"
    # virtio-vga-gl gives the guest a GPU with a real Mesa driver, which is the
    # closest a VM gets to the Vulkan stack OmniOS actually targets.
    -device virtio-vga-gl
    -display gtk,gl=on
    -audiodev pa,id=snd0
    -device intel-hda
    -device hda-output,audiodev=snd0
    -device qemu-xhci
    -device usb-tablet
    -netdev user,id=net0
    -device virtio-net-pci,netdev=net0
    -drive "file=${iso},media=cdrom,readonly=on"
    -boot d
)

# KVM makes the difference between "usable" and "unwatchable" here.
if [[ -w /dev/kvm ]]; then
    args+=(-enable-kvm -cpu host)
else
    printf '\033[33mwarning:\033[0m /dev/kvm not available — this will be very slow\n'
    args+=(-cpu max)
fi

if [[ $uefi -eq 1 ]]; then
    ovmf=""
    for candidate in /usr/share/edk2/x64/OVMF_CODE.4m.fd \
                     /usr/share/edk2-ovmf/x64/OVMF_CODE.fd \
                     /usr/share/OVMF/OVMF_CODE.fd; do
        [[ -f "$candidate" ]] && { ovmf="$candidate"; break; }
    done
    [[ -n "$ovmf" ]] || die "OVMF firmware not found — pacman -S edk2-ovmf"
    args+=(-drive "if=pflash,format=raw,readonly=on,file=${ovmf}")
fi

if [[ $with_disk -eq 1 ]]; then
    if [[ ! -f "$DISK" ]]; then
        printf '==> creating a 40G test disk at %s\n' "$DISK"
        qemu-img create -f qcow2 "$DISK" 40G >/dev/null
    fi
    args+=(-drive "file=${DISK},if=virtio,format=qcow2")
fi

printf '==> booting %s%s\n' "$(basename "$iso")" "$([[ $uefi -eq 1 ]] && echo ' (UEFI)')"
exec qemu-system-x86_64 "${args[@]}"
