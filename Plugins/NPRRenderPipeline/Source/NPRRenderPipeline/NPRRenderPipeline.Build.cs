// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NPRRenderPipeline : ModuleRules
{
	public NPRRenderPipeline(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",       // FSceneViewExtension / FSceneTextureShaderParameters
				"RenderCore",   // AddShaderSourceDirectoryMapping / FGlobalShader / RDG
				"RHI",          // FRHIBlendState / FRHIDepthStencilState
				"Renderer",     // FScreenPassVS / AddDrawScreenPass / FPostProcessMaterialInputs
				"Projects",     // IPluginManager（定位插件 Shaders/ 目录）
			}
			);
	}
}
