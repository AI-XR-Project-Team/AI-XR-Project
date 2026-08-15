#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavMinimapWidget.h"   // UNavMinimapWidget, FOnNavDestinationChosen
#include "NavFullMapWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * 전체 지도 오버레이. UNavMinimapWidget(Full 모드)을 품고, 반투명 배경과 터치 처리를 얹는다.
 *
 * 화면 전체를 덮는 위젯이라 닫기가 단순하다 — NativeOnMouseButtonDown 에서 노드를
 * 눌렀으면 목적지로 알리고, 그 밖의 아무 곳이나 누르면 닫는다(MapView 가 배경을
 * 꽉 채워 "바깥"이 없어도 닫힌다).
 *
 * ## 띄우기/닫기 (spec §2.1, §3.5)
 *
 * UNavMinimapWidget::OpenFullMap 이 C++ 에서 CreateWidget + AddToViewport(ZOrder=100)로 띄운다.
 * .uasset(팀원2의 WBP_DocentChat) 을 건드리지 않으려는 결정이다. 닫기는 RemoveFromParent.
 *
 * ## WBP 로 상속할 때(WBP_NavMinimapFull)
 *
 * 필수 자식(BindWidget):
 *   - MapView   (부모 = NavMinimapWidget, Mode=Full)
 *   - Backdrop  (UBorder, 화면 전체 앵커, 반투명 검정, Visibility=Visible)
 * 선택:
 *   - TitleText (UTextBlock)
 *
 * 터치가 루트까지 오도록 MapView 의 Visibility 는 Not Hit-Testable 로 둔다(노드 판정은
 * 이 클래스가 MapView 의 그리기 변환을 되짚어 직접 한다). 루트/ Backdrop 은 Visible.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UNavFullMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Follow 미니맵이 들고 있던 상태를 그대로 받아 전체 지도를 채운다.
	 * @param InGraph        전체 그래프(벽·구조물·노드·엣지)
	 * @param InRouteXY      현재 경로 폴리라인(맵 cm). 없으면 빈 배열
	 * @param bHasPose       현재 측위가 유효한가
	 * @param PoseXY         현재 위치(맵 cm)
	 * @param PoseHeadingDeg 바라보는 방향(+X 기준 CCW °)
	 * @param bPoseHasHeading heading 유효 여부
	 * @param InDestNodeId   현재 목적지 노드(강조용). 없으면 빈 문자열
	 */
	void ApplyState(const FNavGraph& InGraph, const TArray<FVector2D>& InRouteXY,
		bool bHasPose, const FVector2D& PoseXY, float PoseHeadingDeg, bool bPoseHasHeading,
		const FString& InDestNodeId);

	/** 노드를 골랐을 때. Follow 미니맵이 받아 재방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "Nav|FullMap")
	FOnNavDestinationChosen OnDestinationChosen;

	/** 전체 지도를 닫는다(뷰포트에서 제거). */
	UFUNCTION(BlueprintCallable, Category = "Nav|FullMap")
	void Close();

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;

	/** 전체 지도 본체(Full 모드). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Nav|FullMap")
	TObjectPtr<UNavMinimapWidget> MapView;

	/** 반투명 배경(화면 전체). 바깥 터치를 받아 닫기 판정에 쓴다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Nav|FullMap")
	TObjectPtr<UBorder> Backdrop;

	/** 선택 제목. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|FullMap")
	TObjectPtr<UTextBlock> TitleText;
};
