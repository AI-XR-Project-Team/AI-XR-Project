#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "NavDestMarkerWidget.generated.h"

/**
 * 목적지 위에 뜨는 **마름모 + 아이콘 + 남은 거리**(9단계 §C-2). 전체화면 투명 HUD 오버레이.
 *
 * 목적지 월드 좌표를 화면에 **투영**해 그 자리에 그린다. 시야 밖이면 화면 가장자리에 방향
 * 화살표를 낸다(final §C-2 "시야 밖 목적지 = 화면 가장자리 방향 표시"). 벽 뒤여도 뚫고 보인다
 * (벽 데이터 없음).
 *
 * ## 왜 HUD 투영인가 (월드 액터 아님)
 *
 * "시야 밖이면 가장자리 표시"가 요구라 스크린 공간이 자연스럽다. 마름모 테두리색은 node_type,
 * 안쪽 거리 숫자는 매 프레임 바뀌므로 **코드로 그린다**(텍스처 아님). 아이콘(§B 6종)만 재사용.
 *
 * ## 미니맵이 소유·구동한다 (에디터 배선 0)
 *
 * Follow(HUD) 미니맵이 C++ 로 생성해 화면에 붙이고(8단계 GuideLog 와 같은 방식), 목적지가
 * 바뀌면 `SetDestination`, 매 프레임 `SetRemaining` 을 부른다. 목적지 **맵 좌표**를 들고 있다가
 * 매 프레임 측위 계층으로 월드를 구하므로, 재측위(relatch)로 변환이 바뀌어도 자동으로 따라간다.
 */
UCLASS(Config = Game)
class TIMEMACHINEAR_API UNavDestMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 목적지가 정해졌을 때(경로/목적지 변경). 맵 좌표·종류·이름을 받아 아이콘·색을 갖춘다. */
	void SetDestination(const FVector2D& MapXY, const FString& InNodeType, const FString& InLabel);

	/** 목적지를 지운다(안내 종료·측위 상실). 아무것도 안 그린다. */
	void ClearDestination();

	/** 남은 거리(cm)를 갱신한다. 미니맵이 매 프레임 `FNavProgress.RemainingCm` 로 부른다. */
	void SetRemaining(float InRemainingCm) { RemainingCm = InRemainingCm; }

	// ------------------------------------------------------------------ 스타일(ini)

	/** 마름모 중심→꼭짓점(px). 목적지 표시판을 크게(약 4배) — 54→216. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|DestMarker",
		meta = (ClampMin = "10.0"))
	float DiamondHalfPx = 216.f;

	/** 마름모 테두리 굵기(px). 커진 마름모에 맞춰 굵게. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|DestMarker",
		meta = (ClampMin = "1.0"))
	float DiamondThicknessPx = 12.f;

	/** 안쪽 아이콘 한 변(px). 마름모와 함께 약 4배 — 64→256. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|DestMarker",
		meta = (ClampMin = "8.0"))
	float IconSizePx = 256.f;

	/** 바닥에서 마름모를 띄우는 높이(cm). final §C-2 = 100cm. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|DestMarker")
	float MarkerHeightCm = 100.f;

	/** 화면 가장자리 방향 표시 여백(px). 커진 마름모가 화면 끝에서 잘리기 전에 화살표로 넘어가도록 넉넉히. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|DestMarker",
		meta = (ClampMin = "0.0"))
	float EdgeMarginPx = 120.f;

protected:
	/** WBP 없이 만들어졌으면 전체화면 빈 캔버스를 루트로 세운다(NativePaint 가 전체화면 위에 그린다). */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool bHasDest = false;
	FVector2D DestMapXY = FVector2D::ZeroVector;
	FString NodeType;
	FString Label;
	FLinearColor Accent = FLinearColor::Gray;
	float RemainingCm = 0.f;

	/** 아이콘 브러시(SetDestination 에서 1회 로드). 로드 실패면 마름모만 그린다. */
	FSlateBrush IconBrush;
	bool bHasIcon = false;

	void LoadIcon();

	/** 화면 안 목적지: 마름모 + 아이콘 + 거리 pill 을 At 에 그린다. */
	void DrawMarker(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geom,
		const FVector2D& At) const;

	/** 화면 밖 목적지: 가장자리 EdgePos 에 Dir 방향 화살표 + 거리를 그린다. */
	void DrawEdgeArrow(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geom,
		const FVector2D& EdgePos, const FVector2D& Dir) const;

	/** 거리 문자열("12 m"). */
	FString DistanceText() const;
};
