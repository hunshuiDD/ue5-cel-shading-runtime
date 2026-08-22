// ShadowCaptureComponent.cpp  (UE5.7)
// 依赖模块: Engine（USceneCaptureComponent2D / UKismetMaterialLibrary 都在 Engine 内，
// Build.cs 通常已含 "Engine"，无需新增模块）。

#include "ShadowCaptureComponent.h"

#include "Engine/Engine.h"
#include "Engine/DirectionalLight.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"

namespace
{
	// FVector(double) -> FLinearColor(float)，避免跨版本构造函数歧义。
	FORCEINLINE FLinearColor ToLC(const FVector& V)
	{
		return FLinearColor((float)V.X, (float)V.Y, (float)V.Z);
	}

	// 把一个已有 Actor 的世界包围盒并进 Box（不新建任何几何，只读现有 Actor 的 Bounds）。
	void AddActorBox(FBox& Box, const AActor* A)
	{
		if (!A)
		{
			return;
		}
		FVector Origin, Extent;
		A->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
		Box += FBox(Origin - Extent, Origin + Extent);
	}
}

UShadowCaptureComponent::UShadowCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 动画姿势/包围盒在 PostUpdateWork 前已算好，放这里摆位才拿到当帧姿势。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// 编辑器视口也 Tick：不进 PIE 也能实时跟编辑器相机更新 CameraBlend。
	// 需视口开 Realtime(Ctrl+R) 才会持续刷新；关掉则仅交互时刷新。
	bTickInEditor = true;

	ProjectionType = ECameraProjectionMode::Orthographic;
	// 线性相机空间深度(cm)：绕开 Reversed-Z / NDC 符号坑。这是全套方案的地基。
	CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	bCaptureEveryFrame = false; // 手动 CaptureScene()
	bCaptureOnMovement = false;
	bAlwaysPersistRenderingState = true;
	PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

	ApplyCaptureShowFlags();
}

void UShadowCaptureComponent::ApplyCaptureShowFlags()
{
	// 【本方案最大的一笔性能开销来源】SceneCapture 默认继承一整套 ShowFlags，
	// 即便 CaptureSource=SCS_SceneDepth 只要深度，引擎仍会为这张 RT 完整跑一遍
	// Lumen GI / 反射 / AO / 雾 / 后处理 / 动态阴影，算完再把颜色结果整个丢掉。
	// 这里逐项关掉与深度无关的特性，只保留几何光栅化。
	//
	// 若某一行在你的引擎版本上编译不过（ShowFlag 名跨版本有增删），删掉那一行即可，
	// 各行之间彼此独立，不影响其余优化。
	ShowFlags.SetLighting(false);
	ShowFlags.SetDynamicShadows(false);
	ShowFlags.SetGlobalIllumination(false);
	ShowFlags.SetLumenGlobalIllumination(false);
	ShowFlags.SetLumenReflections(false);
	ShowFlags.SetReflectionEnvironment(false);
	ShowFlags.SetScreenSpaceReflections(false);
	ShowFlags.SetAmbientOcclusion(false);
	ShowFlags.SetDistanceFieldAO(false);
	ShowFlags.SetSkyLighting(false);
	ShowFlags.SetAtmosphere(false);
	ShowFlags.SetFog(false);
	ShowFlags.SetVolumetricFog(false);
	ShowFlags.SetPostProcessing(false);
	ShowFlags.SetBloom(false);
	ShowFlags.SetLensFlares(false);
	ShowFlags.SetEyeAdaptation(false);
	ShowFlags.SetToneCurve(false);
	ShowFlags.SetDepthOfField(false);
	ShowFlags.SetMotionBlur(false);
	// 抗锯齿/TAA 对深度捕获只有害处：TAA 会在深度上引入时域抖动，
	// 正是之前刘海 dither 闪烁那一类问题的放大器。
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	// 半透明本来就不写深度；粒子/贴花/植被在 ShowOnlyList 之外，这里是双保险。
	ShowFlags.SetTranslucency(false);
	ShowFlags.SetSeparateTranslucency(false);
	ShowFlags.SetParticles(false);
	ShowFlags.SetDecals(false);
	ShowFlags.SetLandscape(false);
	ShowFlags.SetInstancedFoliage(false);
}

AActor* UShadowCaptureComponent::ResolveTargetActor() const
{
	return TargetActor ? TargetActor.Get() : GetOwner();
}

bool UShadowCaptureComponent::GetHeadCenter(AActor* Target, FVector& OutCenter) const
{
	if (!Target || HeadSocketName == NAME_None)
	{
		return false;
	}

	// 不能用 FindComponentByClass：角色身上有多个骨骼网格(本体/描边/第二形态)，
	// 它返回哪一个不保证顺序。抓到隐藏的第二形态 mesh 时骨骼变换未评估，
	// DoesSocketExist 仍为 true(骨架相同) 但 GetSocketLocation 给出 NaN。
	TArray<USkeletalMeshComponent*> Skels;
	Target->GetComponents<USkeletalMeshComponent>(Skels);

	for (USkeletalMeshComponent* Skel : Skels)
	{
		if (!Skel)
		{
			continue;
		}
		// 指定了组件名就只认这一个，不做可见性筛选（美术可能有意用隐藏的代理骨架）。
		if (HeadMeshComponentName != NAME_None)
		{
			if (Skel->GetFName() != HeadMeshComponentName)
			{
				continue;
			}
		}
		else if (!Skel->IsVisible())
		{
			// 自动模式下跳过隐藏组件——它们是 NaN 的主要来源。
			continue;
		}

		if (!Skel->DoesSocketExist(HeadSocketName))
		{
			continue;
		}

		const FVector Loc = Skel->GetSocketLocation(HeadSocketName);
		if (Loc.ContainsNaN())
		{
			// 宁可回退整只包围盒(阴影变糊)，也绝不把 NaN 交出去污染相机变换。
			continue;
		}

		OutCenter = Loc;
		return true;
	}

	return false;
}

bool UShadowCaptureComponent::GetCameraForward(FVector& OutFwd) const
{
	// 优先用美术指定的相机 Actor（展示常用 CineCamera / CameraActor，编辑器里也能用）。
	if (AActor* Cam = ViewCamera)
	{
		// 有 CameraComponent 就用它的朝向（相机组件可能相对 Actor 有旋转）；否则用 Actor 前向。
		if (UCameraComponent* CamComp = Cam->FindComponentByClass<UCameraComponent>())
		{
			OutFwd = CamComp->GetForwardVector();
		}
		else
		{
			OutFwd = Cam->GetActorForwardVector();
		}
		return true;
	}
	// 运行时玩家相机（PIE / 打包）。
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
			{
				OutFwd = CamMgr->GetCameraRotation().Vector();
				return true;
			}
		}
	}

	return false;
}

void UShadowCaptureComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshShowOnlyList();
}

void UShadowCaptureComponent::RefreshShowOnlyList()
{
	// 白名单只放【场景里已有的】角色 Actor + 指定挂件，直接渲染它们，不复制、不新建。
	ShowOnlyActors.Reset();
	if (AActor* Target = ResolveTargetActor())
	{
		ShowOnlyActors.Add(Target);
	}
	for (AActor* A : AdditionalCasters)
	{
		if (A)
		{
			ShowOnlyActors.Add(A);
		}
	}
}

void UShadowCaptureComponent::CaptureNow()
{
	// 编辑器按钮：编辑器里 BeginPlay 没跑过，先补白名单再摆位捕获。
	RefreshShowOnlyList();
	UpdateCaptureAndPush(/*bForce=*/true);
}

void UShadowCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateCaptureAndPush(/*bForce=*/false);
}

void UShadowCaptureComponent::UpdateCaptureAndPush(bool bForce)
{
	UWorld* W = GetWorld();

	// 开关无论是否启用都推给阴影 MPC，材质据此走"阴影/直通"。
	// 关闭时也要推 ShadowEnable=0，否则材质会停留在上一帧的启用状态。
	if (ShadowMPC)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(W, ShadowMPC, TEXT("ShadowEnable"), bEnablePerObjectShadow ? 1.f : 0.f);
	}

	if (CharacterRunningMPC)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(W, CharacterRunningMPC, TEXT("ShadowDebugMode"), static_cast<float>(ShadowDebugMode));
	}

	// 关闭：跳过整套采集，直接返回，省掉整张 RT 渲染。
	if (!bEnablePerObjectShadow)
	{
		return;
	}

	// 采集降频。摆位 / CaptureScene / 推 MPC 三者必须原子地同帧完成，
	// 否则材质会拿着新的相机基去采上一次的 RT，阴影整体错位。所以跳帧时整套一起跳，
	// 不做"只推 MPC 不采集"的部分更新。
	if (!bForce && CaptureEveryNFrames > 1)
	{
		if (++FrameCounter < CaptureEveryNFrames)
		{
			return;
		}
		FrameCounter = 0;
	}

	// 每次都按当前 TargetActor 刷新白名单：这样即使 TargetActor 在 BeginPlay 之后才被
	// 控制器喂进来，下一帧也能自动跟上，不会停留在旧的 Owner。
	RefreshShowOnlyList();

	AActor* Target = ResolveTargetActor();
	if (!Target)
	{
		return;
	}

	// 1) 采集框中心 + 半径。
	//    默认只框头部(脸+贴脸头发)：正交框缩到头，RT 分辨率集中给脸，阴影更锐。
	//    找不到头骨则回退整只角色包围盒(旧行为)。注意：框的是"投影者"，身体不投脸可裁掉。
	FVector Center0;
	float Radius;
	FVector HeadCenter = FVector::ZeroVector; // 初始化以消除 C4701（bHeadFramed 为 false 时本就不使用）
	const bool bHeadFramed = bFrameOnHead && GetHeadCenter(Target, HeadCenter);
	if (bHeadFramed)
	{
		Center0 = HeadCenter;
		Radius = HeadRadius;
	}
	else
	{
		FBox Box(ForceInit);
		AddActorBox(Box, Target);
		for (AActor* A : AdditionalCasters)
		{
			AddActorBox(Box, A);
		}
		if (!Box.IsValid)
		{
			return;
		}
		Center0 = Box.GetCenter();
		Radius = Box.GetExtent().Size(); // 半径=包围盒对角一半，任意光角都装得下
	}
	if (Radius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 2) 光的传播方向（不是指向光源）。方向光的 ForwardVector 即光线方向。
	FVector LightFwd = ManualLightDir.GetSafeNormal();
	if (DirectionalLight)
	{
		LightFwd = DirectionalLight->GetActorForwardVector();
	}
	if (LightFwd.IsNearlyZero())
	{
		LightFwd = FVector(0.f, 0.f, -1.f);
	}

	// 2.5) 混合光向：把光的传播方向朝相机朝向 lerp，脸部阴影随镜头更可控。
	//      0=纯光向；值越大越正面(阴影退到边缘)；=1 时基本正面光、脸几乎无影。
	//      只影响这次采集的方向，不改场景光。编辑器无相机时自动跳过。
	if (CameraBlend > KINDA_SMALL_NUMBER)
	{
		FVector CamFwd;
		if (GetCameraForward(CamFwd))
		{
			LightFwd = FMath::Lerp(LightFwd, CamFwd, CameraBlend).GetSafeNormal();
			if (LightFwd.IsNearlyZero())
			{
				LightFwd = FVector(0.f, 0.f, -1.f);
			}
		}
	}

	// 3) 正交宽度 = 包围球直径 * 余量。
	const float OrthoW = Radius * 2.0f * Padding;

	// 4) 先定朝向，再从组件读回真实基向量（与实际投影矩阵严格一致，避免自算基有 roll 偏差）。
	SetWorldRotation(LightFwd.Rotation());
	const FVector CFwd = GetForwardVector();
	const FVector CRight = GetRightVector();
	const FVector CUp = GetUpVector();

	// 5) 分辨率取自 RT。
	float Reso = 2048.f;
	if (TextureTarget && TextureTarget->SizeX > 0)
	{
		Reso = (float)TextureTarget->SizeX;
	}

	// 6) texel snap：把中心量化到光源相机平面网格，消抖动。
	FVector Center = Center0;
	if (bSnapToTexel && Reso > 0.f)
	{
		const float Texel = OrthoW / Reso;
		float x = FVector::DotProduct(Center, CRight);
		float y = FVector::DotProduct(Center, CUp);
		const float z = FVector::DotProduct(Center, CFwd);
		x = FMath::GridSnap(x, Texel);
		y = FMath::GridSnap(y, Texel);
		Center = CRight * x + CUp * y + CFwd * z;
	}

	// 7) 相机拉到光源侧（拉开量 > 半径即可，SceneDepth 线性、不必精卡 near/far）。
	const FVector CamPos = Center - CFwd * (Radius * 2.0f);

	// NaN 总闸：CamPos 一旦为 NaN 写进 SetWorldLocation，采集相机的世界变换就永久失效，
	// 投影矩阵作废、RT 内容无意义，而且从画面上只表现为"阴影没了"，极难定位。
	// 这里宁可整帧跳过（保留上一帧有效的 RT），也不写入无效变换。
	if (CamPos.ContainsNaN() || Center.ContainsNaN())
	{
		if (bDrawDebug && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1007, 5.f, FColor::Red,
				TEXT("[ShadowCapture] CamPos/Center = NaN，本帧跳过。检查 HeadSocketName 与 HeadMeshComponentName。"));
		}
		return;
	}

	OrthoWidth = OrthoW;
	SetWorldLocation(CamPos);

	// 诊断开关：忽略白名单捕获整个场景，用来分清"目标/白名单问题"还是"根本没渲染角色"。
	PrimitiveRenderMode = bCaptureEverything
		? ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives
		: ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

	// 8) 捕获。
	CaptureScene();

	// 8.5) 调试：把关键数值打屏，判断采集框是否歪/过大。
	if (bDrawDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 5.f, FColor::Yellow,
			FString::Printf(TEXT("[ShadowCapture] Radius=%.1f OrthoWidth=%.1f ShowOnly=%d"),
				Radius, OrthoW, ShowOnlyActors.Num()));
		GEngine->AddOnScreenDebugMessage(1002, 5.f, FColor::Cyan,
			FString::Printf(TEXT("[ShadowCapture] Center=%s CamPos=%s"),
				*Center.ToCompactString(), *CamPos.ToCompactString()));
		GEngine->AddOnScreenDebugMessage(1003, 5.f, FColor::Green,
			FString::Printf(TEXT("[ShadowCapture] Target=%s Fwd=%s"),
				*GetNameSafe(Target), *CFwd.ToCompactString()));
		// 框定模式：Head=用到 HeadRadius；Bounds=没找到头骨已回退整只包围盒(改 HeadRadius 无效)。
		GEngine->AddOnScreenDebugMessage(1004, 5.f, bHeadFramed ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("[ShadowCapture] FrameMode=%s Socket=%s"),
				bHeadFramed ? TEXT("Head") : TEXT("Bounds(fallback)"), *HeadSocketName.ToString()));

		// 配置体检：换场景后最常见的失效原因是这几个引用没跟着新场景重新指定，
		// 或 RT 格式不是 R32f（8bit RT 会把 cm 深度截断成全黑 → mask 恒为 0 / 恒为 1）。
		const bool bRTOk = TextureTarget && TextureTarget->RenderTargetFormat == RTF_R32f;
		GEngine->AddOnScreenDebugMessage(1005, 5.f, bRTOk ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("[ShadowCapture] RT=%s %dx%d Fmt=%s"),
				*GetNameSafe(TextureTarget),
				TextureTarget ? TextureTarget->SizeX : 0,
				TextureTarget ? TextureTarget->SizeY : 0,
				bRTOk ? TEXT("R32f(OK)") : TEXT("NOT R32f !!")));

		const bool bRefOk = (ShadowMPC != nullptr) && (DirectionalLight != nullptr);
		GEngine->AddOnScreenDebugMessage(1006, 5.f, bRefOk ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("[ShadowCapture] MPC=%s RunMPC=%s DirLight=%s Owner=%s"),
				*GetNameSafe(ShadowMPC), *GetNameSafe(CharacterRunningMPC),
				*GetNameSafe(DirectionalLight), *GetNameSafe(GetOwner())));
	}

	// 9) 推 MPC：材质侧全部从这里读，不再逐材质实例喂参数。（W 已在函数开头取得）
	if (ShadowMPC)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(W, ShadowMPC, TEXT("ShadowCamPos"), ToLC(CamPos));
		UKismetMaterialLibrary::SetVectorParameterValue(W, ShadowMPC, TEXT("ShadowRight"), ToLC(CRight));
		UKismetMaterialLibrary::SetVectorParameterValue(W, ShadowMPC, TEXT("ShadowUp"), ToLC(CUp));
		UKismetMaterialLibrary::SetVectorParameterValue(W, ShadowMPC, TEXT("ShadowFwd"), ToLC(CFwd));
		// 指向光源方向 = -光传播方向，供材质 NdotL 直接用，免去材质里再取反。
		UKismetMaterialLibrary::SetVectorParameterValue(W, ShadowMPC, TEXT("ShadowLightDir"), ToLC(-CFwd));
		UKismetMaterialLibrary::SetScalarParameterValue(W, ShadowMPC, TEXT("ShadowOrthoWidth"), OrthoW);
		UKismetMaterialLibrary::SetScalarParameterValue(W, ShadowMPC, TEXT("ShadowResolution"), Reso);
	}
}
