// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "BlueprintAssistBlueprintHandler.h"
#include "BlueprintAssistToolbar.h"

class BLUEPRINTASSIST_API FBAAssetEditorHandler
{
public:
	static FBAAssetEditorHandler& Get();

	~FBAAssetEditorHandler();

	void Init();

	void Cleanup();

	void Tick();

	IAssetEditorInstance* GetEditorFromTab(const TSharedPtr<SDockTab> Tab) const;

	template<class AssetClass, class EditorClass>
	EditorClass* GetEditorFromTabCasted(const TSharedPtr<SDockTab> Tab) const
	{
		if (const TWeakObjectPtr<UObject>* FoundAsset = AssetsByTab.Find(Tab))
		{
			if (FoundAsset->IsValid() && FoundAsset->Get()->IsValidLowLevelFast(false) && FoundAsset->Get()->IsA(AssetClass::StaticClass()))
			{
				if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					return static_cast<EditorClass*>(AssetEditorSubsystem->FindEditorForAsset(FoundAsset->Get(), false));
				}
			}
		}

		return nullptr;
	}

	template<class AssetClass>
	AssetClass* GetAssetFromTab(const TSharedPtr<SDockTab> Tab) const
	{
		if (const TWeakObjectPtr<UObject>* FoundAsset = AssetsByTab.Find(Tab))
		{
			if (FoundAsset->IsValid() && FoundAsset->Get()->IsValidLowLevelFast(false))
			{
				return Cast<AssetClass>(FoundAsset->Get());
			}
		}

		return nullptr;
	}

	TSharedPtr<SDockTab> GetTabForAsset(UObject* Asset) const;

	TSharedPtr<SDockTab> GetTabForAssetEditor(IAssetEditorInstance* AssetEditor) const;

protected:
	void BindAssetOpenedDelegate();

	void UnbindDelegates();

	void OnAssetOpened(UObject* Asset, class IAssetEditorInstance* AssetEditor);

	void OnAssetClosed(UObject* Asset);

	void CheckInvalidAssetEditors();

private:
	TArray<TWeakObjectPtr<UObject>> OpenAssets;
	TMap<FGuid, FBABlueprintHandler> BlueprintHandlers;
	TMap<TWeakPtr<SDockTab>, TWeakObjectPtr<UObject>> AssetsByTab;
};
