import json
import math
from pathlib import Path

import bpy


OUT = Path(r"<PROJECT_ROOT>\Docs\TextureAnalysis\DaniyaChannels\blend_vertex_mask_compare.json")


def safe_name(name):
    try:
        return name.encode("latin1").decode("utf-8")
    except Exception:
        return name


def get_attr_values(mesh, attr_name):
    attr = mesh.attributes.get(attr_name)
    if not attr:
        return None
    values = []
    for item in attr.data:
        if hasattr(item, "color"):
            c = item.color
            values.append((float(c[0]), float(c[1]), float(c[2]), float(c[3])))
        elif hasattr(item, "value"):
            values.append(float(item.value))
    return values


def pearson(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return None
    a = a[:n]
    b = b[:n]
    ma = sum(a) / n
    mb = sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va <= 1e-12 or vb <= 1e-12:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def mse(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return None
    return sum((a[i] - b[i]) ** 2 for i in range(n)) / n


def main():
    rows = []
    for obj in bpy.data.objects:
        mesh = getattr(obj, "data", None)
        if not mesh or not hasattr(mesh, "attributes"):
            continue

        col0 = get_attr_values(mesh, "COL0")
        exclude = get_attr_values(mesh, "描边排除")
        if not col0 and not exclude:
            continue

        row = {
            "object": safe_name(obj.name),
            "attributes": [safe_name(a.name) for a in mesh.attributes],
            "has_COL0": bool(col0),
            "has_描边排除": bool(exclude),
        }

        if col0:
            cols = list(zip(*col0))
            row["col0_channel_pair_correlations"] = {
                "R_G": pearson(cols[0], cols[1]),
                "R_B": pearson(cols[0], cols[2]),
                "R_A": pearson(cols[0], cols[3]),
                "G_B": pearson(cols[1], cols[2]),
                "G_A": pearson(cols[1], cols[3]),
                "B_A": pearson(cols[2], cols[3]),
            }

        if col0 and exclude and len(col0) == len(exclude):
            col_cols = list(zip(*col0))
            ex_cols = list(zip(*exclude))
            comparisons = {}
            for ci, cname in enumerate("RGBA"):
                for ei, ename in enumerate("RGBA"):
                    comparisons[f"COL0_{cname}_vs_Exclude_{ename}"] = {
                        "corr": pearson(col_cols[ci], ex_cols[ei]),
                        "mse": mse(col_cols[ci], ex_cols[ei]),
                    }
            row["col0_vs_exclude"] = comparisons

        rows.append(row)

    OUT.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"WROTE {OUT}")


if __name__ == "__main__":
    main()
