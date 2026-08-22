#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/**
 * 屏幕空间内描边的渲染线程实现（替代后处理材质 M_InnerLine）。
 *
 * 注入点选 EPostProcessingPass::BeforeDOF == BL_SceneColorBeforeDOF，与旧 PPM 版一致，
 * 在 TSR 之前——描边跟随当帧被抖动的几何体，由 TSR 一并做时域解算。
 * （不要退回 TSR 之后再手动反 jitter，见 屏幕空间内线描边_实现与TSR位置踩坑_20260727.md §5.3）
 *
 * 帧内结构（RenderDoc 里可见）：
 *   NPRInnerLine
 *     └─ InnerLine     法线 Sobel + 深度闸门 → 当场 lerp(SceneColor, LineColor, Edge)
 *
 * 早先是 EdgeMask → Dilate.H → Dilate.V → Composite 四段，膨胀 Pass 2026-08-22 实测判死
 * （软遮罩糊成块 / 硬遮罩点状虚线 / 中间档仍是麻点，根因是该检测器的遮罩沿边缘本就断续，
 *  而形态学膨胀要求干净的二值遮罩）。膨胀去掉后 EdgeMask 与 Composite 之间没有任何跨像素
 * 操作，两趟与单趟数学等价，故融合为一趟。判据与三组对比见 Scorecard §10，别再拆回去。
 *
 * 开关与参数全部走 CVar（r.NPR.InnerLine.*），由 ARenderDemoController 在游戏线程写入；
 * 不要跨线程直接读 UObject。
 */
class NPRRENDERPIPELINE_API FNPRInnerLineSceneViewExtension : public FSceneViewExtensionBase
{
public:
	explicit FNPRInnerLineSceneViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	//~ 纯游戏线程回调，本方案不需要，留空实现满足纯虚接口
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	FScreenPassTexture ExecuteInnerLinePass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);
};
