"""Convert QEMU's screendump PPM to PNG, with no third-party imaging library."""
import sys
import zlib
from struct import pack


def read_ppm(path):
    data = open(path, 'rb').read()
    if data[:2] != b'P6':
        raise SystemExit('not a P6 PPM: %r' % data[:2])
    i = 2
    fields = []
    while len(fields) < 3:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b'#':
            while data[i:i + 1] not in (b'\n', b''):
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        fields.append(int(data[i:j]))
        i = j
    i += 1  # single whitespace after maxval
    w, h, _maxval = fields
    return w, h, data[i:i + w * h * 3]


def chunk(tag, payload):
    body = tag + payload
    return pack('>I', len(payload)) + body + pack('>I', zlib.crc32(body) & 0xffffffff)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    w, h, px = read_ppm(src)
    # PNG scanlines each carry a leading filter byte; 0 means "no filter".
    raw = b''.join(b'\x00' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))
    open(dst, 'wb').write(png)
    print('wrote %s (%dx%d)' % (dst, w, h))


main()
