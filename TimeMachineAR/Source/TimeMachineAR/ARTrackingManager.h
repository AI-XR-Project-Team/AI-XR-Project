#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ARSessionConfig.h"
#include "ARBlueprintLibrary.h"
#include "ARTrackingManager.generated.h"

class ADinoOverlayActor;
class UARPin;

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

	// 레벨에 들어오자마자 마커를 찾을지. 끄면 StartScan 을 부를 때까지 가만히 있는다.
	// 하단 바의 "AR 스캔" 버튼을 쓰기로 했으므로 기본은 끔.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Tracking")
	bool bAutoScanOnStart = false;

	UPROPERTY(BlueprintAssignable, Category = "AR Tracking")
	FOnScanStateChanged OnScanStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "AR Tracking")
	FOnMarkerFound OnMarkerFound;

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

	// 생성된 공룡 오버레이 참조
	UPROPERTY()
	ADinoOverlayActor* SpawnedOverlay;

	// 오버레이를 고정한 앵커. 걷어낼 때 같이 지워야 트래킹에 찌꺼기가 남지 않는다.
	UPROPERTY()
	UARPin* OverlayPin;

	void CheckForTrackedImages();

	void RequestCameraPermissionAndStart();
	void StartARSessionInternal();

	UFUNCTION()
	void OnPermissionsGranted(const TArray<FString>& Permissions, const TArray<bool>& GrantResults);
};
