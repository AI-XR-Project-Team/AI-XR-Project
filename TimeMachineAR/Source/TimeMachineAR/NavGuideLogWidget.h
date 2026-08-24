#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "NavTypes.h"
#include "NavGuideLogWidget.generated.h"

class UBorder;
class UTextBlock;
class UNavMinimapWidget;
class UNavLocalizer;

/** 안내 로그가 보여 주는 단계(final §D 표). node_type 은 문구 갈래를 위해 따로 본다. */
UENUM()
enum class ENavGuidePhase : uint8
{
	/** 네비 켬, 아직 측위 전. "바닥의 마커를 인식시켜주세요". */
	NavOn,
	/** 측위 성립. "우측의 미니맵을 터치하여 목적지를 설정해주세요". */
	Localized,
	/** 확대 지도 열림. "지도의 아이콘을 누르거나 …". */
	FullMapOpen,
	/** 안내 중. exhibit → 발자국 / facility·entrance → 화살표(FNavDestinations). */
	Guiding,
	/** 목적지 도착. exhibit → "(공룡) 앞에 도착…" / 그 밖 → "목적지에 도착". */
	Arrived,
	/** 전시물 마커 인식 완료(초록). "인식이 완료되었습니다 …". */
	Recognized,
};

/**
 * 화면 상단 한 줄 **안내 로그**(8단계 §D). 지금 사용자가 무엇을 해야 하는지 상태별 문구로 띄운다.
 *
 * ## 왜 C++ 오버레이인가 (final §0-E)
 *
 * 상단바(팀원2 WBP_DocentChat)에 얹으면 공지 트리거다. 그래서 전체 지도(4단계)와 같은 패턴으로
 * **우리 위젯을 C++ 에서 만들어 AddToViewport** 한다. WBP 도 새로 안 만든다 — 위젯 트리를
 * RebuildWidget 에서 직접 세워 에디터 작업을 0 으로 둔다(WBP 로 꾸미려면 상속해서 같은 이름의
 * 자식만 두면 그쪽이 우선한다).
 *
 * ## 상태는 신호로만 굴러간다 (새 데이터 0)
 *
 * BindToMinimap 이 미니맵·측위 서브시스템의 기존 델리게이트에 붙는다:
 *   - NavLocalizer.OnLocalized       → 측위 성립
 *   - NavLocalizer.OnLocalizationLost→ 측위 상실(다시 마커 인식)
 *   - NavLocalizer.OnAnchorChanged   → 전시물 마커 인식(초록)
 *   - Minimap.OnFullMapOpenChanged   → 확대 지도 열림/닫힘
 *   - Minimap.OnDestinationChosen    → 안내 시작
 *   - Minimap.OnGuidanceUpdated      → 안내 중 / 도착
 * 목적지 종류(node_type·label)는 미니맵이 이미 들고 있어 그때그때 물어본다.
 */
UCLASS()
class TIMEMACHINEAR_API UNavGuideLogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 미니맵·측위 신호에 연결한다. 미니맵이 CreateWidget 직후 한 번 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|GuideLog")
	void BindToMinimap(UNavMinimapWidget* Minimap);

	/** 지금 문구/색을 다시 계산해 표시한다. */
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 문구를 띄우는 텍스트. WBP 로 상속하면 같은 이름의 자식이 자동으로 채워진다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<UTextBlock> MessageText;

	/** 문구 뒤 배경 바(선택). 없으면 코드가 만든 것을 쓴다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<UBorder> BarPanel;

private:
	UFUNCTION() void HandleLocalized(const FString& MarkerCode);
	UFUNCTION() void HandleLocalizationLost();
	UFUNCTION() void HandleAnchorChanged(const FString& MarkerCode);
	UFUNCTION() void HandleFullMapOpenChanged(bool bOpen);
	UFUNCTION() void HandleDestinationChosen(const FString& NodeId);
	UFUNCTION() void HandleGuidanceUpdated(const FNavGuidance& Guidance);

	/** 단계 → 문구·색을 정해 텍스트에 반영. */
	void ApplyPhase();

	/**
	 * 미니맵 상태에 맞춰 로그의 표시/숨김을 동기화하고 문구를 갱신한다. **월드 타이머**로 돈다.
	 * 위젯 NativeTick 은 Collapsed 되면 안 돌아(자기 자신을 숨기면 되살아날 tick 이 없다) 못 쓴다 —
	 * 타이머는 visibility 와 무관하게 돌아 미니맵이 다시 떠도 로그를 되살린다.
	 */
	void SyncWithMinimap();

	/** SyncWithMinimap 반복 타이머 핸들. NativeDestruct 에서 해제. */
	FTimerHandle VisSyncTimer;

	/** 현재 목적지가 정해졌나(안내/도착 문구 갈래에 필요). */
	bool bHasDestination = false;

	ENavGuidePhase Phase = ENavGuidePhase::NavOn;

	TWeakObjectPtr<UNavMinimapWidget> BoundMinimap;
	TWeakObjectPtr<UNavLocalizer> BoundLocalizer;
};
