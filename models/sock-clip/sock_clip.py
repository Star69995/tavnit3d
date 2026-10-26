"""Sock clip with a hook: one flat 2D profile extruded, printed lying down.

No supports, no assembly: the spring is two curved flexure strips that join the
two lever arms to a central post; the post carries the hook. Squeeze the two
handles and the jaws open. Units mm.

Usage: python3 sock_clip.py [--no-fem]   (writes sock_clip.stl, sock_clip.png)
"""
import math, sys
import numpy as np
from shapely.geometry import LineString, Polygon, box
from shapely.ops import unary_union
from shapely import affinity

# ---- parameters -----------------------------------------------------------
B = 20.0          # depth (extrusion = jaw width); prints lying down, so this is the print height
T = 3.2          # flexure strip thickness (8 perimeters of a 0.4 nozzle)
R = 7.0           # strip arch radius
W = 10.0          # lever arm width
PW = 6.0          # post width / hook wire thickness
GAP = 0.6         # gap between closed jaws
STROKE = 3.5      # handle travel until it touches the post head
HANDLE_TOP = 36.0
JAW_BOTTOM = -55.0
HOOK_Y = 46.0
HOOK_R = 8.0

pw = PW / 2
x_in = pw + 2 * R - 0.5            # arm inner face
x_arm = -(x_in + W / 2)            # arm centre line
x_jaw = -(GAP / 2 + W / 2)         # jaw pad centre line
head_hw = x_in - STROKE            # half width of the post head (stop)

arm_path = [(x_arm, HANDLE_TOP), (x_arm, -10), (x_jaw, -28), (x_jaw, JAW_BOTTOM)]

def arc_pts(cx, cy, r, a0, a1, n=40):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

# left strip: an arch from the post edge to the arm's inner face (both overlapped by 0.5)
_x0, _x1 = -pw + 0.5, -x_in - 0.0
strip_path = arc_pts((_x0 + _x1) / 2, 0, (_x0 - _x1) / 2, 0, 180)

def left_arm():
    a = LineString(arm_path).buffer(W / 2, cap_style=2, join_style=2)
    # teeth on the inner face (valleys cut in, peaks stay at the gap)
    xf = -GAP / 2
    cuts = []
    y = JAW_BOTTOM + 7
    while y < -31:
        cuts.append(Polygon([(xf + 0.01, y - 1.3), (xf + 0.01, y + 1.3), (xf - 1.2, y)]))
        y += 3.0
    # lead-in chamfer at the jaw tip
    cuts.append(Polygon([(xf + 0.01, JAW_BOTTOM - 0.01), (xf + 0.01, JAW_BOTTOM + 4), (xf - 4, JAW_BOTTOM - 0.01)]))
    a = a.difference(unary_union(cuts))
    # rounded finger rest on the handle top
    return a

def build():
    la = left_arm()
    ra = affinity.scale(la, xfact=-1, origin=(0, 0))
    post = box(-pw, -14, pw, HOOK_Y)
    head = box(-head_hw, 18, head_hw, 32)
    hook_path = [(0, HOOK_Y - 6), (0, HOOK_Y)] + arc_pts(HOOK_R, HOOK_Y, HOOK_R, 180, -55, 60)[0:]
    hook = LineString(hook_path).buffer(PW / 2, cap_style=1, join_style=1)
    strip_l = LineString(strip_path).buffer(T / 2, cap_style=2)
    strip_r = affinity.scale(strip_l, xfact=-1, origin=(0, 0))
    allp = unary_union([la, ra, post, head, hook, strip_l, strip_r])
    # soften the sharp crevices where the strips meet post and arms
    zone = box(-x_in - W, -6, x_in + W, 14)
    closed = allp.buffer(1.5, join_style=1).buffer(-1.5, join_style=1)
    allp = unary_union([allp, closed.intersection(zone)])
    allp = allp.buffer(0.2, join_style=1).buffer(-0.2, join_style=1)   # drop slivers
    return allp

# ---- tiny beam FEM to check force and strain ---------------------------------
E = 1800.0  # N/mm2, PETG-ish (printed, along layers)

def fem():
    nodes, elems, key = [], [], {}
    def nid(p):
        k = (round(p[0], 4), round(p[1], 4))
        if k not in key:
            key[k] = len(nodes); nodes.append(k)
        return key[k]
    def chain(pts, w):
        ids = [nid(p) for p in pts]
        for i, j in zip(ids[:-1], ids[1:]):
            elems.append((i, j, w))
        return ids
    chain([(0, -14), (0, 0), (0, 25), (0, 32)], 8.0)
    out = {}
    for s in (-1, 1):
        sp = [(-s * x, y) for x, y in strip_path]       # s=-1: left (x<0)
        sp = [(s * abs(x), y) for x, y in strip_path]
        ids = chain(sp, T)
        chain([(0, 0), sp[0]], 6.0)
        chain([sp[-1], (s * abs(x_arm), 0)], W)
        top = chain([(s * abs(x_arm), 0), (s * abs(x_arm), 25)], W)
        low = chain([(s * abs(x_arm), 0), (s * abs(x_arm), -10), (s * abs(x_jaw), -28), (s * abs(x_jaw), -40)], W)
        out[s] = dict(top=top[-1], jaw=low[-1])
    return nodes, elems, out

def solve_fem():
    nodes, elems, out = fem()
    n = len(nodes)
    K = np.zeros((3 * n, 3 * n))
    info = []
    for (i, j, w) in elems:
        if i == j: continue
        xi, yi = nodes[i]; xj, yj = nodes[j]
        L = math.hypot(xj - xi, yj - yi)
        if L < 1e-9: continue
        cth, sth = (xj - xi) / L, (yj - yi) / L
        EA = E * B * w; EI = E * B * w ** 3 / 12
        kl = np.zeros((6, 6))
        a = EA / L; b = 12 * EI / L ** 3; c_ = 6 * EI / L ** 2; d = 4 * EI / L; e = 2 * EI / L
        kl[0, 0] = kl[3, 3] = a; kl[0, 3] = kl[3, 0] = -a
        kl[1, 1] = kl[4, 4] = b; kl[1, 4] = kl[4, 1] = -b
        kl[1, 2] = kl[2, 1] = kl[1, 5] = kl[5, 1] = c_
        kl[4, 2] = kl[2, 4] = kl[4, 5] = kl[5, 4] = -c_
        kl[2, 2] = kl[5, 5] = d; kl[2, 5] = kl[5, 2] = e
        Tm = np.zeros((6, 6))
        rot = np.array([[cth, sth, 0], [-sth, cth, 0], [0, 0, 1]])
        Tm[:3, :3] = rot; Tm[3:, 3:] = rot
        kg = Tm.T @ kl @ Tm
        idx = [3 * i, 3 * i + 1, 3 * i + 2, 3 * j, 3 * j + 1, 3 * j + 2]
        K[np.ix_(idx, idx)] += kg
        info.append((i, j, w, Tm, kl, idx, EI, L))
    # clamp the post
    pivot = 1
    free = [d for d in range(3 * n) if d // 3 not in (0, 1, 2, 3)]  # post nodes come first
    def run(forces):
        f = np.zeros(3 * n)
        for dof, val in forces: f[dof] += val
        u = np.zeros(3 * n)
        u[free] = np.linalg.solve(K[np.ix_(free, free)], f[free])
        return u
    def max_strain(u):
        m = 0
        for (i, j, w, Tm, kl, idx, EI, L) in info:
            if w != T: continue
            fl = kl @ (Tm @ u[idx])
            M = max(abs(fl[2]), abs(fl[5]))
            m = max(m, M / EI * w / 2)
        return m
    return nodes, out, run, max_strain

def report():
    nodes, out, run, max_strain = solve_fem()
    left, right = out[-1], out[1]
    # unit inward force at the jaws (to get jaw stiffness), left jaw +x, right jaw -x
    u = run([(3 * left['jaw'], 1.0), (3 * right['jaw'], -1.0)])
    d_jaw = u[3 * left['jaw']]
    k_jaw = 1.0 / d_jaw     # N/mm at each jaw (two jaws pushed together)
    # handle squeeze: force at handle tops, find travel
    u2 = run([(3 * left['top'], 1.0), (3 * right['top'], -1.0)])
    d_handle = u2[3 * left['top']]
    scale = STROKE / d_handle
    jaw_open = -u2[3 * left['jaw']] * scale
    eps_stop = max_strain(u2) * scale
    print(f"jaw stiffness per side : {k_jaw:.2f} N/mm")
    print(f"squeeze force at stop  : {scale:.1f} N per handle")
    print(f"jaw opening at stop    : {2 * jaw_open:.1f} mm total ({jaw_open:.1f} per side)")
    print(f"max strip strain       : {eps_stop * 100:.2f} % at full squeeze")
    for open_total in (4, 8):
        print(f"  clamping force with {open_total} mm of socks: {k_jaw * open_total / 2:.1f} N per jaw")

if __name__ == "__main__":
    shape = build()
    assert shape.geom_type == "Polygon", shape.geom_type
    print("single piece:", shape.is_valid, " holes:", len(shape.interiors), " bounds:", [round(v, 1) for v in shape.bounds])
    if "--no-fem" not in sys.argv:
        report()
    import trimesh
    mesh = trimesh.creation.extrude_polygon(shape, B)
    here = __file__.rsplit("/", 1)[0]
    mesh.export(here + "/sock_clip.stl")
    print("watertight:", mesh.is_watertight, " volume cm3: %.1f" % (mesh.volume / 1000), " extents:", mesh.extents.round(1))
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(5, 7))
    ax.fill(*shape.exterior.xy, color="#4a90d9", ec="k")
    for h in shape.interiors: ax.fill(*h.xy, color="white", ec="k")
    ax.set_aspect("equal"); ax.axis("off")
    fig.savefig(here + "/sock_clip.png", dpi=110, bbox_inches="tight")
