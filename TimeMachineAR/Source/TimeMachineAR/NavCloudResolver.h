// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavCloudResolver.generated.h"

class UARPin;
class UNavCloudResolveHud;

/**
 * 13-1 D23 — 앵커를 **역할로 가르는 규약**. 서버 `label` 하나로 나눈다.
 *
 *   `asset*` : 에셋 전용. 좌표가 촬영용 임의값이라 **측위에 쓰지 않는다**(이 파일이 건너뛴다).
 *   그 외    : 네비 포인트. 측위 전용이고 에셋을 띄우지 않는다(NavCloudAssetSpawner 가 건너뛴다).
 *
 * 두 서브시스템이 같은 `?state=bound` 목록을 받아 여기서 갈라 가지므로, 판정은 한 곳에 둔다.
 */
namespace NavCloudAnchorRoles
{
	inline bool IsAssetAnchorLabel(const FString& Label)
	{
		return Label.StartsWith(TEXT("asset"), ESearchCase::IgnoreCase);
	}
}

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
//  1. AR 세션 Running → `ConfigGoogleARCoreServices(ARPinCloudMode=Enabled)` — 플러그인이 이때 버리는 마커 이미지 DB 는
//     `NavArCoreConfig` 가 되돌린다(2026-09-15 · 마커 트리거)
//  2. `GET /maps/{map}/cloud-anchors?state=bound` → cloud_id 목록
//  3. 13-3 D42 — **동시 6개까지만**, 가까울 법한 것부터 `CreateAndResolveCloudARPin`
//  4. `GetARPinCloudState()==Success` **그리고** `GetTrackingState()==Tracking` 이면 인식 확정
//     → HUD 토스트 `3번 앵커 인식 (1.8초)` + 로그
//  5. 12초 안에 확정되지 않으면 그 시도를 **취소**하고 백오프 뒤 다시 요청한다(D43). 포기는 없다(D44).
//
// ## 13-3 — 앵커 24개를 견디게 (명세 §3.6 D42~D46)
//  - D42 동시 리졸브 상한(기본 6) · **가까울 법한 것부터**. 거리 추정은 heading 에 무관한 하한을 쓴다:
//        |cam→앵커t| ≥ | |맵(t)−맵(r)| − |cam→앵커r|월드 |   (r = 이미 잡힌 앵커 전부 중 최대값)
//    1차 시드는 heading 이 전부 자리표시(90°)라 맵 거리를 그대로 믿으면 엉뚱한 앵커부터 건다 —
//    두 앵커 **사이 거리**는 회전과 무관하므로 이 하한은 1차·2차 시드 모두에서 유효하다.
//  - D43 실패 백오프 12 → 24 → 48 → 60초. 단 요청 시작 간격 12초는 항상 지킨다(빠른 실패 반복 방지).
//    근접 앵커와 **아직 기준이 없을 때**(잡힌 앵커 0 — 거리를 모른다)는 12초에서 키우지 않는다.
//    확정 오류(NotFound·NotAuthorized·SDK 버전)는 근접이어도 백오프한다 — 다시 걸어도 같다.
//  - D44 `bGaveUp` 없음. 멀어지면 큐에서 기다릴 뿐이고, **근접하면 백오프가 풀린다**.
//    근접한 앵커가 슬롯을 못 얻으면 먼 앵커의 진행 중 시도를 양보시킨다(3초 이상 돈 것만).
//  - D45 기준 앵커 = **카메라에서 가장 가까운**(월드 거리 — 실측이라 맵 좌표와 무관) 인식 앵커.
//    지금 기준보다 1.5배 이상 가깝고, 기준을 잡은 지 10초가 지났을 때만 바꾼다
//    (5초면 계획 밀도로 걸을 때 분당 ≈10회 전환 → §4 "재보정 ≤6/분" 초과 — 리뷰 시뮬).
//    같은 앵커의 재관측 재보정(12단계 규칙)은 **기준 앵커에만** 적용한다.
//  - D46 이미 측위 중이면, 새 기준이 함의하는 내 위치가 3m 넘게 튀면 거부·로그.
//    단 ① **같은 틱에** 다른 앵커 하나가 같은 자리(1m 안)를 함의하거나 ② 후보로서 같은 점프(시계를 켠
//    순간의 벡터에서 50cm 안)를 15초 넘게 요구하고 **지금 변환을 지지하는 인식 앵커가 하나도 없으면**
//    지금 변환이 틀린 것으로 보고 받아들인다(추적 재초기화 뒤 영영 못 옮기는 교착 방지).
//    지지 = 다른 Tracking 앵커(기준 포함)가 함의하는 위치가 지금 위치 3m 안 — 그러면 닮은 진열장에서
//    생긴 오탐이 15초 버틴다고 지도를 옮기지 않는다.
//
// ## 진행 중 리졸브는 **취소해야** 끝난다 (13-3 리뷰 blocker)
// UE 5.4 플러그인의 `RemoveCloudARPin` 은 앵커가 붙은(성공한) 핀만 뗀다 — 진행 중 리졸브엔 **아무것도
// 안 한다**(엔진 GoogleARCoreCloudARPinManager.cpp). 그대로 버리고 새로 걸면 ARCore future 가 계속
// 쌓여 동시 작업 한도(40)에 닿고, 동시 6 상한은 숫자로만 지켜진다.
// 그래서 진행 중 핀은 `ReleaseCloudPin` 이 `ArFuture_cancel` 로 **실제로 끊는다**(핸들은 플러그인 공개
// 헤더의 `UCloudARPin::GetFuture()` 와 AR 시스템의 세션 포인터 — Build.cs 무수정). 같은 프레임에 이미
// 끝나 취소가 안 먹으면 `OrphanPins` 에 두고 끝날 때까지 **슬롯에 세며** 지켜보다가 치운다.
//
// ## 측량 로그 (D36·D48, `bSurveyLogEnabled`)
// 켜면 logcat 에 `[NavCloudSurvey]` 줄을 남긴다 — 13-3 §G 정합기(`anchor_fit.py`)의 **입력 계약**이다.
// 형식을 바꾸면 `docs/nav-museum/museum_final/pipeline/survey_parse.py` 도 같이 바꿀 것.
//   SESSION tag=<yyyymmdd-hhmmss> map=<uuid> anchor_s=<초> cam_s=<초>
//   CAM t=<초> world=(x,y,z) yaw=<도> pitch=<도>                        (0.5초마다)
//   #<번호> world=(x,y,z) yaw=<도> t=<초> gap=<초>                      (2초마다, 인식·Tracking 인 핀마다)
//   #<번호> RESOLVED t=<초> world=(x,y,z) yaw=<도> latency=<초> attempts=<회>
//   REF t=<초> from=#<a> to=#<b> d_from=<cm> d_to=<cm> jump_cm=<cm> reason=<..>
//   JUMP_REJECT t=<초> cand=#<b> ref=#<a> jump_cm=<cm>
// t 는 SESSION 이후 경과(단조), world 는 UE 월드 cm, yaw 는 FRotator Yaw(도).
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
	/** 진행 중인 리졸브(인식 전) 또는 인식된 핀. null 이면 큐에서 차례를 기다리는 중이다. */
	UPROPERTY() TObjectPtr<UARPin> Pin = nullptr;

	// 이 앵커가 맵의 어디에 있는가(서버 cloud_anchors 행). 측위 기준점으로 그대로 쓴다.
	UPROPERTY() float PosXCm = 0.f;
	UPROPERTY() float PosYCm = 0.f;
	UPROPERTY() float PosZCm = 0.f;
	/** 맵 +X 축 기준 CCW(도). 1차 시드는 자리표시 90° — 실측값은 13-3 §G 정합 후 재시드. */
	UPROPERTY() float HeadingDeg = 90.f;

	/** 이번 시도의 요청 시각(월드초). 토스트에 쓰는 지연은 이 값 기준이다. 0 = 한 번도 안 걸었다. */
	UPROPERTY() double AttemptStart = 0.0;
	/** 최초 요청 시각(월드초) — 앱 시작부터 걸린 전체 시간(로그용). */
	UPROPERTY() double FirstRequest = 0.0;
	UPROPERTY() int32 Attempts = 0;
	/** 인식 확정되어 토스트를 띄웠나(같은 앵커 재표시 억제). */
	UPROPERTY() bool bRecognized = false;
	/** 13-3 D74 — 인식이 확정된 월드 시각. 가중 보정은 인식 직후 pose 를 BlendMinTrackSeconds 동안 넣지 않는다. */
	UPROPERTY() double RecognizedTime = 0.0;
	/** 이 핀이 마지막으로 Tracking 이던 월드 시각. 재관측(gap) 판정에 쓴다. */
	UPROPERTY() double LastTrackedTime = 0.0;
	/** 가장 최근 재관측 공백(초). 측량 로그가 한 번 싣고 0 으로 돌린다. */
	UPROPERTY() double LastGapSeconds = 0.0;
	/** 재보정(재래치) 횟수 — 로그·보고용. */
	UPROPERTY() int32 Relatches = 0;
	/** 이 앵커로 측위를 세운 적이 있나(로그·토스트 문구 구분용). */
	UPROPERTY() bool bLocalizeApplied = false;

	/** D43 — 연속 실패 횟수(인식·근접 진입 시 0). 백오프 길이를 정한다. */
	UPROPERTY() int32 ConsecutiveFailures = 0;
	/** D43 — 이 시각(월드초) 전엔 다시 걸지 않는다. 0 = 바로 걸어도 된다. */
	UPROPERTY() double NextEligibleTime = 0.0;
	/** D44 — 지난 틱에 근접이었나(먼→가까움 전환 순간에 백오프를 푼다). */
	UPROPERTY() bool bNearLast = false;
};

/** D46 — 3m 넘게 튀라고 한 앵커의 기록(합의·지속 판정용). 리플렉션 불필요. */
struct FNavCloudJumpReject
{
	/** 가장 최근에 관측한 점프 벡터(함의 위치 − 현재 위치, cm)와 그 시각. 합의는 **같은 틱** 기록끼리만 비교한다. */
	FVector2D JumpOffset = FVector2D::ZeroVector;
	double LastTime = 0.0;
	/** 지속 시계 — **후보로** 게이트에 걸릴 때만 돈다. 시계를 켠 순간의 벡터와 비교한다(직전 표본이 아니라). */
	FVector2D StableOffset = FVector2D::ZeroVector;
	double StableSince = 0.0;
	double LastCandidateTime = 0.0;
	double LastLogTime = -1.0;
};

/** 13-3 AR 겹쳐보기(NavMapArOverlay) — 인식됐고 지금 Tracking 인 앵커 핀의 월드 pose. 리플렉션 불필요. */
struct FNavCloudAnchorWorldPose
{
	int32 PointNo = 0;
	FTransform World = FTransform::Identity;
};

/** 13-3 D74 — 근접 앵커 가중 보정의 입력 1건(순수 계산 · 리플렉션 불필요). */
struct FNavBlendSample
{
	/** 이 앵커 하나로 세운 변환에서 본 카메라의 맵 좌표(cm). */
	FVector ImpliedCameraMap = FVector::ZeroVector;
	/** 이 앵커 하나로 세운 변환의 yaw(도) — SolveTransform 의 YawOffsetDeg(= 핀 yaw + heading). */
	double TransformYawDeg = 0.0;
	/** 카메라 ↔ 핀 월드 2D 거리(cm). 가중 = 1/max(d, BlendMinDistanceCm)². */
	double DistanceCm = 0.0;
};

/** StartResolve 결과. 세션 전역 원인은 앵커 탓이 아니라 벌점을 매기지 않는다. */
enum class ENavResolveStart : uint8
{
	Started,
	/** 이 앵커 요청만 안 걸렸다 — 12초 뒤 다시. */
	Failed,
	/** NotTracking·SessionPaused·CloudARPinNotEnabled — 이번 틱 배정을 멈춘다(벌점 없음). */
	SessionBlocked,
	/** ARCore 동시 작업 한도 — 신규 요청 전체 정지. */
	Exhausted,
};

/**
 * 방문객 빌드에서 도는 Cloud Anchor 리졸브 서브시스템.
 *
 * WorldSubsystem 이라 레벨·GameMode·기존 `.uasset` 배선이 **0** 이다(공지 트리거 회피 — CLAUDE.md §3).
 * 설정은 ini **새 섹션** `[/Script/TimeMachineAR.NavCloudResolverSubsystem]` 에서 읽는다(13-3).
 */
UCLASS(Config = Game)
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

	/**
	 * 13-1 §C — 에셋 스포너(NavCloudAssetSpawner)가 **같은 토스트 큐**를 쓰도록 HUD 를 넘긴다.
	 * 각자 위젯을 띄우면 화면 같은 자리에 두 장이 겹친다. 순차 표시 큐는 HUD 안에 이미 있다.
	 * 아직 없으면 만든다. 플러그인 없는 타깃에선 nullptr.
	 */
	UNavCloudResolveHud* GetSharedHud();

	/**
	 * 클라우드 핀을 놓는다. 리졸브가 **진행 중이면 ArFuture_cancel 로 실제로 끊는다** — RemoveCloudARPin 만으로는
	 * 진행 중 리졸브가 끊기지 않는다(헤더 위 "진행 중 리졸브는 취소해야 끝난다"). 스포너도 이것을 쓴다.
	 *
	 * @return ARCore 작업이 지금 끝났으면 true. 취소가 안 먹어 끝날 때까지 지켜봐야 하면 false(슬롯에 센다).
	 */
	bool ReleaseCloudPin(UARPin* Pin);

	/**
	 * 13-3 AR 겹쳐보기 — 인식됐고 지금 Tracking 인 앵커 핀의 월드 pose 를 채운다(읽기 전용).
	 * 플러그인이 없는 타깃에선 빈 배열.
	 */
	void GetRecognizedAnchorWorldPoses(TArray<FNavCloudAnchorWorldPose>& Out) const;

	// ── 13-3 설정 (ini 새 섹션) ───────────────────────────────────────────────

	/** D36·D48 — `[NavCloudSurvey]` 측량 로그를 남길지. 기본 꺼짐(방문객 빌드엔 불필요). */
	UPROPERTY(Config)
	bool bSurveyLogEnabled = false;

	/** 인식된 앵커 pose 를 남기는 주기(초). */
	UPROPERTY(Config, meta = (ClampMin = "0.2"))
	float SurveyLogIntervalSeconds = 2.0f;

	/** 카메라 궤적(CAM)을 남기는 주기(초). 정지점 검출·궤적 정합의 입력이다. */
	UPROPERTY(Config, meta = (ClampMin = "0.1"))
	float SurveyCamIntervalSeconds = 0.5f;

	/** D42 — 동시에 진행할 리졸브 요청 상한. 스포너 몫(1~2)은 따로다. */
	UPROPERTY(Config, meta = (ClampMin = "1"))
	int32 MaxConcurrentResolves = 6;

	/** D43 — 분당 리졸브 요청 상한(구글 300/분의 여유분 · §4 기준 60/분). 근접 앵커는 60 까지 허용. */
	UPROPERTY(Config, meta = (ClampMin = "1"))
	int32 MaxResolveRequestsPerMinute = 40;

	/** D43 — 실패 백오프 상한(초). 12 → 24 → 48 → 이 값. */
	UPROPERTY(Config, meta = (ClampMin = "12.0"))
	float RetryBackoffMaxSeconds = 60.0f;

	/** D44 — 거리 하한이 이 값(cm) 이하면 "근접": 백오프 해제 · 우선 배정 · 먼 시도 양보. */
	UPROPERTY(Config, meta = (ClampMin = "0.0"))
	float NearAnchorCm = 400.0f;

	/** D42 — 계획 좌표가 설치 위치와 다를 수 있는 여유(cm). 거리 하한에서 뺀다(설치 ±1m × 2). */
	UPROPERTY(Config, meta = (ClampMin = "0.0"))
	float PlanSlackCm = 150.0f;

	/** D45 — 새 후보가 지금 기준보다 이 배수 이상 가까워야 기준을 바꾼다. */
	UPROPERTY(Config, meta = (ClampMin = "1.0"))
	float RefSwitchRatio = 1.5f;

	/** D45 — 기준을 잡은 뒤 최소 체류(초). 10초 = 계획 밀도로 쉬지 않고 걸어도 전환 ≤6/분(§4). */
	UPROPERTY(Config, meta = (ClampMin = "0.0"))
	float RefMinDwellSeconds = 10.0f;

	/** D46 — 기준 전환이 함의하는 위치 점프가 이 값(cm)을 넘으면 거부. */
	UPROPERTY(Config, meta = (ClampMin = "50.0"))
	float JumpGateCm = 300.0f;

	// ── 13-3 D74 근접 앵커 가중 보정 (ini 같은 섹션 · nav-stage13-3-결과.md §10.10) ─────────────
	//
	// 기준 앵커 하나로 지도를 세우면(D45) 그 앵커의 heading 오차가 거리만큼 커지고(1° × 10m = 17cm), 기준을 바꿀 때
	// 한꺼번에 튄다. 켜면 매 틱, 가까운 인식 앵커 몇 개가 함의하는 (내 맵 위치·지도 회전)을 1/max(d, 최소)² 로 평균해
	// 부드럽게 따라간다. 기준 앵커 선택(D45)·점프 게이트(D46)·재측위(13-4)는 그대로 돌고, 이 보정은 그 위에서만 움직인다.

	/**
	 * 켜면 근접 앵커 가중 보정을 한다. **기본 꺼짐** — 팀 빌드 동작은 그대로다(2026-09-15 현장 확인 후에도 기본값은 끈 채 둔다).
	 * 켜려면 로컬 DefaultGame.ini 의 이 섹션에 bBlendNearbyAnchors=True.
	 */
	UPROPERTY(Config)
	bool bBlendNearbyAnchors = false;

	/** 가중에 넣을 최대 앵커 수(가까운 순). */
	UPROPERTY(Config, meta = (ClampMin = "1"))
	int32 BlendMaxAnchors = 3;

	/** 이 반경(cm, 카메라↔핀 월드 2D) 밖 앵커는 넣지 않는다. */
	UPROPERTY(Config, meta = (ClampMin = "100.0"))
	float BlendRadiusCm = 1000.0f;

	/**
	 * D45 기준 핀이 함의하는 내 위치(기준 핀이 안 보이면 지금 그려진 위치)에서 이만큼(cm) 넘게 다른 곳을 함의하는 앵커는 뺀다
	 * (틀린 pose — D46·13-4 몫). 그려진 위치에서 재면 옛 기준이 틀렸을 때 새 기준을 빼고 D45 전환을 덮는다(결과 §10.10).
	 */
	UPROPERTY(Config, meta = (ClampMin = "10.0"))
	float BlendGateCm = 50.0f;

	/** 지수 평활 시간 상수(초). 0.8 = 목표와의 차를 0.8초에 63% 줄인다. */
	UPROPERTY(Config, meta = (ClampMin = "0.05"))
	float BlendSmoothSeconds = 0.8f;

	/** 가중 1/max(d, 이 값)² 의 거리 하한(cm) — 핀 바로 옆에서 한 앵커가 가중을 독차지하지 않게. */
	UPROPERTY(Config, meta = (ClampMin = "10.0"))
	float BlendMinDistanceCm = 150.0f;

	/** 인식된 지 이만큼(초) 지난 핀만 넣는다(인식 직후 pose 가 자주 틀린다 — rebuild-0915 ev-survey). */
	UPROPERTY(Config, meta = (ClampMin = "0.0"))
	float BlendMinTrackSeconds = 1.0f;

	// ── 13-3 D74 순수 계산 (에디터 자동화 테스트 NavCloudBlendTest 대상 · 플러그인 비의존) ──

	/** 앵커 하나(핀 월드 pose + 맵 좌표·heading)로 세운 맵→월드 변환. NavLocalizer::SolveTransform 과 같은 식. */
	static FTransform MakeAnchorMapToWorld(const FTransform& AnchorWorld, const FVector& AnchorMap, double HeadingDeg);
	/** 변환에서 본 카메라의 맵 좌표. NavLocalizer::WorldToMap 과 같은 식. */
	static FVector CameraMapFromTransform(const FTransform& MapToWorld, const FVector& CameraWorld);
	/** 카메라가 맵 CameraMap 에 보이고 yaw 가 YawDeg 인 맵→월드 변환. */
	static FTransform MakeMapToWorldFromCamera(double YawDeg, const FVector& CameraWorld, const FVector& CameraMap);
	/** 앵커별 함의(카메라 맵 좌표·변환 yaw)의 1/max(d, MinDistanceCm)² 가중 평균(yaw 는 원형 평균). 입력이 비면 false. */
	static bool BlendImpliedPoses(const TArray<FNavBlendSample>& Samples, double MinDistanceCm,
		FVector& OutCameraMap, double& OutYawDeg);
	/** 지수 평활 한 걸음: Alpha = 1 − exp(−dt/τ). yaw 는 짧은 쪽으로 돈다. */
	static void SmoothToward(FVector& InOutCameraMap, double& InOutYawDeg, const FVector& TargetCameraMap,
		double TargetYawDeg, double DeltaSeconds, double SmoothSeconds);
	/** 가까운 순으로 정렬된 입력의 앞 MaxAnchors(≥1) 개 중 함의 위치가 GateCenter 에서 GateCm 안인 것의 인덱스(오름차순). */
	static void SelectBlendIndices(const TArray<FNavBlendSample>& SortedByDistance, const FVector2D& GateCenter,
		double GateCm, int32 MaxAnchors, TArray<int32>& OutIndices);

private:
	/** ARPinCloudMode=Enabled. 성공할 때까지 매 틱 재시도(세션이 늦게 뜰 수 있다). */
	void TryConfigureCloudMode();
	/** ini(NavClient 섹션)와 NavClient 서브시스템에서 서버 주소·맵 id 를 읽는다. NavClient 는 수정하지 않는다. */
	bool ResolveServerConfig();
	/** GET .../cloud-anchors?state=bound — 실패하면 잠시 뒤 다시 시도한다. */
	void FetchBoundAnchors();
	void ApplyAnchorsJson(const FString& Body);
	/** 한 건의 리졸브 요청을 (재)시작한다. 실패하면 Pin 이 null 로 남아 스케줄러가 다시 배정한다. */
	ENavResolveStart StartResolve(FNavCloudResolveEntry& Entry);
	/** 진행 중인 핀이면 ArFuture_cancel 을 건다. 이번 호출로 취소됐으면 true. */
	bool TryCancelPendingPin(UARPin* Pin) const;
	/** 취소가 안 먹은 핀을 지켜보다 끝나면 치운다(성공으로 끝났으면 붙은 앵커를 뗀다). */
	void PollOrphanPins();
	/** ARCore 에 실제로 걸린 리졸브 수 = 우리 진행 중 + 끝나길 기다리는 고아. 동시 상한은 이 값으로 잰다. */
	int32 CountLiveResolves() const;
	/** 진행 중인 리졸브들을 훑어 확정/타임아웃을 처리한다(새 요청은 걸지 않는다 — ScheduleResolves 몫). */
	void PollResolves();
	/** D42~D44 — 빈 슬롯에 대기 앵커를 가까울 법한 순으로 배정하고, 필요하면 먼 시도를 양보시킨다. */
	void ScheduleResolves(double Now);
	/** 이번 시도를 실패로 닫고 백오프를 매긴다(D43). 확정 오류는 근접이어도 백오프한다. */
	void FailAttempt(FNavCloudResolveEntry& Entry, double Now, bool bResourceExhausted, bool bDeterministicError);
	/**
	 * D42 — 카메라에서 이 앵커까지 거리의 **하한**(cm, 여유 PlanSlackCm 차감). 잡힌 앵커가 없으면 -1.
	 * heading·맵 회전과 무관하다(앵커 사이 맵 거리와 카메라–잡힌 앵커 월드 거리만 쓴다).
	 */
	float EstimateDistanceLowerBoundCm(const FNavCloudResolveEntry& Entry) const;
	/** 토스트 HUD 를 만든다(런타임 생성, WBP 없음). */
	void EnsureHud();
	/**
	 * 인식된 앵커로 **측위를 세운다**(QR 마커 대체 — 사용자 요청). 이미 측위돼 있으면
	 * 건드리지 않는다(마커가 더 정확하므로 QR 측위를 덮지 않는다). 세우면 이 앵커가 기준이 된다.
	 *
	 * @return 이 호출로 새로 측위가 성립했으면 true.
	 */
	bool TryLocalizeWithAnchor(FNavCloudResolveEntry& Entry);

	/**
	 * 이미 인식된 앵커를 **계속 지켜본다**(마커의 상시 재탐색과 같은 자리).
	 *
	 * 핀을 한동안 놓쳤다가 다시 잡은 순간에만 변환을 다시 세운다(마커의 `bRelatchOnReacquire`).
	 * 13-3 D45 — 그 재보정은 **기준 앵커에만** 한다. 기준을 바꾸는 일은 UpdateReferenceAnchor 몫이다.
	 *
	 * 핀이 죽었거나(오류 상태) 사라졌으면 큐로 돌려보내 항상 다시 잡을 준비를 유지한다.
	 */
	void WatchRecognizedAnchor(FNavCloudResolveEntry& Entry, double Now);

	/** D45·D46 — 기준 앵커를 최근접 + 히스테리시스로 고르고, 바꿀 땐 점프 게이트를 통과시킨다. */
	void UpdateReferenceAnchor(double Now);
	/**
	 * D46 — 지금 변환을 **지지하는** 인식 앵커(후보 제외, 기준 포함)의 번호. 없으면 0.
	 * 지지 = 그 앵커가 함의하는 카메라 맵 위치가 지금 위치에서 JumpGateCm 안.
	 */
	int32 FindCurrentTransformSupporter(int32 ExcludePointNo, const FVector2D& CurrentMap) const;
	/** D46 — 통과면 true. OutReason 에 합의·지속 통과 사유를 넣는다(일반 통과면 빈 문자열). */
	bool PassJumpGate(int32 CandidatePointNo, const FVector2D& ImpliedMap, const FVector2D& CurrentMap,
		double Now, FString& OutReason);
	/**
	 * 이 앵커를 기준으로 삼으면 카메라가 맵 어디에 있게 되는가.
	 * **NavLocalizer::SolveTransform · WorldToMap 과 같은 식**이다 — 한쪽을 바꾸면 둘 다 바꿀 것.
	 */
	bool ComputeImpliedCameraMap(const FNavCloudResolveEntry& Entry, FVector& OutMap) const;

	/**
	 * 13-4 D51 — 재측위 중 복구 재래치. 히스테리시스·최소 체류·점프 게이트 **없이** 안정된 최근접 Tracking 핀으로
	 * 곧장 측위를 다시 세운다(틀린 건 지금 변환이다). 안정 = 품질 양호 진입 후 `RelocPinStableSeconds` 이상 연속 Tracking.
	 * 복구 판정(최소 체류 등)은 NavLocalizer 몫이고, 여기선 0.2초에 한 번까지만 건다(프레임 훅 ⑥ 과 중복 방지).
	 * 에셋 앵커(`asset*`)는 ApplyAnchorsJson 이 Entries 에서 이미 뺐다.
	 */
	void RelatchForRelocalization(class UNavLocalizer& Localizer, const FVector& CamLoc, double Now);
	/** 13-4 — 재측위 중 핀별 연속 Tracking 시작 시각(0.2초 표본). 재측위가 아니면 비운다. */
	TMap<int32, double> RelocPinTrackingSince;
	/** 13-4 — 복구 재래치를 마지막으로 건 월드 시각(<0 = 아직). */
	double LastRelocRelatchTime = -1.0;
	/** 13-4 — 이번 재측위에서 RELATCH 로그를 남긴 기준 번호(같은 핀 반복 재래치는 한 줄만). */
	int32 RelocLoggedPointNo = 0;

	/** 서버 목록을 주기적으로 다시 받아 **새로 등록된 앵커·바뀐 좌표**를 반영한다. */
	void RefreshAnchorsIfDue(double Now);

	/** 카메라의 현재 맵 좌표(측위 전이면 ZeroVector). 재보정 드리프트 측정용. */
	FVector GetCameraMapLocation() const;

	/** D36·D48 — 측량 로그(CAM·앵커 pose). bSurveyLogEnabled 일 때만. */
	void TickSurveyLog();
	/** 측량 로그 시각(SESSION 이후 경과 초). 로그가 꺼져 있으면 -1. */
	double SurveyTime() const;

	/** 30초마다 스케줄러 상태 한 줄(§4 검증: 동시 ≤6 · 분당 요청 ≤60). */
	void LogSchedulerSummaryIfDue(double Now);

	/**
	 * D74 — 근접 앵커 가중 보정 한 걸음(매 틱, 기준 전환 뒤). 앵커 측위·정상 상태에서만 돈다.
	 * 후보 = 인식·Tracking · 인식 후 BlendMinTrackSeconds · 반경 안 · 가까운 BlendMaxAnchors 개 중 D45 기준 핀 함의 위치(안 보이면 지금 위치)에서 BlendGateCm 안.
	 * 후보가 없으면 쉰다(그때 변환은 D45·D46·13-4 가 세운 그대로). 쉬었다 다시 켜질 땐 그 변환에서 시작한다.
	 */
	void TickBlend(double Now, float DeltaTime);
	/** D74 — 마지막으로 넣은 가중 변환(평활 기점). 기준 전환·재래치가 변환을 한 앵커로 되돌려도 여기서 이어 간다. */
	FTransform BlendXf = FTransform::Identity;
	/** D74 — 지난 틱에 가중 보정을 넣었나. false 면 다음 틱은 Localizer 의 지금 변환에서 시작한다. */
	bool bBlendActive = false;
	/** D74 — 5초 요약 로그 집계(적용·쉼 틱 수 · 한 틱 최대 이동 · 마지막 가중). */
	double NextBlendLogTime = 0.0;
	int32 BlendAppliedTicks = 0;
	int32 BlendIdleTicks = 0;
	double BlendMaxStepCm = 0.0;
	FString BlendLastUsed;

	bool bCloudConfigured = false;
	bool bConfigResolved = false;
	bool bFetchInFlight = false;
	bool bAnchorsLoaded = false;
	/** 다음 목록 조회를 시도할 월드 시각(실패 시 백오프). */
	double NextFetchTime = 0.0;
	/** 목록을 주기적으로 다시 받을 월드 시각(등록이 늘거나 heading 이 바뀔 수 있다). */
	double NextRefreshTime = 0.0;

	/** D45 — 지금 측위 기준인 앵커 번호(0 = 없음/QR 마커 기준). */
	int32 RefPointNo = 0;
	/** D45 — 기준을 잡은 월드 시각(최소 체류 판정). */
	double RefSince = 0.0;
	/** D46 — 게이트에 걸린 후보들. */
	TMap<int32, FNavCloudJumpReject> JumpRejects;

	/** RESOURCE_EXHAUSTED 를 받으면 이 시각까지 새 요청을 멈춘다. */
	double GlobalResolveHoldUntil = 0.0;
	/** 세션 조건 보류 로그 스로틀(월드초). */
	double NextSessionBlockedLogTime = 0.0;
	/** 최근 60초 요청 시각들(분당 요청 수 로그용). */
	TArray<double> RecentRequestTimes;
	double NextSummaryTime = 0.0;
	/** 스케줄러·기준앵커 판정을 도는 다음 월드 시각(0.2초 간격). */
	double NextSchedulerTickTime = 0.0;

	/** 측량 로그 기준 시각(FPlatformTime 초, <0 = 아직 SESSION 을 안 찍음). */
	double SurveyStartPlatformSeconds = -1.0;
	double NextSurveyCamT = 0.0;
	double NextSurveyAnchorT = 0.0;

	FString ServerBaseUrl;
	FString MapId;

	UPROPERTY(Transient)
	TArray<FNavCloudResolveEntry> Entries;

	/** 취소가 안 먹어 끝나길 기다리는 리졸브 핀(보통 0). 슬롯에 센다 — 헤더 위 "취소해야 끝난다". */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UARPin>> OrphanPins;

	UPROPERTY(Transient)
	TObjectPtr<UNavCloudResolveHud> Hud = nullptr;
};
