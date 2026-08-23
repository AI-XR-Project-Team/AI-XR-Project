#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavTypes.h"
#include "NavLocalizer.generated.h"

class UARTrackedImage;

/** 측위가 성립했을 때. 어느 마커로 잡았는지 넘긴다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavLocalized, const FString&, MarkerCode);

/** 매 틱 현재 위치(맵 좌표). 미니맵이 여기에 붙는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavPoseUpdated, const FNavMapPose&, Pose);

/** 측위를 잃었을 때(마커를 오래 못 보고 추적이 끊김). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNavLocalizationLost);

/**
 * AR 추적 품질이 한동안 나빴을 때(주로 폰을 심하게 흔들어서). Reason 은 사람이 읽는
 * 이유 문구다. BP 가 WBP_NavStatus 경고("QR 다시 찍으세요")로 잇는다(5-B2).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavTrackingDegraded, const FString&, Reason);

/** 추적 품질이 다시 정상으로 돌아왔을 때. 경고 배너를 내린다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNavTrackingRecovered);

/**
 * 측위 후 **다른 마커로 앵커가 바뀌었을 때**(7단계 앵커 전환). 새 마커 code 를 넘긴다.
 * 걸어가다 다음 전시물 마커를 잡으면 드리프트가 씻기고 이 이벤트가 뜬다.
 * 최초 측위(OnLocalized)와 구분한다 — 기존 UI 를 건드리지 않고 8·9단계가 여기에 붙는다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavAnchorChanged, const FString&, MarkerCode);

/**
 * 실내 측위. "지금 내가 맵의 어디에 서 있는가"를 매 틱 알려준다.
 *
 * nav-test-app 의 CoordTransform.kt 를 UE 로 옮긴 것이다. 다만 **훨씬 짧다.**
 * 안드로이드는 ARCore 월드(Y-up 오른손, m)와 서버 맵(Z-up 왼손, cm)이 달라
 * 축 리맵과 단위 환산을 손으로 했지만, UE 의 AR 프레임워크는 pose 를 이미
 * UE 월드(Z-up 왼손, cm)로 준다. 서버 좌표 규약이 애초에 UE 기준이라
 * (docs/nav-server-integration-guide.md) **축 리맵도 스케일 환산도 없다.**
 * 남는 것은 수직축 yaw 회전 + 평행이동뿐이다.
 *
 * ## 원리
 *
 * 실내에는 GPS 가 없다. 대신 바닥의 QR 마커가 기준점이 된다. 서버는 그 마커가
 * 맵 어디에 어느 방향으로 붙어 있는지 알고 있고(GET /maps/{id}/markers), AR 은
 * 그 마커가 지금 카메라 기준 어디에 보이는지 안다. 이 둘을 한 쌍으로 놓으면
 * 맵 좌표계와 UE 월드 좌표계를 잇는 변환이 하나로 정해진다.
 *
 *     맵 좌표  --[ MapToWorld ]-->  UE 월드 좌표
 *
 * 변환이 서면 그 다음은 쉽다. 카메라의 월드 위치를 역변환하면 그게 "내가 맵의
 * 어디에 있는가" 다. AR 이 걷는 동안 카메라를 계속 추적해 주므로 QR 을 한 번만
 * 찍으면 된다.
 *
 * ## 왜 ARTrackingManager 를 안 쓰고 따로 도는가
 *
 * ARTrackingManager 의 OnMarkerFound 는 **공룡 오버레이 스폰에 성공해야만**
 * 발생한다(ARTrackingManager.cpp CheckForTrackedImages). 길찾기를 누를 때마다
 * 공룡이 튀어나오면 안 되고, 그 조건을 떼려면 팀원1 파일을 고쳐야 한다
 * (CLAUDE.md 공지 트리거 2). 그래서 여기서 추적 이미지를 직접 훑는다.
 * AR 세션 자체는 ARTrackingManager 가 BeginPlay 에 이미 켜 두므로 겹치지 않는다.
 *
 * ## 드리프트와 latch
 *
 * AR 추적은 걸을수록 조금씩 밀린다(드리프트). 그렇다고 매 프레임 변환을 다시
 * 세우면 추적 노이즈가 그대로 실려 지도 전체가 부들거린다. 그래서 계획 문서의
 * **latch 원칙**(ue-nav-ui-plan.md §2)을 따른다 — 변환은 처음 잡을 때 확정하고,
 * **마커를 한동안 못 보다가 다시 마주쳤을 때만** 다시 세운다. 그 순간은 새 관측이
 * 들어온 시점이라 요동 없이 드리프트만 씻긴다.
 *
 * 매 틱 재계산은 `bContinuousRelatch` 로 켤 수 있지만 캘리브레이션 디버깅용이다.
 *
 * ## WorldSubsystem 인 이유
 *
 * 레벨에 액터를 놓을 필요가 없다. `.umap` 은 바이너리라 팀원끼리 병합이
 * 불가능하므로(CLAUDE.md), 레벨을 건드리지 않고 사는 편이 안전하다.
 */
UCLASS(Config = Game)
class TIMEMACHINEAR_API UNavLocalizer : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 서브시스템을 찾아 준다. 블루프린트에서 GetSubsystem 노드를 헤매지 않도록.
	 * 이름을 Get 으로 줄이지 않는 이유는 ARTrackingManager 와 같다.
	 */
	UFUNCTION(BlueprintPure, Category = "Nav|Localizer", meta = (WorldContext = "WorldContextObject"))
	static UNavLocalizer* GetNavLocalizer(const UObject* WorldContextObject);

	// ------------------------------------------------------------------ 시작/중지

	/**
	 * 측위를 시작한다. 길찾기 버튼이 부를 자리다.
	 *
	 * 서버에서 마커 목록을 먼저 받아 온 뒤 추적 이미지를 훑기 시작한다.
	 * 이미 측위된 상태에서 다시 부르면 처음부터 다시 잡는다(다른 층으로 이동 등).
	 *
	 * @param MapId 비우면 NavClient 의 DefaultMapId 를 쓴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Localizer")
	void StartLocalizing(const FString& MapId);

	/** 마커 탐색을 멈춘다. 이미 선 변환과 위치 갱신은 그대로 둔다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Localizer")
	void StopScanning();

	/** 측위를 통째로 버린다. 안내를 끝낼 때. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Localizer")
	void ResetLocalization();

	/**
	 * 사용자가 경고를 보고 "QR 다시 찍기" 를 눌렀을 때(5-B2). 변환을 버리고 마지막에 쓰던
	 * 맵으로 다시 탐색을 시작한다 = ResetLocalization + StartLocalizing 한 방. 마커 목록은
	 * 이미 받아 뒀으면 서버를 다시 부르지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Localizer")
	void RescanFromUser();

	// ------------------------------------------------------------------ 상태

	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	bool IsLocalized() const { return bLocalized; }

	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	bool IsScanning() const { return bScanning; }

	/** 측위에 쓴 마커 code. 아직이면 빈 문자열. */
	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	FString GetAnchorMarkerCode() const { return AnchorMarkerCode; }

	/** 현재 위치(맵 좌표). 측위 전이면 bHasHeading=false 인 0 pose. */
	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	FNavMapPose GetCurrentMapPose() const { return CurrentPose; }

	/**
	 * AR 추적 품질이 PoorQualityHoldSeconds 이상 나쁜 상태로 지속되고 있으면 true(5-B1).
	 * 자동 reroute 게이트가 "가짜 이탈" 을 거르는 데 쓴다(NavMinimapWidget).
	 */
	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	bool IsTrackingDegraded() const { return bTrackingDegraded; }

	// ------------------------------------------------------------------ 좌표 변환
	//
	// 경로를 바닥에 그리는 단계에서 그대로 쓴다. 웨이포인트(맵 cm)를 월드로 옮겨
	// 거기에 화살표 액터를 놓으면 된다.

	/** 맵 좌표(cm) → UE 월드 좌표. 측위 전이면 입력을 그대로 돌려준다. */
	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	FVector MapToWorld(float PosXCm, float PosYCm, float PosZCm) const;

	/** UE 월드 좌표 → 맵 좌표(cm). */
	UFUNCTION(BlueprintPure, Category = "Nav|Localizer")
	FVector WorldToMap(const FVector& WorldLocation) const;

	// ------------------------------------------------------------------ 델리게이트

	UPROPERTY(BlueprintAssignable, Category = "Nav|Localizer")
	FOnNavLocalized OnLocalized;

	UPROPERTY(BlueprintAssignable, Category = "Nav|Localizer")
	FOnNavPoseUpdated OnPoseUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Nav|Localizer")
	FOnNavLocalizationLost OnLocalizationLost;

	/** 추적 품질이 한동안 나빠졌을 때(5-B2). 경고 배너를 띄운다. */
	UPROPERTY(BlueprintAssignable, Category = "Nav|Localizer")
	FOnNavTrackingDegraded OnTrackingDegraded;

	/** 추적 품질이 회복됐을 때. 경고 배너를 내린다. */
	UPROPERTY(BlueprintAssignable, Category = "Nav|Localizer")
	FOnNavTrackingRecovered OnTrackingRecovered;

	/** 측위 후 앵커 마커가 다른 마커로 바뀌었을 때(7단계 앵커 전환). */
	UPROPERTY(BlueprintAssignable, Category = "Nav|Localizer")
	FOnNavAnchorChanged OnAnchorChanged;

	// ------------------------------------------------------------------ 설정

	/**
	 * 마커 heading 보정각(도). **현장에서 한 번 맞추고 상수로 고정한다.**
	 *
	 * 서버 heading 은 "맵 +X 축 기준 CCW" 이고, UE 추적 이미지의 로컬 축이 QR
	 * 그림의 어느 변을 가리키는지는 마커를 어떻게 인쇄·부착했느냐에 달려 있다.
	 * 두 규약이 몇 도 어긋나는지는 계산으로 못 정한다 — 실제로 찍어 보고
	 * 미니맵의 내 방향이 90도씩 돌아가 있으면 그만큼 넣는다.
	 *
	 * nav-test-app 의 Config.HEADING_OFFSET_DEG 와 같은 역할이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration")
	float MarkerHeadingOffsetDeg = 0.f;

	/**
	 * 마커가 보이는 동안 **매 틱** 변환을 다시 세운다. 기본은 꺼져 있다.
	 *
	 * `docs/ue-nav-ui-plan.md` §2 의 **latch 원칙** — "T 는 마커를 처음 잡은 순간
	 * 한 번 확정하고 매 프레임 재계산하지 않는다" — 을 따른 것이다. 추적 노이즈가
	 * 그대로 변환에 실려 지도 전체가 부들거린다. 켜는 것은 캘리브레이션 디버깅용이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration")
	bool bContinuousRelatch = false;

	/**
	 * 마커를 한동안 못 보다가 **다시 봤을 때만** 변환을 다시 세운다. 기본 켜짐.
	 *
	 * latch 원칙의 "재측위는 의도적으로만" 에 해당한다. 걷는 동안 쌓인 드리프트는
	 * 마커를 다시 마주쳐야 씻기는데, 그 순간은 프레임 단위로 흔들리는 상황이 아니라
	 * 새로 관측이 들어온 시점이라 요동 걱정이 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration")
	bool bRelatchOnReacquire = true;

	/**
	 * 이 시간(초) 이상 마커를 못 봤다가 다시 보면 "재관측" 으로 친다.
	 *
	 * 짧게 잡으면 마커가 화면 가장자리에서 깜빡일 때마다 재래치가 걸려 결국
	 * 매 틱 재계산과 같아진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration",
		meta = (ClampMin = "0.1"))
	float ReacquireGapSeconds = 1.5f;

	/**
	 * 마커를 이 시간(초) 넘게 못 보면 측위를 잃은 것으로 본다. 0 이면 안 잃는다.
	 *
	 * 잃어도 마지막 위치를 지우지는 않는다. 화면에 "위치를 다시 잡아 주세요" 를
	 * 띄울지 판단할 근거로만 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration")
	float LostAfterSeconds = 0.f;

	/**
	 * true 면 내 위치의 높이를 마커의 맵 Z 로 고정한다.
	 *
	 * 카메라는 눈높이에 있어 맵 Z 가 150 쯤으로 나온다. 미니맵은 위에서 내려다본
	 * 그림이라 상관없지만, 바닥 경로 렌더는 이 값이 그대로 높이로 쓰여서
	 * 화살표가 공중에 뜬다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration")
	bool bSnapHeightToMarker = true;

	/**
	 * 추적 품질이 이 시간(초) 넘게 나쁘게 지속돼야 경고를 띄운다(5-B1). 한 프레임 튐으로
	 * 배너가 깜빡이면 신뢰를 잃으므로 지속 시간 게이트가 핵심이다.
	 *
	 * A(마커 개선)가 6단계로 빠져 드리프트 실측값이 없어 잠정값이다. 오탐(정상 보행 중
	 * 경고)이 뜨면 이 값을 올린다(spec §2·§5). ini 로 노출해 리빌드 없이 튜닝한다.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration",
		meta = (ClampMin = "0.1"))
	float PoorQualityHoldSeconds = 1.5f;

	/**
	 * 경고를 내리기(회복 판정) 전에 품질이 이만큼(초) 연속으로 좋아야 한다(5-B1).
	 *
	 * 흔들 때 추적 품질은 좋음↔나쁨을 프레임 단위로 깜빡인다. 좋은 프레임 하나에
	 * 곧바로 경고를 내리면 배너가 떨린다 — 이 시간만큼 "계속 좋아야" 회복으로 본다.
	 * 그동안 누적 저하 타이머도 지우지 않아, 깜빡이는 흔들림도 결국 임계를 넘긴다.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Localizer|Calibration",
		meta = (ClampMin = "0.05"))
	float QualityRecoverSeconds = 0.5f;

	// ------------------------------------------------------------------ 마커 이미지 등록 (7단계 §A)
	//
	// 정식 경로 = **DA_ARSession 에 baked candidate 로 쿡**(6-1a 해결책과 동일). 마커 텍스처를
	// candidate 이미지로 넣고 저장하면 쿡 때 ARCore 이미지 DB 로 직렬화된다. .uasset 은 규칙상
	// 커밋하지 않으므로(로컬, 실기기 빌드용) 팀원1과 git 충돌은 없고, 통합 시 팀원1이 합쳐 재bake 한다.
	//
	// ⚠️ **런타임 등록(AddRuntimeCandidateImage)은 기본 끈다.** ARCore 에선 (1) 살아있는 세션이
	// 있어야 하고(GoogleARCoreDevice: "No valid session") (2) 세션 재시작 때 baked DB 로 되돌아가며
	// 버려진다 — 6-1a 에서 실기기로 확인했고 엔진 코드(GoogleARCoreAPI ConfigSession)와도 일치한다.
	// 그래서 아래 런타임 경로는 baked 를 못 쓰는 상황용 실험적 폴백일 뿐이다.

	/** 런타임 후보 등록(실험적 폴백). 기본 꺼짐 — 정식 경로는 baked candidate(DA_ARSession)다. */
	UPROPERTY(Config, EditAnywhere, Category = "Nav|Markers")
	bool bRegisterMarkerImages = false;

	/**
	 * 등록할 마커 이미지 목록. 한 줄에 `FriendlyName|텍스처경로`.
	 * FriendlyName 이 곧 서버 마커 code 이자 KnownMarkers 키다(ARTrackingManager 와 동일 규약).
	 * 비우면 아래 기본 7장(과도기: 구 2 + A2 신 5, spec §A 표)을 등록한다. ini 로 덮어쓸 수 있다.
	 * 같은 code 를 두 줄에 두면(구·신) 어느 인쇄물이 잡혀도 같은 지점으로 측위된다.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nav|Markers")
	TArray<FString> MarkerImageEntries;

	/** 등록 시 넘길 물리 폭(cm). A2 단면 긴변 = 42.0(spec §A). 100% 인쇄면 실측 불필요. */
	UPROPERTY(Config, EditAnywhere, Category = "Nav|Markers")
	float MarkerPhysicalWidthCm = 42.0f;

	/**
	 * 동시에 추적할 최대 이미지 수. UE 기본값 1 이면 마커 A 를 무는 동안 B 가 보고되지
	 * 않아 **앵커 전환이 안 된다**(spec §F-1). 시작 전에 세션 설정에 리플렉션으로 올린다.
	 * ⚠️ 문서상 ARKit 기준값이라 ARCore 가 무시할 수 있다 — 현장 1회로 판정(spec §F-1).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nav|Markers", meta = (ClampMin = "1"))
	int32 MaxMarkerImagesTracked = 5;

	// 실제 에셋은 Content/Stuff/Asset/DA_ARSession. ARTrackingManager 가 BP 프로퍼티로 쥔 것과
	// 같은 인스턴스라 여기에 얹으면 그쪽 세션에도 반영된다(같은 경로 = 같은 로드 인스턴스).
	UPROPERTY(Config, EditAnywhere, Category = "Nav|Markers")
	FString MarkerSessionConfigPath = TEXT("/Game/Stuff/Asset/DA_ARSession.DA_ARSession");

	/**
	 * 측위 후 앵커 전환용 추적 이미지 재스캔 주기(초). 매 프레임 훑으면 비싸다 → 5Hz.
	 * (6-1a 프로브의 GetAllGeometriesByClass 순회를 여기로 승격했다, spec §0.)
	 */
	UPROPERTY(EditAnywhere, Category = "Nav|Markers", meta = (ClampMin = "0.05"))
	float AnchorScanIntervalSeconds = 0.2f;

	// ------------------------------------------------------------------ Subsystem

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ------------------------------------------------------------------ Tickable

	virtual void Tick(float DeltaTime) override;

	/**
	 * 할 일이 없을 때는 틱을 아예 돌지 않는다. AR 지오메트리 조회는 싸지 않고,
	 * 도슨트만 쓰는 관람객에게 매 프레임 부담을 줄 이유가 없다.
	 *
	 * Super 를 먼저 확인해야 한다. 초기화 전에 틱이 돌면 부모 Tick 의 checkf 가 터진다.
	 */
	virtual bool IsTickable() const override
	{
		// 탐색 중이거나 측위된 뒤에만 돈다. 도슨트만 쓰는 관람객에겐 매 프레임 부담을 안 준다.
		return Super::IsTickable() && (bScanning || bLocalized);
	}

	virtual TStatId GetStatId() const override;

private:
	/** 서버에서 받은 마커 목록. code 로 찾는다. */
	UPROPERTY()
	TMap<FString, FNavMarker> KnownMarkers;

	/** 측위 기준으로 삼은 추적 이미지. 매 틱 다시 읽어 드리프트를 보정한다. */
	UPROPERTY()
	TObjectPtr<UARTrackedImage> AnchorImage;

	FString AnchorMarkerCode;
	FNavMarker AnchorMarker;

	/** 맵 → UE 월드. yaw 회전 + 평행이동, 스케일 1. */
	FTransform MapToWorldXf = FTransform::Identity;

	FNavMapPose CurrentPose;

	bool bScanning = false;
	bool bLocalized = false;
	bool bMarkersReady = false;

	/** OnLocalizationLost 를 한 번만 쏘기 위한 래치. 마커를 다시 보면 풀린다. */
	bool bLostReported = false;

	float SecondsSinceMarkerSeen = 0.f;

	/** StartLocalizing 이 마커 목록을 기다리는 동안 들고 있는 맵 id. */
	FString PendingMapId;

	/** 마지막으로 StartLocalizing 에 넘어온 맵 id. RescanFromUser 가 같은 맵으로 다시 시작한다. */
	FString LastMapId;

	// --- 추적 품질 감지(5-B1) ---
	/** 품질이 나쁜 상태로 이어진 누적 시간(초). 좋은 상태가 충분히 지속돼야 0 으로 리셋. */
	float SecondsPoorQuality = 0.f;
	/** 품질이 좋은 상태로 이어진 누적 시간(초). 깜빡임 방어용(QualityRecoverSeconds). */
	float SecondsGoodQuality = 0.f;
	/** 현재 "저하" 로 보고 경고를 띄운 상태인가(래치 — 상승/하강 에지에만 방송). */
	bool bTrackingDegraded = false;

	/** 매 틱 추적 품질을 보고, 지속 저하/회복 시 델리게이트를 방송한다(측위 후에만). */
	void MonitorTrackingQuality(float DeltaTime);

	UFUNCTION()
	void HandleMarkersReceived(const TArray<FNavMarker>& Markers);

	UFUNCTION()
	void HandleMarkersFailed(int32 StatusCode, const FString& Reason);

	/** 추적 중인 이미지 중 아는 마커를 찾아 변환을 세운다. 성공하면 true. */
	bool TryLocalizeFromTrackedImages();

	/** 마커의 월드 트랜스폼 + 맵 pose 로 MapToWorldXf 를 다시 계산한다. */
	void SolveTransform(const FTransform& MarkerWorld);

	/** 카메라의 월드 위치를 맵 좌표로 옮겨 CurrentPose 를 갱신하고 알린다. */
	void UpdateCurrentPose();

	// --- 마커 이미지 등록 (7단계 §A) ---
	/** 아직 안 됐으면 마커 후보 이미지를 세션 설정에 등록한다(+MaxNum 상향). */
	void RegisterMarkerImages(bool bAllowSessionRestart);
	bool bMarkerImagesRegistered = false;

	// --- 앵커 전환 (7단계 §A) ---
	/** 측위 후 다른 known 마커가 잡히면 앵커를 그쪽으로 옮긴다. 옮겼으면 true. */
	bool TryTransitionAnchor();
	float SecondsSinceAnchorScan = 0.f;
	/** 지난 스캔에서 추적 중이던 known 마커 code 들. ACQUIRE 에지 판정용. */
	TSet<FString> TrackedMarkerCodesLastScan;

public:
	// --- 순수 헬퍼 (헤드리스 자동화 테스트 대상, spec §A) ---

	/** `FriendlyName|경로` 한 줄을 가른다. 형식이 맞으면 true(양끝 공백 제거). */
	static bool ParseMarkerEntry(const FString& Entry, FString& OutName, FString& OutPath);

	/**
	 * 앵커 전환 결정(순수 로직). 지금 추적 중인 known 마커 code 들과 지난 스캔 집합을 보고
	 * 어느 code 로 앵커를 옮길지 정한다. 옮기지 않으면 빈 문자열.
	 *  - 현재 앵커가 추적 불가면(멀어져 놓침) 추적 중인 아무 마커로 재측위한다.
	 *  - 앵커가 살아 있으면 **이번에 새로 잡힌**(지난 스캔엔 없던) 다른 마커에만 옮긴다
	 *    (전시물 도착 = 드리프트 보정 순간). 이미 계속 보이던 마커로는 안 옮겨 요동을 막는다.
	 */
	static FString DecideAnchorTransition(
		const FString& CurrentAnchorCode, bool bAnchorTracking,
		const TArray<FString>& TrackedKnownNow, const TSet<FString>& TrackedKnownLast);
};
