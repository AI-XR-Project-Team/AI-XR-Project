#include "ARTrackingManager.h"
#include "ARSessionConfig.h"
#include "ARTrackable.h"
#include "ARPin.h"
#include "DinoOverlayActor.h"
#include "Kismet/GameplayStatics.h"
#include "AndroidPermissionFunctionLibrary.h"
#include "AndroidPermissionCallbackProxy.h"

AARTrackingManager::AARTrackingManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AARTrackingManager::BeginPlay()
{
	Super::BeginPlay();

	// 에디터에서 누락되었을 경우를 대비한 안전한 자동 로드
	if (!SessionConfig)
	{
		SessionConfig = Cast<UARSessionConfig>(StaticLoadObject(UARSessionConfig::StaticClass(), nullptr, TEXT("/Game/Stuff/DA_ARSession.DA_ARSession")));
	}

	if (!OverlayActorClass)
	{
		OverlayActorClass = StaticLoadClass(ADinoOverlayActor::StaticClass(), nullptr, TEXT("/Game/Stuff/BP_DinoOverlay.BP_DinoOverlay_C"));
	}

	RequestCameraPermissionAndStart();
}

void AARTrackingManager::RequestCameraPermissionAndStart()
{
	const FString CameraPermission = TEXT("android.permission.CAMERA");

	if (UAndroidPermissionFunctionLibrary::CheckPermission(CameraPermission))
	{
		StartARSessionInternal();
	}
	else
	{
		TArray<FString> Permissions;
		Permissions.Add(CameraPermission);
		UAndroidPermissionCallbackProxy* CallbackProxy = UAndroidPermissionFunctionLibrary::AcquirePermissions(Permissions);
		if (CallbackProxy)
		{
			CallbackProxy->OnPermissionsGrantedDynamicDelegate.AddDynamic(this, &AARTrackingManager::OnPermissionsGranted);
		}
		else
		{
			StartARSessionInternal();
		}
	}
}

void AARTrackingManager::OnPermissionsGranted(const TArray<FString>& Permissions, const TArray<bool>& GrantResults)
{
	StartARSessionInternal();
}

void AARTrackingManager::StartARSessionInternal()
{
	if (SessionConfig)
	{
		UARBlueprintLibrary::StartARSession(SessionConfig);
	}
}

void AARTrackingManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	
	// 레벨을 떠나거나 종료될 때 AR 세션 중지
	UARBlueprintLibrary::StopARSession();
}

void AARTrackingManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 아직 앵커(AR Pin)가 생성되지 않았다면 마커를 계속 탐색
	if (!bIsAnchored)
	{
		CheckForTrackedImages();
	}
}

void AARTrackingManager::CheckForTrackedImages()
{
	// 현재 AR 시스템이 추적 중인 모든 이미지(마커)를 가져옴 (UE5 최신 API 반영)
	TArray<UARTrackedGeometry*> TrackedGeometries = UARBlueprintLibrary::GetAllGeometriesByClass(UARTrackedImage::StaticClass());

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 0.5f, FColor::Green, FString::Printf(TEXT("[AR Status] Session Running | Detected Images: %d"), TrackedGeometries.Num()));
	}

	for (UARTrackedGeometry* TrackedGeometry : TrackedGeometries)
	{
		UARTrackedImage* TrackedImage = Cast<UARTrackedImage>(TrackedGeometry);

		// 이미지가 정상적으로 추적 중인지 확인
		if (TrackedImage && TrackedImage->GetTrackingState() == EARTrackingState::Tracking)
		{
			// 1. 이미지의 위치 정보를 추출
			FTransform ImageTransform = TrackedImage->GetLocalToWorldTransform();

			if (OverlayActorClass)
			{
				// 2. 해당 위치에 공룡 오버레이 액터 스폰
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				SpawnedOverlay = GetWorld()->SpawnActor<ADinoOverlayActor>(OverlayActorClass, ImageTransform, SpawnParams);

				if (SpawnedOverlay)
				{
					// 3. 스폰 즉시 플래그를 true로 변경하여 중복 스폰 방지
					bIsAnchored = true;

					// 4. 스폰된 액터의 루트를 앵커(AR Pin)로 강제 고정 시도
					UARBlueprintLibrary::PinComponent(SpawnedOverlay->GetRootComponent(), ImageTransform, TrackedImage, FName("DinoHybridAnchor"));
					
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(2, 5.0f, FColor::Cyan, TEXT("[AR] Dino Overlay Successfully Spawned & Anchored!"));
					}
					break;
				}
			}
		}
	}
}
