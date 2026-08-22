# inspect_character_morphs.py
# 扫描角色 FBX 的 shape key(morph)，诊断"力度低 / 左右不对称"。
# 用法：Blender -> Scripting -> 打开本文件 -> Run。
#   结果打印在系统控制台：Window > Toggle System Console（Windows）。
# 说明：在独立临时 scene 里导入、扫完即删，不影响你当前场景。
# 读法：maxD=最大顶点位移(越小=力度越弱)；avgL/avgR=左右半脸平均位移；
#       asym%=左右不对称度(>25% 会标注，多半是没开 X 对称雕的或提取搞坏了对称性)。

import bpy

FBX = r"<PROJECT_ROOT>\Exports\Daniya.fbx"


def run():
    prev = bpy.context.window.scene
    tmp = bpy.data.scenes.new("_MorphInspect")
    bpy.context.window.scene = tmp
    before = set(bpy.data.objects.keys())
    bpy.ops.import_scene.fbx(filepath=FBX)
    new = [bpy.data.objects[n] for n in (set(bpy.data.objects.keys()) - before)]
    try:
        for o in new:
            if o.type == 'ARMATURE':
                bones = [b.name for b in o.data.bones]
                fh = [b for b in bones if any(k in b.lower() for k in
                      ["jaw", "eye", "brow", "mouth", "lip", "cheek", "tongue",
                       "teeth", "eyelid", "blink", "face"])]
                print("[ARM] %s bones=%d" % (o.name, len(bones)))
                print("      face-bones:", ", ".join(fh) if fh else "(none)")
        for o in new:
            if o.type != 'MESH':
                continue
            me = o.data
            if not me.shape_keys:
                print("[MESH] %s verts=%d shape_keys=0" % (o.name, len(me.vertices)))
                continue
            kb = me.shape_keys.key_blocks
            basis = kb[0]
            nv = len(basis.data)
            step = max(1, nv // 4000)
            idxs = list(range(0, nv, step))
            xs = [basis.data[i].co.x for i in idxs]
            cx = (min(xs) + max(xs)) / 2
            print("[MESH] %s verts=%d dims=%s shape_keys=%d" %
                  (o.name, nv, [round(v, 3) for v in o.dimensions], len(kb) - 1))
            rows = []
            for k in kb:
                if k == basis:
                    continue
                maxd = 0.0
                sl = sr = 0.0
                nl = nr = 0
                for i in idxs:
                    d = (k.data[i].co - basis.data[i].co).length
                    if d > maxd:
                        maxd = d
                    if basis.data[i].co.x < cx:
                        sl += d
                        nl += 1
                    else:
                        sr += d
                        nr += 1
                al = sl / max(nl, 1)
                ar = sr / max(nr, 1)
                asym = abs(al - ar) / max(al, ar, 1e-6) * 100
                rows.append((maxd, k.name, al, ar, asym))
            rows.sort(reverse=True)
            print("   %-28s %8s %9s %9s %6s" % ("shapekey", "maxD", "avgL", "avgR", "asym%"))
            for maxd, name, al, ar, asym in rows:
                flag = "  <-- ASYM!" if asym > 25 else ""
                print("   %-28s %8.4f %9.5f %9.5f %5.0f%%%s" %
                      (name[:28], maxd, al, ar, asym, flag))
    finally:
        bpy.context.window.scene = prev
        for o in new:
            try:
                bpy.data.objects.remove(o, do_unlink=True)
            except Exception:
                pass
        bpy.data.scenes.remove(tmp)
    print("[done] 扫描完成，临时对象已清理。")


run()
