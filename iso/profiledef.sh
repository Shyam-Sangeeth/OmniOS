#!/usr/bin/env bash
# OmniOS archiso profile — Phase 13 (OmniOS.md §15).
#
# This is the file that makes "the kernel is linux-zen" a property of OmniOS
# rather than a step someone has to remember during archinstall. Everything
# Phases 1-4 ask a user to do by hand is baked in here: the kernel, the GPU and
# audio stack, the sysctl tuning, zram, the CPU governor and the autologin that
# takes the user straight to the launcher.
#
# shellcheck disable=SC2034

iso_name="omnios"
iso_label="OMNIOS_$(date +%Y%m)"
iso_publisher="OmniOS"
iso_application="OmniOS live medium"
iso_version="0.1.0"

# Must be 8 characters or fewer: it becomes a directory name on an ISO 9660
# filesystem, and the bootloader configs below refer to it as %INSTALL_DIR%.
install_dir="omnios"

buildmodes=('iso')

# BIOS and 64-bit UEFI. Both are needed to boot under QEMU either way round:
# plain qemu-system-x86_64 is BIOS, and -bios OVMF is UEFI.
# The four-way split into .mbr/.eltorito and .esp/.eltorito is deprecated;
# current archiso takes one name per firmware and emits both variants itself.
bootmodes=('bios.syslinux' 'uefi.systemd-boot')

arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'xz' '-Xbcj' 'x86' '-b' '1M' '-Xdict-size' '1M')

bootstrap_tarball_compression=('zstd' '-c' '-T0' '--auto-threads=logical' '--long' '-19')

# The profile is edited on a Windows host, where the filesystem does not carry
# a Unix mode bit. Anything that has to be executable or private is listed here
# explicitly rather than relying on what git checked out.
#
# mkarchiso declares this associative array itself before sourcing the profile;
# declaring it here too is harmless and lets the file be sourced standalone for
# checking, which is the only validation possible on a non-Arch host.
declare -A file_permissions
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/etc/gshadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.bash_profile"]="0:0:644"
  ["/usr/local/bin/omni-first-boot"]="0:0:755"
  ["/usr/local/bin/omni-launcher"]="0:0:755"
  ["/usr/local/bin/omnictl"]="0:0:755"
)
