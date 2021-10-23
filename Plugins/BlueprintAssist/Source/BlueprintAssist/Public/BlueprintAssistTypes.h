// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION >= 5
#define BA_PROPERTY FProperty
#define BA_FIND_FIELD FindUField
#define BA_FIND_PROPERTY FindFProperty
#define BA_WEAK_FIELD_PTR TWeakFieldPtr
#else
#define BA_PROPERTY UProperty
#define BA_FIND_FIELD FindField
#define BA_FIND_PROPERTY FindField
#define BA_WEAK_FIELD_PTR TWeakObjectPtr
#endif

struct FBANodePinHandle
{
	UEdGraphNode* Node = nullptr;
	FGuid PinId;

	FBANodePinHandle(UEdGraphPin* Pin)
	{
		if (Pin)
		{
			PinId = Pin->PinId;
			Node = Pin->GetOwningNode();
		}
	}

	UEdGraphPin* GetPin() const
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->PinId == PinId)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphNode* GetNode() const
	{
		return Node;
	}

	bool IsValid() const
	{
		return GetPin() != nullptr;
	}
};
