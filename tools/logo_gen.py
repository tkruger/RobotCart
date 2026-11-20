"""Generate a 64x16 pixel-art SLICE 8738 logo for the cart's matrix display.

Cell codes:
  0      off
  1      orange (rind, dividers, hub, flat edge)
  2      white  (SLICE text)
  4..7   interior of slice segment 0..3 (left to right)  -> drawn black or lit
  8..11  pixel of digit 0..3 in "8738"                   -> cyan
"""
import math, sys
from PIL import Image, ImageDraw

W, H = 64, 16
grid = [[0] * W for _ in range(H)]

# --- Half orange slice: flat edge on top, arc below -----------------------
cx, cy, R = 15.5, 0.0, 15.6
for y in range(H):
    for x in range(32):
        px, py = x + 0.5, y + 0.5
        r = math.hypot(px - cx, py - cy)
        if r > R:
            continue
        ang = math.degrees(math.atan2(py - cy, px - cx))  # 0 = right, 180 = left
        if r > R - 1.6 or y == 0 or r < 2.6:
            grid[y][x] = 1            # rind / flat top edge / center hub
            continue
        # Dividers at 45, 90, 135 degrees -> 4 segments
        on_div = False
        for d in (45, 90, 135):
            # perpendicular distance from pixel to divider ray
            dist = abs(math.sin(math.radians(ang - d))) * r
            if dist < 0.75 and abs(ang - d) < 90:
                on_div = True
        if on_div:
            grid[y][x] = 1
            continue
        seg = 3 - min(3, int(ang // 45))   # leftmost segment (ang ~180) = 0
        grid[y][x] = 4 + seg

# --- 5x7 font for the text -------------------------------------------------
FONT = {
    'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    'L': ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    'I': [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    'C': [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    '8': [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    '7': ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    '3': [".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."],
}

def text(s, x0, y0, code_fn):
    for i, ch in enumerate(s):
        for dy, row in enumerate(FONT[ch]):
            for dx, c in enumerate(row):
                if c == '#':
                    grid[y0 + dy][x0 + i * 6 + dx] = code_fn(i)

# Right half is x = 33..63 (31 px). "SLICE" = 29 px, "8738" = 23 px.
text("SLICE", 34, 0, lambda i: 2)
text("8738", 37, 9, lambda i: 8 + i)

# --- Preview -----------------------------------------------------------------
ORANGE, WHITE, CYAN = (255, 127, 0), (255, 255, 255), (0, 220, 255)
def color(c, lit=None):
    if c == 1: return ORANGE
    if c == 2: return WHITE
    if 4 <= c <= 7: return (0, 90, 110) if lit == c - 4 else (0, 0, 0)
    if 8 <= c <= 11: return WHITE if lit == c - 8 else CYAN
    return (0, 0, 0)

S = 14
frames = [None, 0, 1, 2, 3]
img = Image.new("RGB", (W * S + 20, (H * S + 20) * len(frames)), (20, 20, 20))
d = ImageDraw.Draw(img)
for f, lit in enumerate(frames):
    oy = f * (H * S + 20) + 10
    for y in range(H):
        for x in range(W):
            col = color(grid[y][x], lit)
            if col == (0, 0, 0): col = (35, 35, 35)
            d.ellipse([10 + x * S + 2, oy + y * S + 2, 10 + (x + 1) * S - 2, oy + (y + 1) * S - 2], fill=col)
img.save(sys.argv[1])

# --- C array (hex digits per row keeps it readable-ish) ----------------------
rows = []
for y in range(H):
    rows.append('  "' + ''.join('0123456789AB'[c] for c in grid[y]) + '"')
print(",\n".join(rows))
