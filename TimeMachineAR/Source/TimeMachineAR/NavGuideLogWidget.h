#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "NavTypes.h"
#include "NavGuideLogWidget.generated.h"

class UBorder;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UNavMinimapWidget;
class UNavLocalizer;

/** 안내 로그가 보여 주는 단계. node_type 은 문구 갈래를 위해 따로 본다. */
UENUM()
enum class ENavGuidePhase : uint8
{
	/** 네비 켬, 아직 측위 전(측위 상실도 여기로 돌아온다). "바닥의 마커를 비춰주세요". */
	NavOn,
	/** 측위 성립. "미니맵을 눌러 목적지를 골라주세요". */
	Localized,
	/** 확대 지도 열림. 전체 지도가 자기 렉시 말풍선을 가지므로 여기서는 숨긴다. */
	FullMapOpen,
	/** 안내 중. exhibit → 발자국 / facility·entrance → 화살표(FNavDestinations). */
	Guiding,
	/** 경로에서 벗어남(§3). 발자국이 보이는 곳으로 돌아오라는 문구. */
	OffRoute,
	/** 목적지 도착. exhibit → "(공룡) 앞에 도착…" / 그 밖 → 시설·입구별 문구. */
	Arrived,
	/** 전시물 마커 인식 완료(초록). "인식 완료! …". */
	Recognized,
	/** 도착 후 자동 종료 중(§4). Ended 이벤트를 받아 잠깐 마무리 문구를 보여 준다. */
	Ended,
};

/**
 * 화면 상단 **렉시 말풍선**(nav-lexi-guide-design.md §3). 지금 사용자가 무엇을 해야 하는지
 * 상태별 문구를 렉시가 하는 말처럼 아바타 + 이름표 + 말풍선으로 띄운다.
 *
 * ## 왜 C++ 오버레이인가
 *
 * 상단바(팀원2 WBP_DocentChat)에 얹으면 공지 트리거다. 그래서 전체 지도와 같은 패턴으로
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
 *   - Minimap.OnGuidanceUpdated      → 안내 중 / 이탈 / 도착
 *   - Minimap.OnNavigationEnded      → 도착 후 자동 종료의 마무리 문구(§4)
 *   - Minimap.OnNavigationClosed     → 정리 완료, 목적지 없음으로 복귀(§4)
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

	/**
	 * 렌더 미리보기·자동화 테스트 전용. 미니맵 없이 단독 생성했을 때 상태를 강제로 밀어 넣는다.
	 *
	 * 실제 앱은 항상 BindToMinimap 이 신호로만 상태를 바꾸지만, 헤드리스 렌더 테스트
	 * (`TimeMachineAR.Nav.GuideLogPreview`)는 미니맵·측위 서브시스템 없이 GuideLog 만
	 * 떼어 만들어 4장을 찍는다. NativeConstruct 는 미니맵이 실제로 그려질 때까지 위젯을
	 * Collapsed 로 두므로(SyncWithMinimap), 이 함수가 대신 HitTestInvisible 로 켜 준다.
	 */
	void PreviewSetState(ENavGuidePhase InPhase, const FString& NodeType, const FString& Label,
		const FNavGuidance& Guidance);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 렉시 아바타. WBP 로 상속하면 같은 이름의 자식이 자동으로 채워진다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<UImage> AvatarImage;

	/** 아바타를 감싸는 고정 크기 칸(96×96). 텍스처 로드 실패 시 이 칸을 접는다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<USizeBox> AvatarBox;

	/** 말풍선 배경. FSlateRoundedBoxBrush 로 유리 패널 + 단계별 강조색 외곽선을 그린다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<UBorder> BubblePanel;

	/** 말풍선 안의 "렉시" 이름표. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<UTextBlock> NameText;

	/** 문구를 띄우는 텍스트. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|GuideLog")
	TObjectPtr<UTextBlock> MessageText;

private:
	UFUNCTION() void HandleLocalized(const FString& MarkerCode);
	UFUNCTION() void HandleLocalizationLost();
	UFUNCTION() void HandleAnchorChanged(const FString& MarkerCode);
	UFUNCTION() void HandleFullMapOpenChanged(bool bOpen);
	UFUNCTION() void HandleDestinationChosen(const FString& NodeId);
	UFUNCTION() void HandleGuidanceUpdated(const FNavGuidance& Guidance);
	UFUNCTION() void HandleNavigationEnded();
	UFUNCTION() void HandleNavigationClosed();

	/** 단계 → 문구·색·아바타를 정해 위젯에 반영. */
	void ApplyPhase();

	/** 아바타 4종(lexi_question/pointing/happy/idle)을 처음 한 번만 로드한다. */
	void EnsureAvatarTextures();

	/**
	 * AR 스캔 화면의 렉시 말풍선(WBP_DocentChat 의 ScanHintBG/RefHintRobot/ScanHintText)과 **같은 만듦새**를
	 * 그 위젯에서 그대로 복사해 온다(브러시·로봇 그림·글꼴·크기). WBP 를 건드리지 않고 "스캔 화면과 똑같이"를
	 * 보장하는 방법이다. 성공하면 true — 이후 ApplyPhase 는 외곽선 색만 단계별로 바꾼다.
	 * DocentChat 위젯이 아직 없으면(프리뷰 테스트 등) 코드 기본 스타일을 쓴다.
	 */
	bool AdoptScanHintStyle();
	bool bScanStyleAdopted = false;
	FSlateBrush ScanBubbleBrush;

	/**
	 * 미니맵 상태에 맞춰 로그의 표시/숨김을 동기화하고 문구를 갱신한다. **월드 타이머**로 돈다.
	 * 위젯 NativeTick 은 Collapsed 되면 안 돌아(자기 자신을 숨기면 되살아날 tick 이 없다) 못 쓴다 —
	 * 타이머는 visibility 와 무관하게 돌아 미니맵이 다시 떠도 로그를 되살린다.
	 */
	void SyncWithMinimap();

	/** SyncWithMinimap 반복 타이머 핸들. NativeDestruct 에서 해제. */
	FTimerHandle VisSyncTimer;

	/** 현재 목적지가 정해졌나(안내/이탈/도착 문구 갈래에 필요). */
	bool bHasDestination = false;

	ENavGuidePhase Phase = ENavGuidePhase::NavOn;

	/** Guiding 단계의 최신 턴바이턴 안내. 매 틱 바뀔 수 있어 ApplyPhase 가 다시 읽어 문구를 만든다. */
	FNavGuidance LastGuidance;

	/** PreviewSetState 로 들어온 목적지 override(미니맵이 없을 때만 쓴다. 테스트 전용). */
	bool bUsePreviewDestination = false;
	FString PreviewNodeType;
	FString PreviewLabel;

	TWeakObjectPtr<UNavMinimapWidget> BoundMinimap;
	TWeakObjectPtr<UNavLocalizer> BoundLocalizer;

	// ---------------------------------------------------------------- 아바타 텍스처 캐시
	// SetResourceObject 로 브러시가 raw 포인터를 들고 있으므로 GC 로부터 지켜야 한다.

	bool bAvatarTexturesLoaded = false;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> AvatarQuestionTex;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> AvatarPointingTex;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> AvatarHappyTex;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> AvatarIdleTex;
};
