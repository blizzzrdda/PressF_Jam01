// Copyright 2021 fpwong. All Rights Reserved.

#include "GoToSymbolMenu.h"

#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistUtils.h"
#include "BlueprintEditor.h"
#include "K2Node_Event.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

void SGoToSymbolMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBAFilteredList<TSharedPtr<FGoToSymbolStruct>>)
		.InitListItems(this, &SGoToSymbolMenu::InitListItems)
		.OnGenerateRow(this, &SGoToSymbolMenu::CreateItemWidget)
		.OnSelectItem(this, &SGoToSymbolMenu::SelectItem)
		.WidgetSize(GetWidgetSize())
		.MenuTitle(FString("Go To Symbol"))
	];
}

void SGoToSymbolMenu::InitListItems(TArray<TSharedPtr<FGoToSymbolStruct>>& Items)
{
	UBlueprint* Blueprint = FBAUtils::GetAssetForActiveTab<UBlueprint>();
	check(Blueprint)

	TArray<UEdGraph*> BlueprintGraphs;
	Blueprint->GetAllGraphs(BlueprintGraphs);

	Items.Empty();

	for (UEdGraph* Graph : BlueprintGraphs)
	{
		const EGraphType GraphType = FBAUtils::GetGraphType(Graph);

		if (Blueprint->DelegateSignatureGraphs.Contains(Graph))
		{
			continue;
		}

		// add all event nodes on the graph for ubergraphs
		if (GraphType == GT_Ubergraph)
		{
			TArray<UEdGraphNode*> EventNodes;
			Graph->GetNodesOfClass(EventNodes);

			EventNodes = EventNodes.FilterByPredicate(
				[](UEdGraphNode* Node)
				{
					return Cast<UK2Node_Event>(Node) || FBAUtils::IsInputNode(Node);
				});

			for (UEdGraphNode* Node : EventNodes)
			{
				Items.Add(MakeShareable(new FGoToSymbolStruct(Node, Graph)));
			}
		}

		// add the graph itself
		Items.Add(MakeShareable(new FGoToSymbolStruct(nullptr, Graph)));
	}
}

TSharedRef<ITableRow> SGoToSymbolMenu::CreateItemWidget(TSharedPtr<FGoToSymbolStruct> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	FLinearColor IconColor = FLinearColor::White;

	const FSlateBrush* ContextIcon
		= Item->EventNode != nullptr
		? Item->EventNode->GetIconAndTint(IconColor).GetIcon()
		: FBlueprintEditor::GetGlyphForGraph(Item->Graph);

	FString ItemDetails = Item->GetTypeDescription();
	if (Item->EventNode)
	{
		ItemDetails += " | ";
		ItemDetails += FBAUtils::GetGraphName(Item->Graph);
	}

	return
		SNew(STableRow<TSharedPtr<FString>>, OwnerTable).Padding(FMargin(2.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, ContextIcon != nullptr ? 5 : 0, 0)
			[
				SNew(SImage).ColorAndOpacity(IconColor).Image(ContextIcon)
			]
			+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Left).FillWidth(1)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->ToString()))
				]
				+ SVerticalBox::Slot()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.AssetListViewClassFont"))
					.Text(FText::FromString(ItemDetails))
				]
			]
		];
}

void SGoToSymbolMenu::SelectItem(TSharedPtr<FGoToSymbolStruct> Item)
{
	const EGraphType GraphType = FBAUtils::GetGraphType(Item->Graph);

	if (GraphType == GT_Ubergraph)
	{
		// if we selected an event node
		if (Item->EventNode != nullptr)
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Item->EventNode, false);
			return; // we should have focused the node, exit here
		}
	}

	// open the graph
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Item->Graph, false);
}

/*****************/
/* FSymbolStruct */
/*****************/
FString FGoToSymbolStruct::ToString() const
{
	if (EventNode)
	{
		return FBAUtils::GetNodeName(EventNode);
	}

	return FBAUtils::GetGraphName(Graph);
}

FString FGoToSymbolStruct::GetSearchText() const
{
	FString SearchText = FBAUtils::GetGraphName(Graph) + GetTypeDescription();
	if (EventNode)
	{
		SearchText += FBAUtils::GetNodeName(EventNode);
	}

	return SearchText;
}

FString FGoToSymbolStruct::GetTypeDescription() const
{
	return EventNode != nullptr
		? FString("Event")
		: FBAUtils::GraphTypeToString(FBAUtils::GetGraphType(Graph));
}
