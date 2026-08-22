#include "NPRInnerLineSceneViewExtension.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "SceneInterface.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

// ─────────────────────────────────────────────────────────────────────────────
// CVar：游戏线程 → 渲染线程的唯一通道。
// 全部标 ECVF_RenderThreadSafe，值在渲染线程用 GetValueOnRenderThread 读。
// 由 ARenderDemoController::ApplyQualitySettings 按三档写入，不要在渲染线程读 UObject。
// ─────────────────────────────────────────────────────────────────────────────

static TAutoConsoleVariable<int32> CVarNPRInnerLineEnable(
	TEXT("r.NPR.InnerLine.Enable"), 0,
	TEXT("屏幕空间内描边（SceneViewExtension 版）总开关。0=整个 Pass 不进 RDG（Low 档行为）。\n")
	TEXT("默认保持 0，别改成 1：PPM 版 M_InnerLine 的 Blendable 是存在 PostProcessVolume 资产里的，\n")
	TEXT("在 ApplyQualitySettings 跑之前一直挂着；这里默认 1 会让编辑器一打开就两套同时画、出双线。\n")
	TEXT("0 表示\"还没人接管\"。真正的归属由 ARenderDemoController::bUseSceneViewExtensionInnerLine 决定，\n")
	TEXT("它在 ApplyQualitySettings 里同时做两件事：摘/挂 PPM 的 Blendable + 写这个 CVar。\n")
	TEXT("该函数只在 BeginPlay（PIE/打包）或手动调 ApplyHighPC/MidPC/LowSpec 时执行 ——\n")
	TEXT("所以纯编辑器视口下不点按钮、不进 PIE，看到的一直是 PPM 版。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarNPRInnerLineSobel9(
	TEXT("r.NPR.InnerLine.Sobel9"), 1,
	TEXT("1=3x3 Sobel（9 tap，1-2-1 权重自带平滑）；0=十字 5 tap 降级核。\n")
	TEXT("两个核的 NormalGrad 量级对不齐，切核后必须各自重调 NormalThreshold，不要共用数值。"),
	ECVF_RenderThreadSafe);

// 默认值全部取自现役 PPM 版 M_Daniya_InnerLine 里已调好的数值（2026-08-22 用户提供），
// 目的是让新旧两版在同机位下可直接对照，而不是让用户从头再调一遍。
static TAutoConsoleVariable<float> CVarNPRInnerLineSampleStride(
	TEXT("r.NPR.InnerLine.SampleStride"), 0.5f,
	TEXT("Sobel 采样步长（像素）。不是线宽 —— 加大它会同时改掉检测灵敏度。\n")
	TEXT("线宽只能靠 EdgeSoftness 的羽化宽度调；独立的膨胀 Pass 已实测判死并删除，见 Scorecard §10。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineNormalThreshold(
	TEXT("r.NPR.InnerLine.NormalThreshold"), 2.0f,
	TEXT("法线梯度检测门槛，越低线越多。"),
	// 2.0 是 2026-08-22 用户在编辑器里逐档试出来的值（从 1.0 起，2.0 线量最合适）。
	// 1.0 是从旧 PPM 版直接搬来的数，但那一版的检测门槛与线宽共用同一组阈值，
	// 数值不可直接类比 —— 解耦之后门槛需要独立重调，这就是重调后的结果。
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineEdgeSoftness(
	TEXT("r.NPR.InnerLine.EdgeSoftness"), 0.3f,
	TEXT("以 Thresh 为中心上下各 EdgeSoftness 做羽化，只管软硬，不动检测门槛。→0 得到硬 1px 线。\n")
	TEXT("旧版写成 smoothstep(Thresh*0.6, Thresh*SoftRange, ..)，线宽与灵敏度共用同一组阈值、互相锁死。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineFadeStart(
	TEXT("r.NPR.InnerLine.FadeStart"), 100.0f,
	TEXT("距离衰减起点（cm）。远处结构间距 < 1px，内线密度爆炸糊成块，从这里开始抬阈值并淡出。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineFadeEnd(
	TEXT("r.NPR.InnerLine.FadeEnd"), 1000.0f,
	TEXT("距离衰减终点（cm）。到这里内线完全交给壳层外轮廓，且提前 early out。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineDepthGate(
	TEXT("r.NPR.InnerLine.DepthGate"), 0.03f,
	TEXT("深度闸门：深度不连续处放弃内描边，让给壳层外轮廓，两套描边靠这个机制不打架。越低壳层接管得越早。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineGrazingClamp(
	TEXT("r.NPR.InnerLine.GrazingClamp"), 0.25f,
	TEXT("掠射角补偿的下限：Thresh = NormalThreshold / max(NdotV, GrazingClamp)。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineCrossGain(
	TEXT("r.NPR.InnerLine.CrossKernelGain"), 1.0f,
	TEXT("仅作用于 5 tap 降级核的梯度增益，用来把它的量级对齐到 9 tap Sobel，免得两档共用一套阈值。"),
	ECVF_RenderThreadSafe);

// ⚠ 这里曾有 r.NPR.InnerLine.DilateRadius（可分离 max-filter 膨胀 Pass），
// 2026-08-22 实测判死并删除，别再加回来 —— 三档参数全试过，没有一档能得到
// 「连续且不合并」的粗线（软遮罩糊成黑块 / 硬遮罩点状虚线 / 中间档仍是麻点）。
// 根因是结构性的：该检测器产出的遮罩沿边缘本就断续，形态学膨胀要求干净的二值遮罩。
// 详细判据与三组对比图见 Docs/其他文档/Kuro_Render_Performance_Scorecard.md §10。
// 膨胀删掉之后 EdgeMask/Composite 拆两趟也失去意义（中间无跨像素操作，与单趟数学等价），
// 已融合为单个 InnerLinePS。

static TAutoConsoleVariable<float> CVarNPRInnerLineOpacity(
	TEXT("r.NPR.InnerLine.Opacity"), 1.0f,
	TEXT("描边整体不透明度。"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNPRInnerLineColorR(
	TEXT("r.NPR.InnerLine.ColorR"), 0.06f, TEXT("描边颜色 R（线性）。"), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarNPRInnerLineColorG(
	TEXT("r.NPR.InnerLine.ColorG"), 0.04f, TEXT("描边颜色 G（线性）。"), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarNPRInnerLineColorB(
	TEXT("r.NPR.InnerLine.ColorB"), 0.07f, TEXT("描边颜色 B（线性）。"), ECVF_RenderThreadSafe);

// —— 脚部（或其它区域）专用抬阈值遮罩 ——
// 身体材质在目标区域把 Metallic 写成一个哨兵值（0.04），后处理这里按容差判档。
// 用 0.04 而不是 1.0，是为了不破坏 Lumen / SSR 的反射表现。
// FootThreshScale 默认 1.0 = 整条遮罩逻辑不起作用；材质侧接好线之后再调到 3.0~5.0。
static TAutoConsoleVariable<float> CVarNPRInnerLineFootSentinel(
	TEXT("r.NPR.InnerLine.FootSentinel"), 0.04f,
	TEXT("区域遮罩的 Metallic 哨兵值，需与身体材质写入值一致。"),
	ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarNPRInnerLineFootTolerance(
	TEXT("r.NPR.InnerLine.FootTolerance"), 0.015f,
	TEXT("哨兵值判定容差。GBuffer Metallic 是 8bit（1/255≈0.004），容差别小于 0.01。"),
	ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarNPRInnerLineFootThreshScale(
	TEXT("r.NPR.InnerLine.FootThreshScale"), 1.0f,
	TEXT("命中遮罩区域时 Thresh 的倍数。1.0=遮罩不生效（默认）；3.0~5.0 为实用起调区间。"),
	ECVF_RenderThreadSafe);

// ─────────────────────────────────────────────────────────────────────────────
// Shader
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 单 Pass：法线 Sobel + 深度闸门算出边缘强度，当场与 SceneColor 合成输出。
 *
 * 早先是 EdgeMask（写 R8 遮罩）→ Dilate.H → Dilate.V → Composite 四段。
 * 膨胀 2026-08-22 判死删除后，EdgeMask 与 Composite 之间不存在任何跨像素操作，
 * 两趟的输出与单趟数学等价，拆分纯属浪费一次全屏 RT 写 + 一次全屏采样 + 一次 draw。
 */
class FNPRInnerLinePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FNPRInnerLinePS);
	SHADER_USE_PARAMETER_STRUCT(FNPRInnerLinePS, FGlobalShader);

	class FSobel9Dim : SHADER_PERMUTATION_BOOL("NPR_SOBEL_9TAP");
	using FPermutationDomain = TShaderPermutationDomain<FSobel9Dim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, InvExtent)
		SHADER_PARAMETER(float, SampleStride)
		SHADER_PARAMETER(float, NormalThreshold)
		SHADER_PARAMETER(float, EdgeSoftness)
		SHADER_PARAMETER(float, DepthGate)
		SHADER_PARAMETER(float, GrazingClamp)
		SHADER_PARAMETER(float, CrossKernelGain)
		SHADER_PARAMETER(float, FadeStart)
		SHADER_PARAMETER(float, FadeEnd)
		SHADER_PARAMETER(float, FootSentinel)
		SHADER_PARAMETER(float, FootTolerance)
		SHADER_PARAMETER(float, FootThreshScale)
		SHADER_PARAMETER(FVector3f, LineColor)
		SHADER_PARAMETER(float, LineOpacity)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FNPRInnerLinePS, "/Plugin/NPRRenderPipeline/Private/NPRInnerLine.usf", "InnerLinePS", SF_Pixel);

// ─────────────────────────────────────────────────────────────────────────────
// SceneViewExtension
// ─────────────────────────────────────────────────────────────────────────────

/**
 * shader 里用的是延迟路径的 GetGBufferData，它只在 r.Substrate.ProjectGBufferFormat = 0 时正确
 * （该格式下 Substrate 仍写传统 GBuffer）。格式为 1 时数据在 Substrate MaterialTextureArray 里，
 * 要绑 FSubstratePublicGlobalUniformParameters —— 该类型不在 Renderer/Public，插件拿不到。
 *
 * 这个前提放在运行时查，不在 .usf 里用 #if/#error 拦：运行时的失败方式更好（报错一次 + 透传
 * SceneColor），而不是让整个引擎的全局 shader 编译失败、编辑器直接起不来。
 * （附带教训：原先那条 #error 后面写了中文，UE 的 shader 预处理器会丢掉条件栈并报出误导性的
 *  "No #endif found for previous #if"。#error 本身没问题，是**预处理指令行不能有非 ASCII**。）
 *
 * 该 CVar 是启动期只读项，静态缓存一次即可。
 */
static bool IsSubstrateGBufferFormatSupported()
{
	static const IConsoleVariable* CVarFormat =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.Substrate.ProjectGBufferFormat"));

	return CVarFormat == nullptr || CVarFormat->GetInt() == 0;
}

bool FNPRInnerLineSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Low 档在这里返回 false —— 整个 Pass 根本不进 RDG，RenderDoc 的 Event 树里查无此项。
	return CVarNPRInnerLineEnable.GetValueOnAnyThread() != 0;
}

void FNPRInnerLineSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}

	// SceneCapture 视图不描边：PerObjectShadow 插件靠 SceneCapture 采集角色阴影，
	// 描边混进那张 RT 会污染阴影结果。
	if (InView.bIsSceneCapture || InView.bIsSceneCaptureCube || InView.bIsReflectionCapture)
	{
		return;
	}

	// 缩略图 / 资产预览视图不描边。
	// 起因：2026-08-22 排障时打到过 Output.ViewRect = (0,0)-(160,160)，说明这个 SVE 也在
	// 内容浏览器缩略图和资产编辑器预览窗口上跑 —— 上面那三个 bIsSceneCapture 系标志都挡不住。
	// 后果有二：预览缩略图被画上描边（不该有），以及性能测量被这些额外视图污染。
	//
	// 判据用 WorldType 而不是 FSceneViewFamily::bThumbnailRendering：后者在 UE 5.8 里
	// **只有 WorldThumbnailRenderer 一处赋值**（Editor/UnrealEd/Private/ThumbnailRendering/
	// WorldThumbnailRenderer.cpp:51），盖不住骨骼网格/材质等其它资产的缩略图与预览窗口。
	// 这些预览场景一律跑在 EWorldType::EditorPreview 里，正好是我们要排除的那一类。
	// 白名单写法（只放行 Game/Editor/PIE）优于黑名单：以后引擎新增世界类型时默认是不画，
	// 失败方式安全。
	if (InView.Family != nullptr && InView.Family->Scene != nullptr)
	{
		const UWorld* World = InView.Family->Scene->GetWorld();
		const bool bIsRealView =
			World != nullptr &&
			(World->WorldType == EWorldType::Game ||
			 World->WorldType == EWorldType::Editor ||
			 World->WorldType == EWorldType::PIE);

		if (!bIsRealView)
		{
			return;
		}
	}

	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(
		this, &FNPRInnerLineSceneViewExtension::ExecuteInnerLinePass_RenderThread));
}

FScreenPassTexture FNPRInnerLineSceneViewExtension::ExecuteInnerLinePass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	Inputs.Validate();

	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	FRDGTextureRef SceneColorTexture = SceneColorSlice.TextureSRV ? SceneColorSlice.TextureSRV->GetParent() : nullptr;

	// Substrate GBuffer 格式不对时报错一次并透传 —— 宁可不画，也不静默画错。
	if (!IsSubstrateGBufferFormatSupported())
	{
		static bool bLoggedFormatError = false;
		if (!bLoggedFormatError)
		{
			bLoggedFormatError = true;
			UE_LOG(LogTemp, Error,
				TEXT("[NPRInnerLine] r.Substrate.ProjectGBufferFormat 不是 0，本 Pass 依赖传统 GBuffer，已跳过。")
				TEXT("要支持该格式需改走 SubstratePublic_GetSubstrateGBufferFromFirstBSDF，但其 uniform buffer 不在 Renderer/Public。"));
		}
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	// 三种情况直接透传，不要静默画错：
	//  · 开关关掉
	//  · 纹理数组（立体渲染）路径本方案不支持
	//  · 移动端路径（SceneTextures 为空、只有 MobileSceneTextures）——shader 用的是延迟路径的 GetGBufferData
	const bool bBypass =
		CVarNPRInnerLineEnable.GetValueOnRenderThread() == 0 ||
		SceneColorTexture == nullptr ||
		SceneColorTexture->Desc.IsTextureArray() ||
		// TRDGUniformBufferBinding 只有 operator bool()，没有 operator==(nullptr)，别写成 == nullptr
		// （定义见 RenderCore/Public/ShaderParameterMacros.h:234）。
		!Inputs.SceneTextures.SceneTextures;

	if (bBypass)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FIntPoint Extent = SceneColorTexture->Desc.Extent;
	const FIntRect ViewRect = SceneColorSlice.ViewRect;
	const FVector2f InvExtent(1.0f / Extent.X, 1.0f / Extent.Y);

	// A2 验收就靠这一行：RenderDoc 的 Event 树里能看到 NPRInnerLine。
	RDG_EVENT_SCOPE(GraphBuilder, "NPRInnerLine");

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(GlobalShaderMap);

	const FScreenPassViewInfo ViewInfo(View);
	const FScreenPassTextureViewport Viewport(Extent, ViewRect);
	FRHIBlendState* BlendState = TStaticBlendState<>::GetRHI();
	FRHIDepthStencilState* DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	FRHISamplerState* PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		const bool bCoversWholeTexture = (ViewRect.Min == FIntPoint::ZeroValue) && (ViewRect.Max == Extent);

		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				Extent, SceneColorTexture->Desc.Format, FClearValueBinding::Black,
				TexCreate_RenderTargetable | TexCreate_ShaderResource),
			TEXT("NPRInnerLine.Output"));

		Output = FScreenPassRenderTarget(
			OutputTexture, ViewRect,
			bCoversWholeTexture ? ERenderTargetLoadAction::ENoAction : ERenderTargetLoadAction::EClear);
	}

	{
		FNPRInnerLinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNPRInnerLinePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = Inputs.SceneTextures;
		PassParameters->InputTexture = SceneColorSlice.TextureSRV;
		PassParameters->InputSampler = PointSampler;
		PassParameters->InvExtent = InvExtent;
		PassParameters->SampleStride = CVarNPRInnerLineSampleStride.GetValueOnRenderThread();
		PassParameters->NormalThreshold = CVarNPRInnerLineNormalThreshold.GetValueOnRenderThread();
		PassParameters->EdgeSoftness = FMath::Max(CVarNPRInnerLineEdgeSoftness.GetValueOnRenderThread(), 1e-4f);
		PassParameters->DepthGate = CVarNPRInnerLineDepthGate.GetValueOnRenderThread();
		PassParameters->GrazingClamp = FMath::Max(CVarNPRInnerLineGrazingClamp.GetValueOnRenderThread(), 1e-3f);
		PassParameters->CrossKernelGain = CVarNPRInnerLineCrossGain.GetValueOnRenderThread();
		PassParameters->FadeStart = CVarNPRInnerLineFadeStart.GetValueOnRenderThread();
		PassParameters->FadeEnd = CVarNPRInnerLineFadeEnd.GetValueOnRenderThread();
		PassParameters->FootSentinel = CVarNPRInnerLineFootSentinel.GetValueOnRenderThread();
		PassParameters->FootTolerance = FMath::Max(CVarNPRInnerLineFootTolerance.GetValueOnRenderThread(), 1e-4f);
		PassParameters->FootThreshScale = CVarNPRInnerLineFootThreshScale.GetValueOnRenderThread();
		PassParameters->LineColor = FVector3f(
			CVarNPRInnerLineColorR.GetValueOnRenderThread(),
			CVarNPRInnerLineColorG.GetValueOnRenderThread(),
			CVarNPRInnerLineColorB.GetValueOnRenderThread());
		PassParameters->LineOpacity = CVarNPRInnerLineOpacity.GetValueOnRenderThread();
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		FNPRInnerLinePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FNPRInnerLinePS::FSobel9Dim>(CVarNPRInnerLineSobel9.GetValueOnRenderThread() != 0);
		TShaderMapRef<FNPRInnerLinePS> PixelShader(GlobalShaderMap, PermutationVector);

		AddDrawScreenPass(
			GraphBuilder, RDG_EVENT_NAME("InnerLine"), ViewInfo,
			FScreenPassTextureViewport(Output), Viewport, VertexShader, PixelShader,
			BlendState, DepthStencilState, PassParameters);
	}

	return Output;
}
