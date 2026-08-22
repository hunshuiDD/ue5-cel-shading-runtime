// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "UMyFurComponent.generated.h"

/**
 * 
 */
UCLASS(Blueprintable,meta=(BlueprintSpawnableComponent))
class RENDERCONTROLLEARN_API UMyFurComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()
	
public:
	UMyFurComponent()=default;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fur")
	USkeletalMesh* FurMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fur")
	UMaterialInterface* FurMat;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fur")
	UMaterialInterface* InVisableMat;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fur")
	int32 ShellCount =32;
	
private:
	void ClearShells();
	void BuildShells();
	
	UPROPERTY(Transient)
	TArray<USkeletalMeshComponent*> Shells;
};
