// Copyright 2021 fpwong. All Rights Reserved.

#include "KnotTrack.h"

#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistUtils.h"
#include "K2Node_Knot.h"
#include "BlueprintAssist/GraphFormatters/FormatterInterface.h"

UEdGraphPin* FKnotNodeCreation::GetPinToConnectTo() const
{
	return FBAUtils::GetPinFromGraph(PinToConnectToHandle, OwningKnotTrack->GraphHandler->GetFocusedEdGraph());
}

UK2Node_Knot* FKnotNodeCreation::CreateKnotNode(const FVector2D InKnotPos, UEdGraphPin* PreviousPin, UK2Node_Knot* KnotNodeToReuse, UEdGraph* Graph)
{
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Create knot node for pin %s"), *FBAUtils::GetPinName(PreviousPin));

	CreatedKnot = nullptr;

	UEdGraphPin* MainPinToConnectTo = FBAUtils::GetPinFromGraph(PinToConnectToHandle, Graph);

	if (KnotNodeToReuse == nullptr)
	{
		CreatedKnot = FBAUtils::CreateKnotNode(Graph, InKnotPos, MainPinToConnectTo, PreviousPin);
	}
	else
	{
		CreatedKnot = KnotNodeToReuse;
		FBAUtils::LinkKnotNodeBetween(KnotNodeToReuse, InKnotPos, MainPinToConnectTo, PreviousPin);
	}

	for (FGraphPinHandle& PinHandle : PinHandlesToConnectTo)
	{
		UEdGraphPin* Pin = FBAUtils::GetPinFromGraph(PinHandle, Graph);

		// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tLinking to pin %s"), *FBAUtils::GetPinName(Pin));

		if (Pin->Direction == EGPD_Input)
		{
			CreatedKnot->GetOutputPin()->MakeLinkTo(Pin);
		}
		else
		{
			CreatedKnot->GetInputPin()->MakeLinkTo(Pin);
		}
	}

	return CreatedKnot;
}

bool FKnotNodeCreation::HasHeightDifference() const
{
	if (KnotToConnectTo.IsValid())
	{
		return KnotToConnectTo->KnotPos.Y == KnotPos.Y;
	}

	return false;
}

float FKnotNodeTrack::GetTrackHeight() const
{
	if (UEdGraphPin* Pin = GetPinToAlignTo())
	{
		return GraphHandler->GetPinY(Pin);
	}

	return TrackHeight;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////

FKnotNodeTrack::FKnotNodeTrack(
	TSharedPtr<FFormatterInterface> Formatter,
	TSharedPtr<FBAGraphHandler> InGraphHandler,
	UEdGraphPin* InParentPin,
	TArray<UEdGraphPin*> InLinkedTo,
	float InTrackY,
	bool bInIsLoopingTrack)
	: GraphHandler(InGraphHandler)
	, ParentPin(InParentPin)
	, LinkedTo(InLinkedTo)
	, TrackHeight(InTrackY)
	, PinToAlignTo(nullptr)
	, PinAlignedX(0)
	, bIsLoopingTrack(bInIsLoopingTrack)
{
	ParentPinPos = FBAUtils::GetPinPos(GraphHandler, InParentPin);

	SetTrackHeight(Formatter);
}

UEdGraphPin* FKnotNodeTrack::GetParentPin() const
{
	return ParentPin;
}

UEdGraphPin* FKnotNodeTrack::GetLastPin() const
{
	return LinkedTo.Last();
}

UEdGraphPin* FKnotNodeTrack::GetPinToAlignTo() const
{
	return FBAUtils::GetPinFromGraph(PinToAlignTo, GraphHandler->GetFocusedEdGraph());
}

FSlateRect FKnotNodeTrack::GetTrackBounds() const
{
	const float TrackSpacing = GetDefault<UBASettings>()->BlueprintKnotTrackSpacing;
	const float LocalTrackY = GetTrackHeight();
	const float LastPinX = FBAUtils::GetPinPos(GraphHandler, GetLastPin()).X;
	const float TrackXLeft = FMath::Min(ParentPinPos.X, LastPinX) + 5;
	const float TrackXRight = FMath::Max(ParentPinPos.X, LastPinX) - 5;

	return FSlateRect(FVector2D(TrackXLeft, LocalTrackY - (TrackSpacing - 1) * 0.5f),
					FVector2D(TrackXRight, LocalTrackY + (TrackSpacing - 1) * 0.5f));
}

TArray<FGraphPinHandle> FKnotNodeTrack::GetLinkedToSafe()
{
	TArray<FGraphPinHandle> OutHandles;
	OutHandles.Reserve(LinkedTo.Num());
	for (UEdGraphPin* Pin : LinkedTo)
	{
		OutHandles.Add(Pin);
	}

	return OutHandles;
}

void FKnotNodeTrack::SetTrackHeight(TSharedPtr<FFormatterInterface> Formatter)
{
	const float TrackSpacing = GetDefault<UBASettings>()->BlueprintKnotTrackSpacing;
	const TArray<UEdGraphNode*>& AllNodes = Formatter->GetFormattedNodes().Array();

	UEdGraphPin* LastPin = GetLastPin();

	const float TrackStart = GetTrackBounds().Left + 10;
	const float TrackEnd = GetTrackBounds().Right - 10;

	//UE_LOG(LogBlueprintAssist, Error, TEXT("FindTrackHeight for pin (%s) | Start %f | End %f"), *FBAUtils::GetPinName(ParentPin), TrackStart, TrackEnd);

	// use looping track height which was set when track is created
	if (bIsLoopingTrack)
	{
		return;
	}

	// Try align track to the parent pin or last pin
	for (UEdGraphPin* Pin : { ParentPin, LastPin })
	{
		const float PinHeight = GraphHandler->GetPinY(Pin);
		if (TryAlignTrack(Formatter, TrackStart, TrackEnd, PinHeight))
		{
			TrackHeight = PinHeight;
			return;
		}
	}

	const float StartingPoint = GraphHandler->GetPinY(LastPin);

	float TestSolution = StartingPoint;

	for (int i = 0; i < 100; ++i)
	{
		bool bNoCollisionInDirection = true;

		FVector2D StartPoint(TrackStart, TestSolution);
		FVector2D EndPoint(TrackEnd, TestSolution);

		for (UEdGraphNode* NodeToCollisionCheck : AllNodes)
		{
			FSlateRect NodeBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToCollisionCheck).ExtendBy(FMargin(0, TrackSpacing - 1));

			const bool bSkipNode = NodeToCollisionCheck == ParentPin->GetOwningNode() || NodeToCollisionCheck == LastPin->GetOwningNode();
			if (!bSkipNode)
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Checking collision for node %s | %f %f  | %f"), *FBAUtils::GetNodeName(NodeToCollisionCheck), TrackStart, TrackEnd, TestSolution);
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *NodeBounds.ToString());
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%f %f | %f"), TrackStart, TrackEnd, TestSolution);

				if (FBAUtils::LineRectIntersection(NodeBounds, StartPoint, EndPoint))
				{
					// UE_LOG(LogBlueprintAssist, Error, TEXT("\tNode collision  (%s) (%f) | %s"), *FBAUtils::GetNodeName(NodeToCollisionCheck), TestSolution, *NodeBounds.ToString());
					bNoCollisionInDirection = false;
					TestSolution = NodeBounds.Bottom + 1;
				}
			}
		}

		if (bNoCollisionInDirection)
		{
			TrackHeight = TestSolution;
			break;
		}
	}
}

bool FKnotNodeTrack::IsFloatingTrack() const
{
	const bool bSameAsParentPin = TrackHeight != FBAUtils::GetPinPos(GraphHandler, ParentPin).Y;
	const bool bSameAsLastPin = TrackHeight != FBAUtils::GetPinPos(GraphHandler, GetLastPin()).Y;
	return bSameAsParentPin && bSameAsLastPin;
}

void FKnotNodeTrack::UpdateTrackHeight(const float NewTrackY)
{
	const float Delta = NewTrackY - TrackHeight;

	for (TSharedPtr<FKnotNodeCreation> Creation : KnotCreations)
	{
		Creation->KnotPos.Y += Delta;
	}

	TrackHeight = NewTrackY;
}

TSet<UEdGraphNode*> FKnotNodeTrack::GetNodes(UEdGraph* Graph)
{
	TSet<UEdGraphNode*> OutNodes;
	OutNodes.Add(ParentPin->GetOwningNode());

	for (UEdGraphPin* Pin : LinkedTo)
	{
		if (UEdGraphPin* SafePin = FBAUtils::GetPinFromGraph(Pin, Graph))
		{
			OutNodes.Add(SafePin->GetOwningNode());
		}
	}

	return OutNodes;
}

bool FKnotNodeTrack::DoesTrackOverlapNode(UEdGraphNode* Node) const
{
	const FSlateRect Bounds = GetTrackBounds();

	return FBAUtils::LineRectIntersection(
		FBAUtils::GetNodeBounds(Node),
		Bounds.GetTopLeft(),
		Bounds.GetBottomRight());
}

bool FKnotNodeTrack::HasPinToAlignTo() const
{
	return PinToAlignTo.IsValid();
}

bool FKnotNodeTrack::TryAlignTrack(TSharedPtr<FFormatterInterface> Formatter, float TrackStart, float TrackEnd, float TestHeight)
{
	const float TrackSpacing = GetMutableDefault<UBASettings>()->BlueprintKnotTrackSpacing;

	UEdGraphPin* MyPin = ParentPin;
	UEdGraphPin* LastPin = GetLastPin();

	const TArray<UEdGraphNode*>& AllNodes = Formatter->GetFormattedNodes().Array();
	for (UEdGraphNode* NodeToCollisionCheck : AllNodes)
	{
		FSlateRect NodeBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToCollisionCheck).ExtendBy(FVector2D(0, TrackSpacing - 1));

		FVector2D StartPoint(TrackStart, TestHeight);
		FVector2D EndPoint(TrackEnd, TestHeight);

		const bool bSkipNode = NodeToCollisionCheck == MyPin->GetOwningNode() || NodeToCollisionCheck == LastPin->GetOwningNode();
		if (!bSkipNode)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Checking collision for node %s | %f %f  | %f"), *FBAUtils::GetNodeName(NodeToCollisionCheck), TrackStart, TrackEnd, TestHeight);
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *NodeBounds.ToString());
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%f %f | %f"), TrackStart, TrackEnd, TestHeight);

			if (FBAUtils::LineRectIntersection(NodeBounds, StartPoint, EndPoint))
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tCollision!"));
				return false;
			}
		}
	}

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("No collision!"));
	return true;
}

FString FKnotNodeTrack::ToString()
{
	return FString::Printf(TEXT("%s (%f)"), *FBAUtils::GetPinName(ParentPin), GetTrackHeight());
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////