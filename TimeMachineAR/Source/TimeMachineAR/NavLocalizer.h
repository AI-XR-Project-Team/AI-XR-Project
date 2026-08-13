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
UCLASS()
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
};
