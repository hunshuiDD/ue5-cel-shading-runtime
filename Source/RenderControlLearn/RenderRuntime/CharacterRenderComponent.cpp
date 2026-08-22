#include "CharacterRenderComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

UCharacterRenderComponent::UCharacterRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 默认不开 Tick：没有激活的渲染状态时组件完全不参与每帧更新；有状态时由 ApplyRenderState 打开，跑完再关
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCharacterRenderComponent::BeginPlay()
{
	Super::BeginPlay();

	InitializeRenderBindings();

	if (DefaultQualityData)
	{
		ApplyQualityData(DefaultQualityData);
	}

	// 配了球心跟踪骨骼名才需要每帧更新：这个用途和"有没有激活渲染状态"无关，动画只要在播就得跟，
	// 不能像状态那样跑完就关 Tick。没配置这个字段的角色（没用球面法线高光技术）Tick 行为完全不变。
	if (!HairSphereCenterSocketName.IsNone())
	{
		SetComponentTickEnabled(true);
		UpdateHairSphereCenter();
	}
}

void UCharacterRenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshRuntimeState(DeltaTime);
	UpdateHairSphereCenter();
}

void UCharacterRenderComponent::UpdateHairSphereCenter()
{
	if (HairSphereCenterSocketName.IsNone() || !ResolvedTargetSkeletalMesh)
	{
		return;
	}

	if (!ResolvedTargetSkeletalMesh->DoesSocketExist(HairSphereCenterSocketName))
	{
		return;
	}

	const FTransform SocketTransform = ResolvedTargetSkeletalMesh->GetSocketTransform(HairSphereCenterSocketName, RTS_Component);

	for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
	{
		if (!RuntimeMaterial.DynamicMaterial)
		{
			continue;
		}

		for (const TPair<FName, FVector>& CenterOffset : RuntimeMaterial.HairSphereCenterSocketOffsets)
		{
			const FVector LocalCenter = SocketTransform.TransformPosition(CenterOffset.Value);
			RuntimeMaterial.DynamicMaterial->SetVectorParameterValue(
				CenterOffset.Key,
				FLinearColor(LocalCenter.X, LocalCenter.Y, LocalCenter.Z, 0.0f));
		}
	}
}

void UCharacterRenderComponent::InitializeRenderBindings()
{
	CacheMaterialSlots();
	if (ResolvedTargetSkeletalMesh)
	{
		AddTickPrerequisiteComponent(ResolvedTargetSkeletalMesh);
	}
	// 二形态本体同样作为球心 Socket 读取的前置：保证在其姿势更新后再读 Socket。
	for (USkeletalMeshComponent* Mesh : ResolvedSecondFormMeshes)
	{
		if (Mesh)
		{
			AddTickPrerequisiteComponent(Mesh);
		}
	}
	CaptureHairSphereCenterOffsets();
}

void UCharacterRenderComponent::CaptureHairSphereCenterOffsets()
{
	if (HairSphereCenterSocketName.IsNone()
		|| !ResolvedTargetSkeletalMesh
		|| !ResolvedTargetSkeletalMesh->DoesSocketExist(HairSphereCenterSocketName))
	{
		return;
	}

	const FTransform SocketTransform = ResolvedTargetSkeletalMesh->GetSocketTransform(HairSphereCenterSocketName, RTS_Component);
	const FName ParameterNames[] = { HairSphereCenterParamName, HairSecondarySphereCenterParamName };

	for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
	{
		RuntimeMaterial.HairSphereCenterSocketOffsets.Empty();
		if (!RuntimeMaterial.DynamicMaterial)
		{
			continue;
		}

		for (const FName ParameterName : ParameterNames)
		{
			if (ParameterName.IsNone())
			{
				continue;
			}

			FLinearColor InitialCenter;
			if (RuntimeMaterial.DynamicMaterial->GetVectorParameterValue(FMaterialParameterInfo(ParameterName), InitialCenter))
			{
				const FVector ComponentSpaceCenter(InitialCenter.R, InitialCenter.G, InitialCenter.B);
				RuntimeMaterial.HairSphereCenterSocketOffsets.Add(
					ParameterName,
					SocketTransform.InverseTransformPosition(ComponentSpaceCenter));
			}
		}
	}
}

// CleanupLegacyRuntimeSkeletalMeshes 已删除：它用全局 TObjectIterator 扫描内存里所有 USkeletalMeshComponent 再销毁，
// 是早期为清理重复网格加的历史 workaround，开销大且有误删其它 Actor 网格的风险。编辑器手动清理请用下面的
// CleanupGeneratedOwnerSkeletalMeshes（CallInEditor 按钮，只扫本 Actor 自己的组件）。

void UCharacterRenderComponent::CleanupGeneratedOwnerSkeletalMeshes()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	ResolvedTargetSkeletalMesh = ResolveSkeletalMeshReference(TargetSkeletalMeshReference);
	ResolvedTargetOutlineMesh = ResolveSkeletalMeshReference(TargetOutlineMeshReference);

	TArray<USkeletalMeshComponent*> OwnerMeshes;
	Owner->GetComponents(OwnerMeshes);

	for (USkeletalMeshComponent* MeshComponent : OwnerMeshes)
	{
		if (!IsValid(MeshComponent) || MeshComponent->IsTemplate())
		{
			continue;
		}

		if (MeshComponent == ResolvedTargetSkeletalMesh || MeshComponent == ResolvedTargetOutlineMesh)
		{
			continue;
		}

		const FString ComponentName = MeshComponent->GetName();
		const bool bLooksGenerated =
			ComponentName.StartsWith(TEXT("SkeletalMeshComponent")) ||
			MeshComponent->HasAnyFlags(RF_Transient) ||
			MeshComponent->CreationMethod == EComponentCreationMethod::Instance ||
			MeshComponent->CreationMethod == EComponentCreationMethod::UserConstructionScript;

		if (!bLooksGenerated)
		{
			continue;
		}

		MeshComponent->SetVisibility(false, true);
		MeshComponent->SetHiddenInGame(true);
		MeshComponent->SetCastShadow(false);
		MeshComponent->SetVisibleInRayTracing(false);
		MeshComponent->DestroyComponent();
	}
}

void UCharacterRenderComponent::ApplyQualitySettings(const FRenderQualitySettings& Settings)
{
	const ERenderQualityTier PreviousTier = CurrentQualityTier;
	CurrentQualityTier = Settings.QualityTier;

	// 壳描边开关走 Mesh 可见性（非材质参数）。这里记录门控；被禁用时立即隐藏描边，
	// 重新启用时由控制器随后调 ApplyFormVisibility(FormBlend) 按当前形态恢复正确的那一套。
	bOutlineEnabledByQuality = Settings.bUseShellOutline;
	if (!bOutlineEnabledByQuality)
	{
		SetMeshVisible(ResolvedTargetOutlineMesh, false);
		SetMeshListVisible(ResolvedSecondFormOutlineMeshes, false);
	}

	// 质量降级项由材质 QualitySwitch + r.MaterialQualityLevel 选择编译期分支；
	// 动态 Quality_* 标量即使同名参数存在也不会删除另一侧 shader 指令，因此不在这里下发。
	// SSR / AO 走 CVar，描边走 Mesh 可见性或 PPV Blendable 增删。接线规格见
	// Docs/其他文档/Kuro_Render_质量分档接通_TechSpec_2026-07-23.md。
	for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
	{
		if (!RuntimeMaterial.DynamicMaterial)
		{
			continue;
		}

		for (const FRenderScalarParameterValue& Override : Settings.ScalarOverrides)
		{
			RuntimeMaterial.DynamicMaterial->SetScalarParameterValue(Override.ParameterName, Override.Value);
		}
	}

	OnQualityTierChanged.Broadcast(PreviousTier, CurrentQualityTier);
}

void UCharacterRenderComponent::ApplyQualityData(URenderQualityDataAsset* QualityDataAsset)
{
	if (!QualityDataAsset)
	{
		return;
	}

	ApplyQualitySettings(QualityDataAsset->QualitySettings);
}

void UCharacterRenderComponent::ApplyRenderState(const FRenderStatePayload& Payload)
{
	FActiveRenderState NewState;
	NewState.Payload = Payload;
	NewState.ElapsedTime = 0.0f;

	ActiveStates.RemoveAll([&Payload](const FActiveRenderState& State)
	{
		return State.Payload.StateName == Payload.StateName;
	});
	ActiveStates.Add(NewState);
	// 有激活状态了，打开 Tick 让 RefreshRuntimeState 每帧推进（状态全部结束后会自动关掉）
	SetComponentTickEnabled(true);
	OnRenderStateStarted.Broadcast(Payload.StateName);
}

void UCharacterRenderComponent::ApplyRenderStateData(URenderStateDataAsset* StateDataAsset)
{
	if (!StateDataAsset)
	{
		return;
	}

	ApplyRenderState(StateDataAsset->StatePayload);
}

void UCharacterRenderComponent::ClearRenderState(FName StateName)
{
	bool bAnyCleared = false;
	for (FActiveRenderState& State : ActiveStates)
	{
		if (State.Payload.StateName != StateName)
		{
			continue;
		}

		if (State.Payload.bRestoreOriginalValues)
		{
			for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
			{
				if (IsTargetPart(RuntimeMaterial, State.Payload))
				{
					RestoreSnapshot(RuntimeMaterial, State.Payload);
				}
			}
		}
		bAnyCleared = true;
	}

	ActiveStates.RemoveAll([StateName](const FActiveRenderState& State)
	{
		return State.Payload.StateName == StateName;
	});

	// 先把 ActiveStates 改完，再广播：即使回调里又调用 Apply/ClearRenderState 改动数组，也不会让正在遍历的迭代器失效
	if (bAnyCleared)
	{
		OnRenderStateFinished.Broadcast(StateName);
	}
	if (ActiveStates.IsEmpty())
	{
		SetComponentTickEnabled(!HairSphereCenterSocketName.IsNone());
	}
}

void UCharacterRenderComponent::ClearAllRenderStates()
{
	TArray<FName> FinishedStateNames;
	FinishedStateNames.Reserve(ActiveStates.Num());

	for (const FActiveRenderState& State : ActiveStates)
	{
		if (State.Payload.bRestoreOriginalValues)
		{
			for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
			{
				if (IsTargetPart(RuntimeMaterial, State.Payload))
				{
					RestoreSnapshot(RuntimeMaterial, State.Payload);
				}
			}
		}
		FinishedStateNames.Add(State.Payload.StateName);
	}

	// 先清空数组、恢复到球心跟踪所需的 Tick 状态，最后再统一广播，避免委托回调里再改 ActiveStates 造成重入
	ActiveStates.Empty();
	SetComponentTickEnabled(!HairSphereCenterSocketName.IsNone());

	for (const FName& StateName : FinishedStateNames)
	{
		OnRenderStateFinished.Broadcast(StateName);
	}
}

void UCharacterRenderComponent::SetScalarParameterOnAllMaterials(FName ParameterName, float Value)
{
	for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
	{
		if (RuntimeMaterial.DynamicMaterial)
		{
			RuntimeMaterial.DynamicMaterial->SetScalarParameterValue(ParameterName, Value);
		}
	}
}

void UCharacterRenderComponent::SetVectorParameterOnAllMaterials(FName ParameterName, FLinearColor Value)
{
	for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
	{
		if (RuntimeMaterial.DynamicMaterial)
		{
			RuntimeMaterial.DynamicMaterial->SetVectorParameterValue(ParameterName, Value);
		}
	}
}

void UCharacterRenderComponent::CacheMaterialSlots()
{
	RuntimeMaterials.Empty();
	// 重建过渡材质切换表。
	FormMaskSlots.Reset();

	ResolvedTargetSkeletalMesh = ResolveSkeletalMeshReference(TargetSkeletalMeshReference);
	ResolvedTargetOutlineMesh = ResolveSkeletalMeshReference(TargetOutlineMeshReference);

	if (!ResolvedTargetSkeletalMesh && bAutoBindOwnerSkeletalMesh)
	{
		ResolvedTargetSkeletalMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}

	CacheMaterialSlotsForMesh(ResolvedTargetSkeletalMesh, false);

	if (ResolvedTargetOutlineMesh)
	{
		ResolvedTargetOutlineMesh->SetCastShadow(false);
		ResolvedTargetOutlineMesh->SetVisibleInRayTracing(false);
		CacheMaterialSlotsForMesh(ResolvedTargetOutlineMesh, true);
	}

	// —— 双形态：把二形态各槽材质并入 RuntimeMaterials，让画质分级/渲染状态/球心跟踪一并作用到二形态 ——
	ResolveSecondFormMeshes();
	for (USkeletalMeshComponent* Mesh : ResolvedSecondFormMeshes)
	{
		CacheMaterialSlotsForMesh(Mesh, false);
	}
	for (USkeletalMeshComponent* OutlineMesh : ResolvedSecondFormOutlineMeshes)
	{
		if (OutlineMesh)
		{
			OutlineMesh->SetCastShadow(false);
			OutlineMesh->SetVisibleInRayTracing(false);
			CacheMaterialSlotsForMesh(OutlineMesh, true);
		}
	}

	// 一/二形态本体持续评估姿势：隐藏的正好是 LeaderPose 主控网格时，跟随网格才不会冻结。
	SetFormMeshesKeepTickingPose();
}

USkeletalMeshComponent* UCharacterRenderComponent::ResolveSkeletalMeshReference(const FComponentReference& ComponentReference) const
{
	return Cast<USkeletalMeshComponent>(ComponentReference.GetComponent(GetOwner()));
}

void UCharacterRenderComponent::ResolveSecondFormMeshes()
{
	ResolvedSecondFormMeshes.Reset();
	for (const FComponentReference& Reference : SecondFormMeshReferences)
	{
		if (USkeletalMeshComponent* Mesh = ResolveSkeletalMeshReference(Reference))
		{
			ResolvedSecondFormMeshes.Add(Mesh);
		}
	}

	ResolvedSecondFormOutlineMeshes.Reset();
	for (const FComponentReference& Reference : SecondFormOutlineMeshReferences)
	{
		if (USkeletalMeshComponent* Mesh = ResolveSkeletalMeshReference(Reference))
		{
			ResolvedSecondFormOutlineMeshes.Add(Mesh);
		}
	}

	bSecondFormResolved = true;
}

void UCharacterRenderComponent::SetFormMeshesKeepTickingPose()
{
	// AlwaysTickPose：即使不被渲染（隐藏）也评估动画姿势，但不刷新渲染骨骼——够 LeaderPose 跟随者读取主控姿势，
	// 又不为隐藏网格做多余蒙皮。若实测发现隐藏主控后跟随网格仍冻结，升级为 AlwaysTickPoseAndRefreshBones。
	if (ResolvedTargetSkeletalMesh)
	{
		ResolvedTargetSkeletalMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	}
	for (USkeletalMeshComponent* Mesh : ResolvedSecondFormMeshes)
	{
		if (Mesh)
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		}
	}
}

void UCharacterRenderComponent::SetMeshVisible(USkeletalMeshComponent* Mesh, bool bVisible)
{
	if (Mesh)
	{
		Mesh->SetVisibility(bVisible);
	}
}

void UCharacterRenderComponent::SetMeshListVisible(const TArray<TObjectPtr<USkeletalMeshComponent>>& Meshes, bool bVisible)
{
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh)
		{
			Mesh->SetVisibility(bVisible);
		}
	}
}

void UCharacterRenderComponent::ApplyFormVisibility(float FormBlendValue)
{
	// Actor 初始化顺序不保证控制器在本组件 BeginPlay 之后才调用，这里惰性补解析。
	if (!ResolvedTargetSkeletalMesh)
	{
		ResolvedTargetSkeletalMesh = ResolveSkeletalMeshReference(TargetSkeletalMeshReference);
	}
	if (!ResolvedTargetOutlineMesh)
	{
		ResolvedTargetOutlineMesh = ResolveSkeletalMeshReference(TargetOutlineMeshReference);
	}
	if (!bSecondFormResolved)
	{
		ResolveSecondFormMeshes();
	}

	const bool bAtForm1 = FormBlendValue <= FormEndpointThreshold;
	const bool bAtForm2 = FormBlendValue >= 1.0f - FormEndpointThreshold;

	// 过渡中（两端点之间）两套都显；到端点只留对应一套。
	const bool bForm1Visible = !bAtForm2;
	const bool bForm2Visible = !bAtForm1;

	// 描边可见性 = 当前形态该显描边 AND 画质允许描边（LowSpec 关壳描边时整体隐藏）。
	SetMeshVisible(ResolvedTargetSkeletalMesh, bForm1Visible);
	SetMeshVisible(ResolvedTargetOutlineMesh, bForm1Visible && bOutlineEnabledByQuality);
	SetMeshListVisible(ResolvedSecondFormMeshes, bForm2Visible);
	SetMeshListVisible(ResolvedSecondFormOutlineMeshes, bForm2Visible && bOutlineEnabledByQuality);

	// 过渡中（两端点之间）两套 mesh 并显，切成 Masked 溶解材质；到端点切回稳态原材质(Opaque DMI)。
	SetFormMaskActive(!bAtForm1 && !bAtForm2);
}

void UCharacterRenderComponent::SetFormMaskActive(bool bActive)
{
	// 逐槽比对实际材质，只在不一致时写，避免组件重注册后缓存状态与真实材质脱节。
	for (const FFormMaskSlot& MaskSlot : FormMaskSlots)
	{
		if (!MaskSlot.Mesh)
		{
			continue;
		}
		UMaterialInterface* Target = bActive ? MaskSlot.MaskMaterial.Get() : MaskSlot.SteadyMaterial.Get();
		if (Target && MaskSlot.Mesh->GetMaterial(MaskSlot.SlotIndex) != Target)
		{
			MaskSlot.Mesh->SetMaterial(MaskSlot.SlotIndex, Target);
		}
	}
}

void UCharacterRenderComponent::CacheMaterialSlotsForMesh(USkeletalMeshComponent* MeshComponent, bool bForceOutlinePart)
{
	if (!MeshComponent)
	{
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < MeshComponent->GetNumMaterials(); ++MaterialIndex)
	{
		FSlotMaterialRuntime RuntimeMaterial;
		const FName SlotName = MeshComponent->GetMaterialSlotNames().IsValidIndex(MaterialIndex)
			? MeshComponent->GetMaterialSlotNames()[MaterialIndex]
			: FName(*FString::Printf(TEXT("Material_%d"), MaterialIndex));
		RuntimeMaterial.PartType = bForceOutlinePart
			? ECharacterPartType::OutLine
			: ResolvePartTypeBySlotName(SlotName);

		// 先取槽上的原材质(MI)，它是过渡材质映射表的键；CreateDynamicMaterialInstance 之后槽会变成 DMI。
		UMaterialInterface* OriginalSlotMaterial = MeshComponent->GetMaterial(MaterialIndex);

		RuntimeMaterial.DynamicMaterial = MeshComponent->CreateDynamicMaterialInstance(MaterialIndex);
		if (RuntimeMaterial.DynamicMaterial)
		{
			// 原始参数值改为"首次被某个状态修改时"按需捕获(见 EnsureScalarCaptured/EnsureVectorCaptured)，
			// 不再用固定参数名清单预捕获——这样任何状态改动过的参数都能被正确还原
			RuntimeMaterials.Add(RuntimeMaterial);

			// 若该槽原材质在映射表里配了 Mask 变体，登记 (mesh,槽,稳态DMI,Mask)：过渡期切 Mask，端点切回 DMI（保留运行时参数）。
			if (const TObjectPtr<UMaterialInterface>* MaskMaterial = FormTransitionMaskMaterials.Find(OriginalSlotMaterial))
			{
				if (*MaskMaterial)
				{
					FFormMaskSlot MaskSlot;
					MaskSlot.Mesh = MeshComponent;
					MaskSlot.SlotIndex = MaterialIndex;
					MaskSlot.SteadyMaterial = RuntimeMaterial.DynamicMaterial;
					MaskSlot.MaskMaterial = *MaskMaterial;
					FormMaskSlots.Add(MaskSlot);
				}
			}
		}
	}
}

ECharacterPartType UCharacterRenderComponent::ResolvePartTypeBySlotName(FName SlotName) const
{
	for (const FPartMaterialBinding& Binding : MaterialBindings)
	{
		if (Binding.SlotNames.Contains(SlotName))
		{
			return Binding.PartType;
		}
	}

	return ECharacterPartType::Up;
}

void UCharacterRenderComponent::EnsureScalarCaptured(FSlotMaterialRuntime& RuntimeMaterial, FName ParameterName)
{
	// 已捕获过就不再覆盖，保证记录的是"任何状态修改之前"的原始值
	if (!RuntimeMaterial.DynamicMaterial || RuntimeMaterial.Snapshot.ScalarValues.Contains(ParameterName))
	{
		return;
	}

	FCapturedScalarParameter CapturedValue;
	// 用返回 bool 的接口真实判断材质里是否存在该参数：不存在则 bWasFound=false，还原时会跳过，
	// 避免像旧版那样把材质根本没有的参数误写成 0
	CapturedValue.bWasFound = RuntimeMaterial.DynamicMaterial->GetScalarParameterValue(
		FMaterialParameterInfo(ParameterName), CapturedValue.Value);
	RuntimeMaterial.Snapshot.ScalarValues.Add(ParameterName, CapturedValue);
}

void UCharacterRenderComponent::EnsureVectorCaptured(FSlotMaterialRuntime& RuntimeMaterial, FName ParameterName)
{
	if (!RuntimeMaterial.DynamicMaterial || RuntimeMaterial.Snapshot.VectorValues.Contains(ParameterName))
	{
		return;
	}

	FCapturedVectorParameter CapturedValue;
	CapturedValue.bWasFound = RuntimeMaterial.DynamicMaterial->GetVectorParameterValue(
		FMaterialParameterInfo(ParameterName), CapturedValue.Value);
	RuntimeMaterial.Snapshot.VectorValues.Add(ParameterName, CapturedValue);
}

void UCharacterRenderComponent::ApplyImmediateState(FSlotMaterialRuntime& RuntimeMaterial, const FActiveRenderState& ActiveState, float Weight)
{
	if (!RuntimeMaterial.DynamicMaterial)
	{
		return;
	}

	for (const FRenderScalarParameterValue& ScalarParameter : ActiveState.Payload.ScalarParameters)
	{
		// 先按需捕获原始值(仅第一次)，再在"原始值 ↔ 目标值"之间按权重插值。
		// 旧版是 目标值×Weight，只有原始值≈0 才正确；带 WeightCurve 且原始值非 0 时会先掉到 0 附近再回升。
		EnsureScalarCaptured(RuntimeMaterial, ScalarParameter.ParameterName);
		const FCapturedScalarParameter* Captured = RuntimeMaterial.Snapshot.ScalarValues.Find(ScalarParameter.ParameterName);
		if (!Captured || !Captured->bWasFound)
		{
			continue; // 材质没有该参数就跳过，不制造垃圾参数
		}
		RuntimeMaterial.DynamicMaterial->SetScalarParameterValue(
			ScalarParameter.ParameterName, FMath::Lerp(Captured->Value, ScalarParameter.Value, Weight));
	}

	for (const FRenderVectorParameterValue& VectorParameter : ActiveState.Payload.VectorParameters)
	{
		EnsureVectorCaptured(RuntimeMaterial, VectorParameter.ParameterName);
		const FCapturedVectorParameter* Captured = RuntimeMaterial.Snapshot.VectorValues.Find(VectorParameter.ParameterName);
		if (!Captured || !Captured->bWasFound)
		{
			continue;
		}
		// FLinearColor 显式插值（同控制器：避免 FMath::Lerp 内部 float*FLinearColor 无匹配运算符的编译问题）
		const FLinearColor Blended = Captured->Value * (1.0f - Weight) + VectorParameter.Value * Weight;
		RuntimeMaterial.DynamicMaterial->SetVectorParameterValue(VectorParameter.ParameterName, Blended);
	}
}

void UCharacterRenderComponent::RestoreSnapshot(FSlotMaterialRuntime& RuntimeMaterial, const FRenderStatePayload& Payload)
{
	if (!RuntimeMaterial.DynamicMaterial)
	{
		return;
	}

	for (const FRenderScalarParameterValue& ScalarParameter : Payload.ScalarParameters)
	{
		if (const FCapturedScalarParameter* Captured = RuntimeMaterial.Snapshot.ScalarValues.Find(ScalarParameter.ParameterName))
		{
			if (Captured->bWasFound)
			{
				RuntimeMaterial.DynamicMaterial->SetScalarParameterValue(ScalarParameter.ParameterName, Captured->Value);
			}
		}
	}

	for (const FRenderVectorParameterValue& VectorParameter : Payload.VectorParameters)
	{
		if (const FCapturedVectorParameter* Captured = RuntimeMaterial.Snapshot.VectorValues.Find(VectorParameter.ParameterName))
		{
			if (Captured->bWasFound)
			{
				RuntimeMaterial.DynamicMaterial->SetVectorParameterValue(VectorParameter.ParameterName, Captured->Value);
			}
		}
	}
}

void UCharacterRenderComponent::RefreshRuntimeState(float DeltaTime)
{
	if (ActiveStates.IsEmpty())
	{
		return;
	}

	for (int32 StateIndex = ActiveStates.Num() - 1; StateIndex >= 0; --StateIndex)
	{
		FActiveRenderState& ActiveState = ActiveStates[StateIndex];
		ActiveState.ElapsedTime += DeltaTime;

		const float Duration = FMath::Max(ActiveState.Payload.Duration, KINDA_SMALL_NUMBER);
		// 持续型(bLooping)：时间对 Duration 取模，让 WeightCurve 循环播放(呼吸/脉冲)；普通型：0→1 收敛
		const float NormalizedTime = ActiveState.Payload.bLooping
			? FMath::Fmod(ActiveState.ElapsedTime, Duration) / Duration
			: FMath::Clamp(ActiveState.ElapsedTime / Duration, 0.0f, 1.0f);
		const float Weight = ActiveState.Payload.WeightCurve
			? ActiveState.Payload.WeightCurve->GetFloatValue(NormalizedTime)
			: 1.0f;

		for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
		{
			if (IsTargetPart(RuntimeMaterial, ActiveState.Payload))
			{
				ApplyImmediateState(RuntimeMaterial, ActiveState, Weight);
			}
		}

		// 持续型永不自动结束(靠手动 ClearRenderState)；普通型到时长后还原并移除
		if (!ActiveState.Payload.bLooping && ActiveState.ElapsedTime >= Duration)
		{
			if (ActiveState.Payload.bRestoreOriginalValues)
			{
				for (FSlotMaterialRuntime& RuntimeMaterial : RuntimeMaterials)
				{
					if (IsTargetPart(RuntimeMaterial, ActiveState.Payload))
					{
						RestoreSnapshot(RuntimeMaterial, ActiveState.Payload);
					}
				}
			}

			OnRenderStateFinished.Broadcast(ActiveState.Payload.StateName);
			ActiveStates.RemoveAt(StateIndex);
		}
	}

	// 所有状态跑完后，仅在不需要球心跟踪时关闭 Tick。
	if (ActiveStates.IsEmpty())
	{
		SetComponentTickEnabled(!HairSphereCenterSocketName.IsNone());
	}
}

bool UCharacterRenderComponent::IsTargetPart(const FSlotMaterialRuntime& RuntimeMaterial, const FRenderStatePayload& Payload) const
{
	if (Payload.TargetParts.IsEmpty())
	{
		return true;
	}

	return Payload.TargetParts.Contains(RuntimeMaterial.PartType);
}
