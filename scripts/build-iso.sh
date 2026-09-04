#!/usr/bin/env bash
# Builds the OmniOS ISO — Phase 13.1 (OmniOS.md §15).
#
# Must run on Arch Linux (or an Arch container) as root: mkarchiso pacstraps a
# full system, needs loop devices, and refuses to run anywhere else.
#
#   sudo ./scripts/build-iso.sh              → out/omnios-0.1.0-x86_64.iso
#   sudo ./scripts/build-iso.sh --skip-core  → don't rebuild omnictl first
#
# Three things happen before mkarchiso is called, all of them because the
# profile is edited on a Windows host:
#   1. omnictl is compiled and installed into the airootfs
#   2. systemd units are enabled by creating symlinks git could not store
#   3. the AUR package list is staged where omni-first-boot expects it
set -euo pipefail

readonly REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly PROFILE="$REPO/iso"
# mkarchiso builds a real root filesystem: ownership, setuid bits, device nodes
# and hardlinks all have to survive. A Windows bind mount (Route A passes the
# repo through Docker's 9p/virtiofs layer) cannot represent any of that, and
# pacstrap dies partway through. Both directories are therefore overridable so
# the container can point them at a Linux volume and copy the ISO back after.
readonly OUT="${OMNIOS_OUT_DIR:-$REPO/out}"
readonly WORK="${OMNIOS_WORK_DIR:-$REPO/build/iso-work}"
# The Linux build gets its own tree. build/ may already hold a CMake cache from
# a Windows host — the repo is bind-mounted into the container in Route A — and
# CMake refuses to reuse a cache whose compiler and source paths are C:/...
readonly CORE_BUILD="$REPO/build/linux"

skip_core=0
[[ "${1:-}" == "--skip-core" ]] && skip_core=1

die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;35m==>\033[0m \033[1m%s\033[0m\n' "$*"; }

[[ $EUID -eq 0 ]] || die "must run as root (mkarchiso needs loop devices)"
command -v mkarchiso >/dev/null || die "mkarchiso not found — pacman -S archiso"
command -v pacman    >/dev/null || die "not an Arch system; see docs/BUILDING.md for the container route"

# --- 1. the core, compiled into the image -----------------------------------
if [[ $skip_core -eq 0 ]]; then
    step "building omnios_core and omnictl"
    command -v cmake >/dev/null || die "cmake not found — pacman -S cmake ninja gcc"
    cmake -S "$REPO" -B "$CORE_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$CORE_BUILD"
    ctest --test-dir "$CORE_BUILD" --output-on-failure
fi

[[ -x "$CORE_BUILD/omnictl" ]] || die "$CORE_BUILD/omnictl missing; run without --skip-core"
install -Dm755 "$CORE_BUILD/omnictl" "$PROFILE/airootfs/usr/local/bin/omnictl"

# --- 2. service symlinks git could not carry --------------------------------
step "enabling systemd units"
while read -r unit target; do
    [[ -z "${unit:-}" || "$unit" == \#* ]] && continue
    link_dir="$PROFILE/airootfs/etc/systemd/system/${target}.wants"
    install -d "$link_dir"
    ln -sf "/usr/lib/systemd/system/${unit}" "${link_dir}/${unit}"
    printf '    %s -> %s\n' "$unit" "$target"
done < "$PROFILE/services.enable"

# --- 3. data the running system needs ---------------------------------------
install -Dm644 "$PROFILE/packages.aur.txt" \
    "$PROFILE/airootfs/usr/share/omnios/packages.aur.txt"
install -Dm644 "$REPO/OmniOS.md" "$PROFILE/airootfs/usr/share/omnios/OmniOS.md"

# --- 4. the image -----------------------------------------------------------
step "running mkarchiso (this pulls a few GB and takes a while)"
rm -rf "$WORK"
install -d "$WORK" "$OUT"
mkarchiso -v -w "$WORK" -o "$OUT" "$PROFILE"

step "done"
ls -lh "$OUT"/*.iso
printf '\nboot it with:  ./scripts/run-qemu.sh %s\n' "$(ls "$OUT"/*.iso | head -1)"
