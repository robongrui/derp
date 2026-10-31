"""Make the Windows and Linux icons (platform/icon/derp.ico and derp.png) from
the DERP wordmark, in the look of the Mac icon (scripts/make-mac-icon.swift).
Usage: py scripts/make-icons.py   (standard library only)

The wordmark sits on a dark rounded tile with a red glow. At 256 pixels it is
drawn 1:1, so the pixel art stays crisp; smaller sizes average 4x4 samples."""
import math
import pathlib
import struct
import zlib

FOLDER = pathlib.Path(__file__).resolve().parent.parent / 'platform' / 'icon'
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
SAMPLES = 4


def read_png(path):
    """Width, height and rows of an 8-bit RGBA PNG."""
    data = path.read_bytes()
    pos, idat = 8, b''
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if (depth, color, interlace) != (8, 6, 0):
                raise SystemExit(f'{path.name}: expected 8-bit RGBA, not interlaced')
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    raw, stride = zlib.decompress(idat), width * 4
    rows, prev = [], bytearray(stride)
    for y in range(height):
        line = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        kind, row = line[0], bytearray(line[1:])
        for i in range(stride):
            a = row[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                row[i] = (row[i] + a) & 255
            elif kind == 2:
                row[i] = (row[i] + b) & 255
            elif kind == 3:
                row[i] = (row[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(row)
        prev = row
    return width, height, rows


def png_bytes(size, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + bytes(row) for row in rows)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def render(size, logo):
    """One icon size as RGBA rows."""
    logo_w, logo_h, logo_rows = logo
    s = float(size)
    # A fuller tile than Apple's grid: Windows and Linux icons don't pad.
    inset = s * 32 / 1024
    radius = (s - 2 * inset) * 185 / 824
    reach = (s - 2 * inset) * 0.7
    inner, outer = (0.16, 0.03, 0.02), (0.03, 0.02, 0.02)
    scale = s / 256
    x0 = round((s - logo_w * scale) / 2)
    y0 = round((s - logo_h * scale) / 2)
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            r = g = b = covered = 0.0
            for sy in range(SAMPLES):
                y = py + (sy + 0.5) / SAMPLES
                for sx in range(SAMPLES):
                    x = px + (sx + 0.5) / SAMPLES
                    dx = max(inset + radius - x, 0, x - (s - inset - radius))
                    dy = max(inset + radius - y, 0, y - (s - inset - radius))
                    if dx * dx + dy * dy > radius * radius:
                        continue
                    t = min(math.hypot(x - s / 2, y - s / 2) / reach, 1)
                    cr, cg, cb = (i + (o - i) * t for i, o in zip(inner, outer))
                    lx, ly = math.floor((x - x0) / scale), math.floor((y - y0) / scale)
                    if 0 <= lx < logo_w and 0 <= ly < logo_h:
                        pixel = logo_rows[ly][lx * 4:lx * 4 + 4]
                        alpha = pixel[3] / 255
                        cr += (pixel[0] / 255 - cr) * alpha
                        cg += (pixel[1] / 255 - cg) * alpha
                        cb += (pixel[2] / 255 - cb) * alpha
                    r, g, b, covered = r + cr, g + cg, b + cb, covered + 1
            if covered:
                row += bytes((round(r / covered * 255), round(g / covered * 255), round(b / covered * 255),
                              round(covered / SAMPLES ** 2 * 255)))
            else:
                row += bytes(4)
        rows.append(row)
    return rows


def main():
    logo = read_png(FOLDER / 'derp-logo.png')
    images = {size: png_bytes(size, render(size, logo)) for size in ICO_SIZES}
    # An .ico of PNG entries (Windows Vista and later).
    offset = 6 + 16 * len(images)
    header, entries, payload = struct.pack('<HHH', 0, 1, len(images)), b'', b''
    for size, image in images.items():
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
        payload += image
    (FOLDER / 'derp.ico').write_bytes(header + entries + payload)
    (FOLDER / 'derp.png').write_bytes(images[256])
    print('Wrote platform/icon/derp.ico and platform/icon/derp.png')


if __name__ == '__main__':
    main()
