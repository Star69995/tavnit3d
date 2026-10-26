"""Adds a long rope hook to the tail of the tiger shark clip (tigershark_original.stl).

The shark is left exactly as it is (flat, 10 mm thick, prints lying down, no supports).
A flat J-hook leaves the tail's notch: hang it tail-up and the shark dives nose-down with
the socks in its mouth. The rope slot is ROPE + 3.2 mm wide and SLOT_DEPTH deep, so a
smooth 4 mm rope stays put.

Usage: python3 -I tiger_hook.py -> tigershark_hook.stl, tigershark_hook.3mf, tigershark_hook.png
"""
import math, os
import trimesh
from shapely import affinity
from shapely.geometry import LineString
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
THICK = 10.0
ROPE = 4.0              # rope thickness
SLOT = ROPE + 3.2       # inner width of the hook
WIRE = 5.0              # hook width in the plane
NOTCH = (-58.0, -2.3)   # root: inside the solid tail, just behind the fork notch (window is 4 mm further in)
BEND_X = -96.0          # where the stem bends (far enough out that the mouth is clear of the tail lobes)
SLOT_DEPTH = 12.0       # how far the return leg runs back along the stem

def arc(cx, cy, r, a0, a1, n=48):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

def hook_polygon():
    r = (SLOT + WIRE) / 2
    x0, y0 = NOTCH
    path = [(x0, y0)] + arc(BEND_X, y0 - r, r, 90, 270) + [(BEND_X + SLOT_DEPTH, y0 - 2 * r)]
    return LineString(path).buffer(WIRE / 2, cap_style=1, join_style=1)

if __name__ == "__main__":
    base = trimesh.load(os.path.join(HERE, "tigershark_original.stl"))
    hp = hook_polygon()
    out = trimesh.boolean.union([base, trimesh.creation.extrude_polygon(hp, THICK)], engine="manifold")
    print("base extents", base.extents.round(1), "-> with hook", out.extents.round(1))
    print("watertight:", out.is_watertight, "| bodies:", out.body_count, "(original %d)" % base.body_count)
    from shapely.geometry import Point
    print("hook to upper tail tip %.1f mm, lower tail tip %.1f mm; rope slot %.1f mm for a %.1f mm rope" %
          (hp.distance(Point(-74.7, 19.3)), hp.distance(Point(-67.0, -14.8)), SLOT, ROPE))
    out.export(os.path.join(HERE, "tigershark_hook.stl"))
    out.export(os.path.join(HERE, "tigershark_hook.3mf"))
    sec = out.section(plane_origin=[0, 0, 5], plane_normal=[0, 0, 1]); p2, T = sec.to_2D()
    fig, ax = plt.subplots(figsize=(14, 6))
    for p in p2.polygons_full:
        p = affinity.translate(p, T[0, 3], T[1, 3])
        ax.fill(*p.exterior.xy, color="#7ab", ec="k", lw=0.7)
        for h in p.interiors: ax.fill(*h.xy, color="w", ec="k", lw=0.7)
    ax.set_aspect("equal"); ax.axis("off")
    fig.savefig(os.path.join(HERE, "tigershark_hook.png"), dpi=100, bbox_inches="tight")
