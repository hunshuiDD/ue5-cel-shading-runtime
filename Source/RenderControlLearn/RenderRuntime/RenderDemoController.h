#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RenderTypes.h"
#include "RenderDemoController.generated.h"

class ADirectionalLight;
class APointLight;
class APostProcessVolume;
class ASkyLight;
class AActor;
class ULightComponent;
class UCharacterRenderComponent;
class UMaterialInterface;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class URenderQualityDataAsset;
class URenderStateDataAsset;

// 渲染控制层日志。主要用途是把"哪个 MPC 参数被谁接管"打出来——
// MPC 运行时实例被写过之后，改资产默认值不会生效也不会报错，
// 不打日志就只能靠翻代码找写入者。
DECLARE_LOG_CATEGORY_EXTERN(LogKuroRender, Log, All);

// 一盏参与卡渲仲裁的局部灯（点光/聚光/矩形光——Spot/Rect 在引擎里都继承自 APointLight，这一个字段三种都能选）。
// 运行时读取它在脸部采样点的亮度与方向，按亮度加权参与"最终主光方向/颜色"的合成。
//
// 这里存的是 Actor 引用而不是组件引用：组件引用（TObjectPtr<ULightComponent>）在关卡里编辑时，
// Details 面板的下拉框只能选"这个 Actor 自己身上"的组件，选不到关卡里其它灯光 Actor——
// 之前想用这个字段引用场景里已放置的点光，就得把灯做成 BP_RenderControl 自己的子组件，
// 而在 Actor 蓝图里新增组件会导致实例组件被重置。改成 Actor 引用后，用法和下面的
// DirectionalLightRef / SkyLightRef 完全一致：直接从关卡大纲拖任意点光/聚光/矩形光 Actor 进来即可。
USTRUCT(BlueprintType)
struct FToonLocalLight
{
	GENERATED_BODY()

	// 局部灯 Actor，运行时通过 GetLightComponent() 读取其世界位置、颜色、强度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight")
	TObjectPtr<APointLight> LightActor = nullptr;

	// 这盏灯自身的权重倍率：0 = 本盏不参与仲裁；1 = 正常；>1 = 手动加强它的话语权。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

UENUM(BlueprintType)
enum class EEnvironmentPreset : uint8
{
	Day,
	Indoor,
	StrongColorLight,
	NoAmbient
};

USTRUCT(BlueprintType)
struct FEnvironmentPresetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float DirectionalLightIntensity = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float SkyLightIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float BaseColorPreserve = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float SceneInfluence = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float IndirectIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	FLinearColor IndirectTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float SHContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float ShadowSoftness = 0.25f;

	// 该环境下的地面反弹色（色相）。半球 SH 的下半球取这个色，让角色下半身吃到脚下的环境色。
	// 每个预设定一次即可，不用运行时手调——这是把 SHGroundColor 收进预设的意义。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	FLinearColor SHGroundColor = FLinearColor(0.12f, 0.11f, 0.10f, 1.0f);

	// 地面反照率。0-1，超过 1 会让下半球比天空亮、上下梯度翻转（见 UpdateSHCoefficients 的负值警告）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SHGroundAlbedo = 0.25f;
};

UCLASS(BlueprintType, Blueprintable)
class RENDERCONTROLLEARN_API ARenderDemoController : public AActor
{
	GENERATED_BODY()

public:
	ARenderDemoController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// 核心：根据平行光方向 + 角色朝向做"正/背光"判断，仲裁局部灯权重，
	// 合成最终主光方向并连同局部灯漫反射/高光贡献一起推到 MPC。
	UFUNCTION(BlueprintCallable, CallInEditor,Category = "Render|ToonLight")
	void UpdateFinalLightArbitration();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<ADirectionalLight> DirectionalLightRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<ASkyLight> SkyLightRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<APostProcessVolume> PostProcessVolumeRef = nullptr;

	// 屏幕空间内描边的 Post Process 材质（M_InnerLine 或其 MI）。
	// 指到这里之后，画质分档才能在 LowSpec 把它从 PostProcessVolumeRef 的 Blendable 数组里摘掉；
	// 留空则分档不碰后处理，描边始终按 PPV 里手填的状态跑。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<UMaterialInterface> InnerLineMaterialRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<AActor> TargetCharacterRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<UCharacterRenderComponent> TargetRenderComponentRef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<UMaterialParameterCollection> CharacterLightingMPC = nullptr;

	// 角色运行态全局参数（FormBlend / MaterialDebugViewMode），与光照 MPC 分离。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TObjectPtr<UMaterialParameterCollection> CharacterRunningMPC = nullptr;

	// 参与卡渲仲裁的局部灯列表（点光/聚光/矩形光都可以）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight")
	TArray<FToonLocalLight> LocalLights;

	// 是否每帧刷新仲裁：灯或角色会动时开启；静态摆拍可关掉省开销，手动点一次 UpdateFinalLightArbitration 即可
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight")
	bool bUpdateArbitrationEveryFrame = true;

	// 【核心旋钮 1】局部光相对主光的整体影响力。
	// 主光(方向光)单位是 lux、局部光(点/聚光)是 candela，物理单位不同、无法直接比较；
	// 这个系数就是把"局部光在脸上的亮度"统一换算到主光量纲的唯一入口：
	// 调大 = 局部光更容易参与、甚至抢主光方向；调小 = 更以主光为主。先用 1.0，按观感微调。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.0"))
	float LocalLightInfluence = 1.0f;

	// 【核心旋钮 2】方向"亮度主导"的锐度(幂次，≥1)。方向按主光/局部光的脸部亮度加权，再用这个幂次放大更亮的一方：
	// 1 = 纯比例混合(两灯亮度接近时方向各占一半)；越大 = 只有当一方强度"远大于"另一方时才赢家通吃、独占方向，
	//     亮度接近时仍是自然的比例混合。较暗的一方即便不主导方向，其亮度仍加进 LightModelStrength(只补强度)。
	// 默认 3：大约 3–4× 亮度差时基本由强者主导方向。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float DominanceSharpness = 3.0f;

	// 最终主光方向的时间平滑速度(仅逐帧刷新时生效)：越大越跟手，越小越平滑抗抖。0 = 不平滑，直接给目标方向。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.0"))
	float DirectionBlendSpeed = 8.0f;

	// 局部光与脸部采样点的最小有效距离(cm)：小于它就认为方向不可靠(灯贴脸/进体内)，退化用主光方向兜底，避免零向量污染
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "1.0"))
	float MinKeyLightSampleDistance = 15.0f;

	// 脸部采样点：优先用角色骨架上这个 Socket/骨骼的世界坐标；没有则退化为角色原点。局部光的距离/方向都以它为基准
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight")
	FName FaceSampleSocketName = TEXT("head");

	// 平行光按此参考强度归一化：FinalLightIntensity = (主光+局部光合计亮度) / 该参考值。
	// 按你材质已经调好观感时的主光 Intensity 填(默认 4)，让常态下 FinalLightIntensity≈1。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.01"))
	float MainLightReferenceIntensity = 4.0f;

	// FinalLightIntensity 上限：防止平行光调亮时无限线性放大、把高光/亮部顶到死白。
	// 默认 1.5：常态(强度=参考值)时约等于1，允许一定的"更亮"余量，但封顶不再无限往上冲。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "1.0"))
	float FinalLightIntensityCap = 1.5f;

	// FinalLightIntensity 下限：暗环境(洞穴/夜景/关灯)下保证角色仍有基础亮部，不塌成一片黑。
	// 为什么下限比上限更重要：NPR 角色的可读性靠明暗交界，光一低到某个点交界就消失、
	// 整个形体一起糊掉——这是"换环境不稳"里最先崩的一项。0.35 = 常态观感的 35%。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.0"))
	float FinalLightIntensityFloor = 0.35f;

	// NPR 光照模型的风格化权重（Ramp 对比 / 阴影浓淡的总控），是美术手调常量，不由运行时光强驱动。
	// 为什么独立：运行时光强走 FinalLightIntensity；两者一旦共用一个参数，调场景亮度就会连带
	// 改变风格化强度，换环境时角色的明暗交界会跟着漂——正是"环境一致性"要消除的失稳来源。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.0"))
	float LightModelStrength = 1.0f;

	// —— 一阶(L1) SH 环境光 · 当前生效值 ——
	// 下半球反射色。角色脚下的地面颜色通过 L1z 影响下半身暗部，是 L1 相比单方向近似唯一
	// 肉眼可辨的收益。正常由环境预设写入(见 FEnvironmentPresetSettings/ApplyEnvironmentSettings)，
	// 切预设自动更新；这里直接暴露只为调试时手动覆盖，不必每次手设。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight")
	FLinearColor SHGroundColor = FLinearColor(0.12f, 0.11f, 0.10f, 1.0f);

	// 地面反照率。真实值通常 0.1-0.35。上限锁 1.0：Ground 超过 Sky 会使 L1z 变负、
	// 上下梯度翻转(底部比顶部亮)，物理不可能；0 = 地面不反光，SHGroundColor 恒为黑无效果。
	// 同样由预设驱动，此处手调仅用于调试。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ToonLight", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SHGroundAlbedo = 0.25f;

	// 调试只读：当前主光在最终方向里的占比(1=纯主光，越小=局部光参与越多)，运行时在细节面板看仲裁是否合理
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ToonLight")
	float CurrentMainLightWeight = 1.0f;

	// 调试只读：本帧实际算出并下发的 SH 系数。PIE 中直接在细节面板看这两个值，
	// 就能把"C++ 没算" / "算了但材质没吃到"这两类故障分开——两者的现象都是"参数没效果"，
	// 但前者这里会是全黑或不更新，后者这里数值正常。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ToonLight")
	FLinearColor CurrentSH_L00 = FLinearColor::Black;

	// L1z 是上下梯度的唯一来源：它接近 0 就说明 Sky 与 Ground 太接近，画面必然是平的。
	// 注意面板上的色块看不出负值（负数一律显示为黑），所以负值另有日志警告。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ToonLight")
	FLinearColor CurrentSH_L1z = FLinearColor::Black;

	// 统一材质中间量可视化。Final 不改变现有材质输出，其余模式由 MF_MaterialDebugView 解释。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Debug")
	EMaterialDebugViewMode DebugViewMode = EMaterialDebugViewMode::Final;

	// MPC 参数名：NPR 光照模型的风格化权重（材质里控制 Ramp 对比/阴影浓淡，不承载运行时光强）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_LightModelStrengthName = TEXT("LightModelStrength");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto")
	bool bApplyDefaultPresetOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto")
	bool bApplyDefaultQualityOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto")
	bool bApplyDefaultStateOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presets")
	EEnvironmentPreset DefaultPreset = EEnvironmentPreset::Day;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presets")
	FEnvironmentPresetSettings DayPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presets")
	FEnvironmentPresetSettings IndoorPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presets")
	FEnvironmentPresetSettings StrongColorLightPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presets")
	FEnvironmentPresetSettings NoAmbientPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	TObjectPtr<URenderQualityDataAsset> QualityHighPC = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	TObjectPtr<URenderQualityDataAsset> QualityMidPC = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	TObjectPtr<URenderQualityDataAsset> QualityLowSpec = nullptr;

	// 引擎侧画质 CVar 的原始值捕获：首次应用画质前记录场景原本的 SSR/AO 设置。
	// HighPC/开启档恢复该原值（不在最高档改动场景色调），仅降档关闭时置 0。运行时缓存，不入 Config。
	bool bQualityCVarsCaptured = false;
	int32 OriginalSSRQuality = 3;
	int32 OriginalAOLevels = -1;
	int32 OriginalMaterialQualityLevel = 3;

	// 是否按档下发 r.MaterialQualityLevel。这是材质里唯一能拿到**编译期常量**的降级通道
	// （对应 Quality Switch 节点），也是唯一能真正减少 shader 指令数的路径；MPC/MID 标量做不到。
	// 代价：切换会触发该档 shader 的编译/缓存切换，首次切档必然卡顿，因此适合“启动时定档”
	// 而不是运行时秒切。作品集录屏演示三档对比时建议开启，交互 Demo 里演示秒切时关闭。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bDriveMaterialQualityLevel = false;

	// 内描边走哪套实现：false = 后处理材质 M_InnerLine（L1）；true = NPRRenderPipeline 插件的
	// SceneViewExtension + RDG 自定义 Pass（L3）。两套画的是同一条线，同时开会出双线，所以互斥。
	//
	// 2026-08-22 默认改为 true（SVE）。**两套实现都保留**，这个开关就是切换入口，不要删任一套。
	// 依据（Scorecard §10）：融合为单 Pass 后两版成本持平（各 0.08 ms @1920×1080），SVE 不再有
	// 性能劣势；但也**没有性能收益，更没有能力收益**（线宽解耦已实测判死，见 §10.3）。
	// 默认选 SVE 的实际理由是「引擎层 RDG 自定义 pass 的能力展示价值」—— 这是非技术理由，
	// 写进任何文档都要标明，别把它包装成技术收益。保留 PPM 版是为了随时能做同机位对照。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bUseSceneViewExtensionInnerLine = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TObjectPtr<URenderStateDataAsset> StateNormal = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TObjectPtr<URenderStateDataAsset> StateHitFlash = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TObjectPtr<URenderStateDataAsset> StateRimBoost = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TObjectPtr<URenderStateDataAsset> StateOutlineBoost = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TObjectPtr<URenderStateDataAsset> StateBattleTint = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_BaseColorPreserveName = TEXT("BaseColorPreserve");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SceneInfluenceName = TEXT("SceneInfluence");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_IndirectIntensityName = TEXT("IndirectIntensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_IndirectTintName = TEXT("IndirectTint");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SHContrastName = TEXT("SHContrast");

	// L0+L1 近似 SH 的主方向(世界空间)。每帧取主光反方向作为兜底——没有真实 SH 采样源时，
	// "背对主光的一面更贴近环境天光"是最合理的近似，让 SceneIndirect 有方向可依。
	// 注意：材质改用下面的真 L1 SH 系数之后这个参数就没有消费者了，届时可连同推送一起删。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SHDirectionName = TEXT("SHDirection");

	// 一阶 SH 系数(每通道 4 个，RGB 共 12 float)。L00=常数项，L1x/L1y/L1z=线性项，
	// 直接对应 UE 世界空间轴向。投影与求值两端约定一致即可，无需 y-up 转换。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SH_L00Name = TEXT("SH_L00");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SH_L1xName = TEXT("SH_L1x");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SH_L1yName = TEXT("SH_L1y");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_SH_L1zName = TEXT("SH_L1z");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_ShadowSoftnessName = TEXT("ShadowSoftness");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName FaceShadowSoftnessParamName = TEXT("FaceShadowSoftness");

	// 仲裁后输出给材质的最终主光方向（Vector，世界空间，xyz 有效）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_FinalLightDirName = TEXT("FinalLightDir");

	// 最终主光颜色（取平行光颜色）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_FinalLightColorName = TEXT("FinalLightColor");

	// 仲裁后输出给材质的最终主光强度（主光+局部光合计亮度归一化后封顶），与 Dir/Color 构成三件套
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_FinalLightIntensityName = TEXT("FinalLightIntensity");

	// 主光在最终方向里占的权重比例（1=完全主光主导，越小说明局部灯参与越多，便于调试可视化）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_MainLightWeightName = TEXT("MainLightWeight");

	// 参与仲裁的局部灯聚合颜色
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_LocalLightColorName = TEXT("LocalLightColor");

	// 局部灯聚合后的漫反射贡献比例（材质里乘到局部灯 diffuse 项）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_LocalDiffuseScaleName = TEXT("LocalDiffuseScale");

	// 局部灯聚合后的高光贡献比例（材质里乘到局部灯 specular 项）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_LocalSpecularScaleName = TEXT("LocalSpecularScale");

	// —— 分光源原始量：供材质 mode 9 LightWeights 逐像素按"最亮的灯"取色 ——
	// 为什么单独暴露：FinalLightDir/FinalLightColor 已是混合结果，材质无法区分主光与局部光，
	// 逐像素比较 saturate(N·L)×Bright 取胜者颜色时必须拿到未混合的分光源方向/颜色/亮度。
	// 主光方向（世界空间，表面指向光源，= -平行光前向）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_MainLightDirName = TEXT("MainLightDir");

	// 主光原始颜色（未参与 Dom 混合，供逐像素主光胜出时取色）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_MainLightColorName = TEXT("MainLightColor");

	// 主光亮度货币（Intensity×颜色感知亮度），与 LocalLightBright 同量纲可直接比较
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_MainLightBrightName = TEXT("MainLightBright");

	// 局部灯聚合方向（脸部采样点 → 灯，表面指向光源；退化时兜底为主光方向）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_LocalLightDirName = TEXT("LocalLightDir");

	// 局部灯聚合亮度货币（∑ 各灯脸部有效亮度×颜色亮度×影响力×权重），与 MainLightBright 同量纲
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_LocalLightBrightName = TEXT("LocalLightBright");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_MaterialDebugViewModeName = TEXT("MaterialDebugViewMode");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_FurParallaxSampleCountName = TEXT("FurParallaxSampleCount");

	// 双形态混合主控标量（0=一形态,1=二形态）。见 PRD §12.4/§12.4.1。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material|Names")
	FName MPC_FormBlendName = TEXT("FormBlend");

	UFUNCTION(BlueprintCallable, Category = "Render|Setup")
	void InitializeController();

	UFUNCTION(BlueprintCallable, Category = "Render|Environment")
	void ApplyEnvironmentPreset(EEnvironmentPreset Preset);

	UFUNCTION(BlueprintCallable, Category = "Render|Environment")
	void ApplyEnvironmentSettings(const FEnvironmentPresetSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "Render|Environment")
	void ApplyPresetDay();

	UFUNCTION(BlueprintCallable, Category = "Render|Environment")
	void ApplyPresetIndoor();

	UFUNCTION(BlueprintCallable, Category = "Render|Environment")
	void ApplyPresetStrongColorLight();

	UFUNCTION(BlueprintCallable, Category = "Render|Environment")
	void ApplyPresetNoAmbient();

	// 三档带 CallInEditor：选中关卡里的 Controller，Details 面板直接出按钮，不必进 PIE 再走蓝图。
	// 用途是画质分档的编辑器内快速验收（尤其 A4：LowSpec 下 NPRInnerLine 应整个从 RDG 消失）。
	// 档位数值来自 URenderProjectSettings 的 HighPCQuality / MidPCQuality / LowSpecQuality。
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Render|Quality")
	void ApplyHighPC();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Render|Quality")
	void ApplyMidPC();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Render|Quality")
	void ApplyLowSpec();

	UFUNCTION(BlueprintCallable, Category = "Render|Quality")
	void ApplyQualityData(URenderQualityDataAsset* QualityData);

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ApplyStateData(URenderStateDataAsset* StateData);

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ApplyStateNormal();

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ApplyStateHitFlash();

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ApplyStateRimBoost();

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ApplyStateOutlineBoost();

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ApplyStateBattleTint();

	UFUNCTION(BlueprintCallable, Category = "Render|State")
	void ResetRenderState();

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetBaseColorPreserve(float Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetSceneInfluence(float Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetIndirectIntensity(float Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetIndirectTint(FLinearColor Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetSHContrast(float Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetShadowSoftness(float Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetLightModelStrength(float Value);

	UFUNCTION(BlueprintCallable, Category = "Render|Slider")
	void SetFaceShadowSoftness(float Value);

	// —— 双形态 FormBlend（持久连续态，独立通道，不进渲染状态栈；见 PRD §12.4）——
	// 0=一形态，1=二形态，可停在任意值；空间接缝语义由材质 MF_FormSeam 解释。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Form", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FormBlend = 0.0f;

	// 立即设定 FormBlend（拖滑杆/按键），会打断正在进行的过渡
	UFUNCTION(BlueprintCallable, Category = "Render|Form")
	void SetFormBlend(float Value);

	// 在 Duration 秒内平滑过渡到 Target（0 时长=瞬切）；过渡中每帧写 MPC，可被 SetFormBlend 打断
	UFUNCTION(BlueprintCallable, Category = "Render|Form")
	void TransitionToForm(float Target, float Duration);

	UFUNCTION(BlueprintCallable, Category = "Render|Debug")
	void SetMaterialDebugViewMode(EMaterialDebugViewMode Mode);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Render|Debug")
	void ApplyMaterialDebugViewMode();

private:
	// 【不要缓存 MPC 实例】UMaterialParameterCollectionInstance 的 Outer 是 UWorld。
	// PIE 启动时会 duplicate 整个编辑器 World，UPROPERTY(Transient) 字段照样被复制，
	// 而目标实例(编辑器 World 的那个)不在复制图里，指针于是原样保留 —— 结果 PIE 里
	// 所有 SetMPC* 都写进了编辑器 World 的实例，PIE 自己的实例停在资产默认值，
	// 表现就是"编辑器点按钮能好，一进 PIE 反而黑"。
	// GetParameterCollectionInstance 内部只是 TMap 查表，每次现取即可，不值得缓存。

	// 上一帧平滑后的主光方向，用于时间平滑，避免灯/角色移动时方向抖动
	FVector SmoothedLightDir = FVector::ZeroVector;

	// FormBlend 平滑过渡状态（TransitionToForm 用）
	float FormBlendTarget = 0.0f;
	float FormBlendSpeed = 0.0f; // 每秒变化量
	bool bFormTransitioning = false;

	// 画质三档 + 五个渲染状态都不再有硬编码兜底：统一读 URenderProjectSettings
	// （画质见 ApplyHighPC/MidPC/LowSpec，状态见 ApplyStateNormal/HitFlash/RimBoost/OutlineBoost/BattleTint 的 fallback 分支）。
	void ApplyQualitySettingsInternal(const FRenderQualitySettings& Settings);
	UCharacterRenderComponent* ResolveRenderComponent();
	// 把当前 FormBlend 下发给渲染组件做一/二形态可见性切换（SetFormBlend/过渡Tick/开局各调一次）。
	void ApplyFormVisibilityToRenderComponent();
	UMaterialParameterCollectionInstance* ResolveMPCInstance();
	UMaterialParameterCollectionInstance* ResolveRunningMPCInstance();
	void SetMPCScalar(FName ParameterName, float Value);
	void SetMPCVector(FName ParameterName, const FLinearColor& Value);
	void SetRunningMPCScalar(FName ParameterName, float Value);
	// 按半球模型(上半球=天光、下半球=地面反光)解析投影出一阶 SH 系数并下发 MPC。
	void UpdateSHCoefficients(const FLinearColor& SkyColor);
	// 开局打印 MPC 参数归属表，避免"参数改了没反应又不报错"只能靠翻代码排查。
	void LogParameterOwnership() const;
	// L1z 出现负通道时只在状态切换那一帧警告，避免每帧刷屏。
	bool bWarnedNegativeL1z = false;
	float ComputeLocalLightIntensityAtLocation(const ULightComponent* LightComp, const FVector& Location) const;
	FVector ResolveFaceSampleLocation() const;
};
