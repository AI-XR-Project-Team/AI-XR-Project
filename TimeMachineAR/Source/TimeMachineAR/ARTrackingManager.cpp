#include "ARTrackingManager.h"
#include "ARSessionConfig.h"
#include "ARTrackable.h"
#include "ARTypes.h"
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

	// 세션은 바로 켜되(카메라 프리뷰가 나와야 하므로), 마커 탐색은 버튼이 시작한다.
	if (bAutoScanOnStart)
	{
		StartScan();
	}
}

AARTrackingManager* AARTrackingManager::GetARTrackingManager(const UObject* WorldContextObject)
{
	return Cast<AARTrackingManager>(UGameplayStatics::GetActorOfClass(
		WorldContextObject, AARTrackingManager::StaticClass()));
}

void AARTrackingManager::StartScan()
{
	// 다시 스캔하려면 먼저 붙여 둔 것을 걷어낸다. 그러지 않으면 bIsAnchored 때문에
	// Tick 이 곧바로 되돌아 나가 아무 일도 일어나지 않는다.
	ClearOverlay();

	if (bIsScanning)
	{
		return;
	}

	bIsScanning = true;
	OnScanStateChanged.Broadcast(true);
}

void AARTrackingManager::StopScan()
{
	if (!bIsScanning)
	{
		return;
	}

	bIsScanning = false;
	OnScanStateChanged.Broadcast(false);
}

void AARTrackingManager::ClearOverlay()
{
	if (OverlayPin)
	{
		UARBlueprintLibrary::RemovePin(OverlayPin);
		OverlayPin = nullptr;
	}

	if (SpawnedOverlay)
	{
		SpawnedOverlay->Destroy();
		SpawnedOverlay = nullptr;
	}

	bIsAnchored = false;
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

	// 스캔 중이고, 아직 앵커(AR Pin)가 생성되지 않았다면 마커를 계속 탐색
	if (bIsScanning && !bIsAnchored)
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
					//    다시 스캔할 때 걷어내야 하므로 핀을 들고 있는다.
					OverlayPin = UARBlueprintLibrary::PinComponent(SpawnedOverlay->GetRootComponent(), ImageTransform, TrackedImage, FName("DinoHybridAnchor"));

					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(2, 5.0f, FColor::Cyan, TEXT("[AR] Dino Overlay Successfully Spawned & Anchored!"));
					}

					// 5. 어느 마커인지 알아낸다. 네비게이션이 이 code 로 서버에서
					//    마커의 지도 좌표를 조회한다.
					FString MarkerCode;
					if (const UARCandidateImage* Candidate = TrackedImage->GetDetectedImage())
					{
						MarkerCode = Candidate->GetFriendlyName();
					}

					// 6. 찾았으니 스캔을 끝낸다. UI 가 "스캔 중" 표시를 지울 수 있게 알린다.
					StopScan();
					OnMarkerFound.Broadcast(OverlayPin, ImageTransform, MarkerCode);
					break;
				}
			}
		}
	}
}
