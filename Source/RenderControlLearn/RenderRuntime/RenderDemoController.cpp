#include "RenderDemoController.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "CharacterRenderComponent.h"
#include "ShadowCaptureComponent.h"
#include "RenderFunctionLibrary.h"
#include "RenderProjectSettings.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY(LogKuroRender);

ARenderDemoController::ARenderDemoController()
{
	// 开启 Tick：仲裁需要在灯/角色移动时每帧刷新最终主光方向
	PrimaryActorTick.bCanEverTick = true;

	DayPreset.DirectionalLightIntensity = 4.0f;
	DayPreset.SkyLightIntensity = 1.0f;
	DayPreset.BaseColorPreserve = 0.65f;
	DayPreset.SceneInfluence = 0.45f;
	DayPreset.IndirectIntensity = 1.0f;
	DayPreset.IndirectTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	DayPreset.SHContrast = 1.0f;
	DayPreset.ShadowSoftness = 0.25f;
	// 室外白天：偏暖的土/草地反弹，反照率取中等。
	DayPreset.SHGroundColor = FLinearColor(0.14f, 0.13f, 0.09f, 1.0f);
	DayPreset.SHGroundAlbedo = 0.30f;

	IndoorPreset.DirectionalLightIntensity = 1.0f;
	IndoorPreset.SkyLightIntensity = 1.5f;
	IndoorPreset.BaseColorPreserve = 0.7f;
	IndoorPreset.SceneInfluence = 0.3f;
	IndoorPreset.IndirectIntensity = 1.4f;
	IndoorPreset.IndirectTint = FLinearColor(0.95f, 0.98f, 1.0f, 1.0f);
	IndoorPreset.SHContrast = 0.7f;
	IndoorPreset.ShadowSoftness = 0.45f;
	// 室内(甜点小屋)：暖木地板/地毯反弹，反照率略高、偏暖。
	IndoorPreset.SHGroundColor = FLinearColor(0.16f, 0.11f, 0.08f, 1.0f);
	IndoorPreset.SHGroundAlbedo = 0.32f;

	StrongColorLightPreset.DirectionalLightIntensity = 5.0f;
	StrongColorLightPreset.SkyLightIntensity = 0.7f;
	StrongColorLightPreset.BaseColorPreserve = 0.85f;
	StrongColorLightPreset.SceneInfluence = 0.6f;
	StrongColorLightPreset.IndirectIntensity = 0.8f;
	StrongColorLightPreset.IndirectTint = FLinearColor(1.0f, 0.45f, 0.8f, 1.0f);
	StrongColorLightPreset.SHContrast = 1.2f;
	StrongColorLightPreset.ShadowSoftness = 0.2f;
	// 强色光：地面反弹压低，别和主色光抢，反照率取小值保持中性暗。
	StrongColorLightPreset.SHGroundColor = FLinearColor(0.08f, 0.07f, 0.09f, 1.0f);
	StrongColorLightPreset.SHGroundAlbedo = 0.18f;

	NoAmbientPreset.DirectionalLightIntensity = 3.0f;
	NoAmbientPreset.SkyLightIntensity = 0.0f;
	NoAmbientPreset.BaseColorPreserve = 0.8f;
	NoAmbientPreset.SceneInfluence = 0.2f;
	NoAmbientPreset.IndirectIntensity = 0.0f;
	NoAmbientPreset.IndirectTint = FLinearColor::Black;
	NoAmbientPreset.SHContrast = 1.0f;
	NoAmbientPreset.ShadowSoftness = 0.15f;
	// 无环境光：天光和 IndirectIntensity 都为 0，间接项本就被乘没，地面反照率归 0 保持一致。
	NoAmbientPreset.SHGroundColor = FLinearColor::Black;
	NoAmbientPreset.SHGroundAlbedo = 0.0f;
}

void ARenderDemoController::BeginPlay()
{
	Super::BeginPlay();

	// 把角色 Actor 可靠地喂给 ShadowCapture：绕开"实例上手填组件属性会被重置"的坑。
	// 组件下一帧的 UpdateCaptureAndPush 会据此刷新白名单并对准角色。
	if (UShadowCaptureComponent* SC = FindComponentByClass<UShadowCaptureComponent>())
	{
		if (TargetCharacterRef)
		{
			SC->TargetActor = TargetCharacterRef;
		}
	}

	InitializeController();
	ApplyMaterialDebugViewMode();

	if (bApplyDefaultPresetOnBeginPlay)
	{
		ApplyEnvironmentPreset(DefaultPreset);
	}

	if (bApplyDefaultQualityOnBeginPlay)
	{
		ApplyHighPC();
	}

	if (bApplyDefaultStateOnBeginPlay)
	{
		ApplyStateNormal();
	}

	// 风格化权重是静态量，开局推一次即可（不进每帧仲裁）
	SetLightModelStrength(LightModelStrength);

	// 开局先仲裁一次，保证静态摆拍（不每帧刷新）时材质也有正确的 FinalLightDir
	UpdateFinalLightArbitration();

	// 放在所有写入之后：此时各开关的实际状态已确定，打出来的归属表才是这一局真实生效的那份
	LogParameterOwnership();
}

void ARenderDemoController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bUpdateArbitrationEveryFrame)
	{
		UpdateFinalLightArbitration();
	}

	// FormBlend 平滑过渡：仅过渡中每帧写 Running MPC，静止不占用（Sequencer 可独占该标量）
	if (bFormTransitioning)
	{
		FormBlend = FMath::FInterpConstantTo(FormBlend, FormBlendTarget, DeltaSeconds, FormBlendSpeed);
		SetRunningMPCScalar(MPC_FormBlendName, FormBlend);
		ApplyFormVisibilityToRenderComponent(); // 过渡期两套并显，到端点这一帧会隐藏另一套
		if (FMath::IsNearlyEqual(FormBlend, FormBlendTarget))
		{
			bFormTransitioning = false;
		}
	}
}

void ARenderDemoController::InitializeController()
{
	ResolveRenderComponent();
	// 不再预热 MPC 实例：Resolve 现在每次现取，没有可预热的缓存。
	SetRunningMPCScalar(MPC_FormBlendName, FormBlend); // 开局同步 FormBlend 初值
	ApplyFormVisibilityToRenderComponent();            // 开局按当前 FormBlend 定初始可见性
}

void ARenderDemoController::UpdateFinalLightArbitration()
{
	// 主光(方向光)是整套卡渲的"基准键光"，无效就不仲裁
	const UDirectionalLightComponent *MainLightComp =
		DirectionalLightRef ? Cast<UDirectionalLightComponent>(DirectionalLightRef->GetLightComponent()) : nullptr;
	if (!MainLightComp)
	{
		return;
	}

	// —— 主光基本量 ——
	// L 向量：表面"指向光源"的方向 = 平行光前向的反方向
	const FVector MainL = (-MainLightComp->GetDirection()).GetSafeNormal();
	// 不加 const：GetLuminance() 需在非 const 对象上调用更保险（不依赖其 const 限定，跨引擎版本都可编译）
	FLinearColor MainColor = MainLightComp->GetLightColor();
	// 统一"亮度货币" = 强度 × 颜色感知亮度。主光是所有权重的基准。
	const float MainWeight = FMath::Max(MainLightComp->Intensity * MainColor.GetLuminance(), KINDA_SMALL_NUMBER);

	// 脸部采样点：局部光的距离衰减和方向都以它为基准
	const FVector FaceLoc = ResolveFaceSampleLocation();

	// —— 聚合所有局部光：方向、颜色、脸部总亮度(原始值) ——
	FVector LocalDirAccum = FVector::ZeroVector;
	FLinearColor LocalColorAccum = FLinearColor::Black;
	float LocalWeightRaw = 0.0f;

	for (const FToonLocalLight &Local : LocalLights)
	{
		if (!Local.LightActor)
		{
			continue;
		}
		ULightComponent *LightComp = Local.LightActor->GetLightComponent();
		if (!LightComp)
		{
			continue;
		}

		// 这盏灯在脸部的有效亮度(含距离/锥角衰减) × 颜色亮度 × 全局影响力 × 本盏权重 → 统一到主光量纲
		const float FaceBrightness = ComputeLocalLightIntensityAtLocation(LightComp, FaceLoc);
		FLinearColor LocalColor = LightComp->GetLightColor();
		const float Wi = FaceBrightness * LocalColor.GetLuminance() * LocalLightInfluence * FMath::Max(Local.Weight, 0.0f);
		if (Wi <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// 方向：脸 → 灯(表面指向光源)。距离过近方向不可靠时退化用主光方向，避免零向量污染加权和
		const FVector Delta = LightComp->GetComponentLocation() - FaceLoc;
		const FVector Li = (Delta.SizeSquared() > FMath::Square(MinKeyLightSampleDistance))
							   ? Delta.GetSafeNormal()
							   : MainL;

		LocalDirAccum += Li * Wi;
		LocalColorAccum += LocalColor * Wi;
		LocalWeightRaw += Wi;
	}

	// —— 局部光聚合方向/颜色 ——
	FVector LocalDir = LocalDirAccum.GetSafeNormal();
	if (LocalDir.IsNearlyZero())
	{
		LocalDir = MainL; // 局部光方向退化(互相抵消/没有局部光)时兜底
	}
	const FLinearColor LocalColorAvg = (LocalWeightRaw > KINDA_SMALL_NUMBER)
									   ? LocalColorAccum * (1.0f / LocalWeightRaw)
									   : MainColor;

	const float TotalWeight = MainWeight + LocalWeightRaw;

	// —— 核心：方向由"更亮的一方"主导，但只有强度"远大于"时才完全赢家通吃 ——
	// 把两者亮度归一到 [0,1](更亮的一方=1)再取幂 DominanceSharpness：
	//   p=1 → 纯比例混合(两灯亮度接近时方向各占一半)；p 越大 → 亮度差越大、强者越快独占方向，
	//   亮度接近时仍是自然的比例混合。较暗的一方即便不主导方向，其亮度仍进下面的 FinalLightIntensity。
	// 归一化后再取幂：既防大数取幂溢出，又让"倍数关系"而非绝对值决定主导程度。
	const float P = FMath::Max(DominanceSharpness, 1.0f);
	const float MaxW = FMath::Max(MainWeight, LocalWeightRaw); // MainWeight 有下限，MaxW>0，除法安全
	const float MainPow = FMath::Pow(MainWeight / MaxW, P);
	const float LocalPow = FMath::Pow(LocalWeightRaw / MaxW, P);
	const float PowSum = MainPow + LocalPow;
	const float T = (PowSum > KINDA_SMALL_NUMBER) ? (LocalPow / PowSum) : 0.0f; // 0=方向纯主光, 1=纯局部光

	// 线性亮度占比：给材质补光强度用(方向用的是上面幂次放大后的 T)
	const float Dom = (TotalWeight > KINDA_SMALL_NUMBER) ? (LocalWeightRaw / TotalWeight) : 0.0f;

	// 方向：主光方向 ↔ 局部光方向 之间按 T 选择
	FVector TargetDir = FMath::Lerp(MainL, LocalDir, T).GetSafeNormal();
	if (TargetDir.IsNearlyZero())
	{
		TargetDir = MainL;
	}

	// 最终光颜色按"亮度占比 Dom"混合，而不是按方向赢家 T——这样局部灯即使不主导方向，
	// 也能按它的亮度贡献给亮部染色（红色补光就让亮部偏红），实现"方向归赢家、颜色按亮度分"。
	// 显式插值(避免 FMath::Lerp 对 FLinearColor 的 float*Color 编译问题)。
	const FLinearColor FinalColor = MainColor * (1.0f - Dom) + LocalColorAvg * Dom;

	// —— 最终方向做时间平滑，避免灯/角色移动时方向抖动 ——
	const float Dt = (GetWorld() && bUpdateArbitrationEveryFrame) ? GetWorld()->GetDeltaSeconds() : 0.0f;
	if (Dt > 0.0f && DirectionBlendSpeed > 0.0f && !SmoothedLightDir.IsNearlyZero())
	{
		SmoothedLightDir = FMath::VInterpTo(SmoothedLightDir, TargetDir, Dt, DirectionBlendSpeed).GetSafeNormal();
	}
	else
	{
		SmoothedLightDir = TargetDir;
	}

	// —— FinalLightIntensity：主光 + 局部光的总亮度一起进 —— 这就是"较弱的一方只补强度、不抢方向"的落点
	// 上下限一起夹：Cap 防平行光调亮时无限线性放大把亮部顶到死白；
	// Floor 防暗环境(洞穴/夜景)下角色塌成一片黑、明暗交界消失导致形体不可读。
	// 为什么不再推给 LightModelStrength：那是 NPR 风格化权重(Ramp 对比/阴影浓淡)，
	// 让物理光强驱动它会导致换环境时明暗交界位置一起漂，风格与光强必须分离。
	const float FinalLightIntensityRaw = TotalWeight / FMath::Max(MainLightReferenceIntensity, KINDA_SMALL_NUMBER);
	// Floor 取 Min(Floor, Cap) 兜底：面板上把 Floor 填得比 Cap 还大时 Clamp 不至于反转
	const float FinalLightIntensity = FMath::Clamp(FinalLightIntensityRaw,
												  FMath::Min(FinalLightIntensityFloor, FinalLightIntensityCap),
												  FinalLightIntensityCap);

	// 调试只读：主光在最终方向里的占比(1=方向纯主光, 0=方向纯局部光)
	CurrentMainLightWeight = 1.0f - T;

	// —— 推送到 MPC(输出参数名不变；间接光 IndirectTint/Intensity 交回环境预设管理) ——
	SetMPCVector(MPC_FinalLightDirName, FLinearColor(SmoothedLightDir.X, SmoothedLightDir.Y, SmoothedLightDir.Z, 0.0f));
	SetMPCVector(MPC_FinalLightColorName, FinalColor);
	SetMPCScalar(MPC_MainLightWeightName, 1.0f - T);
	SetMPCVector(MPC_LocalLightColorName, LocalColorAvg);
	SetMPCScalar(MPC_LocalDiffuseScaleName, Dom);
	SetMPCScalar(MPC_LocalSpecularScaleName, Dom);
	SetMPCScalar(MPC_FinalLightIntensityName, FinalLightIntensity);

	// —— 分光源原始量：供材质 mode 9 LightWeights 逐像素按"最亮的灯"取色 ——
	// 这些是未经 Dom 混合的分量，不改动上面的 Final* 合成行为，纯附加输出。
	// 亮度用 Main/LocalWeightRaw（同一"亮度货币"量纲），材质端逐像素乘 saturate(N·L) 后比大小。
	SetMPCVector(MPC_MainLightDirName, FLinearColor(MainL.X, MainL.Y, MainL.Z, 0.0f));
	SetMPCVector(MPC_MainLightColorName, MainColor);
	SetMPCScalar(MPC_MainLightBrightName, MainWeight);
	SetMPCVector(MPC_LocalLightDirName, FLinearColor(LocalDir.X, LocalDir.Y, LocalDir.Z, 0.0f));
	SetMPCScalar(MPC_LocalLightBrightName, LocalWeightRaw);

	// —— 间接光交给 SkyLight 直接控制 ——
	// 每帧读 SkyLight 的颜色×强度，实时推给材质的 IndirectTint / IndirectIntensity。
	// 这样职责清晰：SkyLight=间接光、DirectionalLight=主光色、点/聚光=局部光；
	// 最终光 = 主光⊕局部光(上面合成) 再叠加这里的间接光。编辑器里调 SkyLight，环境光就跟着变。
	if (SkyLightRef && SkyLightRef->GetLightComponent())
	{
		if (const USkyLightComponent *Sky = Cast<USkyLightComponent>(SkyLightRef->GetLightComponent()))
		{
			SetMPCVector(MPC_IndirectTintName, Sky->GetLightColor());
			SetMPCScalar(MPC_IndirectIntensityName, Sky->Intensity);

			// 同一个天光既喂旧的 IndirectTint/Intensity，也喂新的 L1 SH 系数。
			// 两套并存是过渡态：材质切到 SH 求值后，旧的方向近似链路才能拆。
			UpdateSHCoefficients(Sky->GetLightColor() * Sky->Intensity);
		}
	}

	// —— SH 主方向：用主光反方向兜底 ——
	// SceneIndirect = EvaluateSH(Normal) 需要一个方向基准；没有真实环境探针采样时，
	// "背光面朝向环境天光"是最省成本的近似，让材质里 dot(Normal, SHDirection) 能选出暗部。
	SetMPCVector(MPC_SHDirectionName, FLinearColor(-SmoothedLightDir.X, -SmoothedLightDir.Y, -SmoothedLightDir.Z, 0.0f));
}

void ARenderDemoController::LogParameterOwnership() const
{
	// 为什么值得专门打这张表：MPC 的运行时实例一旦被 SetXxxParameterValue 写过，
	// 再去改 MPC 资产里的默认值就不会生效——参数看起来"锁死"了，却没有任何报错。
	// 排查一次要翻遍代码找写入者，成本很高，所以开局直接把归属关系和解除方式打出来。
	UE_LOG(LogKuroRender, Log, TEXT("========== MPC 参数归属表 (%s) =========="), *GetName());

	const FString ArbitrationParams = FString::Join(
		TArray<FString>{
			MPC_FinalLightDirName.ToString(), MPC_FinalLightColorName.ToString(),
			MPC_FinalLightIntensityName.ToString(), MPC_MainLightWeightName.ToString(),
			MPC_LocalLightColorName.ToString(), MPC_LocalDiffuseScaleName.ToString(),
			MPC_LocalSpecularScaleName.ToString(), MPC_SHDirectionName.ToString(),
			MPC_IndirectTintName.ToString(), MPC_IndirectIntensityName.ToString(),
			MPC_SH_L00Name.ToString(), MPC_SH_L1xName.ToString(),
			MPC_SH_L1yName.ToString(), MPC_SH_L1zName.ToString()},
		TEXT(", "));

	if (bUpdateArbitrationEveryFrame)
	{
		UE_LOG(LogKuroRender, Log,
			   TEXT("[每帧覆写] %s\n  → 手动改 MPC 资产默认值无效（下一帧就被盖掉）。"
					"要手调请取消勾选 bUpdateArbitrationEveryFrame。"),
			   *ArbitrationParams);
	}
	else
	{
		UE_LOG(LogKuroRender, Log,
			   TEXT("[仅开局写一次] %s\n  → bUpdateArbitrationEveryFrame 已关闭，"
					"这些值不再随灯光变化，静态摆拍可手调。"),
			   *ArbitrationParams);
	}

	const FString PresetParams = FString::Join(
		TArray<FString>{
			MPC_BaseColorPreserveName.ToString(), MPC_SceneInfluenceName.ToString(),
			MPC_SHContrastName.ToString(), MPC_ShadowSoftnessName.ToString()},
		TEXT(", "));

	if (bApplyDefaultPresetOnBeginPlay)
	{
		UE_LOG(LogKuroRender, Log,
			   TEXT("[预设接管] %s（外加 DirectionalLight.Intensity / SkyLight.LightColor+Intensity）\n"
					"  → BeginPlay 已按预设写过一次，改 MPC 资产无效。"
					"要手调请取消勾选 bApplyDefaultPresetOnBeginPlay，或直接改 Controller 上对应的 Preset 结构。"),
			   *PresetParams);
	}
	else
	{
		UE_LOG(LogKuroRender, Log,
			   TEXT("[预设未接管] %s\n  → bApplyDefaultPresetOnBeginPlay 已关闭，这些参数沿用 MPC 资产默认值。"),
			   *PresetParams);
	}

	UE_LOG(LogKuroRender, Log,
		   TEXT("[静态常量] %s = %.3f\n  → 美术手调量，BeginPlay 推一次，不随运行时光强变化"
				"（光强走 %s）。"),
		   *MPC_LightModelStrengthName.ToString(), LightModelStrength,
		   *MPC_FinalLightIntensityName.ToString());

	if (!CharacterLightingMPC)
	{
		UE_LOG(LogKuroRender, Warning,
			   TEXT("CharacterLightingMPC 未指定——以上所有写入都会被静默丢弃，材质拿不到任何参数。"));
	}

	// SkyLightRef 是间接光与 SH 系数的唯一数据源，且它们的写入被包在 SkyLight 的判空里：
	// 引用没填时不会报错、不会崩，只是这几个参数永远停在 MPC 资产默认值上——
	// 表现就是"改 SHGroundColor / SHGroundAlbedo 完全没反应"，极难往这个方向想，所以显式警告。
	if (!SkyLightRef || !SkyLightRef->GetLightComponent())
	{
		UE_LOG(LogKuroRender, Warning,
			   TEXT("SkyLightRef 未指定——%s / %s / %s / %s / %s / %s 都不会被更新，"
					"SHGroundColor 与 SHGroundAlbedo 也因此完全不起作用。"),
			   *MPC_IndirectTintName.ToString(), *MPC_IndirectIntensityName.ToString(),
			   *MPC_SH_L00Name.ToString(), *MPC_SH_L1xName.ToString(),
			   *MPC_SH_L1yName.ToString(), *MPC_SH_L1zName.ToString());
	}

	if (!DirectionalLightRef || !DirectionalLightRef->GetLightComponent())
	{
		UE_LOG(LogKuroRender, Warning,
			   TEXT("DirectionalLightRef 未指定——主光仲裁会退化，%s / %s 不可信。"),
			   *MPC_FinalLightDirName.ToString(), *MPC_FinalLightColorName.ToString());
	}

	UE_LOG(LogKuroRender, Log, TEXT("=========================================="));
}

void ARenderDemoController::UpdateSHCoefficients(const FLinearColor &SkyColor)
{
	// 半球模型的解析投影：上半球取 SkyColor、下半球取 GroundColor，投影到一阶球谐。
	// 两个常数分别是 Y00*2π = √π 和 Y1*π = ½√(3π)，其中 2π 是半球立体角、
	// π 是半球上 cosθ 的积分。推导见
	// Docs/其他文档/Kuro_Render_一阶SH球谐光照_实现_2026-07-21.md §2。
	// 这里写死数值而不是运行时算：每帧都要跑，且推导一次就固定了。
	const float K_L00 = 1.7724539f;
	const float K_L1 = 1.5349901f;

	const FLinearColor Ground = SHGroundColor * SHGroundAlbedo;

	const FLinearColor L00 = (SkyColor + Ground) * K_L00;
	const FLinearColor L1z = (SkyColor - Ground) * K_L1;

	// 存一份供细节面板实时查看：排查"参数没效果"时，先看这里有没有数、数对不对，
	// 能直接把"C++ 没算出来"和"算出来了但材质没读到"两类故障分开。
	CurrentSH_L00 = L00;
	CurrentSH_L1z = L1z;

	// L1z 是逐通道的：只要某个通道上 Ground 比 Sky 亮，该通道的上下梯度就会翻转
	// （底部反而比顶部亮）。典型触发场景是天光带强色相而地面色偏白——比如红天光配白地面，
	// R 通道正常但 G/B 通道全反，画面会呈现"顶部有色、底部发白"的混色，很难看出是 bug。
	// 面板上的 FLinearColor 色块把负数显示成黑，看不出来，所以这里必须出日志。
	const bool bNegative = (L1z.R < 0.0f) || (L1z.G < 0.0f) || (L1z.B < 0.0f);
	if (bNegative != bWarnedNegativeL1z)
	{
		bWarnedNegativeL1z = bNegative;
		if (bNegative)
		{
			UE_LOG(LogKuroRender, Warning,
				   TEXT("SH L1z 出现负通道 (%.3f, %.3f, %.3f)：该通道地面比天空亮，上下梯度已翻转。"
						"检查 SHGroundColor×SHGroundAlbedo 是否超过了天光颜色×强度。"),
				   L1z.R, L1z.G, L1z.B);
		}
		else
		{
			UE_LOG(LogKuroRender, Log, TEXT("SH L1z 已恢复为全正，上下梯度方向正常。"));
		}
	}

	// L1x / L1y 在纯半球模型下恒为 0（绕 Z 轴旋转对称）。仍然每帧显式下发零值，
	// 否则 MPC 会残留上一次的数据；将来接入方向性来源时直接改这两行即可。
	SetMPCVector(MPC_SH_L00Name, L00);
	SetMPCVector(MPC_SH_L1xName, FLinearColor::Black);
	SetMPCVector(MPC_SH_L1yName, FLinearColor::Black);
	SetMPCVector(MPC_SH_L1zName, L1z);
}

float ARenderDemoController::ComputeLocalLightIntensityAtLocation(const ULightComponent *LightComp, const FVector &Location) const
{
	if (!LightComp)
	{
		return 0.0f;
	}

	// PointLight/SpotLight/RectLight 在引擎里都继承自 UPointLightComponent，共享 Intensity/AttenuationRadius，
	// 所以这一套距离衰减对三者通用；只有 SpotLight 需要额外叠加锥角衰减。
	const UPointLightComponent *PointComp = Cast<UPointLightComponent>(LightComp);
	if (!PointComp)
	{
		// 既不是方向光（外层已排除）也不是 Point/Spot/Rect 的其他灯类型，无衰减半径概念，直接用 Intensity
		return LightComp->Intensity;
	}

	const float Distance = FVector::Dist(PointComp->GetComponentLocation(), Location);
	const float Radius = FMath::Max(PointComp->AttenuationRadius, 1.0f);
	if (Distance >= Radius)
	{
		return 0.0f;
	}

	// 半径内平滑窗口衰减（避免半径外硬截断的观感）× 反平方衰减（以米为单位换算，避免厘米单位下数值过小）
	const float NormalizedDist = Distance / Radius;
	const float WindowFactor = FMath::Square(FMath::Clamp(1.0f - FMath::Square(NormalizedDist), 0.0f, 1.0f));
	const float InvSquareFalloff = 1.0f / FMath::Square(FMath::Max(Distance / 100.0f, 0.1f));

	// 单位归一：不同灯可能用 candela / lumen / EV / unitless 创作，Intensity 的数值量纲天差地别。
	// 把 lumen 换算成 candela（点光 cd = lm / 4π；聚光 cd = lm / 外锥立体角 2π(1-cosθ)），
	// 让"用不同单位创作但实际一样亮"的灯得到一致权重，避免仲裁被某个单位的灯天然放大。
	float UnitToCandela = 1.0f;
	if (PointComp->IntensityUnits == ELightUnits::Lumens)
	{
		float SolidAngle = 4.0f * PI; // 点光：整个球面
		if (const USpotLightComponent *AsSpot = Cast<USpotLightComponent>(PointComp))
		{
			const float CosHalfOuter = FMath::Cos(FMath::DegreesToRadians(AsSpot->OuterConeAngle));
			SolidAngle = 2.0f * PI * (1.0f - CosHalfOuter);
		}
		UnitToCandela = 1.0f / FMath::Max(SolidAngle, KINDA_SMALL_NUMBER);
	}
	// candela / unitless 直接用；EV 单位极少用于角色补光，这里按 1 处理(需要可再加 EV→cd 换算)
	float Result = PointComp->Intensity * UnitToCandela * WindowFactor * InvSquareFalloff;

	// 聚光灯额外叠加锥角衰减：采样点在外锥角外直接判 0；内外锥角之间做线性过渡
	if (const USpotLightComponent *SpotComp = Cast<USpotLightComponent>(PointComp))
	{
		const FVector ToSample = (Location - SpotComp->GetComponentLocation()).GetSafeNormal();
		const float CosAngle = FVector::DotProduct(SpotComp->GetForwardVector(), ToSample);
		const float CosOuter = FMath::Cos(FMath::DegreesToRadians(SpotComp->OuterConeAngle));
		const float CosInner = FMath::Cos(FMath::DegreesToRadians(SpotComp->InnerConeAngle));

		if (CosAngle <= CosOuter)
		{
			return 0.0f;
		}
		const float ConeFalloff = FMath::Clamp((CosAngle - CosOuter) / FMath::Max(CosInner - CosOuter, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Result *= ConeFalloff;
	}

	return Result;
}

FVector ARenderDemoController::ResolveFaceSampleLocation() const
{
	USkeletalMeshComponent *SkelComp = nullptr;
	if (TargetRenderComponentRef && TargetRenderComponentRef->GetOwner())
	{
		SkelComp = TargetRenderComponentRef->GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	}
	if (!SkelComp && TargetCharacterRef)
	{
		SkelComp = TargetCharacterRef->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (SkelComp && !FaceSampleSocketName.IsNone() && SkelComp->DoesSocketExist(FaceSampleSocketName))
	{
		return SkelComp->GetSocketLocation(FaceSampleSocketName);
	}

	if (TargetCharacterRef)
	{
		return TargetCharacterRef->GetActorLocation();
	}

	return GetActorLocation();
}

void ARenderDemoController::ApplyEnvironmentPreset(EEnvironmentPreset Preset)
{
	switch (Preset)
	{
	case EEnvironmentPreset::Day:
		ApplyEnvironmentSettings(DayPreset);
		break;
	case EEnvironmentPreset::Indoor:
		ApplyEnvironmentSettings(IndoorPreset);
		break;
	case EEnvironmentPreset::StrongColorLight:
		ApplyEnvironmentSettings(StrongColorLightPreset);
		break;
	case EEnvironmentPreset::NoAmbient:
		ApplyEnvironmentSettings(NoAmbientPreset);
		break;
	default:
		break;
	}
}

void ARenderDemoController::ApplyEnvironmentSettings(const FEnvironmentPresetSettings &Settings)
{
	SetBaseColorPreserve(Settings.BaseColorPreserve);
	SetSceneInfluence(Settings.SceneInfluence);
	SetSHContrast(Settings.SHContrast);
	SetShadowSoftness(Settings.ShadowSoftness);

	// 地面色不直接下发 MPC——它每帧参与 UpdateSHCoefficients 的半球投影，
	// 所以存进 Actor 成员当"当前生效值"，由这里从预设写入，切预设即自动更新。
	SHGroundColor = Settings.SHGroundColor;
	SHGroundAlbedo = Settings.SHGroundAlbedo;

	// 打出实际写入值：预设一旦生效，这几个参数在 MPC 资产里改就没用了，
	// 日志里能直接看到"被写成了多少"，省掉一轮"到底是谁改的"排查。
	UE_LOG(LogKuroRender, Log,
		   TEXT("环境预设已接管 → %s=%.3f  %s=%.3f  %s=%.3f  %s=%.3f"),
		   *MPC_BaseColorPreserveName.ToString(), Settings.BaseColorPreserve,
		   *MPC_SceneInfluenceName.ToString(), Settings.SceneInfluence,
		   *MPC_SHContrastName.ToString(), Settings.SHContrast,
		   *MPC_ShadowSoftnessName.ToString(), Settings.ShadowSoftness);

	if (DirectionalLightRef && DirectionalLightRef->GetLightComponent())
	{
		DirectionalLightRef->GetLightComponent()->SetIntensity(Settings.DirectionalLightIntensity);
		UE_LOG(LogKuroRender, Log, TEXT("环境预设已接管 → DirectionalLight.Intensity=%.3f"),
			   Settings.DirectionalLightIntensity);
	}

	// 间接光统一走 SkyLight：预设的 IndirectTint→天光颜色、IndirectIntensity→天光强度；
	// 再由 UpdateFinalLightArbitration 每帧把天光颜色/强度读进 MPC 的 IndirectTint/IndirectIntensity。
	// （原先直接写 MPC 的 SetIndirectTint/SetIndirectIntensity 已删——会被天光读值覆盖，现在只有天光一条源。）
	if (SkyLightRef && SkyLightRef->GetLightComponent())
	{
		if (USkyLightComponent *Sky = Cast<USkyLightComponent>(SkyLightRef->GetLightComponent()))
		{
			Sky->SetLightColor(Settings.IndirectTint);
			Sky->SetIntensity(Settings.IndirectIntensity);
			// 天光是间接光的唯一源：它被改了，MPC 的 IndirectTint/IndirectIntensity
			// 和 SH 系数下一帧就跟着变——所以这条要和上面的 MPC 参数一起打出来。
			UE_LOG(LogKuroRender, Log,
				   TEXT("环境预设已接管 → SkyLight.LightColor=%s  SkyLight.Intensity=%.3f"
						"（间接光与 SH 系数由此派生）"),
				   *Settings.IndirectTint.ToString(), Settings.IndirectIntensity);
		}
	}
}

void ARenderDemoController::ApplyPresetDay()
{
	ApplyEnvironmentSettings(DayPreset);
}

void ARenderDemoController::ApplyPresetIndoor()
{
	ApplyEnvironmentSettings(IndoorPreset);
}

void ARenderDemoController::ApplyPresetStrongColorLight()
{
	ApplyEnvironmentSettings(StrongColorLightPreset);
}

void ARenderDemoController::ApplyPresetNoAmbient()
{
	ApplyEnvironmentSettings(NoAmbientPreset);
}

void ARenderDemoController::ApplyHighPC()
{
	if (QualityHighPC)
	{
		ApplyQualityData(QualityHighPC);
		return;
	}

	// 单一数据源：无显式数据资产时读项目设置(URenderProjectSettings)。GetDefault<> 对开发者设置永不返回空。
	ApplyQualitySettingsInternal(GetDefault<URenderProjectSettings>()->HighPCQuality);
}

void ARenderDemoController::ApplyMidPC()
{
	if (QualityMidPC)
	{
		ApplyQualityData(QualityMidPC);
		return;
	}

	ApplyQualitySettingsInternal(GetDefault<URenderProjectSettings>()->MidPCQuality);
}

void ARenderDemoController::ApplyLowSpec()
{
	if (QualityLowSpec)
	{
		ApplyQualityData(QualityLowSpec);
		return;
	}

	ApplyQualitySettingsInternal(GetDefault<URenderProjectSettings>()->LowSpecQuality);
}

void ARenderDemoController::ApplyQualityData(URenderQualityDataAsset *QualityData)
{
	if (!QualityData)
	{
		return;
	}

	ApplyQualitySettingsInternal(QualityData->QualitySettings);
}

void ARenderDemoController::ApplyStateData(URenderStateDataAsset *StateData)
{
	if (!StateData)
	{
		return;
	}

	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyRenderStateData(StateData);
	}
}

void ARenderDemoController::ApplyStateNormal()
{
	if (StateNormal)
	{
		ApplyStateData(StateNormal);
		return;
	}

	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyRenderState(GetDefault<URenderProjectSettings>()->DefaultNormalState);
	}
}

void ARenderDemoController::ApplyStateHitFlash()
{
	if (StateHitFlash)
	{
		ApplyStateData(StateHitFlash);
		return;
	}

	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyRenderState(GetDefault<URenderProjectSettings>()->DefaultHitFlashState);
	}
}

void ARenderDemoController::ApplyStateRimBoost()
{
	if (StateRimBoost)
	{
		ApplyStateData(StateRimBoost);
		return;
	}

	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyRenderState(GetDefault<URenderProjectSettings>()->DefaultRimBoostState);
	}
}

void ARenderDemoController::ApplyStateOutlineBoost()
{
	if (StateOutlineBoost)
	{
		ApplyStateData(StateOutlineBoost);
		return;
	}

	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyRenderState(GetDefault<URenderProjectSettings>()->DefaultOutlineBoostState);
	}
}

void ARenderDemoController::ApplyStateBattleTint()
{
	if (StateBattleTint)
	{
		ApplyStateData(StateBattleTint);
		return;
	}

	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyRenderState(GetDefault<URenderProjectSettings>()->DefaultBattleTintState);
	}
}

void ARenderDemoController::ResetRenderState()
{
	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ClearAllRenderStates();
	}
}

void ARenderDemoController::SetBaseColorPreserve(float Value)
{
	SetMPCScalar(MPC_BaseColorPreserveName, Value);
}

void ARenderDemoController::SetSceneInfluence(float Value)
{
	SetMPCScalar(MPC_SceneInfluenceName, Value);
}

void ARenderDemoController::SetIndirectIntensity(float Value)
{
	SetMPCScalar(MPC_IndirectIntensityName, Value);
}

void ARenderDemoController::SetIndirectTint(FLinearColor Value)
{
	SetMPCVector(MPC_IndirectTintName, Value);
}

void ARenderDemoController::SetSHContrast(float Value)
{
	SetMPCScalar(MPC_SHContrastName, Value);
}

void ARenderDemoController::SetShadowSoftness(float Value)
{
	SetMPCScalar(MPC_ShadowSoftnessName, Value);
}

void ARenderDemoController::SetLightModelStrength(float Value)
{
	LightModelStrength = Value;
	SetMPCScalar(MPC_LightModelStrengthName, Value);
}

void ARenderDemoController::SetFaceShadowSoftness(float Value)
{
	SetMPCScalar(FaceShadowSoftnessParamName, Value);
}

void ARenderDemoController::SetFormBlend(float Value)
{
	FormBlend = FMath::Clamp(Value, 0.0f, 1.0f);
	bFormTransitioning = false; // 手动设值即打断过渡
	SetRunningMPCScalar(MPC_FormBlendName, FormBlend);
	ApplyFormVisibilityToRenderComponent();
}

void ARenderDemoController::TransitionToForm(float Target, float Duration)
{
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		SetFormBlend(Target); // 瞬切
		return;
	}
	FormBlendTarget = FMath::Clamp(Target, 0.0f, 1.0f);
	FormBlendSpeed = FMath::Abs(FormBlendTarget - FormBlend) / Duration;
	bFormTransitioning = true;
}

void ARenderDemoController::ApplyFormVisibilityToRenderComponent()
{
	if (UCharacterRenderComponent* RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyFormVisibility(FormBlend);
	}
}

void ARenderDemoController::SetMaterialDebugViewMode(EMaterialDebugViewMode Mode)
{
	DebugViewMode = Mode;
	SetRunningMPCScalar(MPC_MaterialDebugViewModeName, static_cast<float>(Mode));
}

void ARenderDemoController::ApplyMaterialDebugViewMode()
{
	SetMaterialDebugViewMode(DebugViewMode);
}

// 画质三档 + 五个渲染状态的硬编码兜底（原 BuildFallbackHighPC/MidPC/LowSpec、BuildFallbackNormalState 等）已全部移除：
// 统一由 URenderProjectSettings 提供，消除"控制器兜底数值"与"项目设置数值"多源打架的问题。

void ARenderDemoController::ApplyQualitySettingsInternal(const FRenderQualitySettings &Settings)
{
	if (UCharacterRenderComponent *RenderComponent = ResolveRenderComponent())
	{
		RenderComponent->ApplyQualitySettings(Settings);

		// 壳描边由 Mesh 可见性驱动，切档后按当前 FormBlend 重新同步一次，
		// 让新的画质门控（bOutlineEnabledByQuality）与二形态可见性正确组合。
		RenderComponent->ApplyFormVisibility(FormBlend);
	}

	// FaceShadowSoftness 的真实消费者是 MF_SDF 中的 MCP_CharacterLighting CollectionParameter。
	SetMPCScalar(FaceShadowSoftnessParamName, Settings.FaceShadowSoftness);

	// Fur 采样数属于角色运行态，写入用户已建立的 MCP 参数。
	SetRunningMPCScalar(MPC_FurParallaxSampleCountName, static_cast<float>(Settings.FurParallaxSampleCount));

	// —— 引擎侧屏幕空间效果分档（材质标量管不到的部分，走 CVar，是三档间可实测的 GPU 差异来源）——
	// 关键：首次应用前先捕获场景原始 SSR/AO 值；开启档恢复原值（HighPC 不改变场景既有色调），仅关闭档置 0。
	// SSR / AO 是全局项，对单角色 Demo 视口生效。
	if (IConsoleVariable *SSRQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SSR.Quality")))
	{
		if (!bQualityCVarsCaptured)
		{
			OriginalSSRQuality = SSRQuality->GetInt();
		}
		SSRQuality->Set(Settings.bEnableSSR ? OriginalSSRQuality : 0, ECVF_SetByGameOverride);
	}
	if (IConsoleVariable *AOLevels = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AmbientOcclusionLevels")))
	{
		if (!bQualityCVarsCaptured)
		{
			OriginalAOLevels = AOLevels->GetInt();
		}
		AOLevels->Set(Settings.bEnableGTAO ? OriginalAOLevels : 0, ECVF_SetByGameOverride);
	}

	// —— 屏幕空间内描边（M_InnerLine）：整个全屏 pass 的增删 ——
	// Sobel 3x3 是带宽型开销，降档要省就必须让这个 pass 不执行。
	// RemoveBlendable 才能真的不执行；把权重设 0 只是把混合结果乘没了，pass 照跑、带宽照吃。
	// 验收方式：ProfileGPU 里看 PostProcessMaterial 这一行在不在。
	// API 归属容易记混：AddOrUpdateBlendable 是 APostProcessVolume 的 UFUNCTION（Scene.h 无此成员），
	// FPostProcessSettings 上对应的是 AddBlendable —— 它内部已按 Object 去重、命中则只更新权重，
	// 就是 AddOrUpdate 语义，反复切档不会在数组里堆重复项。见 Engine/Classes/Engine/Scene.h:2639。
	// 注意作用域：在编辑器里（CallInEditor）调用会改动关卡里的 PPV 实例并把关卡标脏，
	// PIE 中的改动则是临时的。录性能对比时建议在 PIE 内切档，避免把降档状态存进关卡。
	if (PostProcessVolumeRef && InnerLineMaterialRef)
	{
		// bUseSceneViewExtensionInnerLine 打开时，这条线交给插件里的自定义 Pass 画，
		// PPM 版必须摘掉，否则同一条线画两遍。
		if (Settings.bEnableInnerLine && !bUseSceneViewExtensionInnerLine)
		{
			PostProcessVolumeRef->Settings.AddBlendable(InnerLineMaterialRef.Get(), 1.0f);
		}
		else
		{
			PostProcessVolumeRef->Settings.RemoveBlendable(InnerLineMaterialRef.Get());
		}
	}

	// —— 屏幕空间内描边（NPRRenderPipeline 插件 / SceneViewExtension 版）——
	// 游戏线程只能通过 CVar 把开关和参数递到渲染线程，不要跨线程直接读 UObject。
	// Enable=0 时 IsActiveThisFrame_Internal 返回 false，整个 Pass 不进 RDG——
	// 与 PPM 版的 RemoveBlendable 是同一级别的"pass 真的不执行"，不是权重乘零。
	{
		const bool bSVEInnerLine = bUseSceneViewExtensionInnerLine && Settings.bEnableInnerLine;

		if (IConsoleVariable *Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NPR.InnerLine.Enable")))
		{
			Var->Set(bSVEInnerLine ? 1 : 0, ECVF_SetByGameOverride);
		}

		if (bSVEInnerLine)
		{
			// 分档只动一件事：核大小（9 tap Sobel / 5 tap 十字）。
			// 注意两个核的 NormalGrad 量级对不齐，切核后 NormalThreshold 要各自重调，
			// 不能沿用同一组数值（见 内描边脚部遮罩与参数解耦_20260822.md §4）。
			// 这里曾经还按档位下发 r.NPR.InnerLine.DilateRadius，膨胀 Pass 2026-08-22
			// 实测判死并删除后一并移除，别再加回来 —— 判据见 Scorecard §10.3。
			int32 Sobel9 = 1;
			switch (Settings.QualityTier)
			{
			case ERenderQualityTier::HighPC:
				Sobel9 = 1;
				break;
			case ERenderQualityTier::MidPC:
				Sobel9 = 0;
				break;
			default:
				break;
			}

			if (IConsoleVariable *Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NPR.InnerLine.Sobel9")))
			{
				Var->Set(Sobel9, ECVF_SetByGameOverride);
			}
		}
	}

	// —— 材质静态分支分档（唯一能真正减少 shader 指令数的通道）——
	// 材质里的 Quality Switch 节点读的是编译期常量，MPC/MID 标量喂不进去；只有这条 CVar 能驱动它。
	// r.MaterialQualityLevel 取值顺序是引擎定义的 EMaterialQualityLevel，注意不是递增的：
	//   0=Low  1=High  2=Medium  3=Epic
	// 默认关闭（bDriveMaterialQualityLevel），因为切换会触发该档 shader 编译/缓存切换、首次切档必卡；
	// 需要出“三档 PS 指令数 / GPU ms 差异”这类实测数据时再打开，见记分卡填写流程。
	if (bDriveMaterialQualityLevel)
	{
		if (IConsoleVariable *MatQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaterialQualityLevel")))
		{
			if (!bQualityCVarsCaptured)
			{
				OriginalMaterialQualityLevel = MatQuality->GetInt();
			}

			int32 TargetLevel = OriginalMaterialQualityLevel;
			switch (Settings.QualityTier)
			{
			case ERenderQualityTier::HighPC:
				TargetLevel = 3; // Epic
				break;
			case ERenderQualityTier::MidPC:
				TargetLevel = 1; // High
				break;
			case ERenderQualityTier::LowSpec:
				TargetLevel = 0; // Low
				break;
			}
			MatQuality->Set(TargetLevel, ECVF_SetByGameOverride);
		}
	}

	bQualityCVarsCaptured = true;
}

UCharacterRenderComponent *ARenderDemoController::ResolveRenderComponent()
{
	if (TargetRenderComponentRef)
	{
		return TargetRenderComponentRef;
	}

	if (!TargetCharacterRef)
	{
		return nullptr;
	}

	TargetRenderComponentRef = URenderFunctionLibrary::FindOrAddRenderComponent(TargetCharacterRef);
	return TargetRenderComponentRef;
}

UMaterialParameterCollectionInstance *ARenderDemoController::ResolveMPCInstance()
{
	// 每次从当前 World 现取，绝不缓存 —— 缓存会跨 World 泄漏，见 .h 里字段被删处的说明。
	if (!CharacterLightingMPC)
	{
		return nullptr;
	}

	UWorld *World = GetWorld();
	return World ? World->GetParameterCollectionInstance(CharacterLightingMPC) : nullptr;
}

UMaterialParameterCollectionInstance *ARenderDemoController::ResolveRunningMPCInstance()
{
	if (!CharacterRunningMPC)
	{
		return nullptr;
	}

	UWorld *World = GetWorld();
	return World ? World->GetParameterCollectionInstance(CharacterRunningMPC) : nullptr;
}

void ARenderDemoController::SetMPCScalar(FName ParameterName, float Value)
{
	if (UMaterialParameterCollectionInstance *Instance = ResolveMPCInstance())
	{
		Instance->SetScalarParameterValue(ParameterName, Value);
	}
}

void ARenderDemoController::SetMPCVector(FName ParameterName, const FLinearColor &Value)
{
	if (UMaterialParameterCollectionInstance *Instance = ResolveMPCInstance())
	{
		Instance->SetVectorParameterValue(ParameterName, Value);
	}
}

void ARenderDemoController::SetRunningMPCScalar(FName ParameterName, float Value)
{
	if (UMaterialParameterCollectionInstance *Instance = ResolveRunningMPCInstance())
	{
		Instance->SetScalarParameterValue(ParameterName, Value);
	}
}
