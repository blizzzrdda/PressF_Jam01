// Copyright 2021 fpwong. All Rights Reserved.

#include "EdGraphParameterFormatter.h"

#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistSettings.h"
#include "BlueprintAssistUtils.h"
#include "EdGraphFormatter.h"
#include "GraphFormatterTypes.h"
#include "Containers/Queue.h"
#include "EdGraph/EdGraphNode.h"

FEdGraphParameterFormatter::FEdGraphParameterFormatter(
	TSharedPtr<FBAGraphHandler> InGraphHandler,
	UEdGraphNode* InRootNode,
	TSharedPtr<FEdGraphFormatter> InGraphFormatter,
	UEdGraphNode* InNodeToKeepStill,
	TArray<UEdGraphNode*> InIgnoredNodes)
	: GraphHandler(InGraphHandler)
	, RootNode(InRootNode)
	, GraphFormatter(InGraphFormatter)
	, IgnoredNodes(InIgnoredNodes)
	, NodeToKeepStill(InNodeToKeepStill)
{
	Padding = GetDefault<UBASettings>()->BlueprintParameterPadding;
	bFormatWithHelixing = false;

	if (!NodeToKeepStill)
	{
		NodeToKeepStill = InRootNode;
	}

	bCenterBranches = GetDefault<UBASettings>()->bCenterBranchesForParameters;
	NumRequiredBranches = GetDefault<UBASettings>()->NumRequiredBranchesForParameters;

	if (RootNode != nullptr)
	{
		AllFormattedNodes = { RootNode };
	}
}

void FEdGraphParameterFormatter::FormatNode(UEdGraphNode* InNode)
{
	if (!FBAUtils::IsGraphNode(RootNode))
	{
		return;
	}

	if (FBAUtils::GetLinkedPins(RootNode).Num() == 0)
	{
		AllFormattedNodes = { RootNode };
		return;
	}

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Formatting parameter node for root %s (%s)"), *FBAUtils::GetNodeName(RootNode), *FBAUtils::GetNodeName(InNode));

	const FSlateRect SavedBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToKeepStill);

	// we have formatted previously, just check how far the root node has moved
	// and move the nodes on the grid accordingly
	if (bInitialized)
	{
		SimpleRelativeFormatting();
		return;
	}

	AllFormattedNodes = { RootNode };

	NodeOffsets.Reset();

	bFormatWithHelixing = DoesHelixingApply();

	FormatX();

	TSet<UEdGraphNode*> SameRowVisited;
	SameRowMapping.Reset();
	ProcessSameRowMapping(RootNode, nullptr, nullptr, SameRowVisited);

	FormatX();

	// move the output nodes so they don't overlap with the helixed input nodes
	if (bFormatWithHelixing && FormattedInputNodes.Num() > 0)
	{
		const float InputNodesRight = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, FormattedInputNodes.Array()).Right;
		const FSlateRect RootNodeBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, RootNode);
		const float Delta = InputNodesRight - RootNodeBounds.Right;
		if (Delta > 0)
		{
			for (UEdGraphNode* Node : FormattedOutputNodes)
			{
				Node->NodePosX += Delta;
			}
		}
	}

	TSet<UEdGraphNode*> TempVisited;
	TSet<UEdGraphNode*> TempChildren;
	FormatY(RootNode, nullptr, nullptr, TempVisited, false, TempChildren);

	if (GetDefault<UBASettings>()->bExpandParametersByHeight && FBAUtils::IsNodePure(RootNode))
	{
		ExpandByHeight();
	}

	// move the nodes relative to the chosen node to keep still
	const FSlateRect NodeToKeepStillBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToKeepStill);

	const float DeltaX = SavedBounds.Left - NodeToKeepStillBounds.Left;
	const float DeltaY = SavedBounds.Top - NodeToKeepStillBounds.Top;

	if (DeltaX != 0 || DeltaY != 0)
	{
		for (UEdGraphNode* Node : GetFormattedNodes())
		{
			Node->NodePosX += DeltaX;
			Node->NodePosY += DeltaY;
		}
	}
}

bool FEdGraphParameterFormatter::DoesHelixingApply()
{
	EBAParameterFormattingStyle FormattingStyleToUse = GetDefault<UBASettings>()->ParameterStyle;
	if (GraphFormatter->GetFormatterParameters().OverrideFormattingStyle.IsValid())
	{
		FormattingStyleToUse = *GraphFormatter->GetFormatterParameters().OverrideFormattingStyle.Get();
	}

	if (FormattingStyleToUse != EBAParameterFormattingStyle::Helixing)
	{
		return false;
	}

	TSet<UEdGraphNode*> VisitedNodes;
	VisitedNodes.Add(RootNode);
	TArray<UEdGraphNode*> NodeQueue;
	NodeQueue.Add(RootNode);

	const auto& IgnoredNodesCopy = IgnoredNodes;

	TSet<UEdGraphNode*> GatheredInputNodes;

	while (NodeQueue.Num() > 0)
	{
		UEdGraphNode* NextNode = NodeQueue.Pop();

		const auto Pred = [&IgnoredNodesCopy](UEdGraphPin* Pin)
		{
			return FBAUtils::IsParameterPin(Pin) && !IgnoredNodesCopy.Contains(Pin->GetOwningNode());
		};

		TArray<UEdGraphPin*> LinkedToPinsInput = FBAUtils::GetLinkedToPins(NextNode, EGPD_Input).FilterByPredicate(Pred);

		TArray<UEdGraphPin*> LinkedToPinsOutput = FBAUtils::GetLinkedToPins(NextNode, EGPD_Output).FilterByPredicate(Pred);

		TSet<UEdGraphNode*> LinkedToNodesInput;
		for (UEdGraphPin* Pin : LinkedToPinsInput)
		{
			if (FBAUtils::IsNodePure(Pin->GetOwningNode()))
			{
				LinkedToNodesInput.Add(Pin->GetOwningNode());
			}
		}

		TSet<UEdGraphNode*> LinkedToNodesOutput;
		for (UEdGraphPin* Pin : LinkedToPinsOutput)
		{
			auto OwningNode = Pin->GetOwningNode();
			if (OwningNode == RootNode || OwningNode->NodePosX <= RootNode->NodePosX)
			{
				LinkedToNodesOutput.Add(Pin->GetOwningNode());
			}
		}

		const bool bOutputLinkedToMultipleNodes = LinkedToNodesOutput.Num() > 1;

		const bool bInputLinkedToMultipleNodes = LinkedToNodesInput.Num() > 1;
		if (bInputLinkedToMultipleNodes || bOutputLinkedToMultipleNodes)
		{
			return false;
		}

		GatheredInputNodes.Append(LinkedToNodesInput);

		// add linked nodes input to the queue
		for (UEdGraphNode* Node : LinkedToNodesInput)
		{
			if (VisitedNodes.Contains(Node))
			{
				continue;
			}

			if (IgnoredNodes.Contains(Node))
			{
				continue;
			}

			VisitedNodes.Add(Node);
			NodeQueue.Add(Node);
		}
	}

	// check if any single node is extremely tall
	if (GetDefault<UBASettings>()->bLimitHelixingHeight)
	{
		float TotalHeight = 0;
		for (UEdGraphNode* Node : GatheredInputNodes)
		{
			if (Node != RootNode)
			{
				const float NodeHeight = FBAUtils::GetCachedNodeBounds(GraphHandler, Node).GetSize().Y;
				if (NodeHeight > GetDefault<UBASettings>()->SingleNodeMaxHeight)
				{
					return false;
				}

				TotalHeight += NodeHeight;
			}
		}

		// helixing should not apply if the height of the stack is too large
		if (TotalHeight > GetDefault<UBASettings>()->HelixingHeightMax)
		{
			return false;
		}
	}

	return true;
}

void FEdGraphParameterFormatter::ProcessSameRowMapping(UEdGraphNode* CurrentNode,
														UEdGraphPin* CurrentPin,
														UEdGraphPin* ParentPin,
														TSet<UEdGraphNode*>& VisitedNodes)
{
	if (VisitedNodes.Contains(CurrentNode))
	{
		return;
	}

	const TArray<UEdGraphNode*> Children = NodeInfoMap[CurrentNode]->GetChildNodes();

	VisitedNodes.Add(CurrentNode);

	bool bFirstPin = true;

	for (EEdGraphPinDirection Direction : { EGPD_Input, EGPD_Output })
	{
		for (UEdGraphPin* MyPin : FBAUtils::GetPinsByDirection(CurrentNode, Direction))
		{
			if (FBAUtils::IsExecPin(MyPin))
			{
				continue;
			}

			TArray<UEdGraphPin*> LinkedTo = MyPin->LinkedTo;

			for (UEdGraphPin* OtherPin : LinkedTo)
			{
				UEdGraphNode* OtherNode = OtherPin->GetOwningNode();

				if (!GraphHandler->FilterSelectiveFormatting(OtherNode, GraphFormatter->GetFormatterParameters().NodesToFormat))
				{
					continue;
				}

				if (!Children.Contains(OtherNode))
				{
					continue;
				}

				if (VisitedNodes.Contains(OtherNode))
				{
					continue;
				}

				const bool bApplyHelixing = bFormatWithHelixing && FormattedInputNodes.Contains(OtherNode);

				const bool bSameDirectionAsParent = ParentPin == nullptr || MyPin->Direction == ParentPin->Direction;

				if (bFirstPin && bSameDirectionAsParent && !bApplyHelixing && CurrentNode != RootNode)
				{
					SameRowMapping.Add(FPinLink(MyPin, OtherPin), true);
					SameRowMapping.Add(FPinLink(OtherPin, MyPin), true);
					bFirstPin = false;
				}

				TSet<UEdGraphNode*> LocalChildren;
				ProcessSameRowMapping(OtherNode, OtherPin, MyPin, VisitedNodes);
			}
		}
	}
}

void FEdGraphParameterFormatter::FormatX()
{
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("FORMATTING X for %s"), *FBAUtils::GetNodeName(RootNode));

	TArray<TEnumAsByte<EEdGraphPinDirection>> InOut = { EGPD_Output, EGPD_Input };

	TSet<FPinLink> VisitedLinks;

	TArray<UEdGraphNode*> TempOutput;

	NodeInfoMap.Reset();

	for (EEdGraphPinDirection InitialDirection : InOut)
	{
		VisitedLinks.Empty();

		TQueue<FPinLink> OutputQueue;
		TQueue<FPinLink> InputQueue;

		FPinLink RootNodeLink = FPinLink(nullptr, nullptr, RootNode);
		if (InitialDirection == EGPD_Input)
		{
			InputQueue.Enqueue(RootNodeLink);
		}
		else
		{
			OutputQueue.Enqueue(RootNodeLink);
		}

		while (!InputQueue.IsEmpty() || !OutputQueue.IsEmpty())
		{
			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Input Queue:"));
			//for (PinLink* Link : InputQueue)
			//{
			//	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *Link->ToString());
			//}

			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Output Queue:"));
			//for (PinLink* Link : OutputStack)
			//{
			//	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *Link->ToString());
			//}

			// TODO: This is stupid, fix this!
			FPinLink CurrentPinLink;
			if (InitialDirection == EGPD_Input)
			{
				(!InputQueue.IsEmpty() ? InputQueue : OutputQueue).Dequeue(CurrentPinLink);
			}
			else
			{
				(!OutputQueue.IsEmpty() ? OutputQueue : InputQueue).Dequeue(CurrentPinLink);
			}

			UEdGraphNode* CurrentNode = CurrentPinLink.GetNode();
			UEdGraphPin* ParentPin = CurrentPinLink.From;
			UEdGraphPin* MyPin = CurrentPinLink.To;

			UEdGraphNode* ParentNode = ParentPin != nullptr ? ParentPin->GetOwningNode() : nullptr;

			TSharedPtr<FNodeInfo> ParentInfo;
			if (ParentPin != nullptr)
			{
				ParentInfo = NodeInfoMap[ParentPin->GetOwningNode()];
			}

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tPopped Node: %s | MyPin %s | ParentNode %s | ParentPin %s")
			// 	, *FBAUtils::GetNodeName(CurrentNode)
			// 	, *FBAUtils::GetPinName(MyPin)
			// 	, *FBAUtils::GetNodeName(ParentNode)
			// 	, *FBAUtils::GetPinName(ParentPin));

			if (NodeInfoMap.Contains(CurrentNode) && CurrentNode != RootNode &&
				TempOutput.Contains(CurrentNode) &&
				InitialDirection == EGPD_Input
					//&& !FormattedOutputNodes.Contains(CurrentNode)
			)
			{
				NodeInfoMap.Remove(CurrentNode);
				TempOutput.Remove(CurrentNode);
			}

			if (NodeInfoMap.Contains(CurrentNode))
			{
				if (ParentPin != nullptr && MyPin != nullptr)
				{
					TSharedPtr<FNodeInfo> CurrentInfo = NodeInfoMap[CurrentNode];

					if (ParentNode != CurrentInfo->GetParentNode())
					{
						int32 NewNodePos = GetChildX(CurrentPinLink, ParentPin->Direction);

						const bool bApplyHelixing = bFormatWithHelixing && FormattedInputNodes.Contains(CurrentNode);
						if (bApplyHelixing)
						{
							NewNodePos = CurrentNode->NodePosX;
						}

						const bool bNewLocationIsBetter = (ParentPin->Direction == EGPD_Input
							? NewNodePos < CurrentNode->NodePosX
							: NewNodePos > CurrentNode->NodePosX) || bApplyHelixing;

						const bool bSameDirection = CurrentInfo->Direction == ParentPin->Direction;

						const bool bTakeNewParent = bNewLocationIsBetter && bSameDirection;

						if (bTakeNewParent)
						{
							// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tParameter Node %s was already formatted, taking new parent %s %d -> %d (Old Parent %s)"),
							// 	*FBAUtils::GetNodeName(CurrentNode),
							// 	*FBAUtils::GetNodeName(ParentNode),
							// 	CurrentNode->NodePosX,
							// 	NewNodePos,
							// 	*FBAUtils::GetNodeName(CurrentInfo->GetParentNode()));

							CurrentNode->NodePosX = NewNodePos;
							CurrentInfo->SetParent(ParentInfo, MyPin);
							TSet<UEdGraphNode*> TempChildren;
							CurrentInfo->MoveChildren(CurrentInfo, GraphHandler, Padding, TempChildren);
						}
						else
						{
							// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tNOT TAKING NEW PARENT ( WORSE ) %s"), *FBAUtils::GetNodeName(CurrentNode));
						}
					}
				}
			}
			else
			{
				CurrentNode->Modify();

				if (CurrentNode != RootNode)
				{
					if (InitialDirection == EGPD_Input && CurrentPinLink.GetDirection() == EGPD_Input && (FormattedInputNodes.Contains(ParentNode) || ParentNode == RootNode))
					{
						FormattedInputNodes.Add(CurrentNode);
					}
					else if (InitialDirection == EGPD_Output && CurrentPinLink.GetDirection() == EGPD_Output && (FormattedOutputNodes.Contains(ParentNode) || ParentNode == RootNode))
					{
						FormattedOutputNodes.Add(CurrentNode);
					}

					int32 NewNodePos = GetChildX(CurrentPinLink, ParentPin->Direction);

					const bool bApplyHelixing = bFormatWithHelixing && FormattedInputNodes.Contains(CurrentNode);
					if (bApplyHelixing)
					{
						NewNodePos = ParentNode->NodePosX;
					}

					CurrentNode->NodePosX = NewNodePos;

					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tNode %s | Pos %d | InitialDir %d CurrentDir %d"),
					// 	*FBAUtils::GetNodeName(CurrentNode),
					// 	NewNodePos,
					// 	static_cast<int32>(InitialDirection),
					// 	static_cast<int32>(CurrentPinLink.GetDirection()));

					AllFormattedNodes.Add(CurrentNode);
				}
				else // add the root node
				{
					AllFormattedNodes.Add(RootNode);
				}

				TSharedPtr<FNodeInfo> NewNodeInfo = MakeShareable(new FNodeInfo(CurrentNode, MyPin, ParentInfo, ParentPin, InitialDirection));
				NewNodeInfo->SetParent(ParentInfo, MyPin);
				NodeInfoMap.Add(CurrentNode, NewNodeInfo);

				if (InitialDirection == EGPD_Output)
				{
					TempOutput.Add(CurrentNode);
				}

				//if (ParentNode != nullptr)
				//{
				//	UE_LOG(LogBlueprintAssist, Warning, TEXT("SET New Parent for %s to %s | %d | %d"), *FBAUtils::GetNodeName(CurrentNode), *FBAUtils::GetNodeName(ParentNode), CurrentNode->NodePosX, ParentNode->NodePosX);
				//}
			}

			TSharedPtr<FNodeInfo> CurrentInfo = NodeInfoMap[CurrentNode];

			// if the current node is the root node, use the initial direction when getting linked nodes
			const bool bCurrentNodeIsRootAndImpure = CurrentNode == RootNode && FBAUtils::IsNodeImpure(CurrentNode);
			const EEdGraphPinDirection LinkedDirection = bCurrentNodeIsRootAndImpure ? InitialDirection : EGPD_MAX;

			const auto AllowedPins = [](UEdGraphPin* Pin)
			{
				return FBAUtils::IsParameterPin(Pin) || FBAUtils::IsDelegatePin(Pin);
			};

			TArray<UEdGraphPin*> LinkedParameterPins = FBAUtils::GetLinkedPins(CurrentNode, LinkedDirection).FilterByPredicate(AllowedPins);

			for (int i = 0; i < LinkedParameterPins.Num(); i++)
			{
				UEdGraphPin* Pin = LinkedParameterPins[i];

				TArray<UEdGraphPin*> LinkedTo = Pin->LinkedTo;
				for (UEdGraphPin* LinkedPin : LinkedTo)
				{
					UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();

					if (!GraphHandler->FilterSelectiveFormatting(LinkedNode, GraphFormatter->GetFormatterParameters().NodesToFormat))
					{
						continue;
					}

					if (FBAUtils::IsNodeImpure(LinkedNode) || FBAUtils::IsKnotNode(LinkedNode))
					{
						continue;
					}

					FPinLink CurrentLink = FPinLink(Pin, LinkedPin);
					if (VisitedLinks.Contains(CurrentLink))
					{
						continue;
					}
					VisitedLinks.Add(CurrentLink);

					if (NodeInfoMap.Contains(LinkedNode))
					{
						TSharedPtr<FNodeInfo> LinkedInfo = NodeInfoMap[LinkedNode];
						if (CurrentInfo->DetectCycle(LinkedInfo))
						{
							continue;
						}
					}

					if (LinkedNode == RootNode && FBAUtils::IsNodeImpure(RootNode))
					{
						//UE_LOG(LogBlueprintAssist, Warning, TEXT("SKIPPED Node %s (RootNode)"), *BlueprintAssistUtils::GetNodeName(LinkedNode));
						continue;
					}

					if (IgnoredNodes.Contains(LinkedNode))
					{
						continue;
					}

					if (Pin->Direction == EGPD_Input)
					{
						InputQueue.Enqueue(CurrentLink);
					}
					else
					{
						OutputQueue.Enqueue(CurrentLink);
					}
				}
			}
		}
	}
}

void FEdGraphParameterFormatter::FormatY(
	UEdGraphNode* CurrentNode,
	UEdGraphPin* CurrentPin,
	UEdGraphPin* ParentPin,
	TSet<UEdGraphNode*>& VisitedNodes,
	const bool bSameRow,
	TSet<UEdGraphNode*>& OutChildren)
{
	//UE_LOG(LogBlueprintAssist, Warning, TEXT("ParameterFormatter: FormatY %s | Pin %s | Parent %s"), 
	//	*FBAUtils::GetNodeName(CurrentNode),
	//	*FBAUtils::GetPinName(CurrentPin),
	//	*FBAUtils::GetPinName(ParentPin)
	//);

	if (VisitedNodes.Contains(CurrentNode))
	{
		return;
	}

	const TArray<UEdGraphNode*> Children = NodeInfoMap[CurrentNode]->GetChildNodes();

	// solve collisions against visited nodes
	for (int i = 0; i < 100; ++i)
	{
		bool bNoCollision = true;

		for (UEdGraphNode* NodeToCollisionCheck : VisitedNodes)
		{
			if (NodeToCollisionCheck == CurrentNode)
			{
				continue;
			}

			TSet<UEdGraphNode*> NodesToMove;

			FSlateRect MyBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, CurrentNode);

			FMargin ExtendPadding(0, 0, 0, Padding.Y);

			FSlateRect OtherBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToCollisionCheck);

			OtherBounds = OtherBounds.ExtendBy(ExtendPadding);

			if (FSlateRect::DoRectanglesIntersect(MyBounds.ExtendBy(ExtendPadding), OtherBounds))
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("%s: Collision with %s | %s | %s"),
				//        *FBAUtils::GetNodeName(CurrentNode), *FBAUtils::GetNodeName(NodeToCollisionCheck),
				//        *MyBounds.ToString(), *OtherBounds.ToString()
				// );

				const float Delta = OtherBounds.Bottom - MyBounds.Top;

				if (NodesToMove.Num() > 0)
				{
					for (UEdGraphNode* Node : NodesToMove)
					{
						Node->NodePosY += Delta + 1;
					}
				}
				else
				{
					CurrentNode->NodePosY += Delta + 1;
				}

				bNoCollision = false;
				break;
			}
		}

		if (bNoCollision)
		{
			break;
		}
	}

	VisitedNodes.Add(CurrentNode);

	bool bFirstPin = true;

	bool bCenteredParent = false;

	const EEdGraphPinDirection ParentDirection = ParentPin == nullptr ? EGPD_Output : ParentPin->Direction.GetValue();

	for (EEdGraphPinDirection Direction : { EGPD_Input, EGPD_Output })
	{
		UEdGraphPin* LastLinked = nullptr;

		TArray<ChildBranch> ChildBranches;

		for (UEdGraphPin* MyPin : FBAUtils::GetPinsByDirection(CurrentNode, Direction))
		{
			if (FBAUtils::IsExecPin(MyPin))
			{
				LastLinked = MyPin;
				continue;
			}

			TArray<UEdGraphPin*> LinkedTo = MyPin->LinkedTo;

			for (UEdGraphPin* OtherPin : LinkedTo)
			{
				UEdGraphNode* OtherNode = OtherPin->GetOwningNode();

				if (IgnoredNodes.Contains(OtherNode) ||
					!GraphHandler->FilterSelectiveFormatting(OtherNode, GraphFormatter->GetFormatterParameters().NodesToFormat) ||
					!Children.Contains(OtherNode) ||
					VisitedNodes.Contains(OtherNode))
				{
					continue;
				}

				// if we applied helixing, then do not format any input nodes
				const bool bApplyHelixing = bFormatWithHelixing && FormattedInputNodes.Contains(OtherNode);
				if (bApplyHelixing)
				{
					OtherNode->NodePosY = FBAUtils::GetCachedNodeBounds(GraphHandler, MyPin->GetOwningNode()).Bottom + Padding.Y;
				}
				else
				{
					FBAUtils::StraightenPin(GraphHandler, MyPin, OtherPin);
				}

				const bool bSameDirectionAsParent = ParentPin == nullptr || MyPin->Direction == ParentPin->Direction;

				bool bChildIsSameRow = false;

				if (bFirstPin && bSameDirectionAsParent && !bApplyHelixing && CurrentNode != RootNode)
				{
					bChildIsSameRow = true;
					bFirstPin = false;
				}

				TSet<UEdGraphNode*> LocalChildren;
				FormatY(OtherNode, OtherPin, MyPin, VisitedNodes, bChildIsSameRow, LocalChildren);
				OutChildren.Append(LocalChildren);

				ChildBranches.Add(ChildBranch(OtherPin, MyPin, LocalChildren));

				// TODO: Fix issue with this. Output nodes should not be moved here, they should be processed later - see BABadCases
				if (!(bFormatWithHelixing && Direction == EGPD_Input) && LocalChildren.Num() > 0 && LastLinked != nullptr)
				{
					UEdGraphPin* PinToAvoid = LastLinked;
					const FSlateRect Bounds = GraphFormatter->GetNodeArrayBounds(LocalChildren.Array(), false);

					//UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tPin to avoid %s (%s)"), *FBAUtils::GetPinName(PinToAvoid), *FBAUtils::GetPinName(OtherPin));

					const float PinPos = GraphHandler->GetPinY(PinToAvoid) + GetDefault<UBASettings>()->ParameterVerticalPinSpacing;
					const float Delta = PinPos - Bounds.Top;
					if (Delta > 0)
					{
						for (UEdGraphNode* Child : LocalChildren)
						{
							// UE_LOG(LogBlueprintAssist, Warning, TEXT("Moved child %s by %f (Pin %f (%s) | %f)"), *FBAUtils::GetNodeName(Child), Delta, PinPos, *FBAUtils::GetPinName(PinToAvoid), Bounds.Top);
							Child->NodePosY += Delta;
						}
					}
				}
			}

			if (LinkedTo.Num() > 0)
			{
				LastLinked = MyPin;
			}
		}

		// check if there are any child nodes to the right of us
		TArray<UEdGraphNode*> AllChildren;
		for (auto Branch : ChildBranches)
		{
			AllChildren.Append(Branch.BranchNodes.Array());
		}
		FSlateRect ChildBounds = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, AllChildren);
		const bool bChildrenTooBig = CurrentNode->NodePosX < ChildBounds.Right;

		if (bCenterBranches && Direction == EGPD_Input && ChildBranches.Num() >= NumRequiredBranches && !bChildrenTooBig)
		{
			if (FBAUtils::IsNodePure(CurrentNode))
			{
				CenterBranches(CurrentNode, ChildBranches, VisitedNodes);
			}
		}
	}

	OutChildren.Add(CurrentNode);

	if (bSameRow && ParentPin)
	{
		FBAUtils::StraightenPin(GraphHandler, CurrentPin, ParentPin);
	}
}

int32 FEdGraphParameterFormatter::GetChildX(const FPinLink& Link, const EEdGraphPinDirection Direction) const
{
	float NewNodePos;

	UEdGraphNode* Parent = Link.From->GetOwningNode();
	UEdGraphNode* Child = Link.To->GetOwningNode();

	FSlateRect ParentBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, Parent);

	const FSlateRect ChildBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, Child);

	if (Direction == EGPD_Input)
	{
		NewNodePos = ParentBounds.Left - ChildBounds.GetSize().X - FMath::Max(1.0f, Padding.X);
	}
	else
	{
		NewNodePos = ParentBounds.Right + FMath::Max(1.0f, Padding.X);
	}

	return FMath::RoundToInt(NewNodePos);
}

bool FEdGraphParameterFormatter::AnyLinkedImpureNodes() const
{
	TSet<UEdGraphNode*> VisitedNodes;
	TArray<UEdGraphNode*> NodeQueue;

	VisitedNodes.Add(RootNode);
	NodeQueue.Push(RootNode);

	while (NodeQueue.Num() > 0)
	{
		UEdGraphNode* Node = NodeQueue.Pop();
		if (FBAUtils::IsNodeImpure(Node))
		{
			return true;
		}

		for (UEdGraphNode* LinkedNode : FBAUtils::GetLinkedNodes(Node))
		{
			if (VisitedNodes.Contains(LinkedNode))
			{
				continue;
			}

			VisitedNodes.Add(LinkedNode);
			NodeQueue.Push(LinkedNode);
		}
	}

	return false;
}

FSlateRect FEdGraphParameterFormatter::GetBounds()
{
	return FBAUtils::GetCachedNodeArrayBounds(GraphHandler, GetFormattedNodes().Array());
}

FSlateRect FEdGraphParameterFormatter::GetParameterBounds()
{
	TArray<UEdGraphNode*> WithoutRootNode = GetFormattedNodes().Array();
	WithoutRootNode = WithoutRootNode.FilterByPredicate([this](UEdGraphNode* Node) { return Node != RootNode; });

	if (WithoutRootNode.Num() == 0)
	{
		return FSlateRect(0, 0, 0, 0);
	}

	FSlateRect ParameterBounds = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, WithoutRootNode);

	const FSlateRect RootNodeBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, RootNode);

	const float TopPadding = FMath::Max(0.f, ParameterBounds.Top - RootNodeBounds.Bottom);
	ParameterBounds = ParameterBounds.ExtendBy(FMargin(0, TopPadding, 0, 0));

	return ParameterBounds;
}

void FEdGraphParameterFormatter::MoveBelowBaseline(TSet<UEdGraphNode*> Nodes, const float Baseline)
{
	const FSlateRect Bounds = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, Nodes.Array());

	if (Baseline > Bounds.Top)
	{
		const float Delta = Baseline - Bounds.Top;
		for (UEdGraphNode* Node : Nodes)
		{
			Node->NodePosY += Delta;
		}
	}
}

TSet<UEdGraphNode*> FEdGraphParameterFormatter::GetFormattedNodes()
{
	return AllFormattedNodes;
}

void FEdGraphParameterFormatter::DebugPrintFormatted()
{
	UE_LOG(LogBlueprintAssist, Warning, TEXT("Node Info Map: "));
	for (auto& Elem : NodeInfoMap)
	{
		UEdGraphNode* Node = Elem.Key;
		TSharedPtr<FNodeInfo> Info = Elem.Value;

		UE_LOG(LogBlueprintAssist, Warning, TEXT("\tNode %s | Parent %s"), *FBAUtils::GetNodeName(Node), *FBAUtils::GetNodeName(Info->GetParentNode()));

		for (UEdGraphNode* Child : Info->GetChildNodes())
		{
			UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tChild %s"), *FBAUtils::GetNodeName(Child));
		}
	}

	UE_LOG(LogBlueprintAssist, Warning, TEXT("Formatted Input %s"), *FBAUtils::GetNodeName(RootNode));
	for (UEdGraphNode* Node : FormattedInputNodes)
	{
		UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Node));
	}

	UE_LOG(LogBlueprintAssist, Warning, TEXT("Formatted Output %s"), *FBAUtils::GetNodeName(RootNode));
	for (UEdGraphNode* Node : FormattedOutputNodes)
	{
		UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Node));
	}

	UE_LOG(LogBlueprintAssist, Warning, TEXT("Formatted ALL for %s"), *FBAUtils::GetNodeName(RootNode));
	for (UEdGraphNode* Node : GetFormattedNodes())
	{
		UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Node));
	}
}

void FEdGraphParameterFormatter::SimpleRelativeFormatting()
{
	for (auto Elem : NodeOffsets)
	{
		UEdGraphNode* Node = Elem.Key;
		FVector2D Offset = Elem.Value;

		Node->NodePosX = NodeToKeepStill->NodePosX + Offset.X;
		Node->NodePosY = NodeToKeepStill->NodePosY + Offset.Y;
	}
}

void FEdGraphParameterFormatter::ExpandByHeight()
{
	if (bFormatWithHelixing)
	{
		return;
	}

	TArray<EEdGraphPinDirection> InputOutput = { EGPD_Input }; // , EGPD_Output

	auto FormattedNodes = GetFormattedNodes();
	auto FormattedOutputNodesCapture = FormattedOutputNodes;

	const auto IsFormatted = [FormattedNodes, FormattedOutputNodesCapture](UEdGraphPin* Pin)
	{
		return FormattedNodes.Contains(Pin->GetOwningNode()) || FormattedOutputNodesCapture.Contains(Pin->GetOwningNode());
	};

	TSet<UEdGraphNode*> NodesToExpand = FormattedInputNodes;
	NodesToExpand.Add(RootNode);

	for (UEdGraphNode* FormattedNode : NodesToExpand)
	{
		for (EEdGraphPinDirection Direction : InputOutput)
		{
			const TArray<FPinLink> PinLinks = FBAUtils::GetPinLinks(FormattedNode, Direction);

			float LargestPinDelta = 0;
			for (const FPinLink& Link : PinLinks)
			{
				const float PinDelta = FBAUtils::GetPinPos(GraphHandler, Link.To).Y - FBAUtils::GetPinPos(GraphHandler, Link.From).Y;
				LargestPinDelta = FMath::Max(PinDelta, LargestPinDelta);
			}

			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Checking node %s %f %d"), *FBAUtils::GetNodeName(FormattedNode), LargestPinDelta, Direction);

			if (LargestPinDelta < 150)
			{
				continue;
			}

			const int32 ExpandDirection = Direction == EGPD_Input ? -1 : 1;
			const float ExpandX = ExpandDirection * LargestPinDelta * 0.2f;

			const auto NodeTree = FBAUtils::GetNodeTreeWithFilter(FormattedNode, IsFormatted, Direction, true);
			for (UEdGraphNode* Node : NodeTree)
			{
				if (Node != FormattedNode && Node != RootNode)
				{
					Node->NodePosX += ExpandX;

					//UE_LOG(LogBlueprintAssist, Warning, TEXT("\tExpanded node %s %d"), *FBAUtils::GetNodeName(Node), Direction);
				}
			}
		}
	}
}

void FEdGraphParameterFormatter::SaveRelativePositions()
{
	for (UEdGraphNode* Node : GetFormattedNodes())
	{
		if (Node != NodeToKeepStill)
		{
			const FVector2D RelativePos = FVector2D(Node->NodePosX - NodeToKeepStill->NodePosX, Node->NodePosY - NodeToKeepStill->NodePosY);

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tRelative pos %s to %s : %s"), *FBAUtils::GetNodeName(Node), *FBAUtils::GetNodeName(NodeToKeepStill), *RelativePos.ToString());

			NodeOffsets.Add(Node, RelativePos);
		}
	}
}

void FEdGraphParameterFormatter::CenterBranches(UEdGraphNode* CurrentNode, const TArray<ChildBranch>& ChildBranches, const TSet<UEdGraphNode*>& NodesToCollisionCheck)
{
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Centering Node %s"), *FBAUtils::GetNodeName(CurrentNode));
	// for (auto& Branch : ChildBranches)
	// {
	// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\tBranch %s"), *Branch.ToString());
	// 	for (auto Node : Branch.BranchNodes)
	// 	{
	// 		UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t%s"), *FBAUtils::GetNodeName(Node));
	// 	}
	// }

	// Center branches
	TArray<UEdGraphPin*> ChildPins;
	TArray<UEdGraphPin*> ParentPins;
	for (const ChildBranch& Branch : ChildBranches)
	{
		ChildPins.Add(Branch.Pin);
		ParentPins.Add(Branch.ParentPin);
	}

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Child pins:"));
	// for (auto Pin : ChildPins)
	// {
	// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *FBAUtils::GetPinName(Pin));
	// }
	//
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Parent pins:"));
	// for (auto Pin : ParentPins)
	// {
	// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *FBAUtils::GetPinName(Pin));
	// }

	const float ChildrenCenter = FBAUtils::GetCenterYOfPins(GraphHandler, ChildPins);
	const float ParentCenter = FBAUtils::GetCenterYOfPins(GraphHandler, ParentPins);
	const float Offset = ParentCenter - ChildrenCenter;

	TArray<UEdGraphNode*> AllNodes;

	for (const ChildBranch& Branch : ChildBranches)
	{
		for (UEdGraphNode* Child : Branch.BranchNodes)
		{
			AllNodes.Add(Child);
			Child->NodePosY += Offset;
		}
	}

	// Resolve collisions
	AllNodes.Add(CurrentNode);

	FSlateRect AllNodesBounds = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, AllNodes);
	const float InitialTop = AllNodesBounds.Top;
	for (auto Node : NodesToCollisionCheck)
	{
		if (AllNodes.Contains(Node))
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Skipping %s"), *FBAUtils::GetNodeName(Node));
			continue;
		}

		FSlateRect Bounds = FBAUtils::GetCachedNodeBounds(GraphHandler, Node).ExtendBy(FMargin(0, Padding.Y));
		if (FSlateRect::DoRectanglesIntersect(Bounds, AllNodesBounds))
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Colliding with %s"), *FBAUtils::GetNodeName(Node));
			const float OffsetY = Bounds.Bottom - AllNodesBounds.Top;
			AllNodesBounds = AllNodesBounds.OffsetBy(FVector2D(0, OffsetY));
		}
	}

	const float DeltaY = AllNodesBounds.Top - InitialTop;
	if (DeltaY != 0)
	{
		for (auto Node : AllNodes)
		{
			Node->NodePosY += DeltaY;
		}
	}
}
