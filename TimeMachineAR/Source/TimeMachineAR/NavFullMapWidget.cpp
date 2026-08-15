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

	if (MapView != nullptr)
	{
		const FGeometry& MapGeom = MapView->GetCachedGeometry();
		if (MapGeom.IsUnderLocation(Abs))
		{
			// 지도 영역 안 — 노드를 골랐는지 본다.
			const FVector2D Local = MapGeom.AbsoluteToLocal(Abs);
			FString NodeId;
			if (MapView->FindNodeAtLocal(Local, MapView->NodeHitRadiusPx, NodeId))
			{
				MapView->SetDestinationNode(NodeId);
				OnDestinationChosen.Broadcast(NodeId);
				Close();   // 목적지를 골랐으니 전체 지도를 닫는다.
			}
			// 지도 안이지만 노드가 아니면 열어 둔 채로 소비만 한다.
			return FReply::Handled();
		}
	}

	// 지도 바깥 → 닫기.
	Close();
	return FReply::Handled();
}
