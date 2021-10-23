// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

// TODO: make this a UObject so we don't need to create these structs
struct FBAVariableMetaDataEntry
{
	FName DataKey;
	FName DataValue;

	FBAVariableMetaDataEntry(const FBPVariableMetaDataEntry& MetaDataEntry)
		: DataKey(MetaDataEntry.DataKey)
	{
		DataValue = FName(*MetaDataEntry.DataValue);
	}
};

struct FBAVariableDescription
{
	FName VarName;
	FGuid VarGuid;

	FName PinCategory;
	EPinContainerType ContainerType;

	TArray<FBAVariableMetaDataEntry> MetaDataArray;

	FBAVariableDescription(const FBPVariableDescription& VariableDescription)
	{
		VarName = VariableDescription.VarName;
		VarGuid = VariableDescription.VarGuid;
		PinCategory = VariableDescription.VarType.PinCategory;
		ContainerType = VariableDescription.VarType.ContainerType;

		for (const auto& Entry : VariableDescription.MetaDataArray)
		{
			MetaDataArray.Add(FBAVariableMetaDataEntry(Entry));
		}
	}

	const FName& GetMetaData(FName Key) const;
	bool HasMetaData(FName Key) const;
	int32 FindMetaDataEntryIndexForKey(FName Key) const;
};

class FBABlueprintHandler
{
public:
	~FBABlueprintHandler();

	void BindBlueprintChanged(UBlueprint* Blueprint);

	void UnbindBlueprintChanged(UBlueprint* Blueprint);

	void SetLastVariables(UBlueprint* Blueprint);

	void SetLastFunctionGraphs(UBlueprint* Blueprint);

	void OnBlueprintChanged(UBlueprint* Blueprint);

	void ResetProcessedChangesThisFrame();

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	void OnVariableAdded(UBlueprint* Blueprint, FBAVariableDescription& Variable);

	void OnVariableRenamed(UBlueprint* Blueprint, const FBAVariableDescription& OldVariable, FBAVariableDescription& NewVariable);

	void OnVariableTypeChanged(UBlueprint* Blueprint, const FBAVariableDescription& OldVariable, FBAVariableDescription& NewVariable);

	void RenameGettersAndSetters(UBlueprint* Blueprint, const FBAVariableDescription& OldVariable, FBAVariableDescription& NewVariable);

	void OnFunctionAdded(UBlueprint* Blueprint, UEdGraph* FunctionGraph);

	UFunction* FindFunctionFromEntryNode(UK2Node_EditablePinBase* FunctionEntry, UEdGraph* Graph);

	FKismetUserDeclaredFunctionMetadata* GetMetadataBlock(UK2Node_EditablePinBase* FunctionEntryNode) const;

	void OnBlueprintCompiled(UBlueprint* Blueprint);

	void DetectGraphIssues(UEdGraph* Graph);

private:
	TWeakObjectPtr<UBlueprint> BlueprintPtr;

	TArray<FBAVariableDescription> LastVariables;

	TArray<TWeakObjectPtr<UEdGraph>> LastFunctionGraphs;

	bool bProcessedChangesThisFrame = false;

	bool bActive = false;
};
