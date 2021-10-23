// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistModule.h"

#include "BlueprintAssistAssetEditorHandler.h"
#include "BlueprintAssistCommands.h"
#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistGraphCommands.h"
#include "BlueprintAssistGraphExtender.h"
#include "BlueprintAssistGraphPanelNodeFactory.h"
#include "BlueprintAssistInputProcessor.h"
#include "BlueprintAssistSettings.h"
#include "BlueprintAssistSizeCache.h"
#include "BlueprintAssistTabHandler.h"
#include "BlueprintAssistToolbar.h"
#include "BlueprintEditorModule.h"

#if WITH_EDITOR
#include "MessageLogInitializationOptions.h"
#include "MessageLogModule.h"
#endif

#include "PropertyEditorModule.h"
#include "BlueprintAssist/BlueprintAssistObjects/BARootObject.h"
#include "Developer/Settings/Public/ISettingsModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

#define LOCTEXT_NAMESPACE "BlueprintAssist"

#define BA_ENABLED (!IS_MONOLITHIC && !UE_BUILD_SHIPPING && !UE_BUILD_TEST && !UE_GAME && !UE_SERVER)

class FBlueprintAssistModule final : public IBlueprintAssistModule
{
public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	void BindLiveCodingSound();

	void RegisterSettings();

	virtual UBARootObject* GetRootObject() override { return RootObject; };

	virtual bool IsUsingUObjects() override { return bUsingUObjects; }

private:
	bool bWasModuleInitialized = false;

	FBAToolbar ToolbarMenu;

	TSharedPtr<FBlueprintAssistGraphPanelNodeFactory> BANodeFactory;

	UBARootObject* RootObject = nullptr;

	bool bUsingUObjects = false;

	void OnPostEngineInit();
};

void FBlueprintAssistModule::StartupModule()
{
	UE_LOG(LogBlueprintAssist, Log, TEXT("Blueprint Assist Startup Module"));
#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FBlueprintAssistModule::OnPostEngineInit);
#endif
}


void FBlueprintAssistModule::OnPostEngineInit()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogBlueprintAssist, Log, TEXT("FBlueprintAssistModule: Slate App is not initialized, not loading the plugin"));
		return;
	}

	bWasModuleInitialized = true;

	FBACommands::Register();
	FBAGraphCommands::Register();

	FBAGraphExtender::ApplyExtender();

	// Init singletons
	FBASizeCache::Get().Init();
	FBAAssetEditorHandler::Get().Init();
	FBATabHandler::Get().Init();
	FBAInputProcessor::Create();

#if WITH_EDITOR
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions InitOptions;
	InitOptions.bShowFilters = false;
	InitOptions.bDiscardDuplicates = true;
	MessageLogModule.RegisterLogListing("BlueprintAssist", FText::FromString("Blueprint Assist"), InitOptions);
#endif

	ToolbarMenu.Init();

	// Register the graph node factory
	BANodeFactory = MakeShareable(new FBlueprintAssistGraphPanelNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(BANodeFactory);

	BindLiveCodingSound();

	RegisterSettings();

	bUsingUObjects = GetDefault<UBASettings>()->bUseUObjects;

	if (bUsingUObjects)
	{
		RootObject = NewObject<UBARootObject>();
		RootObject->Init();
		RootObject->AddToRoot();
	}

	UE_LOG(LogBlueprintAssist, Log, TEXT("Finished loaded BlueprintAssist Module"));
}


void FBlueprintAssistModule::ShutdownModule()
{
#if BA_ENABLED
	if (!bWasModuleInitialized)
	{
		return;
	}

	FBATabHandler::Get().Cleanup();

	FBAInputProcessor::Get().Cleanup();

	FBAAssetEditorHandler::Get().Cleanup();

	if (RootObject)
	{
		UE_LOG(LogBlueprintAssist, Log, TEXT("Remove BlueprintAssist Root Object"));
		RootObject->Cleanup();
		RootObject->RemoveFromRoot();
	}

#if WITH_EDITOR
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.UnregisterLogListing("BlueprintAssist");
#endif

	ToolbarMenu.Cleanup();

	// Unregister the graph node factory
	if (BANodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(BANodeFactory);
		BANodeFactory.Reset();
	}

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "Plugins", "BlueprintAssist");
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(UBASettings::StaticClass()->GetFName());

	FBACommands::Unregister();
	FBAToolbarCommands::Unregister();

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	UE_LOG(LogBlueprintAssist, Log, TEXT("Shutdown BlueprintAssist Module"));
#endif
}

void FBlueprintAssistModule::BindLiveCodingSound()
{
#if WITH_LIVE_CODING
	if (ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME))
	{
		if (LiveCoding->IsEnabledByDefault() || LiveCoding->IsEnabledForSession())
		{
			auto PlaySound = []()
			{
				if (GetDefault<UBASettings>()->bPlayLiveCompileSound)
				{
					GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue"));
				}
			};

			LiveCoding->GetOnPatchCompleteDelegate().AddLambda(PlaySound);
			UE_LOG(LogBlueprintAssist, Log, TEXT("Bound to live coding patch complete"));
		}
	}
#endif
}

void FBlueprintAssistModule::RegisterSettings()
{
	// Register custom settings to appear in the project settings
	ISettingsModule& SettingsModule = FModuleManager::GetModuleChecked<ISettingsModule>("Settings");
	SettingsModule.RegisterSettings(
		"Editor",
		"Plugins",
		"BlueprintAssist",
		LOCTEXT("BlueprintAssistSettingsName", "Blueprint Assist"),
		LOCTEXT("BlueprintAssistSettingsNameDesc", "Configure the Blueprint Assist plugin"),
		GetMutableDefault<UBASettings>()
	);

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(UBASettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FBASettingsDetails::MakeInstance));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintAssistModule, BlueprintAssist)
