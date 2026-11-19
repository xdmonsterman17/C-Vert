#!/usr/bin/env python3
"""Counts pixels in a PNG matching a colour rule, with only the standard library (no PIL here).
   python3 tests/png_stats.py FILE.png            prints size, distinct colours, and orange pixels
Reads 8-bit RGB / RGBA, non-interlaced PNGs (what raylib / Godot write)."""
import struct, sys, zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError("only 8-bit RGB/RGBA non-interlaced PNGs")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev = w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: cur[i] = (cur[i] + a) & 255
            elif f == 2: cur[i] = (cur[i] + b) & 255
            elif f == 3: cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(cur)
        prev = cur
    return w, h, bpp, rows


def stats(path):
    w, h, bpp, rows = read_png(path)
    colours, orange = set(), 0
    for row in rows:
        for x in range(0, w * bpp, bpp):
            r, g, b = row[x], row[x + 1], row[x + 2]
            colours.add((r, g, b))
            if r > 130 and 60 < g < 130 and b < 60:   # the lit orange of a selected face
                orange += 1
    return w, h, len(colours), orange


if __name__ == "__main__":
    w, h, n, orange = stats(sys.argv[1])
    print("size %dx%d  colours %d  orange pixels %d" % (w, h, n, orange))
