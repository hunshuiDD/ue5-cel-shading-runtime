// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreObjectShadow.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FPreObjectShadowModule"

void FPreObjectShadowModule::StartupModule()
{
	// 把插件 Shader/ 目录映射到虚拟路径 /PreObjectShadow，材质 Custom 节点即可
	// #include "/PreObjectShadow/PerShadow.ush"。必须在 shader 编译前完成，
	// 故本模块 LoadingPhase 设为 PostConfigInit（见 .uplugin）。
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PreObjectShadow")))
	{
		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shader"));
		AddShaderSourceDirectoryMapping(TEXT("/PreObjectShadow"), ShaderDir);
	}
}

void FPreObjectShadowModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPreObjectShadowModule, PreObjectShadow)