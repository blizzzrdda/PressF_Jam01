// Copyright 2021 fpwong. All Rights Reserved.
#include "BlueprintAssistWorkflowModeMenu.h"

#include "AssetToolsModule.h"
#include "BlueprintAssistUtils.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor/Kismet/Public/SSCSEditor.h"
#include "Runtime/SlateCore/Public/Types/SlateEnums.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "BlueprintAssistGlobals.h"

/**********************/
/* FBAWorkflowModeItem */
/**********************/

class FContentBrowserModule;

FString FBAWorkflowModeItem::ToString() const
{
	return Name;
}

void SBAWorkflowModeMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBAFilteredList<TSharedPtr<FBAWorkflowModeItem>>)
		.InitListItems(this, &SBAWorkflowModeMenu::InitListItems)
		.OnGenerateRow(this, &SBAWorkflowModeMenu::CreateItemWidget)
		.OnSelectItem(this, &SBAWorkflowModeMenu::SelectItem)
		.WidgetSize(GetWidgetSize())
		.MenuTitle(FString("Switch workflow"))
	];
}

void SBAWorkflowModeMenu::InitListItems(TArray<TSharedPtr<FBAWorkflowModeItem>>& Items)
{
	const TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow)
	{
		return;
	}

	TArray<TSharedPtr<SWidget>> ModeWidgets;
	FBAUtils::GetChildWidgets(ActiveWindow, "SModeWidget", ModeWidgets);

	for (TSharedPtr<SWidget> ModeWidget : ModeWidgets)
	{
		TSharedPtr<SWidget> ModeBorder = FBAUtils::GetChildWidget(ModeWidget, "SBorder");
		TSharedPtr<STextBlock> ModeTextWidget = FBAUtils::GetChildWidgetCasted<STextBlock>(ModeWidget, "STextBlock");
		if (!ModeBorder || !ModeTextWidget)
		{
			UE_LOG(LogBlueprintAssist, Warning, TEXT("Mode widget is missing a border or text block"));
			continue;
		}

		if (ModeTextWidget.IsValid())
		{
			// The mode widget reacts when we click on its border
			Items.Add(MakeShareable(new FBAWorkflowModeItem(EBAWorkflowItemType::WorkflowMode, ModeBorder, ModeTextWidget->GetText().ToString())));
		}
	}

	TArray<TSharedPtr<SWidget>> AssetShortcutWidgets;
	FBAUtils::GetChildWidgets(ActiveWindow, "SAssetShortcut", AssetShortcutWidgets);

	for (TSharedPtr<SWidget> AssetShortcut : AssetShortcutWidgets)
	{
		TSharedPtr<SWidget> InteractWidget = FBAUtils::GetChildWidget(AssetShortcut, "SCheckBox");
		TArray<TSharedPtr<STextBlock>> TextBlocks;
		FBAUtils::GetChildWidgetsCasted<STextBlock>(AssetShortcut, "STextBlock", TextBlocks);

		TSharedPtr<STextBlock> TextWidget = TextBlocks.Last();

		if (!InteractWidget || !TextWidget)
		{
			UE_LOG(LogBlueprintAssist, Warning, TEXT("AssetShorcut is missing a border or text block"));
			continue;
		}

		if (TextWidget->GetText().IsEmpty())
		{
			continue;
		}

		if (TextWidget.IsValid())
		{
			// UE_LOG(LogTemp, Warning, TEXT("Found asset shortcut %s"), *TextWidget->GetText().ToString());
			Items.Add(MakeShareable(new FBAWorkflowModeItem(EBAWorkflowItemType::AssetShortcut, InteractWidget, TextWidget->GetText().ToString())));
		}
	}
}

TSharedRef<ITableRow> SBAWorkflowModeMenu::CreateItemWidget(TSharedPtr<FBAWorkflowModeItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return
		SNew(STableRow<TSharedPtr<FString>>, OwnerTable).Padding(FMargin(4.0, 2.0))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Left).FillWidth(1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->ToString()))
			]
		];
}

void SBAWorkflowModeMenu::SelectItem(TSharedPtr<FBAWorkflowModeItem> Item)
{
	auto Widget = Item->InteractWidget;
	switch (Item->Type)
	{
		case EBAWorkflowItemType::WorkflowMode:
			FBAUtils::TryClickOnWidget(Item->InteractWidget);
			break;
		case EBAWorkflowItemType::AssetShortcut:
			FBAUtils::InteractWithWidget(Item->InteractWidget);
			break;
		default: ;
	}
}
