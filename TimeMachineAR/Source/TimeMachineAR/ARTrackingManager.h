#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ARSessionConfig.h"
#include "ARBlueprintLibrary.h"
#include "ARTrackingManager.generated.h"

class ADinoOverlayActor;
class UARPin;
class UDinoInfoData;
class UDinoRegistry;

/** 스캔이 켜지거나 꺼졌을 때. 하단 바가 버튼 모양을 바꾸는 데 쓴다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScanStateChanged, bool, bIsScanning);

/**
 * 마커를 찾아 오버레이를 붙였을 때.
 *
 * 네비게이션이 여기에 붙는다. 마커의 월드 pose 와 code 를 같이 넘기는 이유는,
 * 네비가 "이 마커가 지도상 어디인지"를 서버에서 조회해(GET /maps/{id}/markers)
 * 맵↔월드 변환을 세워야 하기 때문이다. 실내에는 GPS 가 없으므로 마커 하나가
 * 유일한 측위 기준점이다. docs/nav-server-integration-guide.md §5 참조.
 *
 * @param Pin        오버레이를 고정한 앵커. 트래킹이 갱신되면 따라 움직인다.
 * @param MarkerPose 마커의 월드 트랜스폼. 맵↔월드 변환의 한쪽 항이다.
 * @param MarkerCode UARSessionConfig 후보 이미지의 이름. 서버 마커 code 와 맞춘다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMarkerFound,
	UARPin*, Pin, const FTransform&, MarkerPose, const FString&, MarkerCode);

/** 전면/후면 카메라가 바뀌었을 때. 스캔 화면이 전환 버튼 상태를 맞추는 데 쓴다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCameraFacingChanged, bool, bFrontCamera);

UENUM(BlueprintType)
enum class EDinoScanPhase : uint8
{
	Idle       UMETA(DisplayName = "대기"),
	Searching  UMETA(DisplayName = "탐색 중"),
	Aiming     UMETA(DisplayName = "조준"),
	Recognized UMETA(DisplayName = "인식됨")
};

UCLASS()
class TIMEMACHINEAR_API AARTrackingManager : public AActor
{
	GENERATED_BODY()

public:
	AARTrackingManager();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// AR 세션 설정 에셋 (블루프린트에서 할당)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Tracking")
	UARSessionConfig* SessionConfig;

	// 스폰할 오버레이 액터 클래스 (BP_DinoOverlay 할당)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Tracking")
	TSubclassOf<ADinoOverlayActor> OverlayActorClass;

	/**
	 * 마커 이름 → 공룡 종 대응표. 비워 두면 BP_DinoOverlay 의 기본 공룡만 나온다.
	 *
	 * 비워 두는 것을 오류로 보지 않는 이유는, 마커가 정해지기 전에도 앱이
	 * 지금처럼 티라노 하나로 돌아가야 하기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Tracking")
	TObjectPtr<UDinoRegistry> DinoRegistry;

	/**
	 * 대응표에 없는 마커는 무시할지.
	 *
	 * 켜면 등록된 마커만 공룡을 띄운다. 전시장에 네비게이션용 마커가 섞여 있을 때
	 * 엉뚱한 마커에 공룡이 튀어나오는 것을 막는다. 단 대응표에 마커가 하나도
	 * 없으면 이 설정과 무관하게 예전처럼 아무 마커에나 반응한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Tracking")
	bool bIgnoreUnknownMarkers = true;

	// 레벨에 들어오자마자 마커를 찾을지. 끄면 StartScan 을 부를 때까지 가만히 있는다.
	// 하단 바의 "AR 스캔" 버튼을 쓰기로 했으므로 기본은 끔.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Tracking")
	bool bAutoScanOnStart = false;

	UPROPERTY(BlueprintAssignable, Category = "AR Tracking")
	FOnScanStateChanged OnScanStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "AR Tracking")
	FOnMarkerFound OnMarkerFound;

	UPROPERTY(BlueprintAssignable, Category = "AR Tracking")
	FOnCameraFacingChanged OnCameraFacingChanged;

	/**
	 * 전면(셀카) ↔ 후면 카메라를 바꾼다. 스캔 화면의 전환 버튼이 부른다.
	 *
	 * ARCore 는 세션 하나가 카메라 하나를 잡으므로 세션을 내렸다가 다른 설정으로
	 * 다시 올린다. 전면 카메라는 마커(증강 이미지) 추적을 지원하지 않아 스캔과
	 * 오버레이를 먼저 걷어낸다. 후면으로 돌아오면 원래 설정으로 복귀한다.
	 * 안드로이드 외 플랫폼에서는 아무 일도 하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "AR Tracking")
	void ToggleCameraFacing();

	UFUNCTION(BlueprintPure, Category = "AR Tracking")
	bool IsFrontCamera() const { return bFrontCamera; }

	/**
	 * 마커 탐색을 시작한다. 이미 붙여 둔 오버레이가 있으면 지우고 처음부터 다시 찾는다.
	 * 하단 바의 "AR 스캔" 버튼이 부른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "AR Tracking")
	void StartScan();

	/** 마커 탐색을 멈춘다. 이미 붙어 있는 오버레이는 그대로 둔다. */
	UFUNCTION(BlueprintCallable, Category = "AR Tracking")
	void StopScan();

	/** 붙여 둔 오버레이와 앵커를 걷어낸다. */
	UFUNCTION(BlueprintCallable, Category = "AR Tracking")
	void ClearOverlay();

	UFUNCTION(BlueprintPure, Category = "AR Tracking")
	bool IsScanning() const { return bIsScanning; }

	UFUNCTION(BlueprintPure, Category = "AR Tracking|Scan UI")
	EDinoScanPhase GetScanPhase() const { return ScanPhase; }

	/**
	 * 레벨에 놓인 매니저를 찾아 준다. 위젯에서 참조를 들고 다니지 않아도 되도록.
	 * 레벨에 하나만 놓는 것을 전제로 한다.
	 *
	 * 이름을 Get 으로 줄이지 않는다. 블루프린트 검색에서 엔진의 수많은 Get 노드에
	 * 파묻혀 찾을 수가 없다.
	 */
	UFUNCTION(BlueprintPure, Category = "AR Tracking", meta = (WorldContext = "WorldContextObject"))
	static AARTrackingManager* GetARTrackingManager(const UObject* WorldContextObject);

private:
	// 하이브리드 트래킹 핸드오프 상태 플래그
	bool bIsAnchored = false;

	// 스캔 중일 때만 마커를 찾는다.
	bool bIsScanning = false;

	bool bFrontCamera = false;

	/** 전면 카메라용 세션 설정. 처음 전환할 때 SessionConfig 를 바탕으로 만든다. */
	UPROPERTY()
	TObjectPtr<UARSessionConfig> FrontSessionConfig;

	UARSessionConfig* BuildFrontSessionConfig() const;

	EDinoScanPhase ScanPhase = EDinoScanPhase::Idle;
	int32 VisibleCandidateCount = 0;

	// 생성된 공룡 오버레이 참조
	UPROPERTY()
	ADinoOverlayActor* SpawnedOverlay;

	// 오버레이를 고정한 앵커. 걷어낼 때 같이 지워야 트래킹에 찌꺼기가 남지 않는다.
	UPROPERTY()
	UARPin* OverlayPin;

	void CheckForTrackedImages();

	void UpdateScanPhase();

	/** 마커 이름으로 띄울 공룡을 고른다. 못 찾으면 폴백, 그것도 없으면 nullptr. */
	UDinoInfoData* ResolveSpecies(const FString& MarkerCode) const;

	void RequestCameraPermissionAndStart();
	void StartARSessionInternal();

	UFUNCTION()
	void OnPermissionsGranted(const TArray<FString>& Permissions, const TArray<bool>& GrantResults);
};
