#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RenderTypes.h"
#include "RenderProjectSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Render Settings"))
class RENDERCONTROLLEARN_API URenderProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URenderProjectSettings();

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render")
	FRenderQualitySettings HighPCQuality;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render")
	FRenderQualitySettings MidPCQuality;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render")
	FRenderQualitySettings LowSpecQuality;

	// 五个内置渲染状态的默认兜底数据，原先分散硬编码在 ARenderDemoController::BuildFallback*State() 里，
	// 现在和画质三档一样统一收敛到这里——控制器侧不再有另一份可能不一致的默认值
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render|State")
	FRenderStatePayload DefaultNormalState;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render|State")
	FRenderStatePayload DefaultHitFlashState;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render|State")
	FRenderStatePayload DefaultRimBoostState;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render|State")
	FRenderStatePayload DefaultOutlineBoostState;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Render|State")
	FRenderStatePayload DefaultBattleTintState;

	virtual FName GetCategoryName() const override
	{
		return TEXT("Project");
	}
};
