#include "RenderProjectSettings.h"

URenderProjectSettings::URenderProjectSettings()
{
	// 三档画质的唯一数据源。编辑器 Project Settings → Project → "Render Settings" 可直接改，写入 DefaultGame.ini。
	//
	// 每个字段的“接通归属”见 Docs/其他文档/Kuro_Render_质量分档接通_TechSpec_2026-07-23.md：
	//   [已接通]   FaceShadowSoftness —— MCP_CharacterLighting 标量，MF_SDF 已消费。
	//   [引擎接通] bUseShellOutline（描边 Mesh 可见性）、bEnableSSR / bEnableGTAO
	//             （CVar r.SSR.Quality / r.AmbientOcclusionLevels）。这三项 + 上面一项是当前唯一能实测出差异的。
	//   [MPC 已下发、材质待接] FurParallaxSampleCount —— 写入 MCP_CharaterRuning；
	//                            Fur Master 尚未读取该参数，接线清单见 TechSpec。
	// Face SDF 与最高层发丝高光始终开启，不属于质量分档，不在本结构体保留开关。
	//
	// 2026-07-27 已删除 5 个无消费者字段：bUsePostProcessOutline / bEnableScenePostProcess
	// （三档无区分）、RefractionMode / bEnableTranslucentSSS / SecondFormFXLevel（对应材质未落地）。
	// 需要静态分支真正砍指令时，用 ARenderDemoController::bDriveMaterialQualityLevel 走
	// r.MaterialQualityLevel + 材质 Quality Switch，不再往本结构体加标量字段。
	//
	// 下列数值按“成本量级经验”给出（POM 采样 / SSR / AO / 透明 overdraw 最贵），尚未实测；
	// 用记分卡（Kuro_Render_Performance_Scorecard.md）跑出真实 GPU ms 后回填校准，校准前不得声称达标。

	// —— HighPC：全开，画质上限 ——
	HighPCQuality.QualityTier = ERenderQualityTier::HighPC;
	HighPCQuality.bUseShellOutline = true;
	HighPCQuality.bEnableInnerLine = true;          // 屏幕空间内描边，Epic/Default 18-tap（Quality Switch）
	HighPCQuality.bEnableSSR = true;
	HighPCQuality.bEnableGTAO = true;
	HighPCQuality.FaceShadowSoftness = 0.35f;
	HighPCQuality.FurParallaxSampleCount = 32;       // 毛领 POM 满采样（最贵之一）

	// —— MidPC：从 High 继承后关闭 SSR、POM 采样减半，保留角色可读性 ——
	MidPCQuality = HighPCQuality;
	MidPCQuality.QualityTier = ERenderQualityTier::MidPC;
	MidPCQuality.bEnableSSR = false;                  // SSR 收益低、成本高，先砍
	MidPCQuality.FaceShadowSoftness = 0.40f;
	MidPCQuality.FurParallaxSampleCount = 16;         // POM 步数减半
	// 内描边在 MidPC 保持开启：降的是采样数（18→10），由 r.MaterialQualityLevel=High 档的
	// Quality Switch 分支决定，不在这里用布尔关掉——关掉会丢失 NPR 的关键可读性。

	// —— LowSpec：可读性底线，关闭 AO / POM；Face SDF 与最高层发丝高光始终保留 ——
	LowSpecQuality = MidPCQuality;
	LowSpecQuality.QualityTier = ERenderQualityTier::LowSpec;
	LowSpecQuality.bUseShellOutline = false;          // 关壳描边（描边 Mesh 隐藏）
	LowSpecQuality.bEnableInnerLine = false;          // 摘掉内描边 Blendable，省掉整个全屏 Sobel pass
	LowSpecQuality.bEnableGTAO = false;               // 关 AO
	LowSpecQuality.FaceShadowSoftness = 0.50f;
	LowSpecQuality.FurParallaxSampleCount = 0;        // 关 POM，仅保留颜色

	// 五个内置渲染状态的默认值，原样从 ARenderDemoController::BuildFallback*State() 搬过来，数值不变
	DefaultNormalState.StateName = TEXT("Normal");
	DefaultNormalState.StateType = ERenderStateType::Custom;
	DefaultNormalState.bRestoreOriginalValues = true;
	DefaultNormalState.Duration = 0.01f;

	DefaultHitFlashState.StateName = TEXT("HitFlash");
	DefaultHitFlashState.StateType = ERenderStateType::HitFlash;
	DefaultHitFlashState.Duration = 0.12f;
	DefaultHitFlashState.bRestoreOriginalValues = true;
	{
		FRenderScalarParameterValue HitFlashScalar;
		HitFlashScalar.ParameterName = TEXT("HitFlash");
		HitFlashScalar.Value = 1.0f;
		FRenderVectorParameterValue HitFlashColor;
		HitFlashColor.ParameterName = TEXT("TintColor");
		HitFlashColor.Value = FLinearColor::White;
		DefaultHitFlashState.ScalarParameters.Add(HitFlashScalar);
		DefaultHitFlashState.VectorParameters.Add(HitFlashColor);
	}

	DefaultRimBoostState.StateName = TEXT("RimBoost");
	DefaultRimBoostState.StateType = ERenderStateType::RimBoost;
	DefaultRimBoostState.bRestoreOriginalValues = true;
	DefaultRimBoostState.bLooping = true; // 持续型：一直生效到手动 ResetRenderState/ClearRenderState
	{
		FRenderScalarParameterValue RimScalar;
		RimScalar.ParameterName = TEXT("RimIntensity");
		RimScalar.Value = 1.75f;
		FRenderVectorParameterValue RimColor;
		RimColor.ParameterName = TEXT("RimColor");
		RimColor.Value = FLinearColor(0.85f, 0.95f, 1.0f, 1.0f);
		DefaultRimBoostState.ScalarParameters.Add(RimScalar);
		DefaultRimBoostState.VectorParameters.Add(RimColor);
	}

	DefaultOutlineBoostState.StateName = TEXT("OutlineBoost");
	DefaultOutlineBoostState.StateType = ERenderStateType::OutlineBoost;
	DefaultOutlineBoostState.bRestoreOriginalValues = true;
	DefaultOutlineBoostState.bLooping = true;
	{
		FRenderScalarParameterValue OutlineScalar;
		OutlineScalar.ParameterName = TEXT("OutlineIntensity");
		OutlineScalar.Value = 1.5f;
		FRenderVectorParameterValue OutlineColor;
		OutlineColor.ParameterName = TEXT("OutlineColor");
		OutlineColor.Value = FLinearColor::Black;
		DefaultOutlineBoostState.ScalarParameters.Add(OutlineScalar);
		DefaultOutlineBoostState.VectorParameters.Add(OutlineColor);
	}

	DefaultBattleTintState.StateName = TEXT("BattleTint");
	DefaultBattleTintState.StateType = ERenderStateType::BattleDamage;
	DefaultBattleTintState.bRestoreOriginalValues = true;
	DefaultBattleTintState.bLooping = true;
	{
		FRenderScalarParameterValue BattleTintScalar;
		BattleTintScalar.ParameterName = TEXT("BattleDamageStrength");
		BattleTintScalar.Value = 0.45f;
		FRenderVectorParameterValue BattleTintColor;
		BattleTintColor.ParameterName = TEXT("TintColor");
		BattleTintColor.Value = FLinearColor(1.0f, 0.85f, 0.9f, 1.0f);
		DefaultBattleTintState.ScalarParameters.Add(BattleTintScalar);
		DefaultBattleTintState.VectorParameters.Add(BattleTintColor);
	}
}
