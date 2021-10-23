// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "UObject/Object.h"

#include "BABlueprintHandlerObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTASSIST_API UBABlueprintHandlerObject : public UObject
{
	GENERATED_BODY()

public:
	~UBABlueprintHandlerObject();

	void BindBlueprintChanged(UBlueprint* Blueprint);

	void UnbindBlueprintChanged(UBlueprint* Blueprint);

	void SetLastVariables(UBlueprint* Blueprint);

	void OnBlueprintChanged(UBlueprint* Blueprint);

	void ResetProcessedChangesThisFrame();

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	void OnVariableAdded(UBlueprint* Blueprint, FBPVariableDescription& Variable);

	void OnVariableRenamed(UBlueprint* Blueprint, const FBPVariableDescription& OldVariable, FBPVariableDescription& NewVariable);

	void OnVariableTypeChanged(UBlueprint* Blueprint, const FBPVariableDescription& OldVariable, FBPVariableDescription& NewVariable);

	void RenameGettersAndSetters(UBlueprint* Blueprint, const FBPVariableDescription& OldVariable, FBPVariableDescription& NewVariable);

	void OnBlueprintCompiled(UBlueprint* Blueprint);

	void DetectGraphIssues(UEdGraph* Graph);

private:
	TWeakObjectPtr<UBlueprint> BlueprintPtr;

	UPROPERTY()
	TArray<FBPVariableDescription> LastVariables;

	bool bProcessedChangesThisFrame = false;

	bool bActive = false;
};
