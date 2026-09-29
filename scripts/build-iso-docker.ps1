<#
.SYNOPSIS
    Builds the OmniOS ISO in an Arch Linux container (Route A, docs/BUILDING.md).

.DESCRIPTION
    mkarchiso pacstraps a full Arch system and needs loop devices, so it cannot
    run on Windows. This runs it in a privileged archlinux container with the
    repo bind-mounted, and drops the ISO in out/ on the Windows side.

    --privileged is not optional: mkarchiso mounts loop devices, which a default
    container is not permitted to do.

.PARAMETER SkipCore
    Don't rebuild omnictl inside the container; reuse what is in build/.

.PARAMETER KeepCache
    Persist pacman's package cache in a named volume, so a second build does not
    re-download several GB.

.EXAMPLE
    .\scripts\build-iso-docker.ps1
    .\scripts\build-iso-docker.ps1 -KeepCache
#>
[CmdletBinding()]
param(
    [switch]$SkipCore,
    [switch]$KeepCache
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker not found. Install Docker Desktop, or see docs/BUILDING.md for the WSL route."
}

# A stopped Docker Desktop reports the CLI as present but every command fails,
# so probe the daemon rather than the binary.
docker info --format '{{.ServerVersion}}' 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "The Docker daemon is not responding. Start Docker Desktop and wait for it to say 'Engine running'."
}

# The work tree lives in a Linux volume, never on the bind mount: mkarchiso
# builds a real root filesystem and the Windows 9p/virtiofs layer cannot carry
# Unix ownership, setuid bits or device nodes. Only the finished ISO is copied
# back to the repo at the end.
# One build at a time. Two concurrent runs share the work volume, and the
# second one's "rm -rf $WORK" deletes the first's tree from under it — which
# surfaces as a wall of llistxattr errors during squashfs and a failed build
# that looks like corruption rather than a collision.
$running = (& docker ps --filter 'name=^omnios-build$' --format '{{.Names}}' 2>$null)
if ($running) {
    throw "An OmniOS build is already running (container 'omnios-build'). Wait for it, or stop it with: docker stop omnios-build"
}

$dockerArgs = @(
    'run', '--rm', '--privileged', '--name', 'omnios-build',
    '-v', "${repo}:/repo",
    '-v', 'omnios-work:/work',
    '-w', '/repo',
    '-e', 'OMNIOS_WORK_DIR=/work/iso-work',
    '-e', 'OMNIOS_OUT_DIR=/work/out'
)

if ($KeepCache) {
    $dockerArgs += @('-v', 'omnios-pacman-cache:/var/cache/pacman/pkg')
}

$buildFlags = if ($SkipCore) { '--skip-core' } else { '' }

# Keyring first: a fresh archlinux image often has a stale one, and every
# package then fails signature verification for no obvious reason.
$script = @"
set -euo pipefail
echo '==> refreshing keyring and installing build tools'
# Not fatal. Upgrading archlinux-keyring runs a post-install hook that signs the
# new keys, and the archlinux image has no local signing key, so the hook fails:
#
#   ==> ERROR: There is no secret key available to sign with.
#
# The keyring itself is upgraded regardless, and the image ships it already
# populated, so this is noise. Under 'set -e' it aborted the whole build, and
# only on the runs where the keyring package actually changed — which is why it
# looked intermittent. Generating a master key to silence it is worse: in a
# container pacman-key --init can sit waiting on entropy.
#
# If the keyring were genuinely broken, pacstrap says so a minute later with a
# signature error naming the package.
pacman -Sy --noconfirm archlinux-keyring || echo '    (keyring hook failed; continuing)'
# sdl3 is the launcher's controller support. CMake treats it as optional, so
# without it the build still succeeds — and ships a Game Mode that ignores
# every gamepad, which no VM test notices because the VM has none. Every image
# up to 2026-09-26 was built that way.
pacman -S --noconfirm --needed archiso cmake ninja gcc git pkgconf qt6-base qt6-declarative qt6-tools sdl3 mpv layer-shell-qt
echo '==> building'
./scripts/build-iso.sh $buildFlags
echo '==> copying the ISO out of the build volume'
mkdir -p /repo/out
cp -v /work/out/*.iso /repo/out/
# Only reached once the copy succeeded. The work tree is about 12 GB and the
# next build deletes it before starting anyway; the ISO is now in /repo/out.
# Left in the volume they held 14.5 GB of Docker's disk between builds for
# nothing. A failed build stops before this, so its tree is still there to
# look at. The package cache is a different volume and is kept.
echo '==> clearing the build volume'
rm -rf /work/iso-work /work/out
"@

$dockerArgs += @('archlinux:latest', 'bash', '-c', $script)

Write-Host "==> running mkarchiso in a container (expect several GB of downloads)" -ForegroundColor Magenta
& docker @dockerArgs

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

$iso = Get-ChildItem -Path (Join-Path $repo 'out') -Filter *.iso |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1

Write-Host ""
Write-Host "==> built $($iso.Name) ($([math]::Round($iso.Length / 1GB, 2)) GB)" -ForegroundColor Green
Write-Host "    boot it with:  .\scripts\run-qemu.ps1"
