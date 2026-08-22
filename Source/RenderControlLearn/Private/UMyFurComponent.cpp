// Fill out your copyright notice in the Description page of Project Settings.


#include "UMyFurComponent.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ObjectSaveContext.h"

namespace
{
	const FName MyFurShellTag(TEXT("MyFurRuntimeShell"));
}

void UMyFurComponent::ClearShells()
{
	TArray<USkeletalMeshComponent*> ComponentsToDestroy = Shells;
	
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Shell = *It;
		if (Shell && Shell->GetOuter() == this && Shell != this)
		{
			ComponentsToDestroy.AddUnique(Shell);
		}
	}
	
	for (USkeletalMeshComponent* Shell : ComponentsToDestroy)
	{
		if (Shell)
		{
			Shell->DestroyComponent();
		}
	}
	Shells.Empty();
}

void UMyFurComponent::BuildShells()
{
	AActor* Owner = GetOwner();
	if (!Owner||!FurMesh||!FurMat)return;
	
	USkeletalMeshComponent* LeaderComp =Cast<USkeletalMeshComponent>(Owner->GetComponentByClass(USkeletalMeshComponent::StaticClass()));
	if (!LeaderComp||LeaderComp==this)return;
	
	int32 FurMaterialSlotIndex=6;
	int32 TotalSlots=LeaderComp->GetNumMaterials();
	for (int32 i = 0; i < ShellCount; i++)
	{
		USkeletalMeshComponent* Shell = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
		Shell->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Shell->ComponentTags.Add(MyFurShellTag);
		Shell->SetIsVisualizationComponent(true);
		Shell->SetSkeletalMesh(FurMesh);
		Shell->SetMaterial(0,FurMat);
		Shell->SetupAttachment(LeaderComp);
		Shell->SetLeaderPoseComponent(LeaderComp);
		
		
		for (int32 Slot=0;Slot<TotalSlots;++Slot)
		{
			if (Slot==FurMaterialSlotIndex)
			{
				Shell->SetMaterial(Slot,FurMat);
			}
			else
			{
				Shell->SetMaterial(Slot,InVisableMat);
			}
		}
		
		
		float layerNorm = (ShellCount > 1) ? (float)i / (float)(ShellCount - 1) : 0.0f;
		Shell->SetCustomPrimitiveDataFloat(0,layerNorm);
		Shell->SetTranslucentSortPriority(i);
		
		Shell->RegisterComponent();
		
		Shells.Add(Shell);
	}
}

void UMyFurComponent::OnUnregister()
{
	ClearShells();
	
	Super::OnUnregister();
}

void UMyFurComponent::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	ClearShells();
	
	Super::PreSave(ObjectSaveContext);
}

void UMyFurComponent::OnRegister()
{
	Super::OnRegister();
	
	ClearShells();
	if (IsTemplate())return;
	
	BuildShells();
}
