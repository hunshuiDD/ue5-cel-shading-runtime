// ShadowCaptureComponent.h
// 角色自阴影采集组件 (UE5.7)。每帧从主光方向对【场景中已有的角色 Actor】做一次
// 正交 SceneDepth 捕获，并把相机基/OrthoWidth 推给 MPC 供材质采样。
//
// 不创建任何新 mesh / 代理 / 复制体：通过 ShowOnlyActors 直接渲染你在场景里已有的
// 那个角色 Actor。组件可直接加在角色 Actor 上（TargetActor 留空即用 Owner），
// 也可以放在任意 Actor 上再用 TargetActor 指向场景里的角色。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ShadowCaptureComponent.generated.h"

class UMaterialParameterCollection;
class ADirectionalLight;

UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class RENDERCONTROLLEARN_API UShadowCaptureComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UShadowCaptureComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 编辑器 Details 面板里的按钮：不进 PIE 也能立即摆位+捕获+推 MPC。
	// 用途：编辑器里调完光/参数点一下，直接在视口看效果（编辑器默认不跑组件 Tick）。
	UFUNCTION(CallInEditor, Category = "PerObjectShadow")
	void CaptureNow();

	// 总开关：关闭时跳过采集（连 RT 都不渲染），并推 ShadowEnable=0 让材质直通受光。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	bool bEnablePerObjectShadow = true;

	// 要投阴影的角色 Actor —— 直接引用场景中已有的 Actor。留空则用本组件的 Owner。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	TObjectPtr<AActor> TargetActor = nullptr;

	// 额外参与投影的已有 Actor（头发/武器等独立 Actor）。会加进 ShowOnlyActors 并并入包围盒。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	TArray<TObjectPtr<AActor>> AdditionalCasters;

	// 主方向光。留空则用 ManualLightDir。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	TObjectPtr<ADirectionalLight> DirectionalLight = nullptr;

	// 没接方向光时使用的固定光方向（世界空间，光的“传播方向”，不是指向光源）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	FVector ManualLightDir = FVector(1.f, 0.f, -1.f);

	// 光向混相机：0=纯平行光(和环境一致)，1=纯相机朝向(脸基本正面无影)。
	// 只改自阴影的采集方向，不动场景光。脸部风格化常用低值(约 0.2~0.4)。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraBlend = 0.f;

	// CameraBlend 用哪台相机。留空则仅在运行时使用玩家相机；编辑器视口不再参与，非 PIE 时回退纯光向。
	// 展示/过场用 CineCameraActor 或场景里的相机 Actor 时，把它拖到这里。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	TObjectPtr<AActor> ViewCamera = nullptr;

	// 推阴影采集参数用的 MPC，必须与材质里引用的同一个。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	TObjectPtr<UMaterialParameterCollection> ShadowMPC = nullptr;

	// 运行态 MPC，只承载 ShadowDebugMode；与阴影采集/角色光照参数分离。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	TObjectPtr<UMaterialParameterCollection> CharacterRunningMPC = nullptr;

	// 包围球外扩余量，1.1 表示留 10% 边。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow", meta = (ClampMin = "1.0"))
	float Padding = 1.1f;

	// 采集框只框头部(脸+贴脸头发)，而不是整只角色包围盒：正交框缩到头 →
	// 同一张 RT 分辨率几乎全给脸，阴影更锐。找不到头骨时自动回退整只包围盒。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	bool bFrameOnHead = true;

	// 头骨/插槽名，用来定位采集中心。本工程 Daniya 骨架是 3ds Max Biped 命名 → "Bip001Head"。
	// 换角色时改成对应头骨名（在 SkeletalMesh 的 Skeleton Tree 里看）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow", meta = (EditCondition = "bFrameOnHead"))
	FName HeadSocketName = TEXT("Bip001Head");

	// 从哪个 SkeletalMeshComponent 上取头骨。留空 = 自动挑第一个"可见且含该骨骼"的组件。
	// 【为什么需要这个】角色身上有多个骨骼网格(本体/描边/第二形态)，旧代码用
	// FindComponentByClass 取"第一个"，而这个顺序是不保证的。一旦抓到隐藏的第二形态
	// mesh，其骨骼变换在编辑器下可能从未被评估过：DoesSocketExist 仍为 true(骨架相同)，
	// 但 GetSocketLocation 返回 NaN，NaN 写进采集相机的世界变换后投影矩阵整个失效，
	// RT 内容作废——表现就是"换个关卡 shadowmask 就没了"。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow", meta = (EditCondition = "bFrameOnHead"))
	FName HeadMeshComponentName = NAME_None;

	// 头部采集半径(cm)，需覆盖头+贴脸投影的头发。太小会把投脸的发丝裁出框。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow", meta = (EditCondition = "bFrameOnHead", ClampMin = "1.0"))
	float HeadRadius = 30.f;

	// 相机位置量化到纹素网格，消动画抖动导致的阴影闪烁。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow")
	bool bSnapToTexel = true;

	// ---- 性能 ----

	// 每 N 帧才真正采集一次（1=每帧）。脸部阴影对 1~2 帧延迟不可见，2~3 能直接省掉
	// 对应比例的 SceneCapture 开销。注意：摆位/推 MPC 与采集必须同帧发生，否则材质会
	// 用新的相机基去采旧 RT 导致阴影错位，所以跳帧时整套流程一起跳过。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow|Performance", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CaptureEveryNFrames = 4;

	// 打开后把 Radius/OrthoWidth/Center/CamPos/ShowOnly 数量打到屏幕，用来排查采集框歪/过大。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow|Debug")
	bool bDrawDebug = false;

	// 材质可视化调试模式，推给 MPC 的 ShadowDebugMode 供 MF_PerShadow 读取。
	// 0=正常 mask  1=UV  2=RT采样深度  3=接收点深度  4=N·L  5=阴影红叠加  6=采样框内外
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow|Debug", meta = (ClampMin = "0", ClampMax = "6"))
	int32 ShadowDebugMode = 0;

	// 【诊断用】忽略白名单，捕获整个场景。角色若此时出现在 RT 里 = 目标/白名单问题；
	// 仍是空 = SceneCapture 本身没渲染角色（更底层）。查完记得关掉。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PerObjectShadow|Debug")
	bool bCaptureEverything = false;

private:
	// 返回目标 Actor（TargetActor 优先，否则 Owner）。
	AActor* ResolveTargetActor() const;

	// 取头骨/插槽世界位置作为采集中心（bFrameOnHead 用）。找不到返回 false。
	bool GetHeadCenter(AActor* Target, FVector& OutCenter) const;

	// 取显式 ViewCamera 或运行时玩家相机朝向（CameraBlend 用）；不读取编辑器视口相机。
	bool GetCameraForward(FVector& OutFwd) const;

	// 用目标 Actor + 挂件刷新白名单（BeginPlay 和编辑器按钮都用）。
	void RefreshShowOnlyList();

	// 摆位 -> 捕获 -> 推 MPC 的一帧完整流程。bForce=true 时无视 CaptureEveryNFrames
	// 立即执行（编辑器 CaptureNow 按钮用）。
	void UpdateCaptureAndPush(bool bForce);

	// 关掉与"只取深度"无关的所有渲染特性。SceneCapture 默认带完整 ShowFlags，
	// 即使 CaptureSource=SceneDepth 也会照跑 Lumen/反射/雾/后处理再把结果丢掉。
	void ApplyCaptureShowFlags();

	// 采集跳帧计数。
	int32 FrameCounter = 0;
};
