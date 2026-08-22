#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "RenderTypes.generated.h"

UENUM(BlueprintType)
enum class ECharacterPartType : uint8
{
	Bang,
	Face,
	Hair,
	Eye,
	Up,
	OutLine,
	Down,
	Cloth
};

UENUM(BlueprintType)
enum class ERenderQualityTier : uint8
{
	HighPC,
	MidPC,
	LowSpec
};

UENUM(BlueprintType)
enum class ERenderStateType : uint8
{
	HitFlash,
	RimBoost,
	OutlineBoost,
	BattleDamage,
	Custom
};

UENUM(BlueprintType)
enum class EMaterialDebugViewMode : uint8
{
	Final = 0,
	BaseColor = 1,
	WorldNormal = 2,
	DirectLighting = 3,
	FaceSDF = 4,
	HairTangent = 5,
	RegionID = 6,
	FurSignal = 7,
	PerShadow = 8,
	LightWeights = 9
};

USTRUCT(BlueprintType)
struct FRenderScalarParameterValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	FName ParameterName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct FRenderVectorParameterValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	FName ParameterName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	FLinearColor Value = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FPartMaterialBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	ECharacterPartType PartType = ECharacterPartType::Up;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	TArray<FName> SlotNames;
};

USTRUCT(BlueprintType)
struct FRenderStatePayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	FName StateName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	ERenderStateType StateType = ERenderStateType::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	TArray<ECharacterPartType> TargetParts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	TArray<FRenderScalarParameterValue> ScalarParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	TArray<FRenderVectorParameterValue> VectorParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	TObjectPtr<UCurveFloat> WeightCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	float Duration = 0.25f;

	// true = 持续型状态：不按 Duration 自动结束，一直生效到手动 ClearRenderState（替代旧写法里 Duration=9999 的魔数）。
	// 若配了 WeightCurve，曲线会在 Duration 时长内循环播放（可做呼吸/脉冲效果）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	bool bLooping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
	bool bRestoreOriginalValues = true;
};

UCLASS(BlueprintType)
class RENDERCONTROLLEARN_API URenderStateDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render")
	FRenderStatePayload StatePayload;
};

USTRUCT(BlueprintType)
// 分档字段的去留判据：一个字段只有在“关掉它 GPU 真的少干活”时才留下。
// MPC/MID 标量在材质里只能接动态分支（两条分支都会被编译进 shader），所以布尔型开关
// 靠动态标量降级并不省 shader 指令；需要真正省指令的静态分支走
// r.MaterialQualityLevel + 材质 Quality Switch，由 ARenderDemoController::ApplyQualitySettingsInternal 按档下发。
// 2026-07-27 已删除 5 个无消费者字段（PP 描边/场景 PP/折射/半透 SSS/二形态 FX）：
// 前两者三档无区分，后三者对应材质尚未落地；材质落地时再加回，避免“看起来接了其实空转”。
struct FRenderQualitySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	ERenderQualityTier QualityTier = ERenderQualityTier::HighPC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bUseShellOutline = true;

	// 屏幕空间内描边（M_InnerLine，Post Process domain，Sobel 3x3 = 每像素 8 次 SceneTexture 采样）。
	// 成本是**全屏固定开销**，与角色屏占比无关：角色占屏 30% 时另外 70% 的像素也在跑边缘检测。
	// 关闭时由控制器从 PostProcessVolume 的 Blendable 数组里移除该材质（不是把 Weight 设 0——
	// Weight 只影响结果混合，pass 仍会执行，省不掉带宽）。
	// 采样数 18→10 的降级靠材质里的 Quality Switch + r.MaterialQualityLevel，不走标量（PP 材质里
	// 采样通常是展开写死的，动态标量改不动），见 ARenderDemoController::bDriveMaterialQualityLevel。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bEnableInnerLine = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bEnableSSR = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bEnableGTAO = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	float FaceShadowSoftness = 1.0f;

	// 视差毛 POM 采样数：由 MCP_CharaterRuning.FurParallaxSampleCount 下发。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	int32 FurParallaxSampleCount = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	TArray<FRenderScalarParameterValue> ScalarOverrides;
};

UCLASS(BlueprintType)
class RENDERCONTROLLEARN_API URenderQualityDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quality")
	FRenderQualitySettings QualitySettings;
};
