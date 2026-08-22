#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssetNamingSettings.generated.h"

USTRUCT(BlueprintType)
struct ASSETNAMINGTOOL_API FAssetNamingRule
{
    GENERATED_BODY()

    /** Asset Registry class name, for example Material or Texture2D. */
    UPROPERTY(EditAnywhere, Config, Category = "Rule")
    FName AssetClassName;

    UPROPERTY(EditAnywhere, Config, Category = "Rule")
    FString Prefix;

    UPROPERTY(EditAnywhere, Config, Category = "Rule")
    FString Suffix;
};

UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "Asset Naming Tool"))
class ASSETNAMINGTOOL_API UAssetNamingSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UAssetNamingSettings();

    /** Enables naming for assets created or manually renamed after editor startup. */
    UPROPERTY(EditAnywhere, Config, Category = "General")
    bool bEnableAutoRename = true;

    /** Supported tokens: {Prefix}, {Name}, {Suffix}, {Class}. Must contain {Name}. */
    UPROPERTY(EditAnywhere, Config, Category = "General")
    FString NamePattern = TEXT("{Prefix}_{Name}{Suffix}");

    /** Assets below these virtual paths are ignored. */
    UPROPERTY(EditAnywhere, Config, Category = "General")
    TArray<FString> IgnoredPathPrefixes;

    /** The first exact AssetClassName match is used. */
    UPROPERTY(EditAnywhere, Config, Category = "Rules")
	TArray<FAssetNamingRule> Rules;

	/**
	 * Underscore-delimited suffix tokens added by asset creation workflows.
	 * These tokens are removed before the configured naming pattern is applied.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Rules")
	TArray<FString> GeneratedSuffixTokens;

    virtual FName GetCategoryName() const override;
};
