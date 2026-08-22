#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RenderTypes.h"
#include "RenderFunctionLibrary.generated.h"

class AActor;
class UCharacterRenderComponent;

UCLASS()
class RENDERCONTROLLEARN_API URenderFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Render")
	static UCharacterRenderComponent* FindOrAddRenderComponent(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Render")
	static void QuickApplyRenderState(AActor* Actor, const FRenderStatePayload& Payload);

	UFUNCTION(BlueprintCallable, Category = "Render")
	static void QuickApplyQuality(AActor* Actor, const FRenderQualitySettings& QualitySettings);
};
