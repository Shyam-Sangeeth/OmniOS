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

# The Phase 10 shell. Absent when the build host has no Qt6, in which case the
# image falls back to the terminal launcher — say so rather than shipping a
# broken exec-once that leaves a blank desktop.
launcher_bin="$CORE_BUILD/src/launcher/omni-launcher"
if [[ -x "$launcher_bin" ]]; then
    install -Dm755 "$launcher_bin" "$PROFILE/airootfs/usr/local/bin/omni-launcher-qml"
    step "including the Qt6 launcher"
else
    # profiledef.sh lists this path in file_permissions, and mkarchiso aborts
    # the build if a listed path is missing. Install a shim so a Qt6-less build
    # degrades to the terminal launcher instead of failing outright — archiso
    # copies the airootfs with --no-preserve=mode, so file_permissions is the
    # only thing that can make either version executable.
    install -Dm755 /dev/stdin "$PROFILE/airootfs/usr/local/bin/omni-launcher-qml" <<'SHIM'
#!/usr/bin/env bash
echo "omni-launcher-qml: not built into this image (no Qt6 at build time)" >&2
exit 127
SHIM
    echo "warning: Qt6 launcher not built; image uses the terminal launcher" >&2
fi

# --- 2. service symlinks git could not carry --------------------------------
step "enabling systemd units"
while read -r unit target; do
    [[ -z "${unit:-}" || "$unit" == \#* ]] && continue
    link_dir="$PROFILE/airootfs/etc/systemd/system/${target}.wants"
    install -d "$link_dir"
    ln -sf "/usr/lib/systemd/system/${unit}" "${link_dir}/${unit}"
    printf '    %s -> %s\n' "$unit" "$target"
done < "$PROFILE/services.enable"

# Masked units are symlinks to /dev/null. Same Windows-symlink reason as above.
if [[ -r "$PROFILE/services.mask" ]]; then
    while read -r unit; do
        [[ -z "${unit:-}" || "$unit" == \#* ]] && continue
        ln -sfn /dev/null "$PROFILE/airootfs/etc/systemd/system/${unit}"
        printf '    masked %s\n' "$unit"
    done < "$PROFILE/services.mask"
fi

# /etc/localtime is a symlink into the zoneinfo database. Git on Windows cannot
# check one out, so it is made here rather than committed — without it systemd
# has no timezone and asks for one interactively on first boot.
ln -sfn /usr/share/zoneinfo/UTC "$PROFILE/airootfs/etc/localtime"

# --- 3. data the running system needs ---------------------------------------
install -Dm644 "$PROFILE/packages.aur.txt" \
    "$PROFILE/airootfs/usr/share/omnios/packages.aur.txt"
install -Dm644 "$REPO/OmniOS.md" "$PROFILE/airootfs/usr/share/omnios/OmniOS.md"

# Which desktop entries ship with the image.
#
# The Apps tab is built from the machine's desktop entries, so that anything
# installed from the store turns up as a tile without a catalogue in this
# repository to maintain. The cost is that everything the image itself drags in
# would turn up too: settings dialogs from the file manager's dependencies,
# Vulkan and Avahi tools, an Xwayland launcher. Several dozen tiles nobody
# chose.
#
# So the launcher hides what came with the image, and this is where that list
# comes from. It is computed from the dependency closure of packages.x86_64
# rather than by looking inside the built root filesystem: pacman can answer
# both questions without one existing, which keeps this a step that runs in
# seconds before mkarchiso rather than a second pass over a 6GB tree.
#
# A missing file means no filtering, which is the safe direction — a developer
# build shows everything rather than nothing.
step "recording which desktop entries ship with the image"
baseline="$PROFILE/airootfs/usr/share/omnios/baseline-apps.txt"
install -d "$(dirname "$baseline")"
{
    echo "# Desktop entry ids that came with the image, so the Apps tab can"
    echo "# show only what someone chose to install. Generated by build-iso.sh."
} > "$baseline"

# The whole query runs with errexit and pipefail off, in a subshell.
#
# This is an optimisation — a missing or short list only means the Apps tab
# shows more than it should — so nothing in it may abort the build. It already
# did once: "pacman -Sp" exits non-zero over a warning, pipefail turned that
# into a failed pipeline, and errexit ended the run at this step with no message
# and an exit status that looked like success.
#
# --config is not optional either: the profile enables multilib and the build
# host's pacman.conf does not, so without it every lib32 package and steam
# resolve as "target not found" and the query returns nothing at all.
(
    set +e
    set +o pipefail

    pacconf="$PROFILE/pacman.conf"

    # A throwaway database, not the build host's.
    #
    # "pacman -Sp" leaves out anything already installed where it is asked, and
    # the build container has cmake, gcc, git and Qt on it — so against the host
    # database the closure came back 144 packages short, and three Avahi tiles
    # nobody asked for turned up on the Apps tab. Against an empty database
    # nothing is installed, so the answer is the whole closure.
    #
    # Both databases are synced into it: -Sy for the package database, which is
    # what -Sp resolves names against, and -Fy for the files database, which is
    # what -Fl reads. With only the second, every package came back as "target
    # not found" and the list was silently empty.
    pacdb="$(mktemp -d)"
    trap 'rm -rf "$pacdb"' EXIT
    pacman --config "$pacconf" --dbpath "$pacdb" -Sy --noconfirm >/dev/null 2>&1
    pacman --config "$pacconf" --dbpath "$pacdb" -Fy --noconfirm >/dev/null 2>&1

    # sed strips comments the same way mkarchiso does.
    sed 's/#.*//' "$PROFILE/packages.x86_64" | awk 'NF { print $1 }' > /tmp/omnios-wanted.txt

    # The dependency closure, not just the listed packages: most of the noise
    # arrives as a dependency of something perfectly reasonable.
    xargs -r pacman --config "$pacconf" --dbpath "$pacdb" -Sp --print-format '%n' --noconfirm \
        < /tmp/omnios-wanted.txt 2>/dev/null \
        | sort -u > /tmp/omnios-closure.txt

    xargs -r pacman --config "$pacconf" --dbpath "$pacdb" -Fl \
        < /tmp/omnios-closure.txt 2>/dev/null \
        | awk '$2 ~ /^usr\/share\/applications\/[^/]+\.desktop$/ {
               n = split($2, parts, "/"); id = parts[n]; sub(/\.desktop$/, "", id); print id }' \
        | sort -u >> "$baseline"
)

found=$(grep -cv '^#' "$baseline" || true)
echo "    $found entries"
if [[ $found -eq 0 ]]; then
    echo "    warning: none found; the Apps tab will show every desktop entry" >&2
fi

# --- 4. the image -----------------------------------------------------------
step "running mkarchiso (this pulls a few GB and takes a while)"
rm -rf "$WORK"
install -d "$WORK" "$OUT"
mkarchiso -v -w "$WORK" -o "$OUT" "$PROFILE"

step "done"
ls -lh "$OUT"/*.iso
printf '\nboot it with:  ./scripts/run-qemu.sh %s\n' "$(ls "$OUT"/*.iso | head -1)"
