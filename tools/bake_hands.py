"""Original MIT-licensed Studio glove. Offline authoring only; requires NumPy.

Smooth implicit union + marching tetrahedra produces a closed, connected surface.
Coordinates: +Y back of palm, +Z wrist, -X thumb. Runtime maps this to OpenXR grip.
No downloaded mesh, texture, model service or proprietary authoring dependency.
"""
from pathlib import Path
import struct
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SEGMENTS = [(np.array(a), np.array(b), r) for a, b, r in [
    ((0, 0, .026), (0, 0, .062), .019),
    ((-.026, -.002, .018), (-.045, -.010, -.004), .012),
    ((-.045, -.010, -.004), (-.040, -.030, -.028), .010),
    ((-.040, -.030, -.028), (-.025, -.035, -.039), .008),
]]
for i in range(4):
    x, z, r = -.027+i*.018, -.026+(i == 3)*.008, .0092-i*.0006
    a, b, c, d = (x, 0, z), (x, -.005, z-.027), (x, -.026, z-.034), (x, -.043, z-.014)
    for p, q, radius in [(a, b, r), (b, c, r*.9), (c, d, r*.76)]:
        SEGMENTS.append((np.array(p), np.array(q), radius))

def field(p):
    radii = np.array([.036, .0145, .039])
    q = p - np.array([0, .001, .004])
    k0, k1 = np.linalg.norm(q/radii, axis=-1), np.linalg.norm(q/(radii*radii), axis=-1)
    value = k0*(k0-1)/np.maximum(k1, .001)
    for a, b, r in SEGMENTS:
        v = b-a
        t = np.clip(np.sum((p-a)*v, axis=-1)/np.dot(v, v), 0, 1)
        distance = np.linalg.norm(p-a-t[..., None]*v, axis=-1)-r
        k = .005
        h = np.maximum(k-np.abs(value-distance), 0)/k
        value = np.minimum(value, distance)-h*h*k*.25
    return np.maximum(value, p[..., 2]-.066)

axes = [np.arange(-.063, .049, .004), np.arange(-.058, .030, .004), np.arange(-.083, .073, .004)]
grid = np.stack(np.meshgrid(*axes, indexing="ij"), axis=-1)
values = field(grid)
offsets = np.array([(0,0,0), (1,0,0), (1,1,0), (0,1,0), (0,0,1), (1,0,1), (1,1,1), (0,1,1)])
tetrahedra = [(0,5,1,6), (0,1,2,6), (0,2,3,6), (0,3,7,6), (0,7,4,6), (0,4,5,6)]
shape = np.array(values.shape)-1
corner_values = np.stack([values[o[0]:o[0]+shape[0], o[1]:o[1]+shape[1], o[2]:o[2]+shape[2]] for o in offsets], axis=-1)
cells = np.argwhere((corner_values.min(axis=-1)<0) & (corner_values.max(axis=-1)>=0))
triangles = []
for cell in cells:
    corners = cell+offsets
    p, v = grid[tuple(corners.T)], values[tuple(corners.T)]
    for tet in tetrahedra:
        inside = [i for i in tet if v[i]<0]
        outside = [i for i in tet if v[i]>=0]
        if not inside or not outside:
            continue
        def edge(a,b):
            return p[a]+(p[b]-p[a])*(v[a]/(v[a]-v[b]))
        if len(inside)==1:
            triangles.extend(edge(inside[0], b) for b in outside)
        elif len(outside)==1:
            triangles.extend(edge(outside[0], b) for b in inside)
        else:
            a,b = inside
            c,d = outside
            q = [edge(a,c),edge(a,d),edge(b,c),edge(b,d)]
            triangles.extend(q[i] for i in [0,1,2,1,3,2])
positions = np.array(triangles)
epsilon = .0001
normals = np.stack([field(positions+np.eye(3)[i]*epsilon)-field(positions-np.eye(3)[i]*epsilon) for i in range(3)], axis=-1)
normals /= np.maximum(np.linalg.norm(normals,axis=-1,keepdims=True),1e-8)
vertices = np.concatenate([positions,normals],axis=-1).astype("<f4")
out = ROOT / "assets/hand_studio.bin"
out.write_bytes(b"CYRH"+struct.pack("<II",1,len(vertices))+vertices.tobytes())
print(f"Studio glove: {len(vertices)//3} triangles, {out.stat().st_size} bytes")
