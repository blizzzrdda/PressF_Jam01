// Copyright 2021 fpwong. All Rights Reserved.
#include "VariableSelectorMenu.h"

#include "BlueprintAssistUtils.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorSettings.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EditorCategoryUtils.h"
#include "GraphEditorSettings.h"
#include "K2Node_FunctionEntry.h"
#include "SKismetInspector.h"
#include "Editor/GraphEditor/Public/SGraphActionMenu.h"
#include "Editor/Kismet/Public/SSCSEditor.h"
#include "Editor/PropertyEditor/Public/PropertyPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Runtime/Slate/Public/Widgets/Text/SRichTextBlock.h"
#include "Runtime/SlateCore/Public/Types/SlateEnums.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#if ENGINE_MINOR_VERSION >= 26 || ENGINE_MAJOR_VERSION >= 5
#include "Widgets/Images/SLayeredImage.h"
#else
#include "Widgets/Images/SImage.h"
#endif

void SVariableSelectorMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBAFilteredList<TSharedPtr<FVariableSelectorStruct>>)
		.InitListItems(this, &SVariableSelectorMenu::InitListItems)
		.OnGenerateRow(this, &SVariableSelectorMenu::CreateItemWidget)
		.OnSelectItem(this, &SVariableSelectorMenu::SelectItem)
		.OnMarkActiveSuggestion(this, &SVariableSelectorMenu::MarkActiveSuggestion)
		.WidgetSize(GetWidgetSize())
		.MenuTitle(FString("Select Variable"))
	];
}

// Reference: SMyBlueprint::CollectAllActions
void SVariableSelectorMenu::InitListItems(TArray<TSharedPtr<FVariableSelectorStruct>>& Items)
{
	TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();

	// Grab action menu
	TSharedPtr<SWidget> ActionMenuWidget = FBAUtils::GetChildWidget(Window, "SGraphActionMenu");
	ActionMenu = StaticCastSharedPtr<SGraphActionMenu>(ActionMenuWidget);

	FBlueprintEditor* BPEditor = FBAUtils::GetEditorFromActiveTabCasted<UBlueprint, FBlueprintEditor>();
	if (!BPEditor)
	{
		return;
	}

	// Grab SCSNodes
	struct FLocal
	{
		static void ReadNodes(TArray<TSharedPtr<FVariableSelectorStruct>>& OutItems, FSCSEditorTreeNodePtrType CurrentNode)
		{
			OutItems.Add(MakeShareable(new FVariableSelectorStruct(CurrentNode)));

			for (auto Child : CurrentNode->GetChildren())
			{
				ReadNodes(OutItems, Child);
			}
		}
	};

	SCSEditor = BPEditor->GetSCSEditor();
	if (SCSEditor.IsValid())
	{
		FSCSEditorTreeNodePtrType RootNode = SCSEditor->GetSceneRootNode();
		if (RootNode.IsValid())
		{
			FLocal::ReadNodes(Items, RootNode);
		}
	}

	// Grab Variables
	UEdGraph* FocusedEdGraph = BPEditor->GetFocusedGraph();
	UBlueprint* BlueprintObj = BPEditor->GetBlueprintObj();

	const EFieldIteratorFlags::SuperClassFlags FieldIteratorSuperFlag = GetDefault<UBlueprintEditorSettings>()->bShowInheritedVariables
		? EFieldIteratorFlags::IncludeSuper
		: EFieldIteratorFlags::ExcludeSuper;

	for (TFieldIterator<BA_PROPERTY> PropertyIt(BlueprintObj->SkeletonGeneratedClass, FieldIteratorSuperFlag); PropertyIt; ++PropertyIt)
	{
		BA_PROPERTY* Property = *PropertyIt;

		// Don't show delegate & components, there is special handling for these
		if (ShouldSkipProperty(Property))
		{
			continue;
		}

		Items.Add(MakeShareable(new FVariableSelectorStruct(Property)));
	}

	// We want to pull local variables from the top level function graphs
	UEdGraph* TopLevelGraph = FBlueprintEditorUtils::GetTopLevelGraph(FocusedEdGraph);
	if (TopLevelGraph)
	{
		TArray<UK2Node_FunctionEntry*> FunctionEntryNodes;
		TopLevelGraph->GetNodesOfClass<UK2Node_FunctionEntry>(FunctionEntryNodes);

		// Search in all FunctionEntry nodes for their local variables
		for (UK2Node_FunctionEntry* const FunctionEntry : FunctionEntryNodes)
		{
			for (const FBPVariableDescription& Variable : FunctionEntry->LocalVariables)
			{
				UFunction* Func = BA_FIND_FIELD<UFunction>(BlueprintObj->SkeletonGeneratedClass, TopLevelGraph->GetFName());
				if (Func)
				{
					if (BA_PROPERTY* Property = BA_FIND_PROPERTY<BA_PROPERTY>(Func, Variable.VarName))
					{
						Items.Add(MakeShareable(new FVariableSelectorStruct(Property)));
					}
				}
			}
		}
	}
}

TSharedRef<ITableRow> SVariableSelectorMenu::CreateItemWidget(TSharedPtr<FVariableSelectorStruct> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	FSlateColor PrimaryColor;
	FSlateColor SecondaryColor;
	const FSlateBrush* PrimaryIcon = FEditorStyle::GetBrush(TEXT("Kismet.AllClasses.VariableIcon"));
	const FSlateBrush* SecondaryIcon = nullptr;

	if (Item->bSCSNode)
	{
		if (UActorComponent* ComponentTemplate = Item->SCSNode->GetComponentTemplate())
		{
			PrimaryIcon = FSlateIconFinder::FindIconBrushForClass(ComponentTemplate->GetClass(), TEXT("SCS.Component"));
		}

		PrimaryColor = GetDefault<UGraphEditorSettings>()->ObjectPinTypeColor;
	}
	else
	{
#if ENGINE_MINOR_VERSION >= 26 || ENGINE_MAJOR_VERSION >= 5
		PrimaryIcon = FBlueprintEditor::GetVarIconAndColorFromProperty(Item->Property, PrimaryColor, SecondaryIcon, SecondaryColor);
#else
		if (Item->Property)
		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

			FEdGraphPinType PinType;
			if (K2Schema->ConvertPropertyToPinType(Item->Property, PinType)) // use schema to get the color
			{
				PrimaryColor = K2Schema->GetPinTypeColor(PinType);
				SecondaryColor = K2Schema->GetSecondaryPinTypeColor(PinType);

				PrimaryIcon = FBlueprintEditorUtils::GetIconFromPin(PinType);
				SecondaryIcon = FBlueprintEditorUtils::GetSecondaryIconFromPin(PinType);
			}
		}
#endif
	}

	return
		SNew(STableRow<TSharedPtr<FString>>, OwnerTable).Padding(FMargin(2.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.0f, 0.0f, 4.0f, 0.0f))
			[
#if ENGINE_MINOR_VERSION >= 26 || ENGINE_MAJOR_VERSION >= 5
				SNew(SLayeredImage, SecondaryIcon, SecondaryColor)
				.Image(PrimaryIcon)
				.ColorAndOpacity(PrimaryColor)
#else
				SNew(SImage)
				.Image(PrimaryIcon)
				.ColorAndOpacity(PrimaryColor)
#endif
			]
			+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Left).VAlign(VAlign_Center).FillWidth(1)
			[
				SNew(STextBlock).Text(FText::FromString(Item->ToString()))
			]
		];
}

// See SMyBlueprint::OnActionSelectedHelper
void SVariableSelectorMenu::SelectItem(TSharedPtr<FVariableSelectorStruct> Item)
{
	FBlueprintEditor* BPEditor = FBAUtils::GetEditorFromActiveTabCasted<UBlueprint, FBlueprintEditor>();
	if (!BPEditor)
	{
		return;
	}

	TSharedRef<SKismetInspector> Inspector = BPEditor->GetInspector();

	if (Item->bSCSNode)
	{
		UBlueprint* BlueprintObj = BPEditor->GetBlueprintObj();
		TSharedPtr<FSCSEditorTreeNode> NodePtr = Item->SCSNode;
		if (NodePtr.IsValid())
		{
			if (FBAUtils::IsSCSActorNode(NodePtr))
			{
				if (AActor* DefaultActor = FBAUtils::GetSCSNodeDefaultActor(NodePtr, BlueprintObj))
				{
					auto InspectorTitle = FText::FromString(DefaultActor->GetName());
					SKismetInspector::FShowDetailsOptions Options(InspectorTitle, true);
					Options.bShowComponents = false;
					Inspector->ShowDetailsForSingleObject(DefaultActor, Options);
				}
			}
			else
			{
				UActorComponent* EditableComponent = NodePtr->GetOrCreateEditableComponentTemplate(BlueprintObj);
				if (EditableComponent)
				{
					auto InspectorTitle = FText::FromString(NodePtr->GetDisplayString());
					SKismetInspector::FShowDetailsOptions Options(InspectorTitle, true);
					Options.bShowComponents = true;
					Inspector->ShowDetailsForSingleObject(EditableComponent, Options);
				}
			}

			SCSEditor->HighlightTreeNode(FName(*NodePtr->GetDisplayString()), FPropertyPath());
		}
	}
	else
	{
		SKismetInspector::FShowDetailsOptions Options(FText::FromName(Item->Property->GetFName()));
#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION >= 5
		Inspector->ShowDetailsForSingleObject(Item->Property->GetUPropertyWrapper(), Options);
#else
		Inspector->ShowDetailsForSingleObject(Item->Property, Options);
#endif

		if (ActionMenu.IsValid())
		{
			ActionMenu->SelectItemByName(Item->Property->GetFName(), ESelectInfo::OnKeyPress);
		}
	}
}

void SVariableSelectorMenu::MarkActiveSuggestion(TSharedPtr<FVariableSelectorStruct> Item)
{
	SelectItem(Item);
}

bool SVariableSelectorMenu::ShouldSkipProperty(BA_PROPERTY* Property)
{
#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION >= 5
	const bool bMulticastDelegateProp = Property->IsA(FMulticastDelegateProperty::StaticClass());
	const bool bDelegateProp = (Property->IsA(FDelegateProperty::StaticClass()) || bMulticastDelegateProp);
	const bool bShouldShowAsVar = (!Property->HasAnyPropertyFlags(CPF_Parm) && Property->HasAllPropertyFlags(CPF_BlueprintVisible)) && !bDelegateProp;
	const bool bShouldShowAsDelegate = !Property->HasAnyPropertyFlags(CPF_Parm) && bMulticastDelegateProp
		&& Property->HasAnyPropertyFlags(CPF_BlueprintAssignable | CPF_BlueprintCallable);
	FObjectPropertyBase* Obj = CastField<FObjectPropertyBase>(Property);
	const bool bComponentProperty = Obj && Obj->PropertyClass ? Obj->PropertyClass->IsChildOf<UActorComponent>() : false;
	return bShouldShowAsDelegate || bComponentProperty || !bShouldShowAsVar;
#else
	const bool bMulticastDelegateProp = Property->IsA(UMulticastDelegateProperty::StaticClass());
	const bool bDelegateProp = (Property->IsA(UDelegateProperty::StaticClass()) || bMulticastDelegateProp);
	const bool bShouldShowAsVar = (!Property->HasAnyPropertyFlags(CPF_Parm) && Property->HasAllPropertyFlags(CPF_BlueprintVisible)) && !bDelegateProp;
	const bool bShouldShowAsDelegate = !Property->HasAnyPropertyFlags(CPF_Parm) && bMulticastDelegateProp 
		&& Property->HasAnyPropertyFlags(CPF_BlueprintAssignable | CPF_BlueprintCallable);
	UObjectPropertyBase* Obj = Cast<UObjectPropertyBase>(Property);
	const bool bComponentProperty = Obj && Obj->PropertyClass ? Obj->PropertyClass->IsChildOf<UActorComponent>() : false;
	return bShouldShowAsDelegate || bComponentProperty || !bShouldShowAsVar;
#endif
}

/***************************/
/* FVariableSelectorStruct */
/***************************/

FVariableSelectorStruct::FVariableSelectorStruct(BA_PROPERTY* InProperty)
{
	bSCSNode = false;
	Property = InProperty;
	DisplayName = Property->GetName();
}

FVariableSelectorStruct::FVariableSelectorStruct(TSharedPtr<FSCSEditorTreeNode> InNode)
{
	SCSNode = InNode;
	bSCSNode = true;

	FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(FBAUtils::GetEditorFromActiveTab());
	check(BPEditor);
	UBlueprint* BlueprintObj = BPEditor->GetBlueprintObj();
	check(BlueprintObj);
	if (FBAUtils::IsSCSActorNode(SCSNode))
	{
		if (AActor* DefaultActor = FBAUtils::GetSCSNodeDefaultActor(SCSNode, BlueprintObj))
		{
			DisplayName = DefaultActor->GetName();
		}
	}
	else
	{
		UActorComponent* EditableComponent = SCSNode->GetOrCreateEditableComponentTemplate(BlueprintObj);
		if (EditableComponent)
		{
			DisplayName = SCSNode->GetDisplayString();
		}
	}
}

FString FVariableSelectorStruct::ToString() const
{
	return DisplayName;
}
