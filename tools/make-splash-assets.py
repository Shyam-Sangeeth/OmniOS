"""Generate the OmniOS Plymouth theme artwork.

The splash is a ring with a play triangle inside — "O" for OmniOS, play for
what it does — over the launcher's own background colour, with a rotating
spinner beneath it.

The assets are generated rather than committed as opaque binaries so the
palette stays tied to the UI spec in OmniOS.md §12 and a colour change is a
one-line edit here. No imaging library: PNG is simple enough to write directly,
and the ISO build must not depend on anything outside the repo.

    python tools/make-splash-assets.py iso/airootfs/usr/share/plymouth/themes/omnios
"""
import math
import os
import struct
import sys
import zlib

# OmniOS palette (OmniOS.md §12).
ACCENT = (0x6C, 0x63, 0xFF)      # #6C63FF
TEXT_DIM = (0x88, 0x88, 0xAA)    # #8888AA

LOGO_SIZE = 220
RING_OUTER = 96.0
RING_INNER = 82.0

SPINNER_SIZE = 64
SPINNER_FRAMES = 12
SPINNER_DOTS = 12
SPINNER_RADIUS = 24.0
SPINNER_DOT = 3.4


def write_png(path, width, height, pixels):
    """pixels: flat list of (r, g, b, a) tuples, row-major."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (none) for each scanline
        for x in range(width):
            raw.extend(pixels[y * width + x])

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
           + chunk(b'IEND', b''))
    with open(path, 'wb') as handle:
        handle.write(png)


def coverage(distance, edge, softness=1.2):
    """Antialiased coverage for a signed distance from an edge."""
    return max(0.0, min(1.0, (edge - distance) / softness + 0.5))


def make_logo(path):
    size = LOGO_SIZE
    centre = size / 2.0
    pixels = []
    for y in range(size):
        for x in range(size):
            dx = x + 0.5 - centre
            dy = y + 0.5 - centre
            dist = math.hypot(dx, dy)

            # Ring: inside the outer edge and outside the inner edge.
            alpha = min(coverage(dist, RING_OUTER), coverage(-dist, -RING_INNER))

            # Play triangle, pointing right, nested inside the ring.
            tri = 0.0
            tx, ty = dx + 8.0, dy
            half = 34.0
            if -half <= ty <= half:
                # Right edge tapers to a point as |ty| approaches half.
                span = 46.0 * (1.0 - abs(ty) / half)
                if -22.0 <= tx <= -22.0 + span:
                    tri = 1.0
                elif tx < -22.0:
                    tri = coverage(-22.0 - tx, 0.0)
                else:
                    tri = coverage(tx - (-22.0 + span), 0.0)
            alpha = max(alpha, tri)

            if alpha <= 0.0:
                pixels.append((0, 0, 0, 0))
            else:
                pixels.append(ACCENT + (int(round(255 * alpha)),))
    write_png(path, size, size, pixels)


def make_spinner(directory):
    size = SPINNER_SIZE
    centre = size / 2.0
    for frame in range(SPINNER_FRAMES):
        pixels = [(0, 0, 0, 0)] * (size * size)
        for dot in range(SPINNER_DOTS):
            # Trailing fade: the dot at the head is brightest.
            age = (dot - frame) % SPINNER_DOTS
            level = 0.15 + 0.85 * ((SPINNER_DOTS - age) / SPINNER_DOTS) ** 2.2
            angle = 2.0 * math.pi * dot / SPINNER_DOTS - math.pi / 2.0
            cx = centre + SPINNER_RADIUS * math.cos(angle)
            cy = centre + SPINNER_RADIUS * math.sin(angle)
            lo_x, hi_x = int(cx - 6), int(cx + 7)
            lo_y, hi_y = int(cy - 6), int(cy + 7)
            for y in range(max(0, lo_y), min(size, hi_y)):
                for x in range(max(0, lo_x), min(size, hi_x)):
                    d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                    a = coverage(d, SPINNER_DOT) * level
                    if a <= 0.0:
                        continue
                    index = y * size + x
                    existing = pixels[index][3] / 255.0
                    merged = max(existing, a)
                    pixels[index] = TEXT_DIM + (int(round(255 * merged)),)
        write_png(os.path.join(directory, 'progress-%d.png' % frame), size, size, pixels)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    make_logo(os.path.join(out, 'logo.png'))
    make_spinner(out)
    print('wrote logo.png and %d spinner frames to %s' % (SPINNER_FRAMES, out))


main()
