#pragma once

#include "AssetRegistry/AssetData.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UToolMenu;

class FAssetNamingToolModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void OnAssetAdded(const FAssetData& AssetData);
    void OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
    void OnFilesLoaded();
    void QueueAsset(const FAssetData& AssetData);
    bool ProcessPendingAssets(float DeltaTime);
    void ProcessAsset(const FAssetData& AssetData);
    void RegisterMenus();
    void PopulateToolsMenu(UToolMenu* Menu);
    void CheckAllAssetNames();
    FString BuildDesiredName(const FAssetData& AssetData) const;

    bool bInitialScanComplete = false;
    bool bIsRenaming = false;
    TArray<FAssetData> PendingAssets;
    FDelegateHandle AssetAddedHandle;
    FDelegateHandle AssetRenamedHandle;
    FDelegateHandle FilesLoadedHandle;
    FTSTicker::FDelegateHandle TickerHandle;
};
