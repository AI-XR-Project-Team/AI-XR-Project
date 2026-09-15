// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavCloudAdminOverlay.generated.h"

class UTextBlock;
class UBorder;
class UButton;
class UEditableTextBox;

/**
 * 관리자 Cloud Anchor 등록 화면의 **보이는 오버레이**(11단계 §E, 버튼 방식).
 *
 * ## 왜 C++ 오버레이인가 (§D 교훈 + admin-mode §3)
 * §D 에서 `AddOnScreenDebugMessage` 가 앱 UI 에 가려 안 보였다. 그래서 8단계 안내로그와 같은
 * 패턴으로 C++ 에서 위젯 트리를 세워 높은 ZOrder 로 AddToViewport 한다(기존 .uasset 무수정).
 *
 * ## 구성 (어제 쓰던 등록 앱처럼 버튼으로)
 *  - 상단 바: 포인트 목록 + 상태 한 줄(BodyText)
 *  - 화면 중앙: 조준점 `＋` — 여기 바닥에 앵커가 놓인다(탭 대신 조준)
 *  - 하단 버튼: `◀ 이전` / `다음 ▶`(포인트 선택) · **`여기 등록`**(호스팅+bind)
 * 버튼 OnClicked 는 `UNavCloudAnchorAdminSubsystem` 이 구독한다. 순수 UMG 만 참조(ARCore 없음).
 */
UCLASS()
class TIMEMACHINEAR_API UNavCloudAdminOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 여러 줄 상태 텍스트(목록·선택·결과)를 통째로 바꾼다. */
	void SetBody(const FString& InText, const FColor& InColor = FColor::White);

	/** 중앙 조준점 색(빨강=트래킹X / 노랑=스캔중 / 초록=등록 준비). 스캔 상태를 직관적으로 보여준다. */
	void SetCrosshairColor(const FColor& InColor);

	/** 미리보기 버튼을 ON/OFF 상태로 표시(라벨·색 변경). */
	void SetPreviewActive(bool bActive);

	/** 오프셋 보정 패드 표시/숨김(미리보기일 때만 보임). */
	void SetNudgeVisible(bool bVisible);

	/** 스텝 버튼 라벨 변경(×1/×5/×10). */
	void SetStepLabel(const FString& InText);

	/** 서버주소 입력칸 텍스트 읽기/쓰기. */
	FString GetServerUrlText() const;
	void SetServerUrlText(const FString& InUrl);

	/** 서버주소 입력칸 + 적용 버튼. */
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ServerUrlBox;
	UPROPERTY(Transient) TObjectPtr<UButton> ApplyUrlBtn;

	// 오프셋 보정 버튼들(서브시스템이 OnClicked 구독).
	UPROPERTY(Transient) TObjectPtr<UButton> NudgeFwd;
	UPROPERTY(Transient) TObjectPtr<UButton> NudgeBack;
	UPROPERTY(Transient) TObjectPtr<UButton> NudgeLeft;
	UPROPERTY(Transient) TObjectPtr<UButton> NudgeRight;
	UPROPERTY(Transient) TObjectPtr<UButton> NudgeUp;
	UPROPERTY(Transient) TObjectPtr<UButton> NudgeDown;
	UPROPERTY(Transient) TObjectPtr<UButton> RotCCW;
	UPROPERTY(Transient) TObjectPtr<UButton> RotCW;
	UPROPERTY(Transient) TObjectPtr<UButton> ScaleUp;
	UPROPERTY(Transient) TObjectPtr<UButton> ScaleDown;
	UPROPERTY(Transient) TObjectPtr<UButton> ResetBtn;
	UPROPERTY(Transient) TObjectPtr<UButton> StepBtn;

	/** 하단 버튼들(서브시스템이 OnClicked 를 구독한다). */
	UPROPERTY(Transient) TObjectPtr<UButton> PrevButton;
	UPROPERTY(Transient) TObjectPtr<UButton> NextButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RegisterButton;
	/** 에셋 미리보기(리졸브→앵커 위에 에셋 배치) 토글. */
	UPROPERTY(Transient) TObjectPtr<UButton> PreviewButton;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> BodyText;
	UPROPERTY(Transient) TObjectPtr<UBorder> BarPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CrosshairText;
	/** 미리보기 버튼의 라벨(상태에 따라 글자 바꿈). */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewLabel;
	/** 오프셋 보정 패드(미리보기일 때만 표시). */
	UPROPERTY(Transient) TObjectPtr<UBorder> NudgePanel;
	/** 스텝 버튼 라벨(×1/×5/×10 표시). */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StepLabelText;
};
