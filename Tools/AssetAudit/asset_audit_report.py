#!/usr/bin/env python3
"""
asset_audit_report.py — UE 资产审计 CSV -> 可行动报表

支持三类输入，按 CSV 表头指纹自动识别：
  1. TextureStats   (Statistics 面板 Texture Stats / Saved/Logs/TextureStats/*.csv)
  2. PrimitiveStats (Statistics 面板 Primitive Stats / Saved/Logs/PrimitiveStats/*.csv)
  3. 其它表格       (Asset Audit 等)，退回模糊列名匹配

每类都产出：派生指标 + 问题标签 Flags + 综合成本分 CostScore + 严重度 Severity，
按团队分表，附 Summary 汇总页和 Config 说明页。

用法：
    python asset_audit_report.py 输入.csv -o 报表.xlsx [--config 项目配置.json]

--config JSON 字段（每个字段整体替换内置默认值，不做逐项合并）：
    teams:        {团队名: [路径或名字关键词, ...]}  资产名或路径包含关键词即归入该团队
    thresholds:   {指标列名: {"warn": 数值, "crit": 数值}}  超过即标黄/标红
    cost_weights: {指标列名: 权重}  CostScore 权重；只有当前表里存在的指标参与，权重会自动重新归一化
    column_hints: {用途: CSV里的实际列名}  仅对模糊匹配分支生效

依赖：pip install pandas openpyxl
"""

import argparse
import json
import re
import sys

import pandas as pd
from openpyxl.utils import get_column_letter
from openpyxl.styles import PatternFill, Font, Alignment
from openpyxl.formatting.rule import ColorScaleRule, CellIsRule

# ================= 内置通用默认值（无 --config 时生效，不含任何项目专属名字）=================
DEFAULT_TEAMS = {
    "Character":   ["/Character", "/Char/", "/Actors"],
    "Environment": ["/Environment", "/Env", "/Scene", "/Prop", "/Map", "/Level"],
    "VFX":         ["/FX", "/VFX", "/Niagara", "/Particle", "/Effects"],
    "UI":          ["/UI", "/HUD", "/Widget"],
    "Audio":       ["/Audio", "/Sound", "/SFX"],
}

# 阈值按指标名索引，三类表共用一张表：某指标不在当前表里就自动跳过。
DEFAULT_THRESHOLDS = {
    # --- 贴图 (TextureStats) ---
    "FullMB":        {"warn": 8,     "crit": 21},     # 21MB ≈ 一张 2K 未压缩 BGRA8 带 mip
    "MaxDim":        {"warn": 2048,  "crit": 4096},
    # --- 网格 (PrimitiveStats) ---
    "TriCount":      {"warn": 30000,   "crit": 100000},
    "TotalTris":     {"warn": 2000000, "crit": 10000000},
    "Sections":      {"warn": 4,       "crit": 8},
    "ResourceMB":    {"warn": 10,      "crit": 50},
    # --- 通用 / Asset Audit ---
    "MaterialCount": {"warn": 3,            "crit": 6},
    "DiskBytes":     {"warn": 10 * 1024**2, "crit": 50 * 1024**2},
}

# 同样是共用一张表；实际参与计算的只有当前表存在的指标，权重会重新归一化。
DEFAULT_COST_WEIGHTS = {
    "FullMB": 0.8, "MaxDim": 0.2,                              # 贴图
    "TotalTris": 0.5, "ResourceMB": 0.3, "Sections": 0.2,      # 网格
    "DiskBytes": 0.3, "MaterialCount": 0.2,                    # 通用
}
DEFAULT_COLUMN_HINTS = {}

# Flag 的含义与建议动作。写进 Summary 页，看表的人不用去翻 Config 页或问作者。
FLAG_DOC = {
    "Uncompressed":   ("像素格式未走块压缩(BGRA8/FloatRGBA 等)", "改 BC7(彩色)/BC5(法线)/BC4(单通道)，2K 图约 21MB→5.3MB"),
    "Oversized":      ("最长边达到 MaxDim 的 crit 阈值", "对照 CurDim：若常年只加载到低一档，直接降分辨率"),
    "NonPow2":        ("最长边非 2 的幂", "重导为 2 的幂，否则 mip 与流送退化"),
    "Unused":         ("Uses<=0，本次统计范围内无引用", "确认不是被 Sequencer/蓝图动态引用后删除"),
    "AlwaysResident": ("常驻内存=满载内存且体量超 warn", "整条 mip 链没流出，查 Never Stream / LODGroup 设置"),
    "HighTris":       ("单实例面数达到 TriCount 的 crit 阈值", "减面或加 LOD；描边壳可用比本体低一档的网格"),
    "ManySections":   ("section 数达到 Sections 的 crit 阈值", "合并材质槽，section 数直接等于 drawcall 数"),
    "DenseForSize":   ("面数/包围球半径处于本表前 10%", "小物件堆高模，优先减面"),
    "InstanceHeavy":  ("多实例且合计面数超 TotalTris 的 warn", "上 HISM/Nanite，或给远处实例配 LOD"),
    "NaniteOnLowPoly":("低模开了 Nanite", "Nanite 有固定开销，低模开了通常是净亏，关掉走传统渲染"),
    "NaniteNoCut":    ("Nanite fallback 保留 100% 面数", "fallback 没减面，等于多存一份数据；调 Fallback Relative Error"),
    "NaniteBloat":    ("Nanite 三角形数超原网格 2 倍", "分簇后几何反而变多，低模上是净亏，考虑关掉 Nanite"),
    "HeavyCollision": ("碰撞数据占磁盘体积比例过高", "装饰件改用简单碰撞或直接关碰撞，别用 UseSimpleAndComplex"),
    "ComplexCollision":("启用了复杂碰撞(按渲染网格逐面)", "确认是否真需要；装饰件用 Simple 或 NoCollision"),
    "PoorVertexReuse":("顶点数/面数偏高，顶点复用差", "查硬边和 UV 岛碎裂，合并法线组或减少 UV 接缝"),
    "NoLOD":          ("未开 Nanite 且只有 1 级 LOD", "补 LOD 或开 Nanite"),
    "ManyUV":         ("UV 通道数 >= 3", "确认每条 UV 都有用，多余通道白占顶点带宽"),
    "Orphan":         ("TotalUsage<=0，无引用", "确认非动态引用后删除；该列在部分平台设置下算不出来，先看是否全表为 0"),
}

# 未走 GPU 块压缩的像素格式：一张 2K 从 BC7 的 5.5MB 涨到 21MB，是贴图内存的头号来源。
UNCOMPRESSED_FORMATS = ("B8G8R8A8", "R8G8B8A8", "A8R8G8B8", "FloatRGBA",
                        "FloatRGB", "A32B32G32R32F", "R16G16B16A16")
# ===========================================================================================

SIZE_UNITS = {"B": 1, "KB": 1e3, "MB": 1e6, "GB": 1e9, "TB": 1e12,
              "KIB": 1024, "MIB": 1024**2, "GIB": 1024**3, "TIB": 1024**4}


def load_config(path):
    """加载 --config JSON，缺失的顶层字段用内置默认值补齐；每个字段整体替换（要改单个阈值也要把
    整个 thresholds 块写全），避免"改了一半"的隐藏状态。"""
    cfg = {
        "teams": DEFAULT_TEAMS,
        "thresholds": DEFAULT_THRESHOLDS,
        "column_hints": DEFAULT_COLUMN_HINTS,
        "cost_weights": DEFAULT_COST_WEIGHTS,
    }
    if not path:
        return cfg
    try:
        with open(path, "r", encoding="utf-8") as f:
            user_cfg = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        sys.exit("--config 读取失败: %s (%s)" % (path, e))
    for key in cfg:
        if key in user_cfg:
            cfg[key] = user_cfg[key]
    return cfg


def parse_size(v):
    s = str(v).strip().replace(",", "")
    if not s:
        return float("nan")
    m = re.match(r"([\d.]+)\s*([a-zA-Z]+)?", s)
    if not m:
        return float("nan")
    return float(m.group(1)) * SIZE_UNITS.get((m.group(2) or "B").upper(), 1)


def to_num(v):
    """UE 导出的数字带千位分隔（空格或逗号），例如 "2 048x2 048" / "537 440"。"""
    s = str(v).replace(",", "").replace(" ", "").replace(" ", "").strip()
    m = re.match(r"[-+]?[\d.]+", s)
    return float(m.group(0)) if m else float("nan")


def find_col(cols, *keywords, avoid=()):
    for c in cols:
        n = c.lower()
        if all(k in n for k in keywords) and not any(a in n for a in avoid):
            return c
    return None


def max_side(v):
    """从 "2 048x2 048" 取最长边。UE 用空格做千位分隔，必须先去掉。"""
    nums = re.findall(r"[\d.]+", str(v).replace(",", "").replace(" ", ""))
    return max(float(x) for x in nums) if nums else float("nan")


def diag_from_dims(v):
    nums = re.findall(r"[\d.]+", str(v).replace(",", "").replace(" ", ""))
    if len(nums) < 2:
        return float("nan")
    return sum(float(x) ** 2 for x in nums[:3]) ** 0.5


def norm(series):
    s = pd.to_numeric(series, errors="coerce").astype(float)
    lo, hi = s.min(), s.max()
    return (s - lo) / (hi - lo) if hi > lo else s * 0


def add_cost_score(out, cost_weights):
    """只用当前表里真实存在的指标算分，并把权重重新归一化——否则贴图表因为缺少网格指标
    会被整体压低分数，跨表不可比。"""
    score = pd.Series(0.0, index=out.index)
    used = 0.0
    for metric, weight in cost_weights.items():
        if metric in out.columns:
            score = score + weight * norm(out[metric].fillna(0))
            used += weight
    out["CostScore"] = (100 * score / used).round(1) if used else 0.0
    return sorted(m for m in cost_weights if m in out.columns)


def add_severity(out, thresholds):
    active = {k: v for k, v in thresholds.items() if k in out.columns}

    def sev(row):
        level = 0
        for k, th in active.items():
            v = row.get(k)
            if pd.notna(v):
                if v >= th["crit"]:
                    level = max(level, 2)
                elif v >= th["warn"]:
                    level = max(level, 1)
        return ["OK", "Warn", "Crit"][level]

    out["Severity"] = out.apply(sev, axis=1)
    return active


# ============================ schema：按表头指纹识别 ============================
# 指纹用"这类表独有的列名组合"，不用列数或顺序——UE 各版本会增删列，但这几列是稳定的。
def detect_schema(cols):
    low = [c.strip().lower() for c in cols]
    if "max dimension" in low and any("fully loaded" in c for c in low):
        return "TextureStats"
    if "sum tris" in low and "inst sections" in low:
        return "PrimitiveStats"
    if "disksize" in low and "totalusage" in low:
        return "AssetAudit"
    return "Generic"


def _pick(df, *names):
    """按列名精确取列（大小写/首尾空格不敏感），取不到返回 None。"""
    lookup = {c.strip().lower(): c for c in df.columns}
    for n in names:
        if n.lower() in lookup:
            return df[lookup[n.lower()]]
    return None


def _flags(rows):
    """rows: [(条件Series, 标签)] -> 每行一个逗号分隔的标签串。"""
    out = pd.Series("", index=rows[0][0].index)
    for cond, label in rows:
        cond = cond.fillna(False)
        out = out.where(~cond, out.where(out == "", out + ",") + label)
    return out.replace("", "-")


def build_texture(df, thresholds):
    """TextureStats：Current/Fully Loaded Memory 单位是 KiB。
    验证：2048x2048 PF_B8G8R8A8 = 2048^2 x 4B x 1.333(mip链) = 21845 KiB，
    与该 CSV 里同规格贴图的 21888 相符，故按 /1024 换 MB。"""
    out = pd.DataFrame(index=df.index)
    out["Asset"] = _pick(df, "Name").astype(str)
    p = _pick(df, "Path")
    out["Path"] = p.astype(str) if p is not None else ""
    for label, src in (("Type", "Type"), ("Format", "Format"), ("Group", "Group"),
                       ("Virtual", "Virtual")):
        s = _pick(df, src)
        if s is not None:
            out[label] = s.astype(str)
    out["MaxDim"] = _pick(df, "Max Dimension").map(max_side)
    cur = _pick(df, "Current Dimension")
    if cur is not None:
        out["CurDim"] = cur.map(max_side)
    out["FullMB"] = (_pick(df, "Fully Loaded Memory").map(to_num) / 1024).round(2)
    cm = _pick(df, "Current Memory")
    if cm is not None:
        out["CurMB"] = (cm.map(to_num) / 1024).round(2)
        # 已流出的部分：常驻 == 满载说明这张图整条 mip 链一直在显存里。
        out["StreamedOutMB"] = (out["FullMB"] - out["CurMB"]).round(2)
    for label, src in (("LODBias", "CombinedLODBias"), ("Uses", "Uses")):
        s = _pick(df, src)
        if s is not None:
            out[label] = s.map(to_num)

    fmt = out.get("Format", pd.Series("", index=out.index)).astype(str)
    uses = out.get("Uses", pd.Series(float("nan"), index=out.index))
    resident = out.get("StreamedOutMB", pd.Series(float("nan"), index=out.index))
    out["Flags"] = _flags([
        (fmt.str.contains("|".join(UNCOMPRESSED_FORMATS), case=False, regex=True), "Uncompressed"),
        (out["MaxDim"] >= thresholds.get("MaxDim", {}).get("crit", 4096), "Oversized"),
        (out["MaxDim"].map(lambda d: pd.notna(d) and d > 0 and int(d) & (int(d) - 1) != 0), "NonPow2"),
        (uses <= 0, "Unused"),
        ((resident <= 0) & (out["FullMB"] >= thresholds.get("FullMB", {}).get("warn", 8)), "AlwaysResident"),
    ])
    return out


def build_primitive(df, thresholds):
    """PrimitiveStats：Object=资源名，Count=场景实例数，Tris=单实例面数，Sum Tris=合计，
    Inst Sections=网格 section 数(≈材质槽)，Avg R=包围球半径。
    Size / VC 列 UE 按 KB 输出（未在文档中逐字核对，若数值明显偏离请以引擎内 Size Map 为准）。"""
    out = pd.DataFrame(index=df.index)
    out["Asset"] = _pick(df, "Object").astype(str)
    t = _pick(df, "Type")
    if t is not None:
        out["Type"] = t.astype(str)
    out["Count"] = _pick(df, "Count").map(to_num).fillna(1)
    out["TriCount"] = _pick(df, "Tris").map(to_num)
    out["TotalTris"] = _pick(df, "Sum Tris").map(to_num)
    sec = _pick(df, "Inst Sections")
    if sec is not None:
        out["Sections"] = sec.map(to_num)
    size = _pick(df, "Size")
    if size is not None:
        out["ResourceMB"] = (size.map(to_num) / 1024).round(2)
    vc = _pick(df, "VC")
    if vc is not None:
        out["VertexColorKB"] = vc.map(to_num).round(1)
    r = _pick(df, "Avg R", "Max R")
    if r is not None:
        out["Radius"] = r.map(to_num).round(1)
        # 单位半径上的面数：小物件堆高模是最典型的浪费，比绝对面数更能定位问题资产。
        out["TriPerUnitR"] = (out["TriCount"] / out["Radius"].replace(0, float("nan"))).round(1)

    sections = out.get("Sections", pd.Series(float("nan"), index=out.index))
    dens = out.get("TriPerUnitR", pd.Series(float("nan"), index=out.index))
    out["Flags"] = _flags([
        (out["TriCount"] >= thresholds.get("TriCount", {}).get("crit", 100000), "HighTris"),
        (sections >= thresholds.get("Sections", {}).get("crit", 8), "ManySections"),
        (dens >= dens.quantile(0.9) if dens.notna().any() else pd.Series(False, index=out.index), "DenseForSize"),
        ((out["Count"] > 1) & (out["TotalTris"] >= thresholds.get("TotalTris", {}).get("warn", 2e6)), "InstanceHeavy"),
    ])
    return out


def build_asset_audit(df, thresholds):
    """Asset Audit 面板导出：Name/Class/Path + 磁盘、几何、碰撞、Nanite、LOD、UV 一整套。
    大小列单位是 byte（SM_Ballerina DiskSize 186061 ≈ 182KB，与该网格 1056 面的量级相符）。"""
    N = lambda c: pd.to_numeric(_pick(df, c), errors="coerce") if _pick(df, c) is not None else None

    out = pd.DataFrame(index=df.index)
    out["Asset"] = _pick(df, "Name").astype(str)
    p = _pick(df, "Path")
    out["Path"] = p.astype(str) if p is not None else ""
    cls = _pick(df, "Class")
    if cls is not None:
        out["Class"] = cls.astype(str).str.rsplit(".", n=1).str[-1]

    out["TriCount"] = N("Triangles")
    out["VertCount"] = N("Vertices")
    out["MaterialCount"] = N("Materials")
    out["DiskMB"] = (N("DiskSize") / 1024**2).round(3)

    # 碰撞占比：装饰件用 UseSimpleAndComplex 时，复杂碰撞按渲染网格逐面生成，
    # 磁盘和运行时都要付钱。绝对值看不出问题，占比才看得出。
    phys = N("PhysicsSize")
    if phys is not None:
        out["PhysicsMB"] = (phys / 1024**2).round(3)
        out["PhysicsRatio"] = (phys / N("DiskSize").replace(0, float("nan"))).round(3)

    # 顶点/面数：三角形共享顶点，正常在 0.5-1.0；偏高说明硬边多或 UV 岛碎。
    if out["VertCount"] is not None:
        out["VertPerTri"] = (out["VertCount"] / out["TriCount"].replace(0, float("nan"))).round(2)

    ap = _pick(df, "ApproxSize")
    if ap is not None:
        out["BoundsMax"] = ap.map(max_side)
        out["TriPerUnit"] = (out["TriCount"] / out["BoundsMax"].replace(0, float("nan"))).round(1)

    for label, src, div in (("LODs", "LODs", 1), ("UVChannels", "UVChannels", 1),
                            ("CollisionPrims", "CollisionPrims", 1),
                            ("NaniteTris", "NaniteTriangles", 1),
                            ("DFieldKB", "DistanceFieldSize", 1024),
                            ("BuildMemMB", "BuildRequiredMemoryEstimate", 1024**2)):
        v = N(src)
        if v is not None:
            out[label] = (v / div).round(2) if div > 1 else v

    for label, src in (("Nanite", "NaniteEnabled"), ("LODGroup", "LODGroup"),
                       ("NeverStream", "NeverStream"), ("CollisionType", "CollisionComplexity")):
        v = _pick(df, src)
        if v is not None:
            out[label] = v.astype(str).replace({"nan": "-", "None": "-", "": "-"})

    nanite_on = _pick(df, "NaniteEnabled").astype(str).str.lower().eq("true") \
        if _pick(df, "NaniteEnabled") is not None else pd.Series(False, index=out.index)
    fallback = N("NaniteFallbackPercent")
    coll = out.get("CollisionType", pd.Series("", index=out.index)).astype(str)
    usage = N("TotalUsage")
    # TotalUsage 在部分平台设置下引擎算不出来，整列为 0。那种情况下标 Orphan 会全表误报，
    # 与其给一列假信号，不如整个跳过。
    usage_valid = usage is not None and usage.max() > 0

    checks = [
        (nanite_on & (out["TriCount"] < thresholds.get("NaniteMinTris", {}).get("warn", 1000)), "NaniteOnLowPoly"),
        (nanite_on & (fallback >= 100) if fallback is not None else None, "NaniteNoCut"),
        (nanite_on & (N("NaniteTriangles") > out["TriCount"] * 2)
         if N("NaniteTriangles") is not None else None, "NaniteBloat"),
        (out.get("PhysicsRatio") >= thresholds.get("PhysicsRatio", {}).get("crit", 0.5)
         if "PhysicsRatio" in out else None, "HeavyCollision"),
        (coll.str.contains("Complex", case=False), "ComplexCollision"),
        (out.get("VertPerTri") > 1.5 if "VertPerTri" in out else None, "PoorVertexReuse"),
        ((~nanite_on) & (out.get("LODs", pd.Series(9, index=out.index)) <= 1)
         & (out["TriCount"] >= thresholds.get("TriCount", {}).get("warn", 30000)), "NoLOD"),
        (out.get("UVChannels") >= 3 if "UVChannels" in out else None, "ManyUV"),
        ((usage <= 0) if usage_valid else None, "Orphan"),
    ]
    out["Flags"] = _flags([(c, l) for c, l in checks if c is not None])
    return out


def build_generic(df, column_hints, thresholds):
    """未识别表头时的退路：模糊匹配列名。"""
    cols = list(df.columns)

    def col(use, *kw, avoid=()):
        return column_hints.get(use) or find_col(cols, *kw, avoid=avoid)

    c_name = col("name", "name") or col("name", "path") or cols[0]
    c_tri = col("tricount", "tri", avoid=("ratio", "material"))
    c_inst = col("instances", "instance") or col("instances", "count", avoid=("tri",))
    c_disk = col("disk", "disk") or col("disk", "exclusive")
    c_mat = col("material", "material")
    c_dim = col("dimensions", "dimension") or col("dimensions", "bounds")

    out = pd.DataFrame(index=df.index)
    out["Asset"] = df[c_name].astype(str)
    if c_tri:
        out["TriCount"] = df[c_tri].map(to_num)
        out["Instances"] = (df[c_inst].map(to_num) if c_inst else 1)
        out["Instances"] = out["Instances"].fillna(1)
        out["TotalTris"] = out["TriCount"] * out["Instances"]
    if c_disk:
        out["DiskBytes"] = df[c_disk].map(parse_size)
    if c_mat:
        out["MaterialCount"] = df[c_mat].map(to_num)
    if c_dim and c_tri:
        diag = df[c_dim].map(diag_from_dims)
        out["TriDensity"] = (out["TriCount"] / diag.replace(0, float("nan"))).round(1)
    out["Flags"] = "-"
    return out


def build(df, cfg):
    schema = detect_schema(df.columns)
    # 关键列为空的行是 UE 附在表尾的合计行：留着会污染 min-max 归一化，也会让下游 str 操作拿到 NaN。
    key = {"TextureStats": "Name", "PrimitiveStats": "Object", "AssetAudit": "Name"}.get(schema)
    if key:
        lookup = {c.strip().lower(): c for c in df.columns}
        df = df[df[lookup[key.lower()]].notna()]

    thresholds = cfg["thresholds"]
    if schema == "TextureStats":
        out = build_texture(df, thresholds)
    elif schema == "PrimitiveStats":
        out = build_primitive(df, thresholds)
    elif schema == "AssetAudit":
        out = build_asset_audit(df, thresholds)
    else:
        out = build_generic(df, cfg["column_hints"], thresholds)

    metrics = add_cost_score(out, cfg["cost_weights"])
    active_th = add_severity(out, thresholds)
    return out, schema, metrics, active_th


def team_of(asset, path, teams):
    """名字和路径都参与匹配：TextureStats 的 Name 列只有资产名不含路径，
    只匹配 Name 的话 config 里 "/Charactor" 这类路径关键词永远命中不了。"""
    hay = ("%s %s" % (asset if pd.notna(asset) else "", path if pd.notna(path) else "")).lower()
    for team, keys in teams.items():
        if any(k.lower() in hay for k in keys):
            return team
    return "Other"


RED = PatternFill("solid", fgColor="F8696B")
YEL = PatternFill("solid", fgColor="FFEB84")
GRN = PatternFill("solid", fgColor="C6EFCE")
SCALE = dict(start_type="percentile", start_value=10, start_color="63BE7B",
             mid_type="percentile", mid_value=60, mid_color="FFEB84",
             end_type="percentile", end_value=95, end_color="F8696B")


def _fit(ws, df, header_row, start_col=1):
    """给一个写在 header_row 行的区块设列宽并加粗表头。列宽按内容取，
    否则中文表头会被截成"ourceMB"这种看不懂的样子。"""
    for i, c in enumerate(df.columns, start_col):
        body = df[c].map(lambda v: len(str(v).encode("gbk", "ignore"))).max() if len(df) else 10
        want = max(len(str(c).encode("gbk", "ignore")), body) + 3
        letter = get_column_letter(i)
        cur = ws.column_dimensions[letter].width or 0
        ws.column_dimensions[letter].width = max(cur, 10, min(48, want))
    for cell in ws[header_row]:
        if cell.value is not None:
            cell.font = Font(bold=True)
            cell.alignment = Alignment(horizontal="center")


def write_sheet(writer, name, df, scale_cols=()):
    name = name[:31]
    if "CostScore" in df.columns:
        df = df.sort_values("CostScore", ascending=False)
    df.to_excel(writer, sheet_name=name, index=False)
    ws = writer.sheets[name]
    nrow, ncol = len(df) + 1, len(df.columns)
    ws.freeze_panes = "A2"
    ws.auto_filter.ref = "A1:%s%d" % (get_column_letter(ncol), nrow)
    for i, c in enumerate(df.columns, 1):
        body = df[c].map(lambda v: len(str(v))).max() if len(df) else 10
        ws.column_dimensions[get_column_letter(i)].width = max(10, min(42, max(len(str(c)), body) + 2))
    for cell in ws[1]:
        cell.font = Font(bold=True)
        cell.alignment = Alignment(horizontal="center")
    if nrow < 2:
        return

    def L(cn):
        return get_column_letter(list(df.columns).index(cn) + 1) if cn in df.columns else None

    for cn in ("CostScore",) + tuple(scale_cols):
        cl = L(cn)
        if cl:
            ws.conditional_formatting.add("%s2:%s%d" % (cl, cl, nrow), ColorScaleRule(**SCALE))
    sl = L("Severity")
    if sl:
        rng = "%s2:%s%d" % (sl, sl, nrow)
        for word, fill in (("Crit", RED), ("Warn", YEL), ("OK", GRN)):
            ws.conditional_formatting.add(rng, CellIsRule(operator="equal", formula=['"%s"' % word], fill=fill))


# 尺寸/密度类指标求和没有物理意义，汇总时取最大值而不是求和。
MAX_NOT_SUM = ("MaxDim", "CurDim", "Radius", "TriPerUnitR", "TriDensity", "LODBias",
               "PhysicsRatio", "VertPerTri", "TriPerUnit", "BoundsMax", "UVChannels", "LODs")


def summary_table(proc, metrics):
    """按团队汇总，外加各 Flag 的命中条数——决定先动哪一批资产靠这张表，不是靠逐行看。"""
    num = [m for m in metrics if m in proc.columns]
    g = proc.groupby("Team")
    rows = pd.DataFrame({"资产数": g.size()})
    for m in num:
        if m in MAX_NOT_SUM:
            rows[m + " 最大"] = g[m].max().round(2)
        else:
            rows[m + " 合计"] = g[m].sum().round(2)
    rows["Crit"] = g["Severity"].apply(lambda s: (s == "Crit").sum())
    rows["Warn"] = g["Severity"].apply(lambda s: (s == "Warn").sum())
    rows = rows.reset_index().sort_values("资产数", ascending=False)

    flags = {}
    for cell in proc["Flags"]:
        for f in str(cell).split(","):
            if f and f != "-":
                flags[f] = flags.get(f, 0) + 1
    flag_df = pd.DataFrame(
        [(k, v) + FLAG_DOC.get(k, ("", "")) for k, v in sorted(flags.items(), key=lambda kv: -kv[1])],
        columns=["Flag", "命中资产数", "含义", "建议动作"])
    return rows, flag_df


def main():
    ap = argparse.ArgumentParser(description="UE 资产审计 CSV -> 分团队 + 问题标签 + 条件色标记 报表")
    ap.add_argument("input")
    ap.add_argument("-o", "--output", default="asset_report.xlsx")
    ap.add_argument("--config", help="JSON 配置文件路径，覆盖内置的 teams/thresholds/column_hints/cost_weights")
    a = ap.parse_args()

    cfg = load_config(a.config)
    df = pd.read_csv(a.input)
    proc, schema, metrics, active_th = build(df, cfg)
    print("识别表类型: %s   参与评分的指标: %s" % (schema, ", ".join(metrics) or "(无)"))

    paths = proc["Path"] if "Path" in proc.columns else pd.Series("", index=proc.index)
    proc.insert(1, "Team", [team_of(n, p, cfg["teams"]) for n, p in zip(proc["Asset"], paths)])

    with pd.ExcelWriter(a.output, engine="openpyxl") as w:
        team_rows, flag_rows = summary_table(proc, metrics)
        team_at = 1
        team_rows.to_excel(w, sheet_name="Summary", index=False, startrow=team_at - 1)
        flag_at = team_at + len(team_rows) + 3
        flag_rows.to_excel(w, sheet_name="Summary", index=False, startrow=flag_at - 1)

        ws = w.sheets["Summary"]
        _fit(ws, team_rows, team_at)
        _fit(ws, flag_rows, flag_at)
        for cn in ("Crit", "Warn", "命中资产数"):
            src = team_rows if cn in team_rows.columns else flag_rows
            if cn not in src.columns or not len(src):
                continue
            top = team_at if src is team_rows else flag_at
            cl = get_column_letter(list(src.columns).index(cn) + 1)
            ws.conditional_formatting.add(
                "%s%d:%s%d" % (cl, top + 1, cl, top + len(src)), ColorScaleRule(**SCALE))
        write_sheet(w, "Overview", proc, scale_cols=metrics)
        for team in list(cfg["teams"].keys()) + ["Other"]:
            sub = proc[proc["Team"] == team]
            if len(sub):
                write_sheet(w, team, sub, scale_cols=metrics)

        weight_desc = " + ".join("%.2f*%s" % (cfg["cost_weights"][m], m) for m in metrics)
        rows = [("输入文件", a.input), ("识别表类型", schema),
                ("综合成本分 CostScore", "%s，各指标 min-max 归一后加权、权重重新归一到 1，输出 0-100" % (weight_desc or "无可用指标")),
                ("Severity", "任一指标超 crit=Crit(红)，超 warn=Warn(黄)，否则 OK(绿)")]
        rows += [("阈值 " + k, "warn≥%s / crit≥%s" % (v["warn"], v["crit"])) for k, v in active_th.items()]
        rows += [("Flag " + k, "%s；建议：%s" % (d[0], d[1])) for k, d in FLAG_DOC.items()]
        if a.config:
            rows += [("配置文件", a.config)]
        pd.DataFrame(rows, columns=["项", "说明"]).to_excel(w, sheet_name="Config", index=False)

    print("OK -> %s  (%d 行)" % (a.output, len(proc)))


if __name__ == "__main__":
    main()
