// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistToolbar.h"

#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistSettings.h"
#include "BlueprintAssistSizeCache.h"
#include "BlueprintAssistUtils.h"
#include "BlueprintEditorModule.h"
#include "ISettingsModule.h"
#include "LevelEditorActions.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "BlueprintAssist"

void FBAToolbarCommandsImpl::RegisterCommands()
{
	UI_COMMAND(
		AutoFormatting_Never,
		"Never auto format",
		"Never auto format when you create a new node",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		AutoFormatting_FormatAll,
		"Always format all connected nodes",
		"Always format all connected nodes when you create a new node",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		AutoFormatting_FormatNewlyCreated,
		"Only format the newly created node",
		"Only format the newly created node when you create a new node",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		FormattingStyle_Compact,
		"Formatting Style Compact",
		"Sets the formatting style to Compact",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		FormattingStyle_Expanded,
		"Formatting Style Expanded",
		"Sets the formatting style to Expanded",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		ParameterStyle_LeftHandSide,
		"Parameter Style Left Hand Side",
		"Parameters will be positioned out on the LHS when formatting",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		ParameterStyle_Helixing,
		"Parameter Style Helixing",
		"Parameter nodes will be positioned below when formatting",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		CommentInsertStyle_Never,
		"Auto Comment Insert: Never",
		"New nodes will never be inserted into connected comment nodes",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		CommentInsertStyle_Always,
		"Auto Comment Insert: Always",
		"New nodes will always be inserted into the comment node when connected",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		CommentInsertStyle_Surrounded,
		"Auto Comment Insert: Surrounded",
		"New nodes will only be inserted into the comment node when both connected nodes are also inside the comment",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		FormatAllStyle_Simple,
		"Format All Style: Simple",
		"Position root nodes into a single column",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		FormatAllStyle_Smart,
		"Format All Style: Smart",
		"Position root nodes into multiple columns based on node position",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		FormatAllStyle_NodeType,
		"Format All Style: Node Type",
		"Position nodes into columns based on root node type",
		EUserInterfaceActionType::RadioButton,
		FInputChord());

	UI_COMMAND(
		BlueprintAssistSettings,
		"Blueprint Assist Settings",
		"Open the blueprint assist settings",
		EUserInterfaceActionType::Button,
		FInputChord());
}

void FBAToolbarCommands::Register()
{
	FBAToolbarCommandsImpl::Register();
}

const FBAToolbarCommandsImpl& FBAToolbarCommands::Get()
{
	return FBAToolbarCommandsImpl::Get();
}

void FBAToolbarCommands::Unregister()
{
	return FBAToolbarCommandsImpl::Unregister();
}

void FBAToolbar::Init()
{
	FBAToolbarCommands::Register();
	BindToolbarCommands();

#if ENGINE_MINOR_VERSION < 24 && ENGINE_MAJOR_VERSION == 4
	OnAssetOpenedDelegateHandle = FAssetEditorManager::Get().OnAssetOpenedInEditor().AddRaw(this, &FBAToolbar::OnAssetOpenedInEditor);
#else
	OnAssetOpenedDelegateHandle = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetOpenedInEditor().AddRaw(this, &FBAToolbar::OnAssetOpenedInEditor);
#endif
}

void FBAToolbar::Cleanup()
{
	if (OnAssetOpenedDelegateHandle.IsValid())
	{
#if ENGINE_MINOR_VERSION < 24 && ENGINE_MAJOR_VERSION == 4
		FAssetEditorManager::Get().OnAssetOpenedInEditor().Remove(OnAssetOpenedDelegateHandle);
#else
		if (GEditor != nullptr)
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(OnAssetOpenedDelegateHandle);
			}
		}
#endif
		OnAssetOpenedDelegateHandle.Reset();
	}
}

void FBAToolbar::OnAssetOpenedInEditor(UObject* Asset, IAssetEditorInstance* AssetEditor)
{
	if (!GetDefault<UBASettings>()->bAddToolbarWidget)
	{
		return;
	}
	FAssetEditorToolkit* AssetEditorToolkit = StaticCast<FAssetEditorToolkit*>(AssetEditor);
	if (AssetEditorToolkit)
	{
		TSharedRef<FUICommandList> ToolkitCommands = AssetEditorToolkit->GetToolkitCommands();

		if (ToolbarExtender.IsValid())
		{
			AssetEditorToolkit->RemoveToolbarExtender(ToolbarExtender);
			ToolbarExtender.Reset();
		}

		ToolbarExtender = MakeShareable(new FExtender);

		ToolbarExtender->AddToolBarExtension(
			"Asset",
			EExtensionHook::After,
			ToolkitCommands,
			FToolBarExtensionDelegate::CreateRaw(this, &FBAToolbar::ExtendToolbar)
		);

		AssetEditorToolkit->AddToolbarExtender(ToolbarExtender);
	}
}

void FBAToolbar::SetAutoFormattingStyle(EBAAutoFormatting FormattingStyle)
{
	if (FBAFormatterSettings* FormatterSettings = GetCurrentFormatterSettings())
	{
		UBASettings* BASettings = GetMutableDefault<UBASettings>();
		FormatterSettings->AutoFormatting = FormattingStyle;
		BASettings->PostEditChange();
		BASettings->SaveConfig();
	}
}

bool FBAToolbar::IsAutoFormattingStyleChecked(EBAAutoFormatting FormattingStyle)
{
	if (FBAFormatterSettings* FormatterSettings = GetCurrentFormatterSettings())
	{
		return FormatterSettings->AutoFormatting == FormattingStyle;
	}

	return false;
}

void FBAToolbar::SetParameterStyle(EBAParameterFormattingStyle Style)
{
	UBASettings* BASettings = GetMutableDefault<UBASettings>();
	BASettings->ParameterStyle = Style;
	BASettings->PostEditChange();
	BASettings->SaveConfig();
}

bool FBAToolbar::IsParameterStyleChecked(EBAParameterFormattingStyle Style)
{
	return GetDefault<UBASettings>()->ParameterStyle == Style;
}

void FBAToolbar::SetNodeFormattingStyle(EBANodeFormattingStyle Style)
{
	UBASettings* BASettings = GetMutableDefault<UBASettings>();
	BASettings->FormattingStyle = Style;
	BASettings->PostEditChange();
	BASettings->SaveConfig();
}

bool FBAToolbar::IsNodeFormattingStyleChecked(EBANodeFormattingStyle Style)
{
	return GetDefault<UBASettings>()->FormattingStyle == Style;
}

void FBAToolbar::SetCommentInsertStyle(EBAAutoInsertComment Style)
{
	UBASettings* BASettings = GetMutableDefault<UBASettings>();
	BASettings->AutoInsertComment = Style;
	BASettings->PostEditChange();
	BASettings->SaveConfig();
}

bool FBAToolbar::IsCommentInsertStyleChecked(EBAAutoInsertComment Style)
{
	return GetDefault<UBASettings>()->AutoInsertComment == Style;
}

void FBAToolbar::SetFormatAllStyle(EBAFormatAllStyle Style)
{
	UBASettings* BASettings = GetMutableDefault<UBASettings>();
	BASettings->FormatAllStyle = Style;
	BASettings->PostEditChange();
	BASettings->SaveConfig();
}

bool FBAToolbar::IsFormatAllStyleChecked(EBAFormatAllStyle Style)
{
	return GetDefault<UBASettings>()->FormatAllStyle == Style;
}

void FBAToolbar::SetUseCommentBoxPadding(ECheckBoxState NewCheckedState)
{
	UBASettings* BASettings = GetMutableDefault<UBASettings>();
	BASettings->bApplyCommentPadding = (NewCheckedState == ECheckBoxState::Checked) ? true : false;
	BASettings->PostEditChange();
	BASettings->SaveConfig();
}

void FBAToolbar::SetGraphReadOnly(ECheckBoxState NewCheckedState)
{
	TSharedPtr<FBAGraphHandler> GraphHandler = FBAUtils::GetCurrentGraphHandler();
	if (GraphHandler.IsValid())
	{
		GraphHandler->GetFocusedEdGraph()->bEditable = (NewCheckedState == ECheckBoxState::Checked) ? false : true;
	}
}

void FBAToolbar::OpenBlueprintAssistSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "Plugins", "BlueprintAssist");
}

void FBAToolbar::BindToolbarCommands()
{
	const FBAToolbarCommandsImpl& Commands = FBAToolbarCommands::Get();
	BlueprintAssistToolbarActions = MakeShareable(new FUICommandList);
	FUICommandList& ActionList = *BlueprintAssistToolbarActions;

	ActionList.MapAction(
		Commands.AutoFormatting_Never,
		FExecuteAction::CreateStatic(&FBAToolbar::SetAutoFormattingStyle, EBAAutoFormatting::Never),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsAutoFormattingStyleChecked, EBAAutoFormatting::Never)
	);

	ActionList.MapAction(
		Commands.AutoFormatting_FormatNewlyCreated,
		FExecuteAction::CreateStatic(&FBAToolbar::SetAutoFormattingStyle, EBAAutoFormatting::FormatSingleConnected),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsAutoFormattingStyleChecked, EBAAutoFormatting::FormatSingleConnected)
	);

	ActionList.MapAction(
		Commands.AutoFormatting_FormatAll,
		FExecuteAction::CreateStatic(&FBAToolbar::SetAutoFormattingStyle, EBAAutoFormatting::FormatAllConnected),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsAutoFormattingStyleChecked, EBAAutoFormatting::FormatAllConnected)
	);

	ActionList.MapAction(
		Commands.FormattingStyle_Compact,
		FExecuteAction::CreateStatic(&FBAToolbar::SetNodeFormattingStyle, EBANodeFormattingStyle::Compact),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsNodeFormattingStyleChecked, EBANodeFormattingStyle::Compact)
	);

	ActionList.MapAction(
		Commands.FormattingStyle_Expanded,
		FExecuteAction::CreateStatic(&FBAToolbar::SetNodeFormattingStyle, EBANodeFormattingStyle::Expanded),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsNodeFormattingStyleChecked, EBANodeFormattingStyle::Expanded)
	);

	ActionList.MapAction(
		Commands.ParameterStyle_LeftHandSide,
		FExecuteAction::CreateStatic(&FBAToolbar::SetParameterStyle, EBAParameterFormattingStyle::LeftSide),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsParameterStyleChecked, EBAParameterFormattingStyle::LeftSide)
	);

	ActionList.MapAction(
		Commands.ParameterStyle_Helixing,
		FExecuteAction::CreateStatic(&FBAToolbar::SetParameterStyle, EBAParameterFormattingStyle::Helixing),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsParameterStyleChecked, EBAParameterFormattingStyle::Helixing)
	);

	ActionList.MapAction(
		Commands.CommentInsertStyle_Never,
		FExecuteAction::CreateStatic(&FBAToolbar::SetCommentInsertStyle, EBAAutoInsertComment::Never),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsCommentInsertStyleChecked, EBAAutoInsertComment::Never)
	);

	ActionList.MapAction(
		Commands.CommentInsertStyle_Always,
		FExecuteAction::CreateStatic(&FBAToolbar::SetCommentInsertStyle, EBAAutoInsertComment::Always),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsCommentInsertStyleChecked, EBAAutoInsertComment::Always)
	);

	ActionList.MapAction(
		Commands.CommentInsertStyle_Surrounded,
		FExecuteAction::CreateStatic(&FBAToolbar::SetCommentInsertStyle, EBAAutoInsertComment::Surrounded),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsCommentInsertStyleChecked, EBAAutoInsertComment::Surrounded)
	);

	ActionList.MapAction(
		Commands.FormatAllStyle_Simple,
		FExecuteAction::CreateStatic(&FBAToolbar::SetFormatAllStyle, EBAFormatAllStyle::Simple),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsFormatAllStyleChecked, EBAFormatAllStyle::Simple)
	);

	ActionList.MapAction(
		Commands.FormatAllStyle_Smart,
		FExecuteAction::CreateStatic(&FBAToolbar::SetFormatAllStyle, EBAFormatAllStyle::Smart),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsFormatAllStyleChecked, EBAFormatAllStyle::Smart)
	);

	ActionList.MapAction(
		Commands.FormatAllStyle_NodeType,
		FExecuteAction::CreateStatic(&FBAToolbar::SetFormatAllStyle, EBAFormatAllStyle::NodeType),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FBAToolbar::IsFormatAllStyleChecked, EBAFormatAllStyle::NodeType)
	);

	ActionList.MapAction(
		Commands.BlueprintAssistSettings,
		FExecuteAction::CreateStatic(&FBAToolbar::OpenBlueprintAssistSettings)
	);
}

TSharedRef<SWidget> FBAToolbar::CreateToolbarWidget()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;

	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, BlueprintAssistToolbarActions, ToolbarExtender);

	TSharedPtr<FBAGraphHandler> GraphHandler = FBAUtils::GetCurrentGraphHandler();
	if (GraphHandler.IsValid())
	{
		MenuBuilder.BeginSection("FormattingSettings");
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("AutoFormattingSubMenu", "Auto Formatting Behaviour"),
				LOCTEXT("AutoFormattingSubMenu_Tooltip", "Allows you to set the auto formatting behavior when a new node is added to the graph"),
				FNewMenuDelegate::CreateRaw(this, &FBAToolbar::MakeAutoFormattingSubMenu));

			MenuBuilder.AddSubMenu(
				LOCTEXT("FormattingStyleSubMenu", "Formatting Style"),
				LOCTEXT("FormattingStyleSubMenu_Tooltip", "Set the formatting style"),
				FNewMenuDelegate::CreateRaw(this, &FBAToolbar::MakeFormattingStyleSubMenu));

			MenuBuilder.AddSubMenu(
				LOCTEXT("ParameterStyleSubMenu", "Parameter Style"),
				LOCTEXT("ParameterStyleSubMenu_Tooltip", "Set the style for parameters when formatting"),
				FNewMenuDelegate::CreateRaw(this, &FBAToolbar::MakeParameterStyleSubMenu));

			MenuBuilder.AddSubMenu(
				LOCTEXT("AutoCommentInsertStyleSubMenu", "Auto Comment Insert Style"),
				LOCTEXT("AutoCommentInsertStyle_Tooltip", "Set the auto comment insert style"),
				FNewMenuDelegate::CreateRaw(this, &FBAToolbar::MakeCommentInsertStyleSubMenu));

			MenuBuilder.AddSubMenu(
				LOCTEXT("FormatAllInsertStyleSubMenu", "Format All Insert Style"),
				LOCTEXT("FormatAllInsertStyle_Tooltip", "Set the format all style"),
				FNewMenuDelegate::CreateRaw(this, &FBAToolbar::MakeFormatAllStyleSubMenu));

			TSharedRef<SWidget> ApplyCommentPaddingCheckbox = SNew(SBox)
			[
				SNew(SCheckBox)
				.IsChecked(GetDefault<UBASettings>()->bApplyCommentPadding ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged(FOnCheckStateChanged::CreateRaw(this, &FBAToolbar::SetUseCommentBoxPadding))
				.Style(FEditorStyle::Get(), "Menu.CheckBox")
				.ToolTipText(LOCTEXT("ApplyCommentPaddingToolTip", "Toggle whether to apply comment padding when formatting"))
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ApplyCommentPadding", "Apply comment padding"))
					]
				]
			];

			MenuBuilder.AddMenuEntry(FUIAction(), ApplyCommentPaddingCheckbox);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("MiscSettings");
		{
			TSharedRef<SWidget> GraphReadOnlyCheckbox = SNew(SBox)
			[
				SNew(SCheckBox)
				.IsChecked(GraphHandler->IsGraphReadOnly() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged(FOnCheckStateChanged::CreateRaw(this, &FBAToolbar::SetGraphReadOnly))
				.Style(FEditorStyle::Get(), "Menu.CheckBox")
				.ToolTipText(LOCTEXT("GraphReadOnlyToolTip", "Set the graph read only state (cannot be undone without the BA plugin!)"))
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GraphReadOnly", "Graph Read Only"))
					]
				]
			];

			MenuBuilder.AddMenuEntry(FUIAction(), GraphReadOnlyCheckbox);
		}
		MenuBuilder.EndSection();

		// open blueprint settings
		MenuBuilder.BeginSection("BlueprintAssistSettings");
		{
			MenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().BlueprintAssistSettings);
		}
		MenuBuilder.EndSection();
	}

	TSharedRef<SWidget> MenuBuilderWidget = MenuBuilder.MakeWidget();

	return MenuBuilderWidget;
}

void FBAToolbar::MakeAutoFormattingSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("AutoFormattingStyle", LOCTEXT("AutoFormattingStyle", "Auto Formatting Style"));
	{
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().AutoFormatting_Never);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().AutoFormatting_FormatNewlyCreated);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().AutoFormatting_FormatAll);
	}
	InMenuBuilder.EndSection();
}

void FBAToolbar::MakeParameterStyleSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("ParameterStyle", LOCTEXT("ParameterStyle", "Parameter Style"));
	{
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().ParameterStyle_Helixing);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().ParameterStyle_LeftHandSide);
	}
	InMenuBuilder.EndSection();
}

void FBAToolbar::MakeFormattingStyleSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("FormattingStyle", LOCTEXT("FormattingStyle", "Formatting Style"));
	{
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().FormattingStyle_Compact);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().FormattingStyle_Expanded);
	}
	InMenuBuilder.EndSection();
}

void FBAToolbar::MakeCommentInsertStyleSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("CommentInsertStyle", LOCTEXT("CommentInsertStyle", "Comment Insert Style"));
	{
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().CommentInsertStyle_Never);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().CommentInsertStyle_Always);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().CommentInsertStyle_Surrounded);
	}
	InMenuBuilder.EndSection();
}

void FBAToolbar::MakeFormatAllStyleSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("FormatAllStyle", LOCTEXT("FormatAllStyle", "Format All Style"));
	{
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().FormatAllStyle_Simple);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().FormatAllStyle_Smart);
		InMenuBuilder.AddMenuEntry(FBAToolbarCommands::Get().FormatAllStyle_NodeType);
	}
	InMenuBuilder.EndSection();
}

void FBAToolbar::ExtendToolbar(FToolBarBuilder& ToolbarBuilder)
{
	const TAttribute<FText> Tooltip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([]
	{
		FString TooltipString = "Blueprint Assist Settings";
		if (!FBAUtils::GetCurrentGraphHandler().IsValid())
		{
			TooltipString += " (Disabled)";

			TWeakPtr<SGraphEditor> InvalidGraphEditor = FBATabHandler::Get().GetUnsupportedGraphEditor();
			if (InvalidGraphEditor.IsValid())
			{
				if (UEdGraph* Graph = InvalidGraphEditor.Pin()->GetCurrentGraph())
				{
					TooltipString += " (Graph Type: " + Graph->GetClass()->GetName() + ")";
				}
			}
		}

		return FText::FromString(TooltipString);
	}));

	const bool bShouldAddToolbarButton = FBAUtils::GetCurrentGraphHandler().IsValid() || FBATabHandler::Get().GetUnsupportedGraphEditor().IsValid();
	if (bShouldAddToolbarButton)
	{
		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FBAToolbar::CreateToolbarWidget),
			LOCTEXT("BlueprintAssist", "BP Assist"),
			Tooltip,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings")
		);
	}
}

FBAFormatterSettings* FBAToolbar::GetCurrentFormatterSettings()
{
	auto GraphHandler = FBAUtils::GetCurrentGraphHandler();
	if (!GraphHandler.IsValid())
	{
		return nullptr;
	}

	return FBAUtils::FindFormatterSettings(GraphHandler->GetFocusedEdGraph());
}

#undef LOCTEXT_NAMESPACE