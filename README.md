# UE5 二次元角色渲染系统 · 运行时与工具

在 UE5 里搭一套可运行、可解释、可优化的二次元角色渲染系统：**材质、运行时、工具三层各自能被单独验证**。
无引擎源码改动，未新增 Shading Model。

引擎 UE 5.7 → 5.8（启动器版）· DX12 / SM6 · 独立 Technical Artist

---

## 这个仓库里有什么

| 目录 | 内容 |
|---|---|
| `Source/RenderControlLearn/` | C++ 数据驱动运行时控制层：状态数据资产、角色渲染组件、函数库、质量分档、Project Settings |
| `Plugins/NPRRenderPipeline/` | 渲染管线扩展插件：`FSceneViewExtension` + RDG 自定义 Pass 实现的屏幕空间内描边（全局 shader `FNPRInnerLinePS`，参数走 12 个 `r.NPR.InnerLine.*` CVar） |
| `Plugins/PreObjectShadow/` | 角色自阴影插件：正交深度采集 ＋ `Shader/PerShadow.ush`（阴影数学全部收敛在这一个文件里） |
| `Plugins/AssetNamingTool/` | 资产命名规范检查插件（C++），配置走 Project Settings，全量核对保持只读 |
| `Tools/AssetAudit/` | 资产审计报表生成器：读 Texture Stats / Primitive Stats，派生贴图内存与面数密度并分级输出 |
| `Tools/UE_Python/` | UE 编辑器脚本：性能采样、资产引用修复 |
| `Tools/Blender/` | Blender 脚本：程序化 blockout（幂等重建）、morph / 顶点色通道诊断 |
| `Scripts/` | PowerShell：材质摘要导出 |

## 这个仓库里没有什么

- **没有 `.uasset`**。材质、蓝图工具（EUW）、关卡都是二进制资产，不适合进 Git 做 review，也无法 diff。
  这部分以截图与录屏呈现（见下方 Demo Reel），可现场演示。
- **没有角色与场景模型**。角色为第三方资产，仅用于渲染技术验证与个人学习，不包含在本仓库中。
- 没有编译产物（`Binaries` / `Intermediate` / `DerivedDataCache` / `Saved`）。

---

## 系统由四层组成

```
状态数据                调用入口                执行组件                      材质实例层
URenderStateDataAsset → ARenderDemoController → UCharacterRenderComponent → UMaterialInstanceDynamic
只描述"是什么"           触发时机                 如何应用、何时恢复             只接收最终参数
StatePayload            URenderFunctionLibrary   ActiveStates · Snapshot      Face · Hair · Cloth · Outline
Duration · WeightCurve
```

分层的目的是**让任何一个异常都能归因到具体一层**，而不是靠试参数。

- 状态资产携带 `StatePayload` / `Duration` / `WeightCurve`，组件逐帧算权重写入材质实例；
  `Duration` 结束后 `bRestoreOriginalValues` 触发 `RestoreSnapshot()` 回滚到材质基线。
- `URenderQualityDataAsset` 描述 High / Mid / Low 三档开关与数值，内置兜底默认值；
  某档加载失败退回上一可用配置，而不是停在半初始化状态。

关键类型见 `Source/RenderControlLearn/RenderRuntime/`。

---

## 几个关键实现

### 灯光仲裁：只认一个最终光

NPR 的面部 SDF、发丝高光、边缘光都需要一个明确的光方向才能算，但场景里同时有主光和若干局部光。
控制层每帧从 MainLight 与 LocalLight 仲裁出唯一的 `FinalLightDir / FinalLightColor / FinalLightIntensity`，
经 Material Parameter Collection 统一下发给所有角色材质
（MPC 引用在 `ShadowCaptureComponent` / `ARenderDemoController` 上以 `TObjectPtr<UMaterialParameterCollection>` 配置，
阴影采集与角色光照分属两个 MPC；MPC 本身是 `.uasset`，不在本仓库）。

**为什么必须这么做**：卡通渲染依赖 ramp 查表还原立绘意图，不具备物理叠加性，多光源同时作用会污染面部色阶。
**代价**：放弃多光源真实感，换取角色换场景、进出局部光时表现稳定可读。

### 角色自阴影 PerObjectShadow

VSM 能解决角色与场景之间的大范围投影，但头发投脸、饰品投身体这类角色内部遮挡，在二次元需求下既不够稳也不够可控。
本插件从主光方向对单个角色做一次正交深度采集，在材质里比较遮挡体与接收点深度，返回 0..1 的 ShadowTerm，
**与已有阴影取 `min()` 而不是相乘**，避免双重变暗。

| 问题 | 处理 |
|---|---|
| 深度口径 | 正交投影 ＋ R32f，全程线性世界空间 cm——绕开 Reversed-Z 与 8bit 截断 |
| 精度分配 | `ShowOnlyActors` 只画目标角色；采集框缩到头骨，把 2048 集中给脸；无效头骨自动回退包围盒 |
| 防闪烁 | `bSnapToTexel` 量化采集中心 ＋ Poisson 样本逐像素旋转 ＋ Const / Slope / Normal 三类 Bias |

采集与 Shader 解耦：采集组件在主模块管相机、RT 与 MPC，插件只注册 Shader 虚拟路径并提供 `PerShadow.ush`，
两边只靠 RT 与 MPC 交换数据，不绑定具体角色材质。

**为什么把阴影数学收进 `.ush`**：旧实现把深度比较散在十几个材质节点里，符号、坐标系、单位到处漂移，修不收敛。

三档过滤：PCSS 可变半影（每帧）/ 16 抽 Poisson PCF（每 2~3 帧）/ 单次采样硬阴影（降频或停用）。

### 一阶 SH 间接光

卡渲角色把光照收回自己算之后，换场景就容易"不融"。C++ 侧对天光做半球解析投影，
每帧推出 L00 / L1x / L1y / L1z 四个向量，材质端算出低频间接光，接进 `BaseColorPreserve`。

```
SH(n)  = 0.282095 · L00 + 0.325735 · dot(L1, n)
result = max(SH(n), 0)
```

**停在 L1 的原因**：末端 `BaseColorPreserve`（0.65~0.85）会把 SH 精度的大半 lerp 掉，
继续升到 L2 的九系数在最终画面上换不回可见收益。

---

## 性能：一行 cvar，52 → 60 FPS

测量环境：独立进程游戏（非 PIE）· 原生 1920×1080 · `CaptureEveryNFrames=4` · PerObjectShadow 开 ·
固定机位 · UE 5.8 / DX12 SM6。三档 Draws / Prims / RenderRes 一致，单变量对照。

| 档 | Frame | Draw | RHIT | GPU |
|---|---|---|---|---|
| A 硬件光追 | 19.12 | 20.86 | 16.23 | 11.33 |
| **B 软件追踪 ★** | **16.60** | **16.66** | **11.56** | **12.22** |
| C 关 Lumen | 13.88 | 13.73 | 10.20 | 8.70 |

**真凶**：角色是骨骼网格且每帧有动画，Lumen 硬件光追必须每帧重建 BVH——**开销压在渲染线程，不在 GPU**。
换成软件追踪把成本转到 GPU；本项目渲染线程 bound、GPU 有余量，纯赚。
A→B 的 Draw 降 4.20 ms 而 GPU 升 0.89 ms，成本转移方向在数字上一眼可见。C 档只是理论下限，为保住 GI 没有采纳。

RenderDoc 截帧证实了推论：过滤 `RayTracingGeometry`，同一帧出现两次——自阴影 SceneCapture 视图一次，主视图一次。
这解释了"降低自阴影采集频率"和"关闭硬件光追"为何互相放大：光追开销按视图数量倍增，单独评估任一项都会低估另一项。

**被数据否掉的五条路**（每一条当时看都"必然有效"）：

| 路径 | 实测 | 判定 |
|---|---|---|
| 自阴影 RT 2048 → 1024 | 省 GPU 3.03 ms，FrameTime 只降 0.21 ms | 证伪 |
| 降渲染分辨率 1080p → 720p | 省 5~6 ms GPU，帧时间零变化 | 无效 |
| `r.RHIThread.Enable 0` | Frame 18.75 → 28.74，反而恶化 10 ms | 证伪 |
| `r.RDG.AsyncCompute 0` | 18.29 → 18.05，落在噪声内 | 证伪 |
| `r.AllowOcclusionQueries 0` | 单次显示有效，反复切换不可复现 | 证伪 |

**判定优化是否有效要看 Draw，不是看 GPU**：渲染线程 bound 的项目里，只动 GPU 的优化换不回帧率。
读数可信度判据：`Draw ≈ Frame` 且 `RHIT` 行存在 → 环境正确；同配置运行间波动约 ±1.4 ms，**跨运行差值 < 1.5 ms 不算收益**；
A/B 必须固定机位。Pass 级成本一律用 `ProfileGPU` 读绝对值，不做 `stat unit` 相减。

---

## 工具

| 工具 | 形态 | 说明 |
|---|---|---|
| 资产审计报表 | Python | 读 Texture Stats / Primitive Stats，派生贴图内存、面数密度，按规则分级输出可检查报表。方法参考行业公开实践，未把方法论归为原创 |
| 资产命名检查 | C++ 插件 | 前后缀、忽略路径、需剥除的自动后缀配置到 Project Settings；全量核对保持只读，只报告不改名 |
| 统一材质 Debug View | C++ ＋ 材质函数 | 由 C++ 写入运行时 MPC，材质函数切换 BaseColor、法线、Face SDF、毛发与阴影等中间结果 |
| 材质实例参数复制 | 蓝图 EUW | 按 Scalar / Vector / Texture / Static Switch 分类批量复制。**资产为 `.uasset` 未入库** |

---

## 一次真实的纠错

最初以为毛领要运行时实例化 32 层几何壳，写了 32 个 `SkeletalMeshComponent`，帧数暴跌。
逐槽隔离定位后改为像素着色器视差展开壳层，删掉几何壳后帧数恢复，画面效果也很好。

**结论**：游戏内无法大量实例化，制作材质效果时必须先考虑运行时帧数。

---

## 相关链接

- Demo Reel（03:10，含分章时间码）：https://www.bilibili.com/video/BV1yp3x6kEuH
- Blender → UE5 资产验证与导入工具链：https://github.com/hunshuiDD/ai-dcc-ue5-pipeline

## 素材声明

角色与场景模型为第三方资产，仅用于渲染技术验证与个人学习，**不包含在本仓库中**。
本仓库内容为材质设计、C++ 运行时、插件、工具与性能优化部分，均为本人实现。
