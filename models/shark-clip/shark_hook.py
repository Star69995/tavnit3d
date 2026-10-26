"""Adds a long rope hook to the tail of the Printables shark clip (shark_original.3mf).

The original shark is left exactly as it is (flat, 6 mm thick, prints lying down, no
supports). The hook is a flat J that reaches out of the tail notch: hang it with the
tail up and the shark dives nose-down with the socks in its mouth. The rope slot is
ROPE + 3.2 mm wide and SLOT_DEPTH deep, so a smooth 4 mm rope stays put.

Usage: python3 -I shark_hook.py  -> shark_clip_hook.stl, shark_clip_hook.3mf, shark_clip_hook.png
"""
import math, os
import numpy as np
import trimesh
from shapely.geometry import LineString, Polygon, box
from shapely.ops import unary_union
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROPE = 4.0            # rope thickness
SLOT = ROPE + 3.2     # inner width of the hook
WIRE = 4.5            # hook width in the plane
TAIL_Y = 5.2          # height of the tail notch
STEM_X0, STEM_X1 = 57.0, 84.0     # stem from the tail notch to the bend
FILL_X = 52.5         # the tail window is filled from here to the right
SLOT_DEPTH = 12.0     # how far the return leg runs back along the stem

def arc(cx, cy, r, a0, a1, n=48):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

def hook_polygon():
    r = (SLOT + WIRE) / 2
    cy = TAIL_Y + r
    path = [(STEM_X0, TAIL_Y)] + arc(STEM_X1, cy, r, -90, 90) + [(STEM_X1 - SLOT_DEPTH, TAIL_Y + 2 * r)]
    return LineString(path).buffer(WIRE / 2, cap_style=1, join_style=1)

def load_base():
    m = trimesh.load(os.path.join(HERE, "shark_original.3mf")).to_geometry()
    m.apply_translation([-115.37, -125.19, 0])      # plate position -> the drawing's own origin
    return m

if __name__ == "__main__":
    base = load_base()
    hp = hook_polygon()
    # the tail's wall at the notch is only a sliver, so the right part of the tail's window is filled
    # to give the hook a solid root (the spring junction further left is untouched)
    sec = base.section(plane_origin=[0, 0, 3], plane_normal=[0, 0, 1]); p2, _ = sec.to_2D()
    window = [Polygon(r) for p in p2.polygons_full for r in p.interiors if max(c[0] for c in r.coords) > 50][0]
    root = window.intersection(box(FILL_X, -30, 80, 30)).buffer(0.05)
    hp = unary_union([hp, root])
    hook = trimesh.creation.extrude_polygon(hp, 6.0)
    out = trimesh.boolean.union([base, hook], engine="manifold")
    print("base extents", base.extents.round(1), "-> with hook", out.extents.round(1))
    print("watertight:", out.is_watertight, "| bodies:", out.body_count, "| volume %.2f cm3" % (out.volume / 1000))
    from shapely.geometry import Point
    tip_gap = hp.distance(Point(63.3, 20.8)); low_gap = hp.distance(Point(62.0, -9.5))
    print("hook to tail tips: %.1f mm (upper), %.1f mm (lower); rope slot %.1f mm for a %.1f mm rope" % (tip_gap, low_gap, SLOT, ROPE))
    out.export(os.path.join(HERE, "shark_clip_hook.stl"))
    out.export(os.path.join(HERE, "shark_clip_hook.3mf"))
    sec = out.section(plane_origin=[0, 0, 3], plane_normal=[0, 0, 1]); p2, _ = sec.to_2D()
    fig, ax = plt.subplots(figsize=(14, 6))
    for p in p2.polygons_full:
        ax.fill(*p.exterior.xy, color="#7ab", ec="k", lw=0.7)
        for h in p.interiors: ax.fill(*h.xy, color="w", ec="k", lw=0.7)
    ax.plot([60, 90], [TAIL_Y + 5.8 + 0.0] * 2, alpha=0)
    ax.set_aspect("equal"); ax.axis("off")
    fig.savefig(os.path.join(HERE, "shark_clip_hook.png"), dpi=100, bbox_inches="tight")
