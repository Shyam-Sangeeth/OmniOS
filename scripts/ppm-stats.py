# For each screendump vm-relay.ps1's <POLL:n> made: its size, and how much
# of it is not black, and how much is bright and grey (console text), to find
# the frames between splash and desktop without looking at all of them.
#   python scripts/ppm-stats.py %TEMP%\omnimon
import glob
import os
import re
import sys

d = sys.argv[1]
files = sorted(glob.glob(os.path.join(d, 'poll-*.ppm')), key=lambda f: int(re.findall(r'(\d+)', os.path.basename(f))[0]))
for f in files:
    data = open(f, 'rb').read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    if not m:
        print(os.path.basename(f), 'unreadable')
        continue
    w, h = int(m.group(1)), int(m.group(2))
    px = data[m.end():]
    n = w * h
    nonblack = text = 0
    for i in range(0, len(px) - 2, 3 * 7):  # every 7th pixel
        r, g, b = px[i], px[i + 1], px[i + 2]
        if r + g + b > 30:
            nonblack += 1
            if abs(r - g) < 12 and abs(g - b) < 12 and r > 150:
                text += 1
    total = len(px) // (3 * 7)
    print(f'{os.path.basename(f):12} {w}x{h}  nonblack {100 * nonblack / total:5.1f}%  grey-text {100 * text / total:5.2f}%')
