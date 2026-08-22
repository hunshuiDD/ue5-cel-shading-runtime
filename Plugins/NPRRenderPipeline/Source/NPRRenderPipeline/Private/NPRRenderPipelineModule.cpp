#include "NPRRenderPipelineModule.h"

#include "NPRInnerLineSceneViewExtension.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FNPRRenderPipelineModule"

void FNPRRenderPipelineModule::StartupModule()
{
	// 虚拟路径前缀跟随引擎约定 /Plugin/<PluginName>，映射到插件根下的 Shaders（不含 Private）。
	// 于是 Shaders/Private/NPRInnerLine.usf 的完整虚拟路径是
	//   /Plugin/NPRRenderPipeline/Private/NPRInnerLine.usf
	// 范例取自 Engine/Plugins/Animation/DeformerGraph 的 OptimusCore。
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NPRRenderPipeline"));
	if (Plugin.IsValid())
	{
		const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/NPRRenderPipeline"), PluginShaderDir);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NPRRenderPipeline] 找不到插件目录，shader 虚拟路径未注册，内描边 Pass 无法编译。"));
	}

	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNPRRenderPipelineModule::CreateSceneViewExtension);
}

void FNPRRenderPipelineModule::CreateSceneViewExtension()
{
	if (!InnerLineViewExtension.IsValid())
	{
		InnerLineViewExtension = FSceneViewExtensions::NewExtension<FNPRInnerLineSceneViewExtension>();
	}
}

void FNPRRenderPipelineModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	// SVE 由引擎以 weak ptr 持有，这里放掉最后一个强引用即完成注销。
	InnerLineViewExtension.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNPRRenderPipelineModule, NPRRenderPipeline)
