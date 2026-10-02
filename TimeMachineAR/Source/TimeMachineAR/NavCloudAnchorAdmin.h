// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "NavCloudAnchorAdmin.generated.h"

class UARPin;
class APlayerController;
class UNavCloudAdminOverlay;
class AActor;

// 11단계 관리자 등록 모드 — Cloud Anchor 호스팅(§D) + 포인트 bind·목록 UI(§E).
//
// ## 빌드 분리를 #if 로 클래스를 감싸서 하지 않는 이유 (UHT 제약)
// UHT 는 `UCLASS`/`UPROPERTY` 를 `#if`(WITH_EDITORONLY_DATA 외) 안에 두는 걸 금지한다.
// 그래서 **클래스 껍데기는 모든 빌드에서 컴파일**하고, 실제 Cloud Anchor 로직·플러그인
// include 는 `.cpp` 안에서 `#if NAV_ADMIN_MODE` 로만 감싼다(UHT 는 .cpp 를 안 읽는다).
// 헤더는 플러그인 타입을 일절 참조하지 않는다 — 멤버는 기반형 `UARPin`(AugmentedReality,
// 전 플랫폼 제공)만 쓰고, UCloudARPin 캐스팅은 .cpp 에서 한다.
//
// 빌드 분리의 실제 보장은 2중이다:
//  - **D10**: 이 브랜치(feature/ue-nav-stage11)를 develop 에 머지하지 않으므로 방문객
//    빌드엔 이 파일 자체가 없다.
//  - **D11**: `NAV_ADMIN_MODE` 는 `Build.cs` 에서 **Android 타깃에만** 정의된다. 정의가
//    없으면 기능 코드(.cpp)가 전부 비고 ShouldCreateSubsystem 이 false 라 생성조차 안 된다.
//
// ## 등록 흐름 (admin-mode §2, runbook §E)
//  1. 시작: `GET .../cloud-anchors?state=unbound` → 미등록 포인트 목록 → 오버레이에 표시.
//  2. **두 손가락 탭** = 다음 포인트 선택(순환). 현장에서 "지금 서 있는 포인트"를 고른다.
//  3. 바닥 비춤 → **한 손가락으로 바닥 탭** → 히트테스트 앵커 → `CreateAndHostCloudARPin`.
//  4. 호스팅 성공 → Δ 식으로 heading 계산 → `PUT .../points/{선택}/bind {cloud_id, heading}`.
//  5. 목록 갱신(해당 포인트 '완료'). 실패는 "다시 누르세요" 안내(품질 API 미노출).
// 한 손가락=호스팅, 두 손가락=선택 으로 갈라 UI 히트테스트 없이 충돌을 피한다.
//
// ## 범위 밖 → §F (별도 4차 빌드)
// 검증(앱 재시작 후 리졸브)·리졸브·**측위(세션→맵 좌표변환)**·인식 토스트는 §F. 좌표변환은
// 불확실 지점이라 admin-mode §7 대로 이 단계와 섞지 않는다.

/** 서버가 준 미등록/등록 포인트 한 건(표시·선택용 최소 정보). UObject 아님 → 평범한 멤버. */
struct FNavCloudAdminPoint
{
	int32   PointNo = 0;
	FString Edge;
	FString Label;
	FString CloudId;          // 바인딩된 구글 cloud anchor id(미등록이면 빈 값)
	bool    bBound = false;   // cloud_id 가 붙었나(=이미 등록)
};

/**
 * 에셋 미리보기(§임시) 엔트리: test 앵커 하나를 리졸브해 그 위에 에셋을 띄운다.
 * Pin 은 실체가 UCloudARPin 이지만 헤더를 플러그인 비의존으로 두려고 기반형으로 들고,
 * .cpp 에서 캐스팅한다.
 */
USTRUCT()
struct FNavCloudPreviewEntry
{
	GENERATED_BODY()

	UPROPERTY() int32 PointNo = 0;
	UPROPERTY() FString Label;
	UPROPERTY() FString CloudId;                     // 재시도용
	UPROPERTY() TObjectPtr<UARPin> Pin = nullptr;    // 리졸브 핀(UCloudARPin)
	UPROPERTY() TObjectPtr<AActor> Asset = nullptr;  // 앵커 위에 띄운 에셋
	UPROPERTY() double ResolveStart = 0.0;           // 이 시도 시작 시각
	UPROPERTY() int32  Retries = 0;                  // 재시도 횟수
};

/** 앵커별 에셋 오프셋(앵커 로컬). 실시간 보정값. */
USTRUCT()
struct FNavCloudOffset
{
	GENERATED_BODY()
	UPROPERTY() FVector Loc = FVector::ZeroVector; // cm (X=앞,Y=우,Z=위)
	UPROPERTY() float   Yaw = 0.f;                 // °
	UPROPERTY() float   Scale = 1.f;               // 배율
};

/** SaveGame: point_no → 오프셋. 앱 재시작·재설치에도 유지된다. */
UCLASS()
class TIMEMACHINEAR_API UNavCloudAdminSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TMap<int32, FNavCloudOffset> Offsets;
	UPROPERTY() FString ServerUrl;  // 앱에서 입력한 서버 주소(IP 안 굳게)
};

UCLASS()
class TIMEMACHINEAR_API UNavCloudAnchorAdminSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UWorldSubsystem — 게임/PIE 월드에서만
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject (UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

public:
	// ── 하단 버튼 핸들러 (오버레이 버튼 OnClicked 가 구독) ──────────────────
	UFUNCTION() void OnPrevClicked();
	UFUNCTION() void OnNextClicked();
	UFUNCTION() void OnRegisterClicked();
	/** 에셋 미리보기 토글(리졸브→앵커 위 에셋 배치). */
	UFUNCTION() void OnPreviewClicked();
	/** 입력한 서버 주소 적용(저장 + 목록 다시 불러오기). */
	UFUNCTION() void OnApplyServerUrl();

	// ── 에셋 오프셋 실시간 보정 버튼(미리보기 중) ───────────────────────────
	UFUNCTION() void OnNudgeFwd();    UFUNCTION() void OnNudgeBack();
	UFUNCTION() void OnNudgeLeft();   UFUNCTION() void OnNudgeRight();
	UFUNCTION() void OnNudgeUp();     UFUNCTION() void OnNudgeDown();
	UFUNCTION() void OnRotCCW();      UFUNCTION() void OnRotCW();
	UFUNCTION() void OnScaleUp();     UFUNCTION() void OnScaleDown();
	UFUNCTION() void OnResetOffset(); UFUNCTION() void OnStepToggle();

private:
	// ── §D 호스팅 ──────────────────────────────────────────────────────────
	void TryConfigureCloudMode();
	void PollPendingHost();
	/** 화면 중앙(조준점) 바닥에 앵커를 만들어 호스팅을 시작한다. 등록 버튼이 호출. */
	void RegisterAtCrosshair();

	// ── §E 선택·목록·bind ──────────────────────────────────────────────────
	bool ResolveServerConfig();
	void EnsureOverlay();
	void FetchPoints();
	void ApplyPointsJson(const FString& Body);
	/** dir(+1/-1) 방향으로 다음 포인트 선택(순환). */
	void SelectAdjacent(int32 Dir);
	void BindSelectedPoint(const FString& CloudId, float HeadingDeg);

	// ── 표시 ────────────────────────────────────────────────────────────────
	/** 헤더+목록+하단줄 을 조립해 오버레이에 밀어넣는다. */
	void ComposeBody(const FString& BottomLine, const FColor& Color);
	/** 매 틱 스캔/호스팅 상태를 직관적으로 갱신(조준점 색 + 하단줄 애니메이션). */
	void UpdateLiveStatus();
	/** 결과 메시지를 잠시(몇 초) 고정 표시하도록 기록. */
	void FlashResult(const FString& Msg, const FColor& Color);

	/** 현재 선택 포인트(없으면 nullptr). */
	const FNavCloudAdminPoint* SelectedPoint() const;

	// ── 상태 ────────────────────────────────────────────────────────────────
	bool bCloudConfigured = false;
	bool bConfigResolved = false;
	bool bPointsRequested = false;
	bool bPointsLoaded = false;
	bool bBindInFlight = false;

	/** 진행 중인 호스팅 핀(실체 UCloudARPin, .cpp 에서 캐스팅). */
	UPROPERTY(Transient)
	TObjectPtr<UARPin> PendingPin = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNavCloudAdminOverlay> Overlay = nullptr;

	TArray<FNavCloudAdminPoint> Points;
	int32 SelectedIdx = INDEX_NONE;

	FString ServerBaseUrl;
	FString MapId;
	FString SavedServerUrl;  // SaveGame 에서 로드한 사용자 지정 주소(있으면 우선)

	float HostAnchorYawDeg = 0.f;
	float HostCameraYawDeg = 0.f;

	// ── 스캔량 게이트(품질 막대 대용) ───────────────────────────────────────
	// UE 플러그인이 estimateFeatureMapQualityForHosting 을 안 줘서 실제 품질 수치는
	// 못 읽는다. 대신 **카메라가 움직인 양**(회전+이동)을 누적해 "충분히 둘러봤는지"를
	// proxy 로 재고, 그게 차기 전엔 등록을 막는다. 어제 앱의 품질 게이트를 대신한다.
	float   ScanAmount = 0.f;              // 누적 스캔량(이동 가중 + 회전)
	float   TransTotal = 0.f;             // 누적 이동량(cm) — 제자리 회전 방지용
	bool    bHasLastCam = false;
	FVector LastCamPos = FVector::ZeroVector;
	float   LastCamYaw = 0.f;
	/** 카메라 이동량을 누적한다(틱마다, 트래킹 양호일 때). */
	void AccumulateScan();

	// ── 에셋 미리보기(임시: 에셋 정렬 검증) ─────────────────────────────────
	/** 미리보기 진입: 등록된 test 앵커들을 리졸브 시작. */
	void EnterPreview();
	/** 미리보기 종료: 리졸브 핀·스폰한 에셋 정리. */
	void ExitPreview();
	/** 매 틱: 리졸브되면 에셋 스폰, 이후 앵커 트랜스폼으로 계속 위치 보정. */
	void TickPreview();
	/** 라벨(test1/test2)에 대응하는 에셋 BP 클래스 경로. 없으면 빈 문자열. */
	FString AssetClassPathForLabel(const FString& Label) const;

	bool bPreviewMode = false;
	UPROPERTY(Transient)
	TArray<FNavCloudPreviewEntry> PreviewEntries;

	// 에셋 오프셋(앵커별). SaveGame 으로 자동 저장 — 재시작·재설치에도 유지.
	UPROPERTY(Transient)
	TMap<int32, FNavCloudOffset> Offsets;
	int32 StepLevel = 0;                         // 0=×1, 1=×5, 2=×10
	/** code 로 선택 포인트의 오프셋을 조정하고 저장·표시 갱신. */
	void ApplyNudge(int32 Code);
	/** 오프셋 전체를 SaveGame 에 기록. */
	void SaveOffsets();
	/** SaveGame 에서 오프셋 로드(시작 시 1회). */
	void LoadOffsets();

	/** 결과 메시지 고정 표시: 이 시각(월드초)까지 하단줄에 ResultMsg 를 띄운다. */
	double ResultUntil = 0.0;
	FString ResultMsg;
	FColor  ResultColor = FColor::White;
	/** 호스팅 시작 시각(경과초 표시용). */
	double HostStartTime = 0.0;
};
