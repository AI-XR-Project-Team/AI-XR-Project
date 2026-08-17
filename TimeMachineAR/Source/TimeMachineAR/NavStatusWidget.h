#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavTypes.h"
#include "NavStatusWidget.generated.h"

class UWidget;
class UTextBlock;
class UNavMinimapWidget;
class UNavLocalizer;

/**
 * 안내 상태 배너(5-D 턴바이턴 · 도착)와 측위 경고(5-B 흔들림)를 한 위젯에서 상태 전환으로
 * 보여 준다. **로직은 여기(C++), WBP 는 배치·스타일만**(팀 원칙).
 *
 * ## 왜 이 클래스가 필요한가
 *
 * 5-C(실시간 미니맵)는 기존 미니맵 위젯 안에서 C++ 가 직접 그려 WBP 없이도 화면에 떴다.
 * 하지만 배너·경고·도착은 텍스트 UI라 UMG 위젯이 있어야 하고, 그 위젯이 없으면
 * OnGuidanceUpdated/OnTrackingDegraded 를 아무도 안 받아 **화면에 아무것도 안 뜬다.**
 * 이 클래스가 그 수신부다.
 *
 * ## WBP 로 상속할 때(WBP_NavStatus) — 배치만 하면 된다
 *
 * 자식은 전부 **선택(BindWidgetOptional)**. 이름만 맞추면 자동으로 채워진다:
 *   - BannerPanel    (아무 컨테이너) : 안내 중 보임. 안에 아래 텍스트를 넣는다.
 *       - InstructionText (TextBlock) : 현재 안내 문구 (예: "앞으로 6m 직진하세요")
 *       - DistanceText    (TextBlock) : 다음 회전/도착까지 남은 거리 (예: "4m")
 *       - NextText        (TextBlock) : 다음 안내 미리보기 (예: "다음: 우회전") — 선택
 *   - WarningPanel   (아무 컨테이너) : 측위 경고. 흔들림 감지 시 보임.
 *       - WarningText     (TextBlock) : 경고 문구 (예: "QR 을 다시 찍어 주세요")
 *       - (그 안의 버튼) OnClicked → RequestRescan 으로 이으면 "다시 찍기" 동작
 *   - ArrivalPanel   (아무 컨테이너) : 도착 화면. 목적지 도착 시 보임.
 *
 * 셋은 같은 자리를 써도 된다 — 우선순위(도착 > 경고 > 배너)로 하나만 보이게 이 클래스가 끈다.
 *
 * ## 배선 (BP 최소)
 *
 * - **5-B 경고는 배선 0.** NativeConstruct 가 NavLocalizer 의 델리게이트에 자동 연결한다.
 * - **5-D 배너는 한 노드.** 허브(WBP_DocentChat)의 Event Construct 에서
 *   `WBP_NavStatus.AttachToMinimap(WBP_NavMinimap)` 을 한 번 부르면 끝. (또는
 *   `WBP_NavMinimap.OnGuidanceUpdated → WBP_NavStatus.ApplyGuidance` 를 직접 배선해도 됨)
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UNavStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 미니맵의 안내 방송(OnGuidanceUpdated)을 이 위젯에 붙인다(5-D). 허브에서 한 번 부른다.
	 * 이미 붙어 있으면 다시 붙이지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Status")
	void AttachToMinimap(UNavMinimapWidget* Minimap);

	/** 안내 스냅샷으로 배너/도착을 갱신한다. OnGuidanceUpdated 에 직접 배선할 때 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Status")
	void ApplyGuidance(const FNavGuidance& Guidance);

	/** 측위 경고를 띄운다(수동). 보통은 NavLocalizer 자동 연결로 호출된다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Status")
	void ShowTrackingWarning(const FString& Reason);

	/** 측위 경고를 내린다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Status")
	void HideTrackingWarning();

	/** 경고의 "QR 다시 찍기" 버튼이 부를 자리 → NavLocalizer.RescanFromUser. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Status")
	void RequestRescan();

	/** 거리(cm) → 표시 문자열("4m", 아주 가까우면 "곧"). 배너 텍스트 스타일링용. */
	UFUNCTION(BlueprintPure, Category = "Nav|Status")
	static FString FormatDistanceCm(float DistanceCm);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- 배너(5-D) ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UWidget> BannerPanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UTextBlock> InstructionText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UTextBlock> DistanceText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UTextBlock> NextText;

	// --- 경고(5-B) ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UWidget> WarningPanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UTextBlock> WarningText;

	// --- 도착(5-D) ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Status")
	TObjectPtr<UWidget> ArrivalPanel;

	/**
	 * true 면 디자이너에서 배너를 보이게 둬 배치를 확인한다. 실행 중에는 무시된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Status")
	bool bPreviewInDesigner = true;

private:
	UFUNCTION()
	void HandleTrackingDegraded(const FString& Reason);
	UFUNCTION()
	void HandleTrackingRecovered();

	/** 우선순위(도착 > 경고 > 배너)로 패널 하나만 보이게 하고 텍스트를 채운다. */
	void RefreshPanels();

	static void SetWidgetShown(UWidget* W, bool bShown);
	static void SetTextSafe(UTextBlock* T, const FString& S);

	UNavLocalizer* ResolveLocalizer() const;

	/** 마지막 안내 스냅샷(경고가 걷힌 뒤 배너를 되살릴 때 참조). */
	FNavGuidance LastGuidance;
	bool bWarningActive = false;
	FString WarningMessage;

	/** 자동 연결한 NavLocalizer(정리용). */
	TWeakObjectPtr<UNavLocalizer> BoundLocalizer;
	/** AttachToMinimap 으로 붙은 미니맵(중복 방지·정리용). */
	TWeakObjectPtr<UNavMinimapWidget> BoundMinimap;
};
