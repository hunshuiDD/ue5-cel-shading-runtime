#include "AssetNamingSettings.h"

namespace
{
FAssetNamingRule MakeRule(const TCHAR* ClassName, const TCHAR* Prefix, const TCHAR* Suffix = TEXT(""))
{
    FAssetNamingRule Rule;
    Rule.AssetClassName = ClassName;
    Rule.Prefix = Prefix;
    Rule.Suffix = Suffix;
    return Rule;
}
}

UAssetNamingSettings::UAssetNamingSettings()
{
	GeneratedSuffixTokens = {TEXT("Inst")};
    IgnoredPathPrefixes = {
        TEXT("/Game/Developers"),
        TEXT("/Game/__ExternalActors__"),
        TEXT("/Game/__ExternalObjects__")
    };

    Rules = {
        MakeRule(TEXT("Material"), TEXT("M")),
        MakeRule(TEXT("MaterialInstanceConstant"), TEXT("MI")),
        MakeRule(TEXT("MaterialFunction"), TEXT("MF")),
        MakeRule(TEXT("MaterialParameterCollection"), TEXT("MPC")),
        MakeRule(TEXT("Texture2D"), TEXT("T")),
        MakeRule(TEXT("StaticMesh"), TEXT("SM")),
        MakeRule(TEXT("SkeletalMesh"), TEXT("SK")),
        MakeRule(TEXT("Blueprint"), TEXT("BP")),
        MakeRule(TEXT("AnimSequence"), TEXT("A")),
        MakeRule(TEXT("Skeleton"), TEXT("SKEL")),
        MakeRule(TEXT("PhysicsAsset"), TEXT("PHYS")),
        MakeRule(TEXT("NiagaraSystem"), TEXT("NS"))
    };
}

FName UAssetNamingSettings::GetCategoryName() const
{
    return TEXT("Plugins");
}
