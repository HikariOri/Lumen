#!/usr/bin/env python3
"""生成与 `asset/lumenmesh_format.hpp` v1 一致的三角面 .lumenmesh（列主序 mat4）。"""
from __future__ import annotations

import struct
from pathlib import Path

# PbrInterleavedVertex: vec3 pos, vec3 n, vec2 uv, vec4 tangent = 48 bytes
STRIDE = 48
MAGIC = b"LUMMSH1\x00"


def f32_le(x: float) -> bytes:
    return struct.pack("<f", x)


def write_triangle(out: Path) -> None:
    # 与 sandbox triangle.obj 同形：XY 平面，法线 +Z
    verts = bytearray()
    tri = [
        (0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0),
        (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0),
        (0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0),
    ]
    for px, py, pz, nx, ny, nz, u, v, tx, ty, tz, tw in tri:
        verts += f32_le(px) + f32_le(py) + f32_le(pz)
        verts += f32_le(nx) + f32_le(ny) + f32_le(nz)
        verts += f32_le(u) + f32_le(v)
        verts += f32_le(tx) + f32_le(ty) + f32_le(tz) + f32_le(tw)

    indices = struct.pack("<III", 0, 1, 2)

    prim = struct.pack("<III", 0, 3, 0)  # first_index, index_count, material_index

    name = b"root"
    # glm::mat4 identity column-major flat
    ident = struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    node = struct.pack("<ii", -1, 0) + ident + struct.pack("<I", len(name)) + name

    hdr = (
        MAGIC
        + struct.pack("<I", 1)  # version
        + struct.pack("<I", 3)  # vertex_count
        + struct.pack("<I", 3)  # index_count
        + struct.pack("<I", 1)  # primitive_count
        + struct.pack("<I", 1)  # node_count
        + struct.pack("<I", STRIDE)
        + struct.pack("<I", 0)
    )
    data = hdr + bytes(verts) + indices + prim + node
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"Wrote {out} ({len(data)} bytes)")


if __name__ == "__main__":
    repo = Path(__file__).resolve().parents[1]
    write_triangle(repo / "assets" / "meshes" / "triangle.lumenmesh")
