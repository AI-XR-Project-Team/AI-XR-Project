// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavCloudAssetSpawner.generated.h"

class AARTrackingManager;
class UARPin;
class UDinoInfoData;
class UTimeRevealProfile;

// 13-1 §C-1 — 하단 바의 「AR 스캔」 을 누르면 label 이 `asset*` 인 Cloud Anchor 를 리졸브해
// 그 앵커 **바로 위에 에셋(Archelon)을 띄운다**. 시연영상 촬영이 목적이다.
//
// ## 앵커를 역할로 나눈다 (D23)
// `asset*`  = 에셋 전용. 좌표가 촬영용 임의값이라 **측위에 쓰지 않는다**(리졸버가 건너뛴다).
// 그 외     = 네비 포인트. 측위 전용이고 에셋을 띄우지 않는다(여기서 건너뛴다).
// 두 서브시스템이 같은 `?state=bound` 목록을 받아 **라벨로 갈라 가진다**.
//
// ## 기존 것을 고치지 않는다 (CLAUDE.md §3)
// `AARTrackingManager::OnScanStateChanged` 는 이미 BlueprintAssignable 로 열려 있어
// **바인딩만** 한다 — 매니저·스캔 WBP·`DinoOverlayActor` 수정 0. WorldSubsystem 이라
// 레벨·GameMode·`.uasset` 배선도 0 이다. → 13-1 팀 공지 0건.
//
// ## 스캔 ON/OFF 를 이렇게 해석한다 (런북 §C-1 에서 한 칸 달라진 곳)
// 런북은 "스캔 OFF 에 스폰한 액터를 지운다" 였다. 그런데 매니저는 **마커를 찾으면 스스로
// `StopScan()` 을 부르고**(ARTrackingManager.cpp:291) 카메라를 뒤집을 때도 부른다 — 그대로
// 따르면 근처에 QR 마커 한 장만 있어도 에셋이 뜨자마자 사라진다. 그래서
//   - **스캔 ON** = 이전 것을 지우고 처음부터 다시 잡는다 (매니저의 `StartScan`→`ClearOverlay`
//     와 같은 자리. 재스캔 중복이 여기서 막힌다)
//   - **스캔 OFF** = 새 시도를 더 걸지 않는다. **진행 중이던 리졸브는 끝까지 간다**
//     (스캔 1초 만에 마커가 잡혀 꺼져도 몇 초 뒤 에셋이 떠야 한다). 떠 있는 에셋은 그대로 둔다
// 로 옮겼다. 지우는 시점만 OFF → 다음 ON 으로 옮긴 것이고, "중복 안 생김"은 그대로다.
//
// ## 현장 보강 (13-1 재개 때 추가 — 런북 §C-1 밖)
// - **종 데이터(DA)를 스폰 전에 넣는다** (`AssetDinoInfoPath`). 매니저가 마커로 공룡을 띄울 때의
//   `SpawnActorDeferred → SetDinoInfo → FinishSpawning` 조립과 같다(ARTrackingManager.cpp
//   CheckForTrackedImages — 거기선 `BP_DinoOverlay_T-Rex` 하나에 종별 `DA_Dino_*` 를 끼운다).
//   `BP_DinoOverlay_Archelon` 은 임포트상 `DA_Dino_TRex` 를 들고 있어, 단독 스폰하면 BeginPlay 의
//   ApplySpecies 가 살점을 T-Rex 로 바꿀 수 있다.
// - **스폰 직후 `StartReveal()`**. 살점 알파는 BeginPlay 에서 0 으로 시작하고, 이를 올리는 3초 응시
//   로직은 `TimeMachineARDebugPawn` 에만 있다 — AR 게임모드의 `BP_ARPawn` 엔 없고 어떤 에셋도 안 부른다.
// - **DA 의 위치 오프셋은 버린다** (`bApplyDinoInfoLocation=false`). MeshTransform 위치는 마커 기준이라
//   (DA_Dino_Archelon = T-Rex 값 X 125.6cm 복사) 그대로 두면 앵커에서 1.2m 앞에 뜬다. 회전·배율은 유지.
// - **서버 주소 폴백**: NavClient 주소가 안 닿으면 `http://127.0.0.1:8000`(USB + `adb reverse`)을 번갈아 쓴다.
// - **진단 토스트는 스캔 회차당 한 번씩**: 서버 연결 실패 / 에셋 앵커 없음 / 목록 받음(인식 대기).
//
// ## 네비 앱으로 이식 (13-3, 2026-09-15 — 테스트 앱 `feature/13-1test` 에서)
// - **서버 배치 보정**: 목록의 `asset_offset`(테스트 앱 조정 패드가 저장한 앵커 로컬 위치·yaw·배율)을 스폰에 얹는다
//   (`ComposeAssetTransform` — 테스트 앱과 같은 식). 조정 패드 UI 는 이식하지 않는다(공지 트리거 3) — 값은 테스트 앱에서 맞춘다.
// - **에셋 지도 분리**(`AssetMapId`): 에셋 앵커를 네비 지도(`DefaultMapId`)와 **다른 지도**에서 받는다. 비우면 예전처럼 네비 지도.
//   네비 리졸버는 이 값을 모른다 — 측위·길 안내와 독립이고, 마커(증강 이미지) 인식에도 기대지 않는다.
// - **전용 오버레이 · 회중시계**(2026-09-15 저녁 — QR 마커 폐기로 앵커가 공룡을 띄우는 유일한 경로): 종 데이터의
//   `CustomOverlayClass`(아르켈론 전용 BP · 물·거품 이펙트)를 띄우고 `TimeRevealProfile` 로 회중시계 연출
//   (`UTimeRevealComponent::AttachTo`)을 붙인다 — `ARTrackingManager` 마커 흐름과 같은 조립이다.
//
// ## 빌드 분리
// 플러그인(GoogleARCoreServices) 호출은 전부 `#if NAV_CLOUD_RESOLVE` 안이다. Mac 에디터
// 타깃엔 그 의존이 없어 클래스 껍데기만 남는다(`ShouldCreateSubsystem=false`).
// UHT 는 UCLASS·UPROPERTY·UFUNCTION 을 `#if` 안에 두는 것을 금지하므로 **선언은 항상** 한다.

/** 리졸브 대상 에셋 앵커 1건. Pin 의 실체는 UCloudARPin 이지만 헤더는 플러그인 비의존으로 둔다. */
USTRUCT()
struct FNavAssetAnchorEntry
{
	GENERATED_BODY()

	UPROPERTY() int32 PointNo = 0;
	UPROPERTY() FString CloudId;
	UPROPERTY() FString Label;
	UPROPERTY() TObjectPtr<UARPin> Pin = nullptr;

	/** 이 앵커 위에 띄운 에셋. **우리가 만든 것만** 우리가 지운다(매니저 오버레이와 무관). */
	UPROPERTY() TObjectPtr<AActor> SpawnedActor = nullptr;

	/** 이번 시도의 요청 시각(월드초) — 토스트에 쓰는 지연은 이 값 기준. */
	UPROPERTY() double AttemptStart = 0.0;
	/** 최초 요청 시각(월드초) — 스캔을 누른 순간부터 걸린 전체 시간(로그용). */
	UPROPERTY() double FirstRequest = 0.0;
	UPROPERTY() int32 Attempts = 0;
	/** 재시도를 다 써서 포기했나. */
	UPROPERTY() bool bGaveUp = false;

	/** 서버에 저장된 배치 보정(`asset_offset` — 13-1 테스트 앱 조정 패드가 저장). 앵커 로컬 cm · yaw ° · 배율. 없으면 0·0·1. */
	UPROPERTY() FVector OffLoc = FVector::ZeroVector;
	UPROPERTY() float OffYaw = 0.f;
	UPROPERTY() float OffScale = 1.f;
};

/**
 * 「AR 스캔」 → `asset*` 앵커 → 에셋 스폰.
 *
 * 리졸브 절차(요청 → `Success && Tracking` 확인 → 타임아웃 재시도)는 12단계
 * `UNavCloudResolverSubsystem` 과 같은 규칙을 쓴다. 다른 것은 **무엇을 고르고
 * 무엇을 하느냐** 뿐이다 — 저쪽은 네비 앵커로 측위를 세우고, 이쪽은 에셋 앵커에 액터를 얹는다.
 */
UCLASS()
class TIMEMACHINEAR_API UNavCloudAssetSpawner : public UTickableWorldSubsystem
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

	/**
	 * 앵커 pose + ini 보정 + 서버 보정 → 에셋 월드 트랜스폼(순수 계산 · 플러그인 비의존 — 자동화 `Nav.AssetCompose`).
	 * 위치 보정은 **앵커 축**으로 얹고(앵커가 돌면 같이 돈다) ini z 는 월드 위, yaw 는 더하고 배율은 곱한다.
	 * ⚠️ 테스트 앱(`feature/13-1test` ComposeTarget)과 **같은 식**이어야 같은 저장값이 두 앱에서 같은 자리에 뜬다.
	 */
	static FTransform ComposeAssetTransform(const FTransform& AnchorXf, const FVector& OffLoc, float OffYaw, float OffScale,
		float IniYawDeg, float IniZCm, float IniScale);
	/** 종 데이터가 지정한 전용 오버레이 BP(`CustomOverlayClass`). 없으면 nullptr — 마커 흐름과 같은 선택(자동화 `Nav.AssetCompose`). */
	static UClass* GetSpeciesOverlayClass(const UDinoInfoData* Info);

private:
	/** 「AR 스캔」 버튼이 매니저를 통해 알려 준다. 매니저는 고치지 않는다(D22). */
	UFUNCTION()
	void HandleScanStateChanged(bool bScanning);

	/** 레벨의 매니저를 찾을 때까지 매 틱 두드린다(레벨이 늦게 뜰 수 있다). 찾으면 델리게이트에 붙는다. */
	void TryBindTrackingManager();
	/** ARPinCloudMode=Enabled. 성공할 때까지 매 틱 재시도. */
	void TryConfigureCloudMode();
	/** ini(NavClient 섹션)와 NavClient 서브시스템에서 서버 주소·맵 id 를 읽는다. NavClient 는 수정하지 않는다. */
	bool ResolveServerConfig();
	/** `[/Script/TimeMachineAR.NavCloudAssetSpawner]` 에서 에셋 경로·오프셋을 읽는다(D25). */
	void LoadSpawnConfig();
	/** GET .../cloud-anchors?state=bound → `asset*` 만 남긴다. 실패하면 잠시 뒤 다시. */
	void FetchAssetAnchors();
	void ApplyAnchorsJson(const FString& Body);
	/** 목록 조회 실패 — 다음 서버 후보로 넘기고, 후보를 한 바퀴 다 실패하면 토스트를 한 번 띄운다. */
	void OnFetchFailed(const FString& Url, int32 Code);
	void StartResolve(FNavAssetAnchorEntry& Entry);
	/** 진행 중인 리졸브를 훑어 스폰/타임아웃을 처리하고, 뜬 에셋을 앵커에 붙여 둔다. */
	void PollResolves();
	/** 인식된 앵커 위에 에셋을 올리고(최초 1회) 매 틱 앵커를 따라가게 한다. */
	void SpawnOrFollow(FNavAssetAnchorEntry& Entry);
	/** 스캔 ON 때 호출 — 이전 회차의 에셋·핀을 전부 걷어낸다(중복 방지). */
	void ClearAll();
	/** ini 의 BP 를 로드한다. 실패하면 T-Rex 폴백(파이프라인만이라도 검증되게 — D25). */
	UClass* LoadAssetClass();
	/** (선택) `AssetDinoInfoPath` 의 종 데이터. 비었거나 못 읽으면 nullptr — BP 기본값대로 뜬다. */
	UDinoInfoData* LoadDinoInfo();
	/**
	 * (선택) `AssetTimeRevealProfilePath` 의 회중시계 연출 설정. 종 데이터에 `TimeRevealProfile` 이 비어 있을 때만 쓴다.
	 * 2026-09-15 develop 의 DA_Dino_Archelon 이 `32315b9`("측위")에서 이 참조를 잃었다 — DA 가 고쳐지면 ini 줄을 지운다.
	 */
	UTimeRevealProfile* LoadTimeRevealProfile();
	/** 12단계 리졸버의 토스트 HUD 를 **같이 쓴다**(각자 띄우면 같은 자리에 두 장이 겹친다). 진단 문구라 개발모드에서만 뜬다(NavAppMode). */
	void Toast(const FString& Message);

	/** 매니저는 레벨 소유라 약참조로 든다. */
	TWeakObjectPtr<AARTrackingManager> TrackingManager;

	/** 지금 「AR 스캔」 회차가 살아 있나(= 새 리졸브 시도를 걸어도 되나). */
	bool bScanArmed = false;
	bool bCloudConfigured = false;
	bool bConfigResolved = false;
	bool bSpawnConfigLoaded = false;
	bool bFetchInFlight = false;
	/** 다음 목록 조회를 시도할 월드 시각(실패 시 백오프). */
	double NextFetchTime = 0.0;

	FString ServerBaseUrl;
	FString MapId;
	/** (선택) 에셋 앵커를 받을 지도 — 네비 지도(`DefaultMapId`)와 달라도 된다. 비우면 `DefaultMapId`. */
	FString AssetMapId;

	/** 서버 주소 후보 — [NavClient 주소, USB(adb reverse) 127.0.0.1:8000]. 실패하면 다음으로 넘어간다. */
	TArray<FString> ServerCandidates;
	int32 ServerCandidateIndex = 0;
	/** 이번 스캔 회차의 목록 조회 실패 수와 이미 띄운 진단 토스트(5초마다 같은 말을 되풀이하지 않게). */
	int32 FetchFailuresThisScan = 0;
	bool bToastedFetchFail = false;
	bool bToastedNoAnchors = false;

	/** D25 — `/Game/.../BP_Archelon.BP_Archelon_C`. 문자열 경로 런타임 로드라 쿡 설정이 필요하다. */
	FString AssetClassPath;
	float SpawnYawOffsetDeg = 0.f;
	float SpawnZOffsetCm = 0.f;
	float SpawnScale = 1.f;
	/** (선택) 스폰 전에 SetDinoInfo 로 넣을 종 데이터 — `/Game/UI/DinoCard/DA_Dino_Archelon.DA_Dino_Archelon`. */
	FString AssetDinoInfoPath;
	bool bDinoInfoResolved = false;
	/** (선택) 종 데이터에 회중시계 설정이 없을 때 쓸 `UTimeRevealProfile` — `/Game/TimeReveal/DA_TimeReveal_Archelon.DA_TimeReveal_Archelon`. */
	FString AssetTimeRevealProfilePath;
	bool bTimeRevealProfileResolved = false;
	/**
	 * DA 의 MeshTransform **위치**까지 쓸지. 기본 false — 그 값은 마커 기준 오프셋이라
	 * (DA_Dino_Archelon 은 T-Rex 값 X=125.6cm 를 물려받았다) 앵커 흐름에선 앵커 자리가 곧 위치다.
	 * 회전·배율은 메시 보정이라 항상 DA 값을 쓴다.
	 */
	bool bApplyDinoInfoLocation = false;

	UPROPERTY(Transient)
	TObjectPtr<UClass> AssetClass = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDinoInfoData> DinoInfoAsset = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTimeRevealProfile> TimeRevealProfileAsset = nullptr;

	UPROPERTY(Transient)
	TArray<FNavAssetAnchorEntry> Entries;
};
