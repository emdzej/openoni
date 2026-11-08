#!/usr/bin/env python3
"""Draw OpenOni's app icon as a PNG of any size.

The design is the site's emblem (docs/public/emblem.svg): the open chrome ring and the zigzag in it, with
the ring's blue glow and the orange bar under them, on a rounded square of night blue (the emblem's thin
circuit lines are left out: they vanish at icon sizes). It is drawn here from the same geometry (the
emblem's 320 x 320 canvas) rather than rasterised from the SVG, so the packaging needs nothing but the
Python standard library (CI runners have no SVG renderer or Pillow). Edges are anti-aliased from signed
distances, one evaluation per pixel. No game art is involved.

    tools/gasm-bundle/icon.py <size> <out.png>
"""
import math
import struct
import sys
import zlib

CANVAS = 320.0
RECT = (0.0, 0.0, 320.0, 320.0, 60.0)          # x0 y0 x1 y1 corner radius
RING_C, RING_R = (160.0, 160.0), 96.0
# The arc from (160, 64) clockwise to (77, 112): from -90 to 210 degrees (y down), square caps.
RING_A0, RING_A1 = math.radians(-90.0), math.radians(210.0)
ZIGZAG = [(196.0, 122.0), (140.0, 198.0), (184.0, 198.0)]
BAR = ((104.0, 268.0), (216.0, 268.0))


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


BACK_RGB = hexrgb('#0d1430')
GLOW_RGB = hexrgb('#6699ee')
ORANGE_RGB = hexrgb('#e08010')
CHROME = [(0.0, hexrgb('#ffffff')), (0.45, hexrgb('#b9c0f0')), (1.0, hexrgb('#5060a0'))]


def chrome(t):
    """The emblem's vertical chrome gradient at t (0 top, 1 bottom of the shape's box)."""
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(CHROME, CHROME[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return tuple(a + (b - a) * k for a, b in zip(c0, c1))
    return CHROME[-1][1]


def rounded_rect_sd(x, y):
    """Signed distance to the rounded rectangle (negative inside)."""
    x0, y0, x1, y1, r = RECT
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def box_sd(u, v, h):
    """Signed distance to a square of half side h centred at the origin."""
    qx, qy = abs(u) - h, abs(v) - h
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0)


def ring_sd(x, y, width):
    """Signed distance to the open ring stroked width wide, with square caps."""
    dx, dy = x - RING_C[0], y - RING_C[1]
    a = math.atan2(dy, dx)
    while a < RING_A0:
        a += 2 * math.pi
    body = abs(math.hypot(dx, dy) - RING_R) - width / 2 if a <= RING_A1 else 1e9
    caps = []
    for e in (RING_A0, RING_A1):
        ex, ey = RING_C[0] + RING_R * math.cos(e), RING_C[1] + RING_R * math.sin(e)
        tx, ty = -math.sin(e), math.cos(e)          # the tangent
        u, v = (x - ex) * tx + (y - ey) * ty, (x - ex) * math.cos(e) + (y - ey) * math.sin(e)
        caps.append(box_sd(u, v, width / 2))
    return min(body, *caps)


def segment_sd(x, y, a, b, width):
    """Signed distance to the segment a-b stroked width wide (round ends)."""
    ax, ay = a
    bx, by = b
    vx, vy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - (ax + vx * t), y - (ay + vy * t)) - width / 2


def zigzag_sd(x, y, width):
    return min(segment_sd(x, y, a, b, width) for a, b in zip(ZIGZAG, ZIGZAG[1:]))


def coverage(sd, px):
    """Fraction of a pixel (px canvas units wide) covered by the shape at signed distance sd."""
    return max(0.0, min(1.0, 0.5 - sd / px))


def over(dst, rgb, a):
    """Composite a straight-alpha colour over a premultiplied pixel."""
    r, g, b, da = dst
    k = 1.0 - a
    return (rgb[0] * a + r * k, rgb[1] * a + g * k, rgb[2] * a + b * k, a + da * k)


def render(size):
    px = CANVAS / size
    glow_px = max(px, 6.0)      # the glow's edge soft at every size
    rows = []
    for j in range(size):
        y = (j + 0.5) * px
        line = bytearray()
        for i in range(size):
            x = (i + 0.5) * px
            p = (0.0, 0.0, 0.0, 0.0)
            rd = rounded_rect_sd(x, y)
            if rd < px:
                p = over(p, BACK_RGB, coverage(rd, px))
                p = over(p, GLOW_RGB, 0.85 * coverage(ring_sd(x, y, 22.0), glow_px))
                p = over(p, chrome((y - 64.0) / 192.0), coverage(ring_sd(x, y, 14.0), px))
                p = over(p, chrome((y - 122.0) / 76.0), coverage(zigzag_sd(x, y, 12.0), px))
                p = over(p, ORANGE_RGB, coverage(segment_sd(x, y, BAR[0], BAR[1], 4.0), px))
                p = tuple(c * coverage(rd, px) for c in p[:3]) + (p[3] * coverage(rd, px),)
            a = p[3]
            u = 1.0 / a if a > 0 else 0.0
            line += bytes((min(255, round(p[0] * u * 255)), min(255, round(p[1] * u * 255)),
                           min(255, round(p[2] * u * 255)), min(255, round(a * 255))))
        rows.append(line)
    return rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    size = int(sys.argv[1])
    if not 8 <= size <= 2048:
        raise SystemExit('size must be 8..2048')
    write_png(sys.argv[2], size, size, render(size))


if __name__ == '__main__':
    main()
