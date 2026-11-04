"""The M mark on a transparent background.

This is the letterform alone: no rounded square, no plate. The silhouette is the
icon, so it can sit on any surface, in a taskbar, a tray, a window header or a
splash screen, without carrying a background that would fight whatever is behind
it.

Two consequences of dropping the background are handled here.

The gradient no longer runs violet-to-cyan across the full canvas, because there
is no full canvas. It runs across the letterform's own bounding box, so the left
leg is violet and the right leg is cyan at any size.

The edges are anti-aliased by supersampling alone, so the alpha channel is smooth
rather than stepped. That is what lets the mark sit on a light background without
a dark fringe.

Every size is written separately instead of one image being scaled at use time,
because a small icon needs a proportionally heavier stroke than a large one to
look equally solid.

Usage:
    python src/gui/assets/mark/render_mark.py
"""
from pathlib import Path
import math
import numpy as np
from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent

VIOLET = (0x6C, 0x5C, 0xE7)
CYAN = (0x4F, 0xC3, 0xF7)

# Sizes written out, matching what Windows and Linux ask for an application icon.
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 180, 256, 512)

# The stroke is drawn with the same optical weight at every size. A fixed fraction
# of the canvas is used, then a minimum is enforced in device pixels so a 16-pixel
# icon does not end up with a one-pixel stroke that disappears.
STROKE_FRACTION = 0.148
MIN_STROKE_PIXELS = 2.0


def diagonal_field(size, start, end, angle_degrees):
    """A linear colour ramp across a square, as a float array."""
    angle = math.radians(angle_degrees)
    axis_x, axis_y = math.cos(angle), math.sin(angle)
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    projection = xs * axis_x + ys * axis_y
    t = (projection - projection.min()) / max(1e-6, projection.max() - projection.min())
    field = np.zeros((size, size, 3), dtype=np.float32)
    for channel in range(3):
        field[:, :, channel] = start[channel] + (end[channel] - start[channel]) * t
    return field


def m_geometry(size, stroke):
    """The path and the cap radius of the M, in pixels for this canvas."""
    # The letterform is inset a little so the anti-aliased edge is not clipped by
    # the canvas border, which would show as a flat side on the rounded cap.
    pad = stroke / 2 + size * 0.012
    left = pad + size * 0.055
    right = size - pad - size * 0.055
    top = pad + size * 0.075
    bottom = size - pad - size * 0.075
    mid_x = (left + right) / 2
    vertex_y = top + (bottom - top) * 0.72
    return [(left, bottom), (left, top), (mid_x, vertex_y), (right, top), (right, bottom)]


def render(size, start=VIOLET, end=CYAN, angle=-52.0):
    """One M at the requested pixel size, with a transparent background."""
    # Supersample generously at small sizes: a 16-pixel icon has very few pixels to
    # smooth over, so the factor is higher there.
    factor = 8 if size <= 48 else 4
    work = size * factor

    stroke = max(MIN_STROKE_PIXELS * factor, work * STROKE_FRACTION)
    path = m_geometry(work, stroke)

    mask = Image.new("L", (work, work), 0)
    draw = ImageDraw.Draw(mask)
    # joint="curve" rounds every bend, so the vertex is an arc and the two legs meet
    # cleanly; the earlier segmented approach left a notch there.
    draw.line(path, fill=255, width=int(round(stroke)), joint="curve")
    radius = stroke / 2
    for x, y in (path[0], path[-1]):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)

    field = diagonal_field(work, start, end, angle)
    image = Image.fromarray(field.astype(np.uint8), "RGB").convert("RGBA")
    image.putalpha(mask)
    return image.resize((size, size), Image.LANCZOS)


def main():
    OUT.mkdir(exist_ok=True)
    for size in SIZES:
        image = render(size)
        path = OUT / f"magiccontrol-m-{size}.png"
        image.save(path)
        # Report the coverage so a stroke that vanishes at small sizes is caught
        # here rather than by eye on a taskbar.
        alpha = np.array(image)[:, :, 3]
        covered = float((alpha > 24).sum()) / (size * size)
        print(f"  {size:>3}px  stroke {max(MIN_STROKE_PIXELS, size * STROKE_FRACTION):5.1f}  "
              f"coverage {covered * 100:5.1f}%  {path.name}")

    # A proof sheet: the mark over the application dark surface, over a light
    # surface, and over a mid-tone, to expose any fringe from the anti-aliasing.
    sheet_width, cell = 3 * 260 + 4 * 20, 260
    sheet = Image.new("RGB", (sheet_width, cell + 40), (0x30, 0x30, 0x38))
    backdrops = [(0x0B, 0x0B, 0x15), (0xF2, 0xF2, 0xF7), (0x6C, 0x5C, 0xE7)]
    big = render(cell - 40)
    for index, colour in enumerate(backdrops):
        tile = Image.new("RGB", (cell, cell), colour)
        tile.paste(big, (20, 20), big)
        sheet.paste(tile, (20 + index * (cell + 20), 20))
    sheet.save(OUT / "mark-proof.png")
    print(f"wrote {OUT / 'mark-proof.png'}")


if __name__ == "__main__":
    main()
