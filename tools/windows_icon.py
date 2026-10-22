#!/usr/bin/env python3
"""Writes data/glyphshift.ico, the Windows application icon, from data/glyphshift.svg.

The SVG uses only rounded rectangles filled with solid colors or the
diagonal linear gradient it defines (per shape, objectBoundingBox units);
this rasterizes exactly that subset with 4x4 supersampling and stores one PNG
image per size in the ICO container. Standard library only:

    python tools/windows_icon.py [--output data/glyphshift.ico]
"""
import argparse
import pathlib
import struct
import xml.etree.ElementTree as ElementTree
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SAMPLES = 4
NAMESPACE = '{http://www.w3.org/2000/svg}'


def color(text):
    text = text.lstrip('#')
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def load(path):
    svg = ElementTree.parse(path).getroot()
    view = [float(value) for value in svg.get('viewBox').split()]
    gradients = {}
    for gradient in svg.iter(NAMESPACE + 'linearGradient'):
        stops = [(float(stop.get('offset')), color(stop.get('stop-color')))
                 for stop in gradient.iter(NAMESPACE + 'stop')]
        vector = [float(gradient.get(name, default)) for name, default in
                  (('x1', 0), ('y1', 0), ('x2', 1), ('y2', 0))]
        gradients[gradient.get('id')] = (vector, stops)
    shapes = []
    for rect in svg.iter(NAMESPACE + 'rect'):
        x, y, width, height = (float(rect.get(name)) for name in ('x', 'y', 'width', 'height'))
        fill = rect.get('fill')
        paint = gradients[fill[5:-1]] if fill.startswith('url(#') else color(fill)
        shapes.append((x, y, width, height, float(rect.get('rx', 0)), paint))
    return view, shapes


def inside(px, py, x, y, width, height, radius):
    if not (x <= px <= x + width and y <= py <= y + height):
        return False
    dx = max(x + radius - px, px - (x + width - radius), 0)
    dy = max(y + radius - py, py - (y + height - radius), 0)
    return dx * dx + dy * dy <= radius * radius


def paint_at(paint, px, py, x, y, width, height):
    if len(paint) == 3 and isinstance(paint[0], int):
        return paint
    (x1, y1, x2, y2), stops = paint
    u, v = (px - x) / width, (py - y) / height
    dx, dy = x2 - x1, y2 - y1
    t = min(max(((u - x1) * dx + (v - y1) * dy) / (dx * dx + dy * dy), 0), 1)
    for (start, first), (end, second) in zip(stops, stops[1:]):
        if t <= end:
            mix = (t - start) / (end - start) if end > start else 0
            return tuple(a + (b - a) * mix for a, b in zip(first, second))
    return stops[-1][1]


def render(view, shapes, size):
    """Premultiplied over-composition of every shape, per subsample."""
    left, top, width, height = view
    rows = []
    for row in range(size):
        line = bytearray()
        for column in range(size):
            total = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    px = left + (column + (sx + .5) / SAMPLES) * width / size
                    py = top + (row + (sy + .5) / SAMPLES) * height / size
                    r = g = b = a = 0.0
                    for x, y, w, h, radius, paint in shapes:
                        if inside(px, py, x, y, w, h, radius):
                            r, g, b = paint_at(paint, px, py, x, y, w, h)
                            a = 1.0
                    total = [total[0] + r * a, total[1] + g * a, total[2] + b * a, total[3] + a]
            count = SAMPLES * SAMPLES
            alpha = total[3] / count
            if alpha:
                line += bytes(round(channel / count / alpha) for channel in total[:3])
            else:
                line += b'\0\0\0'
            line.append(round(alpha * 255))
        rows.append(bytes(line))
    return rows


def png(rows, size):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))
    raw = b''.join(b'\0' + row for row in rows)
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--svg', type=pathlib.Path, default=ROOT / 'data/glyphshift.svg')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'data/glyphshift.ico')
    args = parser.parse_args()
    view, shapes = load(args.svg)
    images = [png(render(view, shapes, size), size) for size in SIZES]
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = b''
    for size, image in zip(SIZES, images):
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(image),
                               offset)
        offset += len(image)
    args.output.write_bytes(header + entries + b''.join(images))
    print(f'wrote {args.output} ({", ".join(map(str, SIZES))} px)')


if __name__ == '__main__':
    main()
