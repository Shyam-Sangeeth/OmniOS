#!/usr/bin/env bash
# omni-nvidia detect, against fake /sys/bus/pci/devices trees: which Nvidia
# cards get Nvidia's driver (Turing and newer), which stay on NVK, and that
# a card's audio or USB-C function is not mistaken for the GPU.
#   bash tests/omni-nvidia-detect.sh
script="$(dirname "$0")/../iso/airootfs/usr/local/bin/omni-nvidia"
fail=0
root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT

dev() { # machine slot vendor device class
    mkdir -p "$root/$1/$2"
    echo "$3" >"$root/$1/$2/vendor"
    echo "$4" >"$root/$1/$2/device"
    echo "$5" >"$root/$1/$2/class"
}
check() { # name machine expected
    local got
    got="$(OMNI_PCI_DEVICES="$root/$2" bash "$script" detect)"
    if [[ $got == "$3" ]]; then echo "ok   $1"; else echo "FAIL $1: got '$got', want '$3'"; fail=1; fi
}

dev amd 0000:03:00.0 0x1002 0x73bf 0x030000
dev amd 0000:03:00.1 0x1002 0xab28 0x040300
check "AMD only" amd none

dev rtx 0000:01:00.0 0x10de 0x2684 0x030000        # RTX 4090
dev rtx 0000:01:00.1 0x10de 0x22ba 0x040300        # its HDMI audio
check "RTX 4090" rtx "nvidia-open 0000:01:00.0 0x2684"

dev gtx16 0000:01:00.0 0x10de 0x1f82 0x030000      # GTX 1650 (TU117)
check "GTX 1650" gtx16 "nvidia-open 0000:01:00.0 0x1f82"

dev tu102 0000:01:00.0 0x10de 0x1e04 0x030000      # RTX 2080 Ti, Turing's first ID range
check "RTX 2080 Ti" tu102 "nvidia-open 0000:01:00.0 0x1e04"

dev pascal 0000:01:00.0 0x10de 0x1b80 0x030000     # GTX 1080
dev pascal 0000:01:00.1 0x10de 0x10f0 0x040300
check "GTX 1080" pascal "nvk 0000:01:00.0 0x1b80"

dev mx 0000:02:00.0 0x10de 0x1d52 0x030200         # MX250 (GP108), a 3D controller
dev mx 0000:00:02.0 0x8086 0x3ea0 0x030000         # beside Intel graphics
check "Intel + MX250" mx "nvk 0000:02:00.0 0x1d52"

dev optimus 0000:01:00.0 0x10de 0x25a2 0x030200    # RTX 3050 laptop GPU
dev optimus 0000:00:02.0 0x8086 0x46a6 0x030000
check "Intel + RTX 3050 laptop" optimus "nvidia-open 0000:01:00.0 0x25a2"

dev usbc 0000:01:00.2 0x10de 0x1ada 0x0c0330       # a Turing card's USB-C controller alone
check "Nvidia USB only" usbc none

check "no devices" nothing none

exit $fail
