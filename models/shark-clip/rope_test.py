"""Can a rope of ROPE mm get from outside into the hook's pocket without touching the shark?
Free space for the rope's centre = plane minus (shark + hook) grown by the rope's radius."""
import sys, os
import trimesh
from shapely import affinity
from shapely.geometry import Point, box
from shapely.ops import unary_union
import tiger_hook as t

def shark_outline():
    base = trimesh.load(os.path.join(t.HERE, "tigershark_original.stl"))
    parts = []
    for z in (0.5, 5.0, 9.5):
        s = base.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1]); p2, T = s.to_2D()
        parts += [affinity.translate(p, T[0, 3], T[1, 3]) for p in p2.polygons_full]
    return unary_union(parts)

def check(rope=t.ROPE, margin=0.3):
    solid = unary_union([shark_outline(), t.hook_polygon()])
    free = box(-200, -120, 120, 120).difference(solid.buffer(rope / 2 + margin))
    r = (t.SLOT + t.WIRE) / 2
    pocket = Point(t.BEND_X, t.NOTCH[1] - r)                        # rope resting in the bend
    outside = Point(t.BEND_X - 10, t.NOTCH[1] - 45)
    comp = [g for g in getattr(free, "geoms", [free]) if g.contains(outside)][0]
    ok_pocket = comp.contains(pocket)
    # widest gap on the way in: the rope-centre corridor width right at the mouth
    mouth = Point(t.BEND_X + t.SLOT_DEPTH + 1, t.NOTCH[1] - r)
    print("rope %.1f mm (+%.1f clearance): pocket reachable from outside: %s" % (rope, margin, ok_pocket))
    print("   mouth: distance from the leg's end to the lower tail tip %.1f mm" % t.hook_polygon().distance(Point(-67.0, -14.8)))
    return ok_pocket

if __name__ == "__main__":
    check()
