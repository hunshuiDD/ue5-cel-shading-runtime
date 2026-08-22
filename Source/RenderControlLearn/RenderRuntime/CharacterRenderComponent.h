#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "RenderTypes.h"
#include "CharacterRenderComponent.generated.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

USTRUCT()
struct FCapturedScalarParameter
{
	GENERATED_BODY()

	UPROPERTY()
	float Value = 0.0f;

	UPROPERTY()
	bool bWasFound = false;
};

USTRUCT()
struct FCapturedVectorParameter
{
	GENERATED_BODY()

	UPROPERTY()
	FLinearColor Value = FLinearColor::Transparent;

	UPROPERTY()
	bool bWasFound = false;
};

USTRUCT()
struct FMaterialParameterSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, FCapturedScalarParameter> ScalarValues;

	UPROPERTY()
	TMap<FName, FCapturedVectorParameter> VectorValues;
};

USTRUCT()
struct FSlotMaterialRuntime
{
	GENERATED_BODY()

	UPROPERTY()
	ECharacterPartType PartType = ECharacterPartType::Up;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial = nullptr;

	UPROPERTY()
	FMaterialParameterSnapshot Snapshot;

	UPROPERTY(Transient)
	TMap<FName, FVector> HairSphereCenterSocketOffsets;
};

USTRUCT()
struct FActiveRenderState
{
	GENERATED_BODY()

	UPROPERTY()
	FRenderStatePayload Payload;

	UPROPERTY()
	float ElapsedTime = 0.0f;
};

// 过渡期材质切换的一条记录：某 mesh 的某槽，稳态用 SteadyMaterial（原 DMI），过渡期用 MaskMaterial（Masked 溶解材质）。
USTRUCT()
struct FFormMaskSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh = nullptr;

	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SteadyMaterial = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MaskMaterial = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRenderQualityChangedSignature, ERenderQualityTier, PreviousTier, ERenderQualityTier, NewTier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRenderStateChangedSignature, FName, StateName);

UCLASS(ClassGroup=(Rendering), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class RENDERCONTROLLEARN_API UCharacterRenderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCharacterRenderComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render", meta = (DisplayName = "Target Skeletal Mesh"))
	FComponentReference TargetSkeletalMeshReference;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render", meta = (DisplayName = "Target Outline Mesh"))
	FComponentReference TargetOutlineMeshReference;

	// —— 双形态：二形态本体 / 描边 Mesh（一形态复用上面的 Target*MeshReference）——
	// 用数组：二形态当前是单个骨骼网格，日后加头顶光环平面等子 Mesh 直接拖进来即可，无需改代码。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render|Form", meta = (DisplayName = "Second Form Meshes"))
	TArray<FComponentReference> SecondFormMeshReferences;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render|Form", meta = (DisplayName = "Second Form Outline Meshes"))
	TArray<FComponentReference> SecondFormOutlineMeshReferences;

	// 端点判定阈值：FormBlend<=阈值 只显一形态；>=1-阈值 只显二形态；两者之间两套都显（交叉溶解过渡）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Form", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FormEndpointThreshold = 0.001f;

	// 过渡期 Masked 材质映射：键=槽上的稳态原材质(MI)，值=对应的 Masked 溶解材质(Mask1/Mask2)。
	// 只有在此登记过的材质，过渡期才会被切换；留空则过渡材质切换不启用，行为回到纯可见性硬切。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render|Form", meta = (DisplayName = "Form Transition Mask Materials"))
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> FormTransitionMaskMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render")
	bool bAutoBindOwnerSkeletalMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render")
	TArray<FPartMaterialBinding> MaterialBindings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render")
	TObjectPtr<URenderQualityDataAsset> DefaultQualityData;

	// 两个头发高光球心共用的跟踪骨骼/Socket。材质中已调好的球心位置会保留为该 Socket 下的局部偏移。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Hair")
	FName HairSphereCenterSocketName = NAME_None;

	// 两个材质球心参数名。它们分别保留自己的初始位置，但跟随同一个 Socket 的动画平移和旋转。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Hair")
	FName HairSphereCenterParamName = TEXT("SphereCenter");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render|Hair")
	FName HairSecondarySphereCenterParamName = TEXT("SphereCenter_LongHair");

	UPROPERTY(BlueprintAssignable, Category = "Render")
	FRenderQualityChangedSignature OnQualityTierChanged;

	UPROPERTY(BlueprintAssignable, Category = "Render")
	FRenderStateChangedSignature OnRenderStateStarted;

	UPROPERTY(BlueprintAssignable, Category = "Render")
	FRenderStateChangedSignature OnRenderStateFinished;

	UFUNCTION(BlueprintCallable, Category = "Render")
	void InitializeRenderBindings();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Render")
	void CleanupGeneratedOwnerSkeletalMeshes();

	UFUNCTION(BlueprintCallable, Category = "Render")
    void ApplyQualitySettings(const FRenderQualitySettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "Render")
	void ApplyQualityData(URenderQualityDataAsset* QualityDataAsset);

	UFUNCTION(BlueprintCallable, Category = "Render")
	void ApplyRenderState(const FRenderStatePayload& Payload);

	UFUNCTION(BlueprintCallable, Category = "Render")
	void ApplyRenderStateData(URenderStateDataAsset* StateDataAsset);

	UFUNCTION(BlueprintCallable, Category = "Render")
	void ClearRenderState(FName StateName);

	UFUNCTION(BlueprintCallable, Category = "Render")
	void ClearAllRenderStates();

	UFUNCTION(BlueprintCallable, Category = "Render")
	void SetScalarParameterOnAllMaterials(FName ParameterName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Render")
	void SetVectorParameterOnAllMaterials(FName ParameterName, FLinearColor Value);

	// 按 FormBlend 端点切换一/二形态本体+描边的可见性（过渡期两套并显，端点隐藏另一套）。
	// 阴影走 ShadowCapture 的 ShowOnlyActors 白名单会自动跟随可见性，无需在此额外处理。
	UFUNCTION(BlueprintCallable, Category = "Render|Form")
	void ApplyFormVisibility(float FormBlendValue);

	UFUNCTION(BlueprintPure, Category = "Render")
	ERenderQualityTier GetCurrentQualityTier() const
	{
		return CurrentQualityTier;
	}

protected:
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ResolvedTargetSkeletalMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ResolvedTargetOutlineMesh = nullptr;

	// —— 双形态二形态解析缓存（一形态复用上面的 ResolvedTarget*Mesh）——
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> ResolvedSecondFormMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> ResolvedSecondFormOutlineMeshes;

	// 二形态引用是否已解析（惰性解析用；控制器可能在本组件 BeginPlay 之前就调用可见性切换）
	bool bSecondFormResolved = false;

	UPROPERTY(Transient)
	TArray<FSlotMaterialRuntime> RuntimeMaterials;

	UPROPERTY(Transient)
	TArray<FActiveRenderState> ActiveStates;

	UPROPERTY(Transient)
	ERenderQualityTier CurrentQualityTier = ERenderQualityTier::HighPC;

	// 画质门控：LowSpec 关壳描边时置 false，ApplyFormVisibility 会把它与“当前形态描边应显”做 AND，
	// 使质量开关与二形态可见性可组合（质量说关就一定隐藏，形态只决定显哪一套）。由 ApplyQualitySettings 写入。
	UPROPERTY(Transient)
	bool bOutlineEnabledByQuality = true;

	// 过渡期材质切换：缓存的 (mesh,槽,稳态DMI,Mask) 表。
	UPROPERTY(Transient)
	TArray<FFormMaskSlot> FormMaskSlots;

	USkeletalMeshComponent* ResolveSkeletalMeshReference(const FComponentReference& ComponentReference) const;
	void ResolveSecondFormMeshes();
	// 让一/二形态本体即使被隐藏也持续评估姿势：避免隐藏的正好是 LeaderPose 主控网格时，跟随网格冻结。
	void SetFormMeshesKeepTickingPose();
	static void SetMeshVisible(USkeletalMeshComponent* Mesh, bool bVisible);
	static void SetMeshListVisible(const TArray<TObjectPtr<USkeletalMeshComponent>>& Meshes, bool bVisible);
	// 过渡期切 Mask 材质 / 端点切回稳态 DMI；逐槽比对实际材质，只对不一致的槽调用 SetMaterial。
	void SetFormMaskActive(bool bActive);
	void CacheMaterialSlots();
	void CacheMaterialSlotsForMesh(USkeletalMeshComponent* MeshComponent, bool bForceOutlinePart);
	ECharacterPartType ResolvePartTypeBySlotName(FName SlotName) const;
	// 按需捕获参数原始值（仅首次、且只在材质真的有该参数时记录 bWasFound），供状态结束后精确还原
	void EnsureScalarCaptured(FSlotMaterialRuntime& RuntimeMaterial, FName ParameterName);
	void EnsureVectorCaptured(FSlotMaterialRuntime& RuntimeMaterial, FName ParameterName);
	void ApplyImmediateState(FSlotMaterialRuntime& RuntimeMaterial, const FActiveRenderState& ActiveState, float Weight);
	void RestoreSnapshot(FSlotMaterialRuntime& RuntimeMaterial, const FRenderStatePayload& Payload);
	void RefreshRuntimeState(float DeltaTime);
	bool IsTargetPart(const FSlotMaterialRuntime& RuntimeMaterial, const FRenderStatePayload& Payload) const;
	void CaptureHairSphereCenterOffsets();
	// 每帧用当前 Socket 变换重建两个球心的组件局部坐标。
	void UpdateHairSphereCenter();
};
