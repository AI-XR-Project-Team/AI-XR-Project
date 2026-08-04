#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ARSessionConfig.h"
#include "ARBlueprintLibrary.h"
#include "ARTrackingManager.generated.h"

class ADinoOverlayActor;

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

private:
	// 하이브리드 트래킹 핸드오프 상태 플래그
	bool bIsAnchored = false;

	// 생성된 공룡 오버레이 참조
	UPROPERTY()
	ADinoOverlayActor* SpawnedOverlay;

	void CheckForTrackedImages();

	void RequestCameraPermissionAndStart();
	void StartARSessionInternal();

	UFUNCTION()
	void OnPermissionsGranted(const TArray<FString>& Permissions, const TArray<bool>& GrantResults);
};
