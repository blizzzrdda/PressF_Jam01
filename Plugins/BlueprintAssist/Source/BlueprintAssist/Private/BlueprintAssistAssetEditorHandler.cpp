// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistAssetEditorHandler.h"

#include "BlueprintEditor.h"
#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/LazySingleton.h"

#if ENGINE_MINOR_VERSION < 24 && ENGINE_MAJOR_VERSION == 4
#include "Toolkits/AssetEditorManager.h"
#endif

FBAAssetEditorHandler& FBAAssetEditorHandler::Get()
{
	return TLazySingleton<FBAAssetEditorHandler>::Get();
}

FBAAssetEditorHandler::~FBAAssetEditorHandler()
{
	UnbindDelegates();
}

void FBAAssetEditorHandler::Init()
{
	if (!IBlueprintAssistModule::Get().IsUsingUObjects())
	{
		BindAssetOpenedDelegate();
	}
}

void FBAAssetEditorHandler::Cleanup()
{
	UnbindDelegates();
	BlueprintHandlers.Empty();
	AssetsByTab.Empty();
}

void FBAAssetEditorHandler::Tick()
{
	CheckInvalidAssetEditors();
}

IAssetEditorInstance* FBAAssetEditorHandler::GetEditorFromTab(const TSharedPtr<SDockTab> Tab) const
{
	if (const TWeakObjectPtr<UObject>* FoundAsset = AssetsByTab.Find(Tab))
	{
		if (FoundAsset->IsValid() && FoundAsset->Get()->IsValidLowLevelFast(false))
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				return AssetEditorSubsystem->FindEditorForAsset(FoundAsset->Get(), false);
			}
		}
	}

	return nullptr;
}

void FBAAssetEditorHandler::BindAssetOpenedDelegate()
{
	// TODO: OnAssetEditorRequestClose is not being properly called in 4.26, maybe this will work in the future?
	check(GEditor);

#if ENGINE_MINOR_VERSION < 24 && ENGINE_MAJOR_VERSION == 4
	OnAssetOpenedDelegateHandle = FAssetEditorManager::Get().OnAssetOpenedInEditor().AddRaw(this, &FBAAssetEditorHandler::OnAssetOpenedInEditor);
#else
	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(this, &FBAAssetEditorHandler::OnAssetOpened);
	}
#endif
}

void FBAAssetEditorHandler::UnbindDelegates()
{
	if (!GEditor)
	{
		return;
	}

#if ENGINE_MINOR_VERSION < 24 && ENGINE_MAJOR_VERSION == 4
	FAssetEditorManager::Get().OnAssetOpenedInEditor().RemoveAll(this);
#else
	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OnAssetOpenedInEditor().RemoveAll(this);
	}
#endif
}

void FBAAssetEditorHandler::OnAssetOpened(UObject* Asset, IAssetEditorInstance* AssetEditor)
{
	const FString AssetEditorName = AssetEditor ? AssetEditor->GetEditorName().ToString() : "nullptr";
	check(Asset);
	UE_LOG(LogBlueprintAssist, Log, TEXT("Asset opened %s (%s)"), *Asset->GetName(), *AssetEditorName);

	// Update assets by tab
	if (TSharedPtr<SDockTab> Tab = GetTabForAssetEditor(AssetEditor))
	{
		AssetsByTab.Add(TWeakPtr<SDockTab>(Tab), Asset);
	}

	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		BlueprintHandlers.FindOrAdd(Blueprint->GetBlueprintGuid()).BindBlueprintChanged(Blueprint);
	}
}

void FBAAssetEditorHandler::OnAssetClosed(UObject* Asset)
{
	UE_LOG(LogBlueprintAssist, Log, TEXT("Asset closed %s"), *Asset->GetName());
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		const FGuid& BPGuid = Blueprint->GetBlueprintGuid();
		if (FBABlueprintHandler* FoundHandler = BlueprintHandlers.Find(BPGuid))
		{
			FoundHandler->UnbindBlueprintChanged(Blueprint);
		}
	}
}

void FBAAssetEditorHandler::CheckInvalidAssetEditors()
{
	if (!GEditor)
	{
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	// Remove invalid tabs
	TArray<TWeakPtr<SDockTab>> Tabs;
	AssetsByTab.GetKeys(Tabs);
	const auto IsTabInvalid = [](const TWeakPtr<SDockTab> Tab) { return !Tab.IsValid(); };
	TArray<TWeakPtr<SDockTab>> InvalidTabs = Tabs.FilterByPredicate(IsTabInvalid);
	for (TWeakPtr<SDockTab> Tab : InvalidTabs)
	{
		AssetsByTab.Remove(Tab);
	}

	// Update the open assets
	TArray<UObject*> CurrentOpenAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (TWeakObjectPtr<UObject> Asset : OpenAssets)
	{
		if (Asset.IsValid() && Asset->IsValidLowLevelFast(false))
		{
			if (!CurrentOpenAssets.Contains(Asset.Get()))
			{
				OnAssetClosed(Asset.Get());
			}
		}
	}

	OpenAssets.Reset(CurrentOpenAssets.Num());
	for (UObject* Asset : CurrentOpenAssets)
	{
		if (Asset != nullptr && Asset->IsValidLowLevelFast(false))
		{
			OpenAssets.Add(Asset);
		}
	}
}

TSharedPtr<SDockTab> FBAAssetEditorHandler::GetTabForAsset(UObject* Asset) const
{
	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		if (IAssetEditorInstance* AssetEditor = AssetEditorSubsystem->FindEditorForAsset(Asset, false))
		{
			return GetTabForAssetEditor(AssetEditor);
		}
	}

	return TSharedPtr<SDockTab>();
}

TSharedPtr<SDockTab> FBAAssetEditorHandler::GetTabForAssetEditor(IAssetEditorInstance* AssetEditor) const
{
	if (AssetEditor)
	{
		TSharedPtr<FTabManager> TabManager = AssetEditor->GetAssociatedTabManager();
		if (TabManager.IsValid())
		{
			return TabManager->GetOwnerTab();
		}
	}

	return TSharedPtr<SDockTab>();
}
