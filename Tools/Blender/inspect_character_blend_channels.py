import json
import math
from collections import Counter, defaultdict
from pathlib import Path

import bpy


OUT = Path(r"<PROJECT_ROOT>\Docs\TextureAnalysis\DaniyaChannels\blend_vertex_channel_report.json")


def safe_name(name):
    try:
        return name.encode("latin1").decode("utf-8")
    except Exception:
        return name


def socket_label(socket):
    node = socket.node
    return f"{safe_name(node.name)}.{safe_name(socket.name)}"


def linked_from(socket):
    return [socket_label(link.from_socket) for link in socket.links]


def linked_to(socket):
    return [socket_label(link.to_socket) for link in socket.links]


def walk_downstream(start_socket, max_depth=8):
    seen = set()
    results = []
    stack = [(start_socket, 0)]
    while stack:
        sock, depth = stack.pop()
        if depth > max_depth:
            continue
        for link in sock.links:
            dst = link.to_socket
            key = (id(dst.node), dst.identifier)
            if key in seen:
                continue
            seen.add(key)
            node = dst.node
            item = {
                "depth": depth + 1,
                "to": socket_label(dst),
                "node_type": node.bl_idname,
            }
            if node.bl_idname == "ShaderNodeGroup" and getattr(node, "node_tree", None):
                item["group"] = safe_name(node.node_tree.name)
            if node.bl_idname in {"ShaderNodeOutputMaterial", "ShaderNodeOutputAOV"}:
                item["output"] = safe_name(getattr(node, "aov_name", node.name))
            results.append(item)
            for out in node.outputs:
                if out.links:
                    stack.append((out, depth + 1))
    return results


def summarize_values(values):
    if not values:
        return None
    n = len(values)
    cols = list(zip(*values))
    summary = []
    for ch in cols:
        mn = min(ch)
        mx = max(ch)
        mean = sum(ch) / n
        var = sum((x - mean) ** 2 for x in ch) / n
        hist = Counter(round(x, 4) for x in ch)
        common = hist.most_common(8)
        summary.append(
            {
                "min": mn,
                "max": mx,
                "mean": mean,
                "std": math.sqrt(var),
                "unique_rounded": len(hist),
                "common": common,
            }
        )
    return summary


def collect_color_attributes():
    objects = []
    for obj in bpy.data.objects:
        mesh = getattr(obj, "data", None)
        if not mesh or not hasattr(mesh, "color_attributes"):
            continue
        entry = {"object": safe_name(obj.name), "type": obj.type, "color_attributes": []}
        for attr in mesh.color_attributes:
            values = []
            for item in attr.data:
                c = item.color
                values.append((float(c[0]), float(c[1]), float(c[2]), float(c[3])))
            entry["color_attributes"].append(
                {
                    "name": safe_name(attr.name),
                    "domain": attr.domain,
                    "data_type": attr.data_type,
                    "count": len(attr.data),
                    "summary_rgba": summarize_values(values),
                }
            )
        if entry["color_attributes"]:
            objects.append(entry)
    return objects


def collect_geometry_attributes():
    objects = []
    for obj in bpy.data.objects:
        mesh = getattr(obj, "data", None)
        if not mesh or not hasattr(mesh, "attributes"):
            continue
        attrs = []
        for attr in mesh.attributes:
            name = safe_name(attr.name)
            if name in {"COL0", "描边排除", "描边颜色"} or "描边" in name:
                values = []
                for item in attr.data[:200000]:
                    if hasattr(item, "value"):
                        values.append(float(item.value))
                    elif hasattr(item, "color"):
                        c = item.color
                        values.append((float(c[0]), float(c[1]), float(c[2]), float(c[3])))
                    elif hasattr(item, "vector"):
                        v = item.vector
                        values.append(tuple(float(x) for x in v))
                attrs.append(
                    {
                        "name": name,
                        "domain": attr.domain,
                        "data_type": attr.data_type,
                        "count": len(attr.data),
                        "sample": values[:8],
                        "summary": summarize_values(values) if values and isinstance(values[0], tuple) else None,
                        "min": min(values) if values and isinstance(values[0], float) else None,
                        "max": max(values) if values and isinstance(values[0], float) else None,
                        "mean": (sum(values) / len(values)) if values and isinstance(values[0], float) else None,
                    }
                )
        if attrs:
            objects.append({"object": safe_name(obj.name), "attributes": attrs})
    return objects


def collect_vertex_color_node_uses():
    uses = []
    for mat in bpy.data.materials:
        nt = mat.node_tree
        if not nt:
            continue
        for node in nt.nodes:
            if node.bl_idname == "ShaderNodeVertexColor":
                layer = safe_name(getattr(node, "layer_name", ""))
                use = {
                    "material": safe_name(mat.name),
                    "node": safe_name(node.name),
                    "layer": layer,
                    "output_links": {},
                    "downstream": {},
                }
                for out in node.outputs:
                    if out.links:
                        use["output_links"][safe_name(out.name)] = linked_to(out)
                        use["downstream"][safe_name(out.name)] = walk_downstream(out)
                uses.append(use)
            if node.bl_idname == "ShaderNodeAttribute":
                attr_name = safe_name(getattr(node, "attribute_name", ""))
                if attr_name:
                    use = {
                        "material": safe_name(mat.name),
                        "node": safe_name(node.name),
                        "attribute": attr_name,
                        "output_links": {},
                        "downstream": {},
                    }
                    for out in node.outputs:
                        if out.links:
                            use["output_links"][safe_name(out.name)] = linked_to(out)
                            use["downstream"][safe_name(out.name)] = walk_downstream(out)
                    uses.append(use)
    return uses


def collect_nodegroup_vertex_color_uses():
    uses = []
    for nt in bpy.data.node_groups:
        for node in nt.nodes:
            if node.bl_idname == "ShaderNodeVertexColor":
                layer = safe_name(getattr(node, "layer_name", ""))
                use = {
                    "node_group": safe_name(nt.name),
                    "node": safe_name(node.name),
                    "layer": layer,
                    "output_links": {},
                    "downstream": {},
                }
                for out in node.outputs:
                    if out.links:
                        use["output_links"][safe_name(out.name)] = linked_to(out)
                        use["downstream"][safe_name(out.name)] = walk_downstream(out)
                uses.append(use)
            if node.bl_idname == "ShaderNodeAttribute":
                attr_name = safe_name(getattr(node, "attribute_name", ""))
                if attr_name:
                    use = {
                        "node_group": safe_name(nt.name),
                        "node": safe_name(node.name),
                        "attribute": attr_name,
                        "output_links": {},
                        "downstream": {},
                    }
                    for out in node.outputs:
                        if out.links:
                            use["output_links"][safe_name(out.name)] = linked_to(out)
                            use["downstream"][safe_name(out.name)] = walk_downstream(out)
                    uses.append(use)
    return uses


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "blend_file": bpy.data.filepath,
        "color_attributes": collect_color_attributes(),
        "geometry_attributes": collect_geometry_attributes(),
        "material_vertex_color_and_attribute_uses": collect_vertex_color_node_uses(),
        "nodegroup_vertex_color_and_attribute_uses": collect_nodegroup_vertex_color_uses(),
    }
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"WROTE {OUT}")


if __name__ == "__main__":
    main()
