#!/usr/bin/env bash
# Rebuilds iso/airootfs/usr/share/omnios/bios/openbios.bin: OpenBIOS, the free
# PS1 BIOS written by the PCSX-Redux authors (MIT), from their nugget repo.
#
# It is kept built in the repo, not built with the ISO, because it needs a
# MIPS cross-compiler and Arch's repositories have none; Debian's do. Run this
# (needs Docker) to update it, with the commit to build as the first argument.
#
#   scripts/build-openbios.sh [commit]
set -euo pipefail

COMMIT="${1:-63ef2ae9be7acad6aa71204c725d3c399561f628}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/iso/airootfs/usr/share/omnios/bios"
mkdir -p "$OUT"

docker run --rm -v "$OUT:/out" debian:stable bash -c "
  set -e
  apt-get update >/dev/null
  apt-get install -y --no-install-recommends git ca-certificates make \
      gcc-mipsel-linux-gnu g++-mipsel-linux-gnu binutils-mipsel-linux-gnu >/dev/null
  git clone -q https://github.com/pcsx-redux/nugget /src
  cd /src && git checkout -q $COMMIT
  make -C openbios PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips -j\$(nproc) >/dev/null
  cp openbios/openbios.bin /out/openbios.bin
  cp LICENSE /out/LICENSE.openbios
  chown $(id -u):$(id -g) /out/openbios.bin /out/LICENSE.openbios
"
echo "built $OUT/openbios.bin from nugget $COMMIT"
