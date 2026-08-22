#include "AssetNamingToolModule.h"

#include "AssetNamingSettings.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Containers/Ticker.h"
#include "Framework/Commands/UIAction.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetNamingTool, Log, All);

#define LOCTEXT_NAMESPACE "FAssetNamingToolModule"

namespace
{
const FName AssetNamingMessageLogName(TEXT("AssetNamingTool"));

bool IsAssetInScope(const FAssetData& AssetData, const UAssetNamingSettings& Settings)
{
    const FString PackagePath = AssetData.PackagePath.ToString();
    if (!PackagePath.StartsWith(TEXT("/Game")))
    {
        return false;
    }

    for (const FString& IgnoredPrefix : Settings.IgnoredPathPrefixes)
    {
        if (!IgnoredPrefix.IsEmpty() && PackagePath.StartsWith(IgnoredPrefix))
        {
            return false;
        }
    }

    return true;
}
}

void FAssetNamingToolModule::StartupModule()
{
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    AssetAddedHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetNamingToolModule::OnAssetAdded);
    AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FAssetNamingToolModule::OnAssetRenamed);

    if (AssetRegistry.IsLoadingAssets())
    {
        FilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddRaw(this, &FAssetNamingToolModule::OnFilesLoaded);
    }
    else
    {
        bInitialScanComplete = true;
    }

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FAssetNamingToolModule::ProcessPendingAssets));

    FMessageLogModule& MessageLogModule =
        FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
    if (!MessageLogModule.IsRegisteredLogListing(AssetNamingMessageLogName))
    {
        MessageLogModule.RegisterLogListing(
            AssetNamingMessageLogName,
            LOCTEXT("AssetNamingMessageLogLabel", "Asset Naming Tool"));
    }

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssetNamingToolModule::RegisterMenus));
}

void FAssetNamingToolModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
    {
        IAssetRegistry& AssetRegistry =
            FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        AssetRegistry.OnAssetAdded().Remove(AssetAddedHandle);
        AssetRegistry.OnAssetRenamed().Remove(AssetRenamedHandle);
        AssetRegistry.OnFilesLoaded().Remove(FilesLoadedHandle);
    }

    if (FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
    {
        FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog"))
            .UnregisterLogListing(AssetNamingMessageLogName);
    }

    PendingAssets.Reset();
}

void FAssetNamingToolModule::OnFilesLoaded()
{
    bInitialScanComplete = true;
    PendingAssets.Reset();
}

void FAssetNamingToolModule::OnAssetAdded(const FAssetData& AssetData)
{
    QueueAsset(AssetData);
}

void FAssetNamingToolModule::OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
    QueueAsset(AssetData);
}

void FAssetNamingToolModule::QueueAsset(const FAssetData& AssetData)
{
    if (bInitialScanComplete && !bIsRenaming)
    {
        PendingAssets.Add(AssetData);
    }
}

bool FAssetNamingToolModule::ProcessPendingAssets(float DeltaTime)
{
    if (PendingAssets.IsEmpty())
    {
        return true;
    }

    TArray<FAssetData> AssetsToProcess = MoveTemp(PendingAssets);
    PendingAssets.Reset();

    for (const FAssetData& AssetData : AssetsToProcess)
    {
        ProcessAsset(AssetData);
    }

    return true;
}

void FAssetNamingToolModule::ProcessAsset(const FAssetData& AssetData)
{
    const UAssetNamingSettings* Settings = GetDefault<UAssetNamingSettings>();
    if (!Settings || !Settings->bEnableAutoRename)
    {
        return;
    }

    if (!IsAssetInScope(AssetData, *Settings))
    {
        return;
    }

    const FString DesiredName = BuildDesiredName(AssetData);
    if (DesiredName.IsEmpty() || DesiredName == AssetData.AssetName.ToString())
    {
        return;
    }

    UObject* Asset = AssetData.GetAsset();
    if (!IsValid(Asset))
    {
        return;
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const FString PackagePath = AssetData.PackagePath.ToString();
    FString UniquePackageName;
    FString UniqueAssetName;
    AssetTools.CreateUniqueAssetName(
        PackagePath / DesiredName,
        TEXT(""),
        UniquePackageName,
        UniqueAssetName);

    TArray<FAssetRenameData> RenameData;
    RenameData.Emplace(Asset, FPackageName::GetLongPackagePath(UniquePackageName), UniqueAssetName);

    TGuardValue<bool> RenameGuard(bIsRenaming, true);
    if (!AssetTools.RenameAssets(RenameData))
    {
        UE_LOG(
            LogAssetNamingTool,
            Warning,
            TEXT("Failed to rename %s to %s"),
            *AssetData.GetObjectPathString(),
            *UniqueAssetName);
    }
}

void FAssetNamingToolModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("AssetNamingTool"));
    Section.AddSubMenu(
        TEXT("AssetNamingTool"),
        LOCTEXT("AssetNamingToolMenu", "Asset Naming Tool"),
        LOCTEXT("AssetNamingToolMenuTooltip", "Check project asset names against the configured rules."),
        FNewToolMenuDelegate::CreateRaw(this, &FAssetNamingToolModule::PopulateToolsMenu));
}

void FAssetNamingToolModule::PopulateToolsMenu(UToolMenu* Menu)
{
    FToolMenuSection& Section = Menu->AddSection(TEXT("AssetNamingAudit"));
    Section.AddMenuEntry(
        TEXT("CheckAllAssetNames"),
        LOCTEXT("CheckAllAssetNames", "Check All Asset Names"),
        LOCTEXT("CheckAllAssetNamesTooltip", "Scan existing /Game assets and report names that do not match the current rules."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FAssetNamingToolModule::CheckAllAssetNames)));
}

void FAssetNamingToolModule::CheckAllAssetNames()
{
    const UAssetNamingSettings* Settings = GetDefault<UAssetNamingSettings>();
    FMessageLog Report(AssetNamingMessageLogName);
    Report.NewPage(LOCTEXT("AssetNamingAuditPage", "Asset naming audit"));

    if (!Settings || !Settings->NamePattern.Contains(TEXT("{Name}")))
    {
        Report.Error(LOCTEXT("InvalidNamePattern", "Name Pattern must contain {Name}."));
        Report.Open(EMessageSeverity::Info, true);
        return;
    }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(TEXT("/Game")));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistryModule.Get().GetAssets(Filter, Assets);
    Assets.Sort(
        [](const FAssetData& Left, const FAssetData& Right)
        {
            return Left.PackageName.ToString() < Right.PackageName.ToString();
        });

    int32 InScopeCount = 0;
    int32 ConfiguredCount = 0;
    int32 CompliantCount = 0;
    int32 IssueCount = 0;

    for (const FAssetData& AssetData : Assets)
    {
        if (!IsAssetInScope(AssetData, *Settings))
        {
            continue;
        }

        ++InScopeCount;
        const FString DesiredName = BuildDesiredName(AssetData);
        if (DesiredName.IsEmpty())
        {
            continue;
        }

        ++ConfiguredCount;
        if (DesiredName == AssetData.AssetName.ToString())
        {
            ++CompliantCount;
            continue;
        }

        ++IssueCount;
        TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Warning);
        Message->AddToken(FAssetNameToken::Create(
            AssetData.GetObjectPathString(),
            FText::FromName(AssetData.AssetName)));
        Message->AddToken(FTextToken::Create(FText::Format(
            LOCTEXT("AssetNamingSuggestion", " should be named {0}"),
            FText::FromString(DesiredName))));
        Report.AddMessage(Message);
    }

    Report.Info(FText::Format(
        LOCTEXT(
            "AssetNamingAuditSummary",
            "Scanned {0} assets; {1} use configured rules, {2} are compliant, and {3} need attention. {4} assets have no rule."),
        FText::AsNumber(InScopeCount),
        FText::AsNumber(ConfiguredCount),
        FText::AsNumber(CompliantCount),
        FText::AsNumber(IssueCount),
        FText::AsNumber(InScopeCount - ConfiguredCount)));

    Report.Notify(
        FText::Format(
            LOCTEXT("AssetNamingAuditNotification", "Asset naming check finished: {0} issue(s)."),
            FText::AsNumber(IssueCount)),
        IssueCount > 0 ? EMessageSeverity::Warning : EMessageSeverity::Info,
        true);
    Report.Open(EMessageSeverity::Info, true);
}

FString FAssetNamingToolModule::BuildDesiredName(const FAssetData& AssetData) const
{
    const UAssetNamingSettings* Settings = GetDefault<UAssetNamingSettings>();
    if (!Settings || !Settings->NamePattern.Contains(TEXT("{Name}")))
    {
        UE_LOG(LogAssetNamingTool, Warning, TEXT("NamePattern must contain {Name}."));
        return FString();
    }

    const FString ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
    const FAssetNamingRule* MatchingRule = Settings->Rules.FindByPredicate(
        [&ClassName](const FAssetNamingRule& Rule)
        {
            return Rule.AssetClassName == FName(*ClassName);
        });

    if (!MatchingRule)
    {
        return FString();
    }

    FString ResolvedPattern = Settings->NamePattern;
    ResolvedPattern.ReplaceInline(TEXT("{Prefix}"), *MatchingRule->Prefix);
    ResolvedPattern.ReplaceInline(TEXT("{Suffix}"), *MatchingRule->Suffix);
    ResolvedPattern.ReplaceInline(TEXT("{Class}"), *ClassName);

    while (ResolvedPattern.Contains(TEXT("__")))
    {
        ResolvedPattern.ReplaceInline(TEXT("__"), TEXT("_"));
    }

    const int32 NameTokenIndex = ResolvedPattern.Find(TEXT("{Name}"));
    const FString FixedBeforeName = ResolvedPattern.Left(NameTokenIndex);
    const FString FixedAfterName = ResolvedPattern.Mid(NameTokenIndex + 6);

    const FString CurrentName = AssetData.AssetName.ToString();
    FString BaseName = CurrentName;
    while (!FixedBeforeName.IsEmpty() && BaseName.StartsWith(FixedBeforeName))
    {
        BaseName.RightChopInline(FixedBeforeName.Len());
    }

    while (!FixedAfterName.IsEmpty() && BaseName.EndsWith(FixedAfterName))
    {
        BaseName.LeftChopInline(FixedAfterName.Len());
    }

    TArray<FString> KnownPrefixes;
    for (const FAssetNamingRule& Rule : Settings->Rules)
    {
        FString PrefixToken = Rule.Prefix;
        PrefixToken.TrimStartAndEndInline();
        while (PrefixToken.StartsWith(TEXT("_")))
        {
            PrefixToken.RightChopInline(1);
        }
        while (PrefixToken.EndsWith(TEXT("_")))
        {
            PrefixToken.LeftChopInline(1);
        }

        if (!PrefixToken.IsEmpty())
        {
            KnownPrefixes.AddUnique(PrefixToken + TEXT("_"));
        }
    }

    KnownPrefixes.Sort(
        [](const FString& Left, const FString& Right)
        {
            return Left.Len() > Right.Len();
        });

    bool bRemovedKnownPrefix = false;
    do
    {
        bRemovedKnownPrefix = false;
        for (const FString& KnownPrefix : KnownPrefixes)
        {
            if (BaseName.StartsWith(KnownPrefix))
            {
                BaseName.RightChopInline(KnownPrefix.Len());
                bRemovedKnownPrefix = true;
                break;
            }
        }
    }
    while (bRemovedKnownPrefix);

    bool bRemovedGeneratedSuffix = false;
    do
    {
        bRemovedGeneratedSuffix = false;
        for (FString SuffixToken : Settings->GeneratedSuffixTokens)
        {
            SuffixToken.TrimStartAndEndInline();
            while (SuffixToken.StartsWith(TEXT("_")))
            {
                SuffixToken.RightChopInline(1);
            }
            while (SuffixToken.EndsWith(TEXT("_")))
            {
                SuffixToken.LeftChopInline(1);
            }

            if (SuffixToken.IsEmpty())
            {
                continue;
            }

            const FString DelimitedSuffix = TEXT("_") + SuffixToken;
            if (BaseName.EndsWith(DelimitedSuffix))
            {
                BaseName.LeftChopInline(DelimitedSuffix.Len());
                bRemovedGeneratedSuffix = true;
                break;
            }

            const int32 SuffixIndex = BaseName.Find(
                DelimitedSuffix,
                ESearchCase::CaseSensitive,
                ESearchDir::FromEnd);
            if (SuffixIndex != INDEX_NONE)
            {
                const FString NumericTail = BaseName.Mid(SuffixIndex + DelimitedSuffix.Len());
                if (!NumericTail.IsEmpty() && NumericTail.IsNumeric())
                {
                    BaseName.LeftInline(SuffixIndex);
                    bRemovedGeneratedSuffix = true;
                    break;
                }
            }
        }
    }
    while (bRemovedGeneratedSuffix);

    FString DesiredName = ResolvedPattern;
    DesiredName.ReplaceInline(TEXT("{Name}"), *BaseName);

    DesiredName = ObjectTools::SanitizeObjectName(DesiredName);

    while (DesiredName.Contains(TEXT("__")))
    {
        DesiredName.ReplaceInline(TEXT("__"), TEXT("_"));
    }

    return DesiredName;
}

IMPLEMENT_MODULE(FAssetNamingToolModule, AssetNamingTool)

#undef LOCTEXT_NAMESPACE
