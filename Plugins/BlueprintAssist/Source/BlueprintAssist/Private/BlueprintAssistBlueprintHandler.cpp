// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistBlueprintHandler.h"

#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistSettings.h"
#include "BlueprintAssistUtils.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"
#include "SGraphActionMenu.h"
#include "K2Node_Knot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/MessageLog.h"

const FName& FBAVariableDescription::GetMetaData(FName Key) const
{
	int32 EntryIndex = FindMetaDataEntryIndexForKey(Key);
	check(EntryIndex != INDEX_NONE);
	return MetaDataArray[EntryIndex].DataValue;
}

bool FBAVariableDescription::HasMetaData(FName Key) const
{
	return FindMetaDataEntryIndexForKey(Key) != INDEX_NONE;
}

int32 FBAVariableDescription::FindMetaDataEntryIndexForKey(FName Key) const
{
	for (int32 i = 0; i < MetaDataArray.Num(); i++)
	{
		if (MetaDataArray[i].DataKey == Key)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FBABlueprintHandler::~FBABlueprintHandler()
{
	if (BlueprintPtr.IsValid())
	{
		BlueprintPtr->OnChanged().RemoveAll(this);
		BlueprintPtr->OnCompiled().RemoveAll(this);
	}

	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
}

void FBABlueprintHandler::BindBlueprintChanged(UBlueprint* Blueprint)
{
	BlueprintPtr = Blueprint;
	SetLastVariables(Blueprint);
	SetLastFunctionGraphs(Blueprint);
	bProcessedChangesThisFrame = false;
	bActive = true;

	Blueprint->OnChanged().RemoveAll(this);
	Blueprint->OnChanged().AddRaw(this, &FBABlueprintHandler::OnBlueprintChanged);

	Blueprint->OnCompiled().RemoveAll(this);
	Blueprint->OnCompiled().AddRaw(this, &FBABlueprintHandler::OnBlueprintCompiled);

	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
		GEditor->OnObjectsReplaced().AddRaw(this, &FBABlueprintHandler::OnObjectsReplaced);
	}
}

void FBABlueprintHandler::UnbindBlueprintChanged(UBlueprint* Blueprint)
{
	LastVariables.Empty();
	bProcessedChangesThisFrame = false;
	bActive = false;

	if (BlueprintPtr.IsValid() && BlueprintPtr->IsValidLowLevelFast())
	{
		BlueprintPtr->OnChanged().RemoveAll(this);
		BlueprintPtr->OnCompiled().RemoveAll(this);
	}

	Blueprint->OnChanged().RemoveAll(this);
	Blueprint->OnCompiled().RemoveAll(this);
}

void FBABlueprintHandler::SetLastVariables(UBlueprint* Blueprint)
{
	LastVariables.Empty(Blueprint->NewVariables.Num());

	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		// Only copy the required values
		LastVariables.Add(FBAVariableDescription(Variable));
	}
}

void FBABlueprintHandler::SetLastFunctionGraphs(UBlueprint* Blueprint)
{
	LastFunctionGraphs.Reset(Blueprint->FunctionGraphs.Num());
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		LastFunctionGraphs.Add(Graph);
	}
}

// See UControlRigBlueprint::OnPostVariableChange
void FBABlueprintHandler::OnBlueprintChanged(UBlueprint* Blueprint)
{
	if (Blueprint != BlueprintPtr)
	{
		UE_LOG(LogBlueprintAssist, Warning, TEXT("Blueprint was changed but it's the wrong blueprint?"));
		return;
	}

	if (!bActive)
	{
		return;
	}

	if (bProcessedChangesThisFrame)
	{
		return;
	}

	bProcessedChangesThisFrame = true;
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateRaw(this, &FBABlueprintHandler::ResetProcessedChangesThisFrame));

	if (Blueprint->IsPendingKill())
	{
		UE_LOG(LogBlueprintAssist, Warning, TEXT("Blueprint was changed while PendingKill, please report this on github!"));
		return;
	}

	// This shouldn't happen!
	check(Blueprint->IsValidLowLevelFast(false));

	TMap<FGuid, int32> OldVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < LastVariables.Num(); VarIndex++)
	{
		OldVariablesByGuid.Add(LastVariables[VarIndex].VarGuid, VarIndex);
	}

	for (FBPVariableDescription& BPNewVariable : Blueprint->NewVariables)
	{
		FBAVariableDescription NewVariable(BPNewVariable);
		if (!OldVariablesByGuid.Contains(NewVariable.VarGuid))
		{
			OnVariableAdded(Blueprint, NewVariable);
			continue;
		}

		const int32 OldVarIndex = OldVariablesByGuid.FindChecked(NewVariable.VarGuid);
		const FBAVariableDescription& OldVariable = LastVariables[OldVarIndex];

		// Make set instance editable to true when you set expose on spawn to true
		if (FBAUtils::HasMetaDataChanged(OldVariable, NewVariable, FBlueprintMetadata::MD_ExposeOnSpawn))
		{
			if (NewVariable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) && NewVariable.GetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) == TEXT("true"))
			{
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, NewVariable.VarName, false);
			}
		}

		// Check if a variable has been renamed (use string cause names are not case-sensitive!)
		if (!OldVariable.VarName.ToString().Equals(NewVariable.VarName.ToString()))
		{
			OnVariableRenamed(Blueprint, OldVariable, NewVariable);
		}

		// Check if a variable type has changed
		if (OldVariable.PinCategory != NewVariable.PinCategory || OldVariable.ContainerType != NewVariable.ContainerType)
		{
			OnVariableTypeChanged(Blueprint, OldVariable, NewVariable);
		}
	}

	SetLastVariables(Blueprint);

	for (UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
	{
		// This means we created a new function?
		if (!LastFunctionGraphs.Contains(FunctionGraph))
		{
			OnFunctionAdded(Blueprint, FunctionGraph);
		}
	}

	SetLastFunctionGraphs(Blueprint);
}

void FBABlueprintHandler::ResetProcessedChangesThisFrame()
{
	bProcessedChangesThisFrame = false;
}

void FBABlueprintHandler::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	if (BlueprintPtr.IsValid())
	{
		if (UObject* Replacement = ReplacementMap.FindRef(BlueprintPtr.Get()))
		{
			UE_LOG(LogBlueprintAssist, Warning, TEXT("Blueprint was replaced with %s"), *Replacement->GetName());
			UnbindBlueprintChanged(BlueprintPtr.Get());

			if (UBlueprint* NewBlueprint = Cast<UBlueprint>(Replacement))
			{
				BindBlueprintChanged(NewBlueprint);
			}
			else
			{
				BlueprintPtr = nullptr;
			}
		}
	}
}

void FBABlueprintHandler::OnVariableAdded(UBlueprint* Blueprint, FBAVariableDescription& Variable)
{
	const UBASettings* BASettings = GetDefault<UBASettings>();
	if (BASettings->bEnableVariableDefaults)
	{
		if (BASettings->bDefaultVariableInstanceEditable)
		{
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, Variable.VarName, false);
		}

		if (BASettings->bDefaultVariableBlueprintReadOnly)
		{
			FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, Variable.VarName, true);
		}

		if (BASettings->bDefaultVariableExposeOnSpawn)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
		}

		if (BASettings->bDefaultVariablePrivate)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, FBlueprintMetadata::MD_Private, TEXT("true"));
		}

		if (BASettings->bDefaultVariableExposeToCinematics)
		{
			FBlueprintEditorUtils::SetInterpFlag(Blueprint, Variable.VarName, true);
		}

		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Variable.VarName, nullptr, BASettings->DefaultVariableCategory);

		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, FBlueprintMetadata::MD_Tooltip, BASettings->DefaultVariableTooltip.ToString());
	}
}

void FBABlueprintHandler::OnVariableRenamed(UBlueprint* Blueprint, const FBAVariableDescription& OldVariable, FBAVariableDescription& NewVariable)
{
	if (GetDefault<UBASettings>()->bAutoRenameGettersAndSetters)
	{
		RenameGettersAndSetters(Blueprint, OldVariable, NewVariable);
	}
}

void FBABlueprintHandler::OnVariableTypeChanged(UBlueprint* Blueprint, const FBAVariableDescription& OldVariable, FBAVariableDescription& NewVariable)
{
	// Boolean variables may need to be renamed as well!
	if (GetDefault<UBASettings>()->bAutoRenameGettersAndSetters)
	{
		RenameGettersAndSetters(Blueprint, OldVariable, NewVariable);
	}
}

void FBABlueprintHandler::RenameGettersAndSetters(UBlueprint* Blueprint, const FBAVariableDescription& OldVariable, FBAVariableDescription& NewVariable)
{
	const FString OldVariableName = FBAUtils::GetVariableName(OldVariable.VarName.ToString(), OldVariable.PinCategory, OldVariable.ContainerType);
	const FString NewVariableName = FBAUtils::GetVariableName(NewVariable.VarName.ToString(), NewVariable.PinCategory, NewVariable.ContainerType);

	// Do nothing if our names didn't change
	if (OldVariableName == NewVariableName)
	{
		return;
	}

	const FString GetterName = FString::Printf(TEXT("Get%s"), *OldVariableName);
	const FString SetterName = FString::Printf(TEXT("Set%s"), *OldVariableName);

	const FString NewGetterName = FString::Printf(TEXT("Get%s"), *NewVariableName);
	const FString NewSetterName = FString::Printf(TEXT("Set%s"), *NewVariableName);

	for (UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
	{
		if (FunctionGraph->GetName() == GetterName)
		{
			FBlueprintEditorUtils::RenameGraph(FunctionGraph, NewGetterName);
		}
		else if (FunctionGraph->GetName() == SetterName)
		{
			FBlueprintEditorUtils::RenameGraph(FunctionGraph, NewSetterName);
		}
	}
}

void FBABlueprintHandler::OnFunctionAdded(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
	const UBASettings* BASettings = GetDefault<UBASettings>();
	if (!BASettings->bEnableFunctionDefaults)
	{
		return;
	}

	UK2Node_EditablePinBase* FunctionEntryNode = nullptr;
	TArray<UK2Node_FunctionEntry*> EntryNodes;
	FunctionGraph->GetNodesOfClass(EntryNodes);

	if ((EntryNodes.Num() > 0) && EntryNodes[0]->IsEditable())
	{
		FunctionEntryNode = EntryNodes[0];
	}
	else
	{
		return;
	}

	EFunctionFlags AccessSpecifier = FUNC_Public;
	switch (BASettings->DefaultFunctionAccessSpecifier)
	{
		case EBAFunctionAccessSpecifier::Public:
			AccessSpecifier = FUNC_Public;
			break;
		case EBAFunctionAccessSpecifier::Protected:
			AccessSpecifier = FUNC_Protected;
			break;
		case EBAFunctionAccessSpecifier::Private:
			AccessSpecifier = FUNC_Private;
			break;
	}

	if (FunctionEntryNode)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "ChangeAccessSpecifier", "Change Access Specifier"));
		FunctionEntryNode->Modify();
		UFunction* Function = FindFunctionFromEntryNode(FunctionEntryNode, FunctionGraph);
		if (Function)
		{
			Function->Modify();
		}

		const EFunctionFlags ClearAccessSpecifierMask = ~FUNC_AccessSpecifiers;
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(FunctionEntryNode))
		{
			int32 ExtraFlags = EntryNode->GetExtraFlags();
			ExtraFlags &= ClearAccessSpecifierMask;
			ExtraFlags |= AccessSpecifier;
			EntryNode->SetExtraFlags(ExtraFlags);

			// Set const
			if (BASettings->bDefaultFunctionConst)
			{
				EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() ^ FUNC_Const);
			}

			// Set exec
			if (BASettings->bDefaultFunctionExec)
			{
				EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() ^ FUNC_Exec);
			}

			// Set pure
			if (BASettings->bDefaultFunctionPure)
			{
				EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() ^ FUNC_BlueprintPure);
			}
		}
		else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(FunctionEntryNode))
		{
			EventNode->FunctionFlags &= ClearAccessSpecifierMask;
			EventNode->FunctionFlags |= AccessSpecifier;
		}

		if (Function)
		{
			Function->FunctionFlags &= ClearAccessSpecifierMask;
			Function->FunctionFlags |= AccessSpecifier;
		}

		if (FKismetUserDeclaredFunctionMetadata* Metadata = GetMetadataBlock(FunctionEntryNode))
		{
			// Set default keywords
			const FText& DefaultKeywords = BASettings->DefaultFunctionKeywords;
			// Remove excess whitespace and prevent keywords with just spaces
			const FText& Keywords = FText::TrimPrecedingAndTrailing(DefaultKeywords);
			if (!Keywords.EqualTo(Metadata->Keywords))
			{
				Metadata->Keywords = Keywords;
				Function->SetMetaData(FBlueprintMetadata::MD_FunctionKeywords, *Keywords.ToString());
			}

			// Set default tooltip
			const FText& DefaultDescription = BASettings->DefaultFunctionTooltip;
			Metadata->ToolTip = DefaultDescription;
			Function->SetMetaData(FBlueprintMetadata::MD_Tooltip, *DefaultDescription.ToString());

			// Set default category
			const FText& DefaultFunctionCategory = BASettings->DefaultFunctionCategory;
			Metadata->Category = DefaultFunctionCategory;
			if (Function)
			{
				check(!Function->IsNative()); // Should never get here with a native function, as we wouldn't have been able to find metadata for it
				Function->Modify();
				Function->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *DefaultFunctionCategory.ToString());
			}
			
			// Refresh category in editor? See FBlueprintGraphActionDetails::OnCategoryTextCommitted | SMyBlueprint::Refresh
			FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);
			if (TSharedPtr<SGraphActionMenu> GraphActionMenu = FBAUtils::GetGraphActionMenu())
			{
				GraphActionMenu->RefreshAllActions(true);
			}
		}

		// Refresh the node after editing properties (from FBaseBlueprintGraphActionDetails::OnParamsChanged)
		{
			const bool bCurDisableOrphanSaving = FunctionEntryNode->bDisableOrphanPinSaving;
			FunctionEntryNode->bDisableOrphanPinSaving = true;

			FunctionEntryNode->ReconstructNode();

			FunctionEntryNode->bDisableOrphanPinSaving = bCurDisableOrphanSaving;
		}
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->HandleParameterDefaultValueChanged(FunctionEntryNode);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
}

UFunction* FBABlueprintHandler::FindFunctionFromEntryNode(UK2Node_EditablePinBase* FunctionEntry, UEdGraph* Graph)
{
	if (UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(FunctionEntry))
	{
		return FFunctionFromNodeHelper::FunctionFromNode(EventNode);
	}
	else if (Graph)
	{
		if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
		{
			UClass* Class = Blueprint->SkeletonGeneratedClass;

			for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
			{
				UFunction* Function = *FunctionIt;
				if (Function->GetName() == Graph->GetName())
				{
					return Function;
				}
			}
		}
	}
	return nullptr;
}

FKismetUserDeclaredFunctionMetadata* FBABlueprintHandler::GetMetadataBlock(UK2Node_EditablePinBase* FunctionEntryNode) const
{
	if (UK2Node_FunctionEntry* TypedEntryNode = Cast<UK2Node_FunctionEntry>(FunctionEntryNode))
	{
		return &(TypedEntryNode->MetaData);
	}
	else if (UK2Node_Tunnel* TunnelNode = ExactCast<UK2Node_Tunnel>(FunctionEntryNode))
	{
		// Must be exactly a tunnel, not a macro instance
		return &(TunnelNode->MetaData);
	}
	else if (UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(FunctionEntryNode))
	{
		return &(EventNode->GetUserDefinedMetaData());
	}

	return nullptr;
}

void FBABlueprintHandler::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint))
	{
		return;
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		DetectGraphIssues(Graph);
	}
}

void FBABlueprintHandler::DetectGraphIssues(UEdGraph* Graph)
{
	if (!IsValid(Graph))
	{
		return;
	}

	struct FLocal
	{
		static void FocusNode(UEdGraphNode* Node)
		{
			if (Node)
			{
				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node, false);
			}
		}
	};

	FMessageLog BlueprintAssistLog("BlueprintAssist");

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (FMath::Abs(Node->NodePosX) > 100000 || FMath::Abs(Node->NodePosY) > 100000)
		{
			TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Info);
			const FText MessageText = FText::FromString(FString::Printf(TEXT("Node %s is extremely far away"), *Node->NodeGuid.ToString()));
			Message->AddToken(FTextToken::Create(MessageText));
			Message->AddToken(FActionToken::Create(
				FText::FromString("GoTo"),
				FText::FromString("Go to node"),
				FOnActionTokenExecuted::CreateStatic(&FLocal::FocusNode, Node)));

			BlueprintAssistLog.AddMessage(Message);
		}
		
		// Detect bad knot nodes
		if (UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(Node))
		{
			// Detect empty knot nodes to be deleted
			if (FBAUtils::GetLinkedPins(KnotNode).Num() == 0)
			{
				TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Info);
				const FText MessageText = FText::FromString(FString::Printf(TEXT("Unlinked reroute node %s"), *KnotNode->NodeGuid.ToString()));
				Message->AddToken(FTextToken::Create(MessageText));
				Message->AddToken(FActionToken::Create(
					FText::FromString("GoTo"),
					FText::FromString("Go to node"),
					FOnActionTokenExecuted::CreateStatic(&FLocal::FocusNode, Cast<UEdGraphNode>(KnotNode))));

				BlueprintAssistLog.AddMessage(Message);
			}
			else
			{
				bool bOpenMessageLog = false;

				// Detect badly linked exec knot nodes
				for (UEdGraphPin* Pin : FBAUtils::GetLinkedPins(KnotNode, EGPD_Output).FilterByPredicate(FBAUtils::IsExecPin))
				{
					if (Pin->LinkedTo.Num() > 1)
					{
						TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error);
						const FText MessageText = FText::FromString(FString::Printf(TEXT("Badly linked reroute node (manually delete and remake this node) %s"), *KnotNode->NodeGuid.ToString()));
						Message->AddToken(FTextToken::Create(MessageText));
						Message->AddToken(FActionToken::Create(
							FText::FromString("GoTo"),
							FText::FromString("Go to node"),
							FOnActionTokenExecuted::CreateStatic(&FLocal::FocusNode, Cast<UEdGraphNode>(KnotNode))));

						BlueprintAssistLog.AddMessage(Message);

						bOpenMessageLog = true;
					}
				}

				if (bOpenMessageLog)
				{
					BlueprintAssistLog.Open();
				}
			}
		}
	}
}
