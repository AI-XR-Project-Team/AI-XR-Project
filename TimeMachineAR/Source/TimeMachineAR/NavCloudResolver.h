// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavCloudResolver.generated.h"

class UARPin;
class UNavCloudResolveHud;

// 12단계 §E — 일반모드(방문객) 빌드의 Cloud Anchor **리졸브**.
//
// ## 이 단계가 증명하는 것 / 하지 않는 것 (D18)
// 산출물은 "**인식됨 + 지연**" 뿐이다. 세션→맵 좌표변환·heading 판정·측위 연결은 13단계다.
// 그래서 이 서브시스템은 앵커를 잡아도 NavLocalizer 를 건드리지 않는다 — 기존 QR 측위와
// 완전히 분리돼 있어 실패해도 앱 동작이 달라지지 않는다.
//
// ## 등록은 자바 도구가 한다 (D14·D15)
// UE 플러그인은 estimateFeatureMapQualityForHosting 을 노출하지 않아 품질을 보증할 수 없다.
// 그래서 호스팅은 ARCore 1.37 자바 도구(저장소 밖)가 맡고, 여기선 그 결과를 서버에서 받아
// 리졸브만 한다. 11단계 관리자 등록 코드(NavCloudAnchorAdmin)는 폴백으로 남겨 둔다(D17).
//
// ## 흐름
//  1. AR 세션 Running → `ConfigGoogleARCoreServices(ARPinCloudMode=Enabled)`
//  2. `GET /maps/{map}/cloud-anchors?state=bound` → cloud_id 목록(12단계는 2개)
//  3. 전부 `CreateAndResolveCloudARPin` (D6 의 ≤40 롤링은 13단계)
//  4. `GetARPinCloudState()==Success` **그리고** `GetTrackingState()==Tracking` 이면 인식 확정
//     → HUD 토스트 `3번 앵커 인식 (1.8초)` + 로그
//  5. 12초 안에 확정되지 않으면 그 시도를 버리고 다시 요청한다(현장에서 걸어 다니며 재시도).
//
// ## 빌드 분리
// `GoogleARCoreServices` 의존은 **Android 타깃에만** 걸린다(Mac 에디터엔 GoogleARCoreSDK 가
// 없어 무조건 걸면 에디터 빌드가 깨진다 — Build.cs 주석). 그래서 플러그인 호출은 전부
// `#if NAV_CLOUD_RESOLVE` 안이고, 헤더는 플러그인 타입을 일절 참조하지 않는다(UHT 는 UCLASS·
// UPROPERTY 를 #if 안에 두는 걸 금지하므로 **클래스 껍데기는 항상 컴파일**한다 — 11단계 교훈).

/** 리졸브 대상 앵커 1건. Pin 의 실체는 UCloudARPin 이지만 헤더를 플러그인 비의존으로 두려고 기반형으로 든다. */
USTRUCT()
struct FNavCloudResolveEntry
{
	GENERATED_BODY()

	UPROPERTY() int32 PointNo = 0;
	UPROPERTY() FString CloudId;
	UPROPERTY() TObjectPtr<UARPin> Pin = nullptr;

	// 이 앵커가 맵의 어디에 있는가(서버 cloud_anchors 행). 측위 기준점으로 그대로 쓴다.
	UPROPERTY() float PosXCm = 0.f;
	UPROPERTY() float PosYCm = 0.f;
	UPROPERTY() float PosZCm = 0.f;
	/** 맵 +X 축 기준 CCW(도). 12단계는 계획값 90° — 실측 보정은 13단계(D18). */
	UPROPERTY() float HeadingDeg = 90.f;

	/** 이번 시도의 요청 시각(월드초). 토스트에 쓰는 지연은 이 값 기준이다. */
	UPROPERTY() double AttemptStart = 0.0;
	/** 최초 요청 시각(월드초) — 앱 시작부터 걸린 전체 시간(로그용). */
	UPROPERTY() double FirstRequest = 0.0;
	UPROPERTY() int32 Attempts = 0;
	/** 인식 확정되어 토스트를 띄웠나(같은 앵커 재표시 억제). */
	UPROPERTY() bool bRecognized = false;
	/** 이 핀이 마지막으로 Tracking 이던 월드 시각. 재관측(gap) 판정에 쓴다. */
	UPROPERTY() double LastTrackedTime = 0.0;
	/** 재보정(재래치) 횟수 — 로그·보고용. */
	UPROPERTY() int32 Relatches = 0;
	/** 재시도를 다 써서 포기했나(더 폴링하지 않는다). */
	UPROPERTY() bool bGaveUp = false;
	/** 이 앵커로 측위를 세운 적이 있나(로그·토스트 문구 구분용). */
	UPROPERTY() bool bLocalizeApplied = false;
};

/**
 * 방문객 빌드에서 도는 Cloud Anchor 리졸브 서브시스템.
 *
 * WorldSubsystem 이라 레벨·GameMode·기존 `.uasset` 배선이 **0** 이다(공지 트리거 회피 — CLAUDE.md §3).
 */
UCLASS()
class TIMEMACHINEAR_API UNavCloudResolverSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UWorldSubsystem — 게임/PIE 월드에서만
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	/** ARPinCloudMode=Enabled. 성공할 때까지 매 틱 재시도(세션이 늦게 뜰 수 있다). */
	void TryConfigureCloudMode();
	/** ini(NavClient 섹션)와 NavClient 서브시스템에서 서버 주소·맵 id 를 읽는다. NavClient 는 수정하지 않는다. */
	bool ResolveServerConfig();
	/** GET .../cloud-anchors?state=bound — 실패하면 잠시 뒤 다시 시도한다. */
	void FetchBoundAnchors();
	void ApplyAnchorsJson(const FString& Body);
	/** 한 건의 리졸브 요청을 (재)시작한다. 실패하면 Pin 이 null 로 남아 다음 틱이 다시 시도한다. */
	void StartResolve(FNavCloudResolveEntry& Entry);
	/** 진행 중인 리졸브들을 훑어 확정/타임아웃을 처리한다. */
	void PollResolves();
	/** 토스트 HUD 를 만든다(런타임 생성, WBP 없음). */
	void EnsureHud();
	/**
	 * 인식된 앵커로 **측위를 세운다**(QR 마커 대체 — 사용자 요청). 이미 측위돼 있으면
	 * 건드리지 않는다(마커가 더 정확하므로 QR 측위를 덮지 않는다).
	 *
	 * 네비 버튼이 `StartLocalizing` → `ResetLocalization` 으로 측위를 비우는 경로가 있어
	 * **매 틱 다시 확인**한다. 그래야 순서에 상관없이(앵커 먼저 / 네비 먼저) 지도가 뜬다.
	 *
	 * @return 이 호출로 새로 측위가 성립했으면 true.
	 */
	bool TryLocalizeWithAnchor(FNavCloudResolveEntry& Entry);

	/**
	 * 이미 인식된 앵커를 **계속 지켜본다**(마커의 상시 재탐색과 같은 자리).
	 *
	 * 마커 측위의 `bRelatchOnReacquire` 규칙을 그대로 옮겼다 — 매 틱 다시 세우면 추적
	 * 노이즈가 그대로 실려 지도가 떨리므로, **핀을 한동안 놓쳤다가 다시 잡은 순간**에만
	 * 변환을 다시 세운다. 그 순간은 새 관측이 들어온 시점이라 요동 없이 드리프트만 씻긴다.
	 *
	 * 핀이 죽었거나(오류 상태) 사라졌으면 **리졸브를 다시 걸어** 항상 잡을 준비를 유지한다.
	 */
	void WatchRecognizedAnchor(FNavCloudResolveEntry& Entry, double Now);

	/** 서버 목록을 주기적으로 다시 받아 **새로 등록된 앵커·바뀐 좌표**를 반영한다. */
	void RefreshAnchorsIfDue(double Now);

	/** 카메라의 현재 맵 좌표(측위 전이면 ZeroVector). 재보정 드리프트 측정용. */
	FVector GetCameraMapLocation() const;

	bool bCloudConfigured = false;
	bool bConfigResolved = false;
	bool bFetchInFlight = false;
	bool bAnchorsLoaded = false;
	/** 다음 목록 조회를 시도할 월드 시각(실패 시 백오프). */
	double NextFetchTime = 0.0;
	/** 목록을 주기적으로 다시 받을 월드 시각(등록이 늘거나 heading 이 바뀔 수 있다). */
	double NextRefreshTime = 0.0;

	FString ServerBaseUrl;
	FString MapId;

	UPROPERTY(Transient)
	TArray<FNavCloudResolveEntry> Entries;

	UPROPERTY(Transient)
	TObjectPtr<UNavCloudResolveHud> Hud = nullptr;
};
