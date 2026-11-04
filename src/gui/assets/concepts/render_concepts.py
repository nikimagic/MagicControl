"""MagicControl icon concepts, drawn as vectors with Pillow.

The existing icon is a gradient M on a dark rounded square. These are sketches of
alternative directions, in the application palette from Theme.h: violet #6C5CE7,
cyan #4FC3F7, page background #0B0B15, card #17162B.

Two techniques matter here.

Geometry and colour are separated. Pillow cannot stroke a path with a gradient,
and the obvious workaround - drawing the path as a series of short segments, each
in its own colour - leaves visible banding and does not join the corners. So each
shape is first drawn flat into a greyscale mask, and the gradient is a separate
image composited through that mask. The geometry is then a single clean path.

The gradient runs across the icon diagonally rather than along the path. A ramp
along the path cannot give both legs of the M their own colour without the two
legs crossing at the vertex, which is what made the first attempt look broken. A
diagonal field gives the left side violet and the right side cyan, and the vertex
takes the middle naturally.

Everything is drawn at 4x and reduced with Lanczos, so the result is smooth and
re-renderable at any size. No image generator is involved.

Usage:
    python src/gui/assets/concepts/render_concepts.py
"""
from pathlib import Path
import math
import numpy as np
from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent
S = 512          # final size
SS = 4           # supersampling factor
N = S * SS

VIOLET = (0x6C, 0x5C, 0xE7)
CYAN = (0x4F, 0xC3, 0xF7)
DARK = (0x0B, 0x0B, 0x15)
CARD = (0x17, 0x16, 0x2B)
BACKGROUND_TOP = (0x1A, 0x18, 0x33)
WHITE = (0xF4, 0xF3, 0xFF)


def mix(a, b, t):
    """Linear blend between two colours."""
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def background(size, top, bottom, radius):
    """A vertical gradient clipped to a rounded square."""
    column = np.linspace(0.0, 1.0, size, dtype=np.float32)[:, None]
    field = np.zeros((size, size, 3), dtype=np.float32)
    for channel in range(3):
        field[:, :, channel] = top[channel] + (bottom[channel] - top[channel]) * column

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)

    image = Image.fromarray(field.astype(np.uint8), "RGB").convert("RGBA")
    image.putalpha(mask)
    return image


def diagonal_gradient(size, start, end, angle_degrees=-45.0):
    """A linear gradient across the canvas at the given angle."""
    angle = math.radians(angle_degrees)
    # Project every pixel onto the gradient axis, then normalise to 0..1.
    axis_x, axis_y = math.cos(angle), math.sin(angle)
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    projection = xs * axis_x + ys * axis_y
    low, high = projection.min(), projection.max()
    t = (projection - low) / max(1e-6, high - low)

    field = np.zeros((size, size, 3), dtype=np.float32)
    for channel in range(3):
        field[:, :, channel] = start[channel] + (end[channel] - start[channel]) * t
    return Image.fromarray(field.astype(np.uint8), "RGB").convert("RGBA")


def blit_gradient(image, mask, start, end, angle=-45.0):
    """Paints a gradient onto image wherever mask is set."""
    gradient = diagonal_gradient(image.width, start, end, angle)
    gradient.putalpha(mask)
    image.alpha_composite(gradient)


def m_mask(size, width_fraction=0.135):
    """Silhouette of the M, drawn once as a single path so the corners join."""
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    w = int(size * width_fraction)
    left, right = size * 0.20, size * 0.80
    top, bottom = size * 0.28, size * 0.72
    mid = size * 0.51
    path = [(left, bottom), (left, top), (mid, size * 0.63), (right, top), (right, bottom)]
    # joint="curve" rounds every bend, so the vertex is a clean arc rather than a
    # notch, and the stroke ends are round like the existing icon.
    draw.line(path, fill=255, width=w, joint="curve")
    radius = w // 2
    for x, y in (path[0], path[-1]):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)
    return mask


def blade_polygon(centre, inner, outer, start_degrees, span_degrees, sweep=34.0, steps=22):
    """One fan blade, built from four arcs.

    Outline: the leading edge sweeps outward from the hub to the rim, the rim
    curls across the tip, the trailing edge comes back to the hub, and the root
    closes the shape along a small arc. The rim is the widest part, which is what
    distinguishes a blade from a spoke, and the tip is swept forward so the wheel
    reads as turning.

    An earlier version tapered the blade to a point at the rim. That made every
    blade a thin petal and the whole icon looked like a pinwheel.
    """
    cx, cy = centre
    hub_radius = inner * 1.06
    points = []

    # 1. Leading edge: hub to rim.
    for step in range(steps + 1):
        t = step / steps
        angle = math.radians(start_degrees + span_degrees * t)
        radius = inner + (outer - inner) * t
        points.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))

    # 2. Tip: a curl across the rim, which gives the blade its width there.
    tip_span = sweep * 0.55
    for step in range(steps + 1):
        t = step / steps
        angle = math.radians(start_degrees + span_degrees + tip_span * t)
        radius = outer - (outer - inner) * 0.16 * t
        points.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))

    # 3. Trailing edge: rim back to the hub, swept further than the leading edge so
    #    the blade is curved rather than symmetric.
    for step in range(steps + 1):
        t = step / steps
        angle = math.radians(start_degrees + span_degrees + tip_span - sweep * t)
        radius = inner + (outer - inner) * (1.0 - t) * 0.86
        points.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))

    # 4. Root: a short arc along the hub.
    for step in range(steps + 1):
        t = step / steps
        angle = math.radians(start_degrees - sweep * 0.12 * (1.0 - t))
        points.append((cx + hub_radius * math.cos(angle), cy + hub_radius * math.sin(angle)))

    return points


def fan_mask(size, centre, outer, blades, sweep=34.0, hub_fraction=0.30):
    """Face-on fan: blades around a hub, as a mask."""
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    inner = outer * hub_fraction
    spin = -90.0
    span = 360.0 / blades * 0.78
    for index in range(blades):
        start = spin + index * (360.0 / blades)
        draw.polygon(blade_polygon(centre, inner, outer, start, span, sweep), fill=255)
    r = inner * 0.62
    cx, cy = centre
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    return mask


def concept_m_fan():
    """The M with a fan in the vertex notch: brand plus function."""
    image = background(N, BACKGROUND_TOP, DARK, int(N * 0.22))

    blit_gradient(image, m_mask(N), VIOLET, CYAN, angle=-52.0)

    # The fan occupies the notch under the vertex, where the M has empty space.
    mask = fan_mask(N, (N * 0.5, N * 0.735), N * 0.155, 5, sweep=36.0)
    image.paste(Image.new("RGBA", (N, N), WHITE + (255,)), (0, 0), mask)
    hub = Image.new("L", (N, N), 0)
    r = N * 0.155 * 0.30 * 0.5
    ImageDraw.Draw(hub).ellipse((N * 0.5 - r, N * 0.735 - r, N * 0.5 + r, N * 0.735 + r), fill=255)
    image.paste(Image.new("RGBA", (N, N), BACKGROUND_TOP + (255,)), (0, 0), hub)
    return image


def concept_fan_gradient():
    """A fan alone, its blades running violet to cyan around the wheel."""
    image = background(N, BACKGROUND_TOP, DARK, int(N * 0.22))
    blit_gradient(image, fan_mask(N, (N * 0.5, N * 0.5), N * 0.335, 7, sweep=30.0),
                  VIOLET, CYAN, angle=-90.0)
    hub = Image.new("L", (N, N), 0)
    r = N * 0.335 * 0.30 * 0.62
    ImageDraw.Draw(hub).ellipse((N * 0.5 - r, N * 0.5 - r, N * 0.5 + r, N * 0.5 + r), fill=255)
    image.paste(Image.new("RGBA", (N, N), DARK + (255,)), (0, 0), hub)
    return image


def concept_thermo_arc():
    """A temperature arc around a fan: control, not merely cooling."""
    image = background(N, BACKGROUND_TOP, DARK, int(N * 0.22))
    box = (N * 0.15, N * 0.15, N * 0.85, N * 0.85)
    draw = ImageDraw.Draw(image)
    width = int(N * 0.045)
    # The unfilled part of the dial is drawn on the icon itself, the filled part
    # through a mask so it can carry the gradient.
    draw.arc(box, start=133, end=407, fill=CARD, width=width)

    arc = Image.new("L", (N, N), 0)
    ImageDraw.Draw(arc).arc(box, start=133, end=340, fill=255, width=width)
    blit_gradient(image, arc, VIOLET, CYAN, angle=-45.0)

    mask = fan_mask(N, (N * 0.5, N * 0.5), N * 0.185, 5, sweep=34.0)
    image.paste(Image.new("RGBA", (N, N), WHITE + (255,)), (0, 0), mask)
    return image


def concept_gauge():
    """The fan curve itself: a scale with a needle."""
    image = background(N, BACKGROUND_TOP, DARK, int(N * 0.22))
    cx, cy = N * 0.5, N * 0.60
    radius = N * 0.31
    ticks = Image.new("L", (N, N), 0)
    pen = ImageDraw.Draw(ticks)
    for index in range(13):
        angle = math.radians(180 + index * 15)
        inner, outer = radius * 0.70, radius
        pen.line([(cx + inner * math.cos(angle), cy + inner * math.sin(angle)),
                  (cx + outer * math.cos(angle), cy + outer * math.sin(angle))],
                 fill=255, width=int(N * 0.030))
    blit_gradient(image, ticks, VIOLET, CYAN, angle=0.0)

    draw = ImageDraw.Draw(image)
    angle = math.radians(228)
    draw.line([(cx, cy), (cx + radius * 0.64 * math.cos(angle), cy + radius * 0.64 * math.sin(angle))],
              fill=WHITE, width=int(N * 0.036))
    r = N * 0.052
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=WHITE)
    return image


def concept_m_flat():
    """The M alone, as a control sample."""
    image = background(N, BACKGROUND_TOP, DARK, int(N * 0.22))
    blit_gradient(image, m_mask(N), VIOLET, CYAN, angle=-52.0)
    return image


def contact_sheet(images, labels, path):
    """One image holding every concept, to compare them side by side."""
    cell, pad, columns = 300, 26, 3
    rows = math.ceil(len(images) / columns)
    sheet = Image.new("RGB", (columns * cell + (columns + 1) * pad,
                              rows * cell + rows * (pad + 30) + pad), (0x08, 0x08, 0x10))
    draw = ImageDraw.Draw(sheet)
    for index, (image, label) in enumerate(zip(images, labels)):
        column, row = index % columns, index // columns
        x = pad + column * (cell + pad)
        y = pad + row * (cell + pad + 30)
        sheet.paste(image.resize((cell, cell), Image.LANCZOS), (x, y))
        draw.text((x, y + cell + 9), label, fill=(0xC8, 0xC6, 0xE4))
    sheet.save(path)
    print(f"wrote {path}")


def main():
    concepts = [
        ("A - M with fan", concept_m_fan()),
        ("B - fan, gradient", concept_fan_gradient()),
        ("C - temperature arc", concept_thermo_arc()),
        ("D - speed scale", concept_gauge()),
        ("E - M only", concept_m_flat()),
    ]
    images, labels = [], []
    for label, image in concepts:
        key = label.split()[0].lower()
        final = image.resize((S, S), Image.LANCZOS)
        path = OUT / f"concept-{key}.png"
        final.save(path)
        print(f"wrote {path}")
        images.append(final)
        labels.append(label)

    contact_sheet(images, labels, OUT / "concepts-sheet.png")

    # How each looks at the size Windows actually draws in the taskbar.
    strip = Image.new("RGB", (len(images) * 84 + 20, 104), (0x08, 0x08, 0x10))
    for index, image in enumerate(images):
        strip.paste(image.resize((64, 64), Image.LANCZOS), (10 + index * 84, 20))
    strip.save(OUT / "concepts-64px.png")
    print(f"wrote {OUT / 'concepts-64px.png'}")


if __name__ == "__main__":
    main()
