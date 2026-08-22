#include "RenderFunctionLibrary.h"

#include "GameFramework/Actor.h"
#include "CharacterRenderComponent.h"

UCharacterRenderComponent* URenderFunctionLibrary::FindOrAddRenderComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (UCharacterRenderComponent* ExistingComponent = Actor->FindComponentByClass<UCharacterRenderComponent>())
	{
		return ExistingComponent;
	}

	UCharacterRenderComponent* NewComponent = NewObject<UCharacterRenderComponent>(Actor, TEXT("CharacterRenderComponent"));
	if (!NewComponent)
	{
		return nullptr;
	}

	NewComponent->RegisterComponent();
	Actor->AddInstanceComponent(NewComponent);
	NewComponent->InitializeRenderBindings();
	return NewComponent;
}

void URenderFunctionLibrary::QuickApplyRenderState(AActor* Actor, const FRenderStatePayload& Payload)
{
	if (UCharacterRenderComponent* RenderComponent = FindOrAddRenderComponent(Actor))
	{
		RenderComponent->ApplyRenderState(Payload);
	}
}

void URenderFunctionLibrary::QuickApplyQuality(AActor* Actor, const FRenderQualitySettings& QualitySettings)
{
	if (UCharacterRenderComponent* RenderComponent = FindOrAddRenderComponent(Actor))
	{
		RenderComponent->ApplyQualitySettings(QualitySettings);
	}
}
