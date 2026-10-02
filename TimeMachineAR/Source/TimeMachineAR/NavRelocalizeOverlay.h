// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavRelocalizeOverlay.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;
class UWidget;
class UNavLocalizer;

/** 스캔 사이클 한 순간의 폰 실루엣 자세(시안 구간표 — `UNavRelocalizeOverlay::EvalScanPose`). */
struct FNavRelocScanPose
{
	/** 가운데 기준 가로 이동(설계 px, ±37). */
	float X = 0.f;
	/** 기울기(도, 시계 방향 +). */
	float AngleDeg = 0.f;
	float Opacity = 1.f;
};

/**
 * 13-4 D52 rev4 — **재측위 오버레이**. 측위가 틀어지면 안내 대신 화면 가운데에
 * "가만히 서서 주변을 천천히 스캔해 주세요" 와 스캔 그래픽을 띄우고, 복구되면 ✓ "측위가 잡혔습니다" 뒤 사라진다.
 *
 * 🚨 WBP 0 — `NavCloudResolveHud` 처럼 `RebuildWidget` 에서 C++ 로 트리를 세우고 런타임에만 `AddToViewport(260)` 한다
 * (토스트 250 위 · 관리자 300 아래). 기존 위젯에 얹지 않는다(CLAUDE.md §3).
 *
 * ## 화면에 얹는 건 셋뿐 (시안 `docs/nav-stage13-4/graphic-options.html`)
 *   ① 화면 전체 남색 어둡기 α`RelocDimAlpha`(0.20 — AR 카메라가 거의 그대로 보인다)
 *   ② 가운데 스캔 그래픽(220×170) — 폰 실루엣·시야 부채꼴·트랙+화살촉·안내판 아이콘 5·속도선 3 / 복구 땐 ✓ 링
 *   ③ 글씨 박스(둥근 α`RelocTextBoxAlpha`, 하늘색 1px 테두리) — 제목 + 이유별 보조 문구
 *
 * ## 트리
 *   Dim(UBorder, 전면) → UScaleBox(ScaleToFit) → USizeBox(320×676 설계 공간) → UVerticalBox(가운데)
 *     ├ USizeBox(220×170) → StageCanvas → ScanGroup(부품들 · PhoneGroup) / OkGroup(✓)
 *     └ TextBox(UBorder) → UVerticalBox → TitleText · SubText
 * 수치는 전부 **시안 CSS px** 그대로다. 시안은 폭 320px 폰 목업이라, 설계 공간을 뷰포트에 맞춰 통째로 늘려야
 * 승인된 비율(그래픽 = 화면 폭의 69%)이 폰에서도 나온다(S21 은 DPI 배율 1.0 이라 px 그대로 두면 손톱만 해진다).
 *
 * ## 애니메이션 — Tick 한 함수, 에셋 0
 * t' = t mod 3.9 를 런북 구간표대로 선형 보간한다(EvalScanPose). 부품은 전부 UBorder(엔진 흰 브러시).
 * ⚠️ 둥근 브러시의 **외곽선은 렌더 불투명도를 따르지 않는다**(엔진 DrawElementTypes.cpp — bUseBrushTransparency 가
 * 꺼져 있으면 외곽선 색 그대로) → 폰·✓ 링·글씨 박스 외곽선은 알파가 바뀔 때 브러시를 다시 넣는다.
 *
 * 입력: 떠 있는 동안 터치를 **먹는다**(Visible + 핸들러 Handled) — 틀린 위치로 네비를 누르지 않게.
 */
UCLASS()
class TIMEMACHINEAR_API UNavRelocalizeOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 스캔 화면으로(재진입이면 처음부터). Reason = shake · occluded · dark · featureless · jump · resume. */
	void ShowScanning(const FString& Reason);

	/** 오래 못 잡을 때 보조 문구 단계. 0 = 이유별 · 1 = 30초 · 2 = 90초(버튼 없음 — 명세 §3.3). */
	void SetStageText(int32 Stage);

	/** 복구: 그래픽 숨김 → ✓ 링 pop 0.5초 → 1.5초 뒤 0.3초 페이드 → RemoveFromParent. */
	void ShowRecovered();

	/** ini 값(NavLocalizer)을 넣는다. ShowScanning 전에. */
	void SetAlphas(float InDimAlpha, float InTextBoxAlpha);

	bool IsShowingRecovered() const { return bRecovered; }
	/** 페이드까지 끝나 화면에서 내려갔다. 서브시스템이 참조를 놓는다. */
	bool IsFinished() const { return bFinished; }

	// --- 순수 헬퍼 (헤드리스 테스트 대상) ---

	/** 이유 코드 → 보조 문구(명세 §3.4). 모르는 코드는 "위치를 다시 확인하고 있어요". */
	static FString SubTextForReason(const FString& Reason);
	/** 단계별 보조 문구(0 이면 이유별). */
	static FString StageSubText(int32 Stage, const FString& Reason);
	/** 사이클 시각(초, 0~3.9)의 폰 자세 — 런북 §B-3 구간표. */
	static FNavRelocScanPose EvalScanPose(float CycleTime);

	/** 한 사이클 길이(초). */
	static constexpr float ScanCycleSeconds = 3.9f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	void BuildTree();
	/** 스캔 중/복구 색 조합을 입힌다(어둡기·글씨 박스·제목). */
	void ApplyStyle(bool bRecoveredStyle);
	void TickScan(float CycleTime);
	void TickRecovered(float T);
	/** 외곽선 있는 부품의 알파를 다시 넣는다(렌더 불투명도가 외곽선엔 안 먹는다). */
	void SetPhoneAlpha(float Alpha);
	void SetOutlinedAlpha(float TextBoxOutlineAlpha, float RingAlpha);

	UPROPERTY(Transient) TObjectPtr<UBorder> Dim;
	UPROPERTY(Transient) TObjectPtr<UBorder> TextBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ScanGroup;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> PhoneGroup;
	UPROPERTY(Transient) TObjectPtr<UBorder> PhoneBody;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> Icons;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> SpeedLines;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> OkGroup;
	UPROPERTY(Transient) TObjectPtr<UBorder> OkRing;

	FString Reason;
	int32 Stage = 0;
	bool bRecovered = false;
	bool bFinished = false;
	/** 현재 화면(스캔/복구)이 시작된 뒤 흐른 시간(초). */
	float Clock = 0.f;
	float DimAlpha = 0.20f;
	float TextBoxAlpha = 0.62f;
	/** 마지막으로 브러시에 넣은 폰 외곽선 알파(같은 값이면 다시 안 넣는다). */
	float AppliedPhoneAlpha = -1.f;
};

/**
 * 13-4 — 오버레이를 **만들고 치우는** 곳. NavLocalizer 의 `OnRelocalizationStarted` · `OnRelocalized` 에 붙는다.
 * 30초·90초 문구 단계 전환도 여기서 센다. 게임·PIE 월드에만 생긴다(리졸버와 같은 조건 — 레벨·WBP 배선 0).
 */
UCLASS()
class TIMEMACHINEAR_API UNavRelocalizeOverlaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 떠 있는 오버레이가 있을 때만 돈다. */
	virtual bool IsTickable() const override { return Super::IsTickable() && Overlay != nullptr; }

private:
	UFUNCTION()
	void HandleRelocalizationStarted(const FString& Reason);

	UFUNCTION()
	void HandleRelocalized(const FString& SourceCode);

	UPROPERTY(Transient)
	TObjectPtr<UNavRelocalizeOverlay> Overlay = nullptr;

	TWeakObjectPtr<UNavLocalizer> Localizer;

	/** 지금 보여 주는 문구 단계(0·1·2). */
	int32 ShownStage = 0;
};
