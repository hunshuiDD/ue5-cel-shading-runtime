# blockout_desserts.py
# 达妮娅甜点小屋 — 甜点道具粗模(blockout)一键生成
# 用法：Blender 打开 -> Scripting -> New -> 粘贴运行；或命令行 blender -P blockout_desserts.py
# 叙事：日常下午茶(非生日)，甜点屋中不经意的一刻。
# 产物：集合 "Dessert_Blockout"
#   主心骨/焦点：茶壶(+前景茶杯茶碟) —— 压场锚点
#   三层甜点架(摆满纸杯蛋糕/蛋挞/曲奇/马卡龙/泡芙/巧克力，顶部纸杯蛋糕作冠)
#   桌面：热可可杯/甜甜圈/果冻/散放马卡龙 + 小装饰(樱桃串/方糖/糖珠/棒棒糖/巧克力)
# 原则：圆润几何 + Bevel + 平滑着色，匹配 PRD 的"圆润蓬松/大圆角"形状语言。
# 单位=米，真实尺度；每个物体 origin 落在底部中心，方便 UE 落地摆放。
# 材质只是占位色，真正的 SSS/半透/ClearCoat 在 UE 里做。
# 幂等：每次运行会重建 Dessert_Blockout 集合。(v3: 去蛋糕/草莓，改茶壶主心骨+日常下午茶)

import bpy
import math
import random
from mathutils import Vector

COL_NAME = "Dessert_Blockout"

# --- 色板(占位)，对应 PRD §1；莓果/绿叶为克制的小面积暖点缀 ---
PAL = {
    "cream":    (0.93, 0.90, 0.82, 1.0),
    "peach":    (0.96, 0.72, 0.70, 1.0),
    "black":    (0.05, 0.05, 0.06, 1.0),
    "caramel":  (0.80, 0.55, 0.22, 1.0),
    "coolblue": (0.45, 0.55, 0.72, 1.0),
    "choco":    (0.34, 0.22, 0.15, 1.0),
    "porcelain":(0.95, 0.95, 0.93, 1.0),
    "berry":    (0.62, 0.20, 0.28, 1.0),
    "stem":     (0.40, 0.46, 0.30, 1.0),
    "sugar":    (0.97, 0.97, 0.96, 1.0),
}


def get_mat(name, rgba):
    m = bpy.data.materials.get(name)
    if not m:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = rgba
    m.diffuse_color = rgba
    return m


def fresh_collection(name):
    old = bpy.data.collections.get(name)
    if old:
        for o in list(old.objects):
            bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(old)
    c = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(c)
    return c


def deselect_all():
    for o in bpy.context.selected_objects:
        o.select_set(False)


def active(o):
    deselect_all()
    bpy.context.view_layer.objects.active = o
    o.select_set(True)


def apply_transform(o, loc=False, rot=False, scale=True):
    active(o)
    bpy.ops.object.transform_apply(location=loc, rotation=rot, scale=scale)


def origin_to_bottom(o):
    minz = min((o.matrix_world @ v.co).z for v in o.data.vertices)
    bpy.context.scene.cursor.location = Vector((o.location.x, o.location.y, minz))
    active(o)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.context.scene.cursor.location = Vector((0, 0, 0))


def to_col(o, col):
    for c in list(o.users_collection):
        c.objects.unlink(o)
    col.objects.link(o)


def set_mat(o, mat):
    o.data.materials.clear()
    o.data.materials.append(mat)


def finalize(o, name, mat, col, bevel=0.003, seg=2, sub=0):
    o.name = name
    set_mat(o, mat)
    _finish(o, col, bevel, seg, sub)
    return o


def finalize_keep(o, name, col, bevel=0.003, seg=2, sub=0):
    o.name = name
    _finish(o, col, bevel, seg, sub)
    return o


def _finish(o, col, bevel, seg, sub):
    b = o.modifiers.new("Bevel", 'BEVEL')
    b.width, b.segments, b.limit_method = bevel, seg, 'ANGLE'
    if sub:
        s = o.modifiers.new("Subsurf", 'SUBSURF')
        s.levels, s.render_levels = sub, sub
    active(o)
    bpy.ops.object.shade_smooth()
    origin_to_bottom(o)
    to_col(o, col)


def cyl(r, depth, loc, verts=48):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, vertices=verts, location=loc)
    return bpy.context.active_object


def sphere(r, loc, zscale=1.0):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc, segments=24, ring_count=12)
    o = bpy.context.active_object
    if zscale != 1.0:
        o.scale.z = zscale
        apply_transform(o, scale=True)
    return o


def torus(major, minor, loc):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor,
                                     location=loc, major_segments=28, minor_segments=14)
    return bpy.context.active_object


def cone(r1, r2, depth, loc, verts=32):
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=depth, vertices=verts, location=loc)
    return bpy.context.active_object


def box(sx, sy, sz, loc):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
    o = bpy.context.active_object
    o.scale = (sx, sy, sz)
    apply_transform(o, scale=True)
    return o


def join(parts, keep):
    active(keep)
    for p in parts:
        p.select_set(True)
    bpy.ops.object.join()
    return bpy.context.active_object


def ring(cx, cy, cz, r, n, a0=0.0):
    return [(cx + r * math.cos(a0 + i * 2 * math.pi / n),
             cy + r * math.sin(a0 + i * 2 * math.pi / n), cz) for i in range(n)]


# 焦点 / 大件
def make_teapot(name, loc, col):
    """主心骨：圆润茶壶(壶身+盖+钮+嘴+把)。"""
    x, y, z = loc
    body = sphere(0.075, (x, y, z + 0.062), zscale=0.82)
    lid = sphere(0.042, (x, y, z + 0.108), zscale=0.42)
    knob = sphere(0.013, (x, y, z + 0.122))
    spout = cone(0.016, 0.005, 0.085, (x + 0.075, y, z + 0.083), verts=20)
    active(spout)
    spout.rotation_euler = (0, 1.0, 0)
    handle = torus(0.030, 0.008, (x - 0.082, y, z + 0.070))
    active(handle)
    handle.rotation_euler = (0, 1.5708, 0)
    set_mat(body, get_mat("M_Teapot_Body", PAL["cream"]))
    set_mat(lid, get_mat("M_Teapot_Lid", PAL["peach"]))
    set_mat(knob, get_mat("M_Teapot_Lid", PAL["peach"]))
    set_mat(spout, get_mat("M_Porcelain", PAL["porcelain"]))
    set_mat(handle, get_mat("M_Porcelain", PAL["porcelain"]))
    merged = join([lid, knob, spout, handle], body)
    finalize_keep(merged, name, col, bevel=0.003, sub=0)


def make_teacup(name, loc, col):
    """前景茶杯 + 茶碟 + 茶汤(琥珀，半透材质演示位)。"""
    x, y, z = loc
    saucer = cyl(0.045, 0.006, (x, y, z + 0.003))
    cup = cone(0.020, 0.028, 0.032, (x, y, z + 0.022))
    handle = torus(0.012, 0.004, (x + 0.030, y, z + 0.020))
    active(handle)
    handle.rotation_euler = (0, 1.5708, 0)
    tea = cyl(0.024, 0.004, (x, y, z + 0.036), verts=32)
    set_mat(saucer, get_mat("M_Porcelain", PAL["porcelain"]))
    set_mat(cup, get_mat("M_Porcelain", PAL["porcelain"]))
    set_mat(handle, get_mat("M_Porcelain", PAL["porcelain"]))
    set_mat(tea, get_mat("M_Tea", PAL["caramel"]))
    merged = join([cup, handle, tea], saucer)
    finalize_keep(merged, name, col, bevel=0.002, sub=0)


def make_cake_stand(col, base=(0.30, 0.05, 0)):
    x, y, z = base
    rod = cyl(0.008, 0.24, (x, y, z + 0.12), verts=16)
    p1 = cyl(0.095, 0.010, (x, y, z + 0.02))
    p2 = cyl(0.072, 0.010, (x, y, z + 0.11))
    p3 = cyl(0.050, 0.010, (x, y, z + 0.20))
    merged = join([p1, p2, p3], rod)
    finalize(merged, "SM_CakeStand", get_mat("M_Porcelain", PAL["porcelain"]), col)


def make_donut(col, loc=(-0.22, 0.10, 0)):
    x, y, z = loc
    o = torus(0.035, 0.016, (x, y, z + 0.016))
    finalize(o, "SM_Donut", get_mat("M_Peach", PAL["peach"]), col, bevel=0.0, sub=1)


def make_jelly(col, loc=(-0.15, -0.12, 0)):
    x, y, z = loc
    o = cone(0.048, 0.030, 0.050, (x, y, z + 0.025))
    finalize(o, "SM_Jelly", get_mat("M_Jelly", PAL["coolblue"]), col, bevel=0.004, sub=1)


def make_mug(col, loc=(0.20, -0.14, 0)):
    x, y, z = loc
    body = cyl(0.040, 0.080, (x, y, z + 0.040))
    handle = torus(0.026, 0.006, (x + 0.045, y, z + 0.045))
    active(handle)
    handle.rotation_euler = (0, 1.5708, 0)
    apply_transform(handle, rot=True, scale=True)
    mug = join([handle], body)
    finalize(mug, "SM_CocoaMug", get_mat("M_Porcelain", PAL["porcelain"]), col)
    liquid = cyl(0.036, 0.006, (x, y, z + 0.074), verts=40)
    finalize(liquid, "SM_CocoaLiquid", get_mat("M_Choco", PAL["choco"]), col, bevel=0.001)


# 甜点种类(小件) — loc 为底部落点
def make_macaron(name, loc, col, color):
    x, y, z = loc
    bot = sphere(0.028, (x, y, z + 0.006), zscale=0.42)
    top = sphere(0.028, (x, y, z + 0.020), zscale=0.42)
    fil = cyl(0.024, 0.010, (x, y, z + 0.013), verts=32)
    set_mat(bot, get_mat("M_Mac_" + name, color))
    set_mat(top, get_mat("M_Mac_" + name, color))
    set_mat(fil, get_mat("M_Cream", PAL["cream"]))
    merged = join([top, fil], bot)
    finalize_keep(merged, name, col, bevel=0.002, seg=2, sub=1)


def make_cupcake(name, loc, col, wrap, cream):
    x, y, z = loc
    liner = cone(0.014, 0.020, 0.024, (x, y, z + 0.012))
    s1 = sphere(0.019, (x, y, z + 0.028), zscale=0.7)
    s2 = sphere(0.014, (x, y, z + 0.039), zscale=0.7)
    s3 = sphere(0.009, (x, y, z + 0.048), zscale=0.8)
    cherry = sphere(0.006, (x, y, z + 0.055))
    set_mat(liner, get_mat("M_Wrap", wrap))
    for s in (s1, s2, s3):
        set_mat(s, get_mat("M_CreamSwirl", cream))
    set_mat(cherry, get_mat("M_Berry", PAL["berry"]))
    merged = join([s1, s2, s3, cherry], liner)
    finalize_keep(merged, name, col, bevel=0.0015, sub=1)


def make_cookie(name, loc, col, color):
    x, y, z = loc
    o = cyl(0.020, 0.008, (x, y, z + 0.004))
    finalize(o, name, get_mat("M_Cookie", color), col, bevel=0.002, sub=1)


def make_chocolate(name, loc, col, color):
    x, y, z = loc
    o = box(0.020, 0.020, 0.010, (x, y, z + 0.005))
    finalize(o, name, get_mat("M_Choc_" + name, color), col, bevel=0.0015)


def make_cream_puff(name, loc, col, color):
    x, y, z = loc
    o = sphere(0.018, (x, y, z + 0.014), zscale=0.85)
    finalize(o, name, get_mat("M_Puff", color), col, bevel=0.002, sub=1)


def make_egg_tart(name, loc, col):
    x, y, z = loc
    shell = cone(0.016, 0.022, 0.012, (x, y, z + 0.006))
    fill = sphere(0.019, (x, y, z + 0.013), zscale=0.4)
    set_mat(shell, get_mat("M_Caramel", PAL["caramel"]))
    set_mat(fill, get_mat("M_Custard", PAL["cream"]))
    merged = join([fill], shell)
    finalize_keep(merged, name, col, bevel=0.0015, sub=1)


def make_cherry(name, loc, col):
    x, y, z = loc
    ball = sphere(0.008, (x, y, z + 0.008))
    stem = cyl(0.0012, 0.016, (x + 0.004, y, z + 0.018), verts=8)
    active(stem)
    stem.rotation_euler = (0.5, 0, 0)
    set_mat(ball, get_mat("M_Berry", PAL["berry"]))
    set_mat(stem, get_mat("M_Stem", PAL["stem"]))
    merged = join([stem], ball)
    finalize_keep(merged, name, col, bevel=0.0, sub=1)


def make_sugar_cube(name, loc, col):
    x, y, z = loc
    o = box(0.012, 0.012, 0.012, (x, y, z + 0.006))
    finalize(o, name, get_mat("M_Sugar", PAL["sugar"]), col, bevel=0.001)


def make_pearl(name, loc, col, color):
    x, y, z = loc
    o = sphere(0.004, (x, y, z + 0.004))
    finalize(o, name, get_mat("M_Pearl_" + name, color), col, bevel=0.0)


def make_lollipop(name, loc, col, color):
    x, y, z = loc
    stick = cyl(0.0015, 0.05, (x, y, z + 0.025), verts=8)
    candy = sphere(0.012, (x, y, z + 0.055), zscale=0.6)
    set_mat(stick, get_mat("M_Porcelain", PAL["porcelain"]))
    set_mat(candy, get_mat("M_Lolli", color))
    merged = join([candy], stick)
    finalize_keep(merged, name, col, bevel=0.0, sub=1)


def make_table_ref(col):
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=(0.05, -0.02, 0))
    o = bpy.context.active_object
    o.scale = (0.9, 0.6, 1.0)
    apply_transform(o, scale=True)
    o.name = "REF_TableTop"
    set_mat(o, get_mat("M_Cream", PAL["cream"]))
    to_col(o, col)


# 摆盘：把三层架摆满
def make_stand_desserts(col, base=(0.30, 0.05, 0)):
    cx, cy = base[0], base[1]
    for i, p in enumerate(ring(cx, cy, 0.025, 0.060, 8)):
        if i % 3 == 0:
            make_cupcake("SM_Std_Cupcake_%d" % i, p, col, PAL["peach"], PAL["cream"])
        elif i % 3 == 1:
            make_egg_tart("SM_Std_Tart_%d" % i, p, col)
        else:
            make_cookie("SM_Std_Cookie_%d" % i, p, col, PAL["caramel"])
    for i, p in enumerate(ring(cx, cy, 0.115, 0.043, 6, a0=0.4)):
        if i % 3 == 0:
            make_macaron("SM_Std_Mac_%d" % i, p, col, PAL["peach"])
        elif i % 3 == 1:
            make_cream_puff("SM_Std_Puff_%d" % i, p, col, PAL["cream"])
        else:
            make_chocolate("SM_Std_Choc_%d" % i, p, col, PAL["choco"])
    # 顶盘：纸杯蛋糕作冠 + 3 樱桃
    make_cupcake("SM_Std_Cupcake_top", (cx, cy, 0.205), col, PAL["peach"], PAL["cream"])
    for i, p in enumerate(ring(cx, cy, 0.205, 0.026, 3, a0=0.2)):
        make_cherry("SM_Std_Cherry_%d" % i, p, col)


# 桌面小装饰(克制散放，留白给 DOF/雾)
def make_table_decorations(col):
    for i, p in enumerate([(-0.30, -0.05, 0), (-0.285, -0.038, 0), (-0.30, -0.028, 0)]):
        make_cherry("SM_Deco_Cherry_%d" % i, p, col)
    make_sugar_cube("SM_Deco_Sugar_1", (0.285, -0.165, 0), col)
    make_sugar_cube("SM_Deco_Sugar_2", (0.302, -0.150, 0), col)
    make_sugar_cube("SM_Deco_Sugar_3", (0.293, -0.158, 0.012), col)
    make_cookie("SM_Deco_Cookie_1", (-0.20, -0.14, 0), col, PAL["caramel"])
    make_chocolate("SM_Deco_Choc_1", (0.13, -0.145, 0), col, PAL["choco"])
    make_chocolate("SM_Deco_Choc_2", (0.16, -0.125, 0), col, PAL["peach"])
    make_lollipop("SM_Deco_Lollipop", (-0.28, 0.14, 0), col, PAL["peach"])
    random.seed(7)
    pearl_cols = [PAL["sugar"], PAL["peach"], PAL["coolblue"]]
    n = 0
    for _ in range(30):
        x = random.uniform(-0.30, 0.30)
        y = random.uniform(-0.20, 0.20)
        if abs(x) < 0.13 and abs(y) < 0.13:
            continue
        make_pearl("SM_Deco_Pearl_%d" % n, (x, y, 0), col, pearl_cols[n % 3])
        n += 1
        if n >= 10:
            break


def main():
    scn = bpy.context.scene
    scn.unit_settings.system = 'METRIC'
    scn.unit_settings.length_unit = 'METERS'
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    col = fresh_collection(COL_NAME)
    make_table_ref(col)
    make_teapot("SM_Teapot", (0.0, 0.0, 0), col)          # 主心骨
    make_teacup("SM_Teacup", (-0.04, -0.21, 0), col)      # 前景茶杯
    make_cake_stand(col, base=(0.30, 0.05, 0))
    make_stand_desserts(col, base=(0.30, 0.05, 0))
    make_donut(col, loc=(-0.22, 0.10, 0))
    make_jelly(col, loc=(-0.15, -0.12, 0))
    make_mug(col, loc=(0.20, -0.14, 0))
    make_macaron("SM_Macaron_A", (-0.05, 0.14, 0), col, PAL["peach"])
    make_macaron("SM_Macaron_B", (0.06, 0.16, 0), col, PAL["cream"])
    make_macaron("SM_Macaron_C", (0.02, -0.16, 0), col, PAL["coolblue"])
    make_table_decorations(col)
    deselect_all()
    print("[blockout_desserts] Done. Collection: %s (%d objects)" % (COL_NAME, len(col.objects)))


if __name__ == "__main__":
    main()
