#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FNPRInnerLineSceneViewExtension;

/**
 * 模块职责只有两件：
 *  1) StartupModule 里注册 shader 虚拟路径映射；
 *  2) 引擎初始化完成后创建 SceneViewExtension，交由引擎持有生命周期。
 *
 * ⚠ .uplugin 的 LoadingPhase 必须是 PostConfigInit，不能用 PreDefault。
 * 本模块用 IMPLEMENT_GLOBAL_SHADER 声明了 shader type，它触发的静态初始化会走
 * FShaderTypeRegistration 构造函数，里面直接 checkf(!AreShaderTypesInitialized(), ...)
 * 点名要求 PostConfigInit（RenderCore/Private/Shader.cpp:315）。晚了就 DLL 加载失败
 * （GetLastError=1114），并在引擎退出时连带崩在 RenderResource.cpp:100 的资源表断言上。
 * 注：引擎自带的 DeformerGraph/OptimusCore 用 PreDefault，但它只做目录映射、不声明 shader type，
 * 别拿它当范例照抄。
 */
class FNPRRenderPipelineModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// 为什么不在 StartupModule 里直接建 SVE：PreDefault 阶段引擎渲染侧尚未初始化完毕，
	// FSceneViewExtensions 的注册要等 OnPostEngineInit 才稳妥。
	void CreateSceneViewExtension();

	TSharedPtr<FNPRInnerLineSceneViewExtension, ESPMode::ThreadSafe> InnerLineViewExtension;
	FDelegateHandle PostEngineInitHandle;
};
