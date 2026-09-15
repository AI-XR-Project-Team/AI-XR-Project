// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavAssetTuneOverlay.generated.h"

class UBorder;
class UButton;
class UTextBlock;

/** 조정 패드 버튼 코드 — 스포너가 한 함수(ApplyTune)로 받는다. */
UENUM()
enum class ENavAssetTune : uint8
{
	Forward, Back, Left, Right, Up, Down,
	RotCCW, RotCW, ScaleUp, ScaleDown,
	Step, NextTarget, Reset, Revert
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavAssetTune, ENavAssetTune);

/**
 * 13-1 — 앵커 위 에셋의 **위치·회전·크기 조정 패드**(11단계 관리자 미리보기 패드의 방문객 앱판).
 *
 * ## 🚨 기존 화면 위에 얹는다 (CLAUDE.md §3 트리거 3 — 2026-09-15 현우 확인: 13-1 로컬 앱 한정, 공지 불필요)
 * - WBP 없이 C++ 로 위젯 트리를 세워 `AddToViewport(260)` — 토스트(250) 위, 관리자(300) 아래
 * - 평소엔 오른쪽 가장자리의 작은 **「조정」 토글 버튼**만 보인다(에셋이 떠 있을 때만).
 *   누르면 패드가 열리고, 다시 누르면 닫힌다 → 촬영 때는 닫아 두면 화면이 깨끗하다
 * - 루트는 SelfHitTestInvisible — 버튼 밖 터치는 아래 앱 UI 로 그대로 간다
 *
 * 순수 UMG 만 참조한다(ARCore 없음) — Mac 에디터 타깃에서도 컴파일된다.
 */
UCLASS()
class TIMEMACHINEAR_API UNavAssetTuneOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 버튼이 눌리면 알린다(스포너가 구독). */
	FOnNavAssetTune OnTune;

	/** 에셋이 하나라도 떠 있을 때만 토글 버튼을 보인다. 없으면 패드도 닫는다. */
	void SetAvailable(bool bAvailable);
	/** 패드 본문(대상·현재값·저장 상태). */
	void SetStatus(const FString& InText, const FColor& InColor = FColor::White);
	/** 스텝 버튼 라벨. */
	void SetStepLabel(const FString& InText);
	/** 대상이 2개 이상일 때만 「대상 ▶」 버튼을 보인다. */
	void SetMultiTarget(bool bMulti);
	bool IsPadOpen() const { return bPadOpen; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleToggle();
	UFUNCTION() void HandleFwd();
	UFUNCTION() void HandleBack();
	UFUNCTION() void HandleLeft();
	UFUNCTION() void HandleRight();
	UFUNCTION() void HandleUp();
	UFUNCTION() void HandleDown();
	UFUNCTION() void HandleRotCCW();
	UFUNCTION() void HandleRotCW();
	UFUNCTION() void HandleScaleUp();
	UFUNCTION() void HandleScaleDown();
	UFUNCTION() void HandleStep();
	UFUNCTION() void HandleNextTarget();
	UFUNCTION() void HandleReset();
	UFUNCTION() void HandleRevert();

	void SetPadOpen(bool bOpen);

	bool bPadOpen = false;
	bool bAvailable = false;

	UPROPERTY(Transient) TObjectPtr<UButton> ToggleBtn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToggleLabel;
	UPROPERTY(Transient) TObjectPtr<UBorder> PadPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StepLabel;
	UPROPERTY(Transient) TObjectPtr<UButton> NextTargetBtn;
};
