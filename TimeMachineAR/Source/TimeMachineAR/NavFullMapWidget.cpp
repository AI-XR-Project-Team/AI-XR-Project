#include "NavFullMapWidget.h"

void UNavFullMapWidget::ApplyState(const FNavGraph& InGraph, const TArray<FVector2D>& InRouteXY,
	bool bHasPose, const FVector2D& PoseXY, float PoseHeadingDeg, bool bPoseHasHeading,
	const FString& InDestNodeId)
{
	if (MapView == nullptr)
	{
		return;
	}
	MapView->Mode = ENavMinimapMode::Full;   // WBP 설정이 빠져도 강제한다.
	MapView->SetGraph(InGraph);
	MapView->SetRouteXY(InRouteXY);
	MapView->SetDestinationNode(InDestNodeId);
	if (bHasPose)
	{
		MapView->SetCurrentPose(PoseXY.X, PoseXY.Y, PoseHeadingDeg, bPoseHasHeading);
	}
	else
	{
		MapView->ClearCurrentPose();
	}
}

void UNavFullMapWidget::Close()
{
	RemoveFromParent();
}

FReply UNavFullMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	const FVector2D Abs = InMouseEvent.GetScreenSpacePosition();

	// 노드를 눌렀으면 목적지로 알리고, 그 밖의 아무 곳이나 눌러도 닫는다.
	// (MapView 가 배경을 꽉 채우면 "바깥"이 없어 닫히지 않던 문제를 없앤다 — 노드가
	//  아닌 모든 터치는 닫기로 본다. 노드만 선택, 나머지는 전부 dismiss.)
	if (MapView != nullptr)
	{
		const FVector2D Local = MapView->GetCachedGeometry().AbsoluteToLocal(Abs);
		FString NodeId;
		if (MapView->FindNodeAtLocal(Local, MapView->NodeHitRadiusPx, NodeId))
		{
			MapView->SetDestinationNode(NodeId);
			OnDestinationChosen.Broadcast(NodeId);
		}
	}

	Close();
	return FReply::Handled();
}
