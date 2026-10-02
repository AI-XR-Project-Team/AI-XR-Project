// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "NavCloudResolveHud.generated.h"

class UBorder;
class UTextBlock;

/**
 * 12단계 §E — **인식 토스트만** 있는 최소 HUD. `3번 앵커 인식 (1.8초)` 한 줄을 잠깐 띄운다.
 *
 * 토스트는 **개발모드에서만** 뜬다(`NavAppMode` — 앵커 번호·지연·설정 오류는 방문객에게 필요 없는 진단이다).
 * 사용자모드에도 보여야 하는 문구는 `ShowRecovered` 처럼 따로 둔다.
 *
 * 🚨 기존 네비 WBP(미니맵·안내 로그·도슨트)에 얹지 않는다 — 공지 트리거 3 회피(CLAUDE.md §3).
 * 8단계 안내 로그와 같은 방식으로 **WBP 없이 위젯 트리를 C++ 에서 세우고** 런타임에
 * `AddToViewport(250)` 한다(미니맵 100 · 안내로그 200 위, 관리자 오버레이 300 아래).
 * 관리자 오버레이(목록·버튼 12개)는 방문객 화면에 맞지 않아 재사용하지 않는다.
 */
UCLASS()
class TIMEMACHINEAR_API UNavCloudResolveHud : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 인식 토스트를 큐에 넣는다. 이미 같은 문구가 떠 있으면 무시하고, 여러 건은 순차로 보여 준다. */
	void ShowRecognized(int32 PointNo, float Seconds);

	/** 인식 + **측위까지 성립**했을 때(QR 마커 대체). 지도가 떴다는 뜻이라 문구를 나눈다. */
	void ShowLocalized(int32 PointNo, float Seconds);

	/** 임의 문구 토스트(실패·안내용). 개발모드에서만 뜬다 — 위 두 토스트도 이걸 거친다. */
	void ShowMessage(const FString& Message);

	/** 13-4 D50 — 점프 게이트를 통과해 위치를 고쳤다. **사용자모드에도** 뜬다(앵커 번호는 개발모드에서만 붙인다). */
	void ShowRecovered(int32 PointNo);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 문구 텍스트. WBP 로 상속하면 같은 이름의 자식이 자동으로 채워진다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|CloudResolve")
	TObjectPtr<UTextBlock> MessageText;

	/** 문구 뒤 배경 바(선택). 없으면 코드가 만든 것을 쓴다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|CloudResolve")
	TObjectPtr<UBorder> BarPanel;

private:
	/** 모드와 무관하게 큐에 넣는다. 같은 문구가 이미 있으면 무시한다. */
	void Enqueue(const FString& Message);

	/** 큐에서 다음 문구를 꺼내 띄운다. 비어 있으면 숨긴다. */
	void ShowNext();

	TArray<FString> Pending;
	FTimerHandle HideTimer;
};
