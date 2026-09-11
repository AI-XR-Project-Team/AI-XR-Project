#include "ARTrackingManager.h"
#include "ARSessionConfig.h"
#include "ARTrackable.h"
#include "ARTypes.h"
#include "ARPin.h"
#include "DinoOverlayActor.h"
#include "DinoInfoData.h"
#include "DinoRegistry.h"
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

	// 레벨에서 지정하지 않아도 마커 대응이 살아 있도록. SessionConfig 와 같은 방식이다.
	// 이걸 안 두면 레벨을 다시 저장하지 않은 사람에게는 조용히 기본 공룡만 나온다.
	if (!DinoRegistry)
	{
		DinoRegistry = Cast<UDinoRegistry>(StaticLoadObject(
			UDinoRegistry::StaticClass(), nullptr, TEXT("/Game/UI/DinoCard/DA_DinoRegistry.DA_DinoRegistry")));
	}

	UE_LOG(LogTemp, Log, TEXT("[AR] 공룡 대응표 %s (등록 %d종, 마커 지정 %s)"),
		DinoRegistry ? TEXT("로드됨") : TEXT("없음"),
		DinoRegistry ? DinoRegistry->Species.Num() : 0,
		(DinoRegistry && DinoRegistry->HasAnyMarker()) ? TEXT("있음") : TEXT("없음"));

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

	UpdateScanPhase();
}

void AARTrackingManager::CheckForTrackedImages()
{
	// 현재 AR 시스템이 추적 중인 모든 이미지(마커)를 가져옴 (UE5 최신 API 반영)
	TArray<UARTrackedGeometry*> TrackedGeometries = UARBlueprintLibrary::GetAllGeometriesByClass(UARTrackedImage::StaticClass());

	VisibleCandidateCount = TrackedGeometries.Num();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 0.5f, FColor::Green, FString::Printf(TEXT("[AR Status] Session Running | Detected Images: %d"), TrackedGeometries.Num()));
	}

	for (UARTrackedGeometry* TrackedGeometry : TrackedGeometries)
	{
		UARTrackedImage* TrackedImage = Cast<UARTrackedImage>(TrackedGeometry);

		if (TrackedImage == nullptr || TrackedImage->GetTrackingState() != EARTrackingState::Tracking)
		{
			continue;
		}

		if (OverlayActorClass == nullptr)
		{
			continue;
		}

		// 1. 어느 마커인지 먼저 확인한다. 예전에는 스폰한 뒤에 봤지만, 이제 이
		//    이름으로 어떤 공룡을 띄울지 고르므로 스폰보다 앞서야 한다.
		FString MarkerCode;
		if (const UARCandidateImage* Candidate = TrackedImage->GetDetectedImage())
		{
			MarkerCode = Candidate->GetFriendlyName();
		}

		UDinoInfoData* Species = ResolveSpecies(MarkerCode);

		// 2. 대응표를 쓰는 중인데 모르는 마커라면 건너뛴다. 네비게이션용 마커에
		//    공룡이 튀어나오지 않게 하려는 것이고, 다른 마커가 시야에 같이
		//    들어와 있으면 그쪽을 계속 본다.
		const bool bUsingMarkerMap = (DinoRegistry != nullptr && DinoRegistry->HasAnyMarker());
		if (bUsingMarkerMap && bIgnoreUnknownMarkers && Species == nullptr)
		{
			continue;
		}

		// 3. 마커 위치에 오버레이를 스폰한다. 종을 BeginPlay 보다 먼저 넣어야
		//    살점 머티리얼이 그 종의 메시 기준으로 만들어지므로 지연 스폰을 쓴다.
		const FTransform ImageTransform = TrackedImage->GetLocalToWorldTransform();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		SpawnedOverlay = GetWorld()->SpawnActorDeferred<ADinoOverlayActor>(
			OverlayActorClass, ImageTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (SpawnedOverlay == nullptr)
		{
			continue;
		}

		if (Species != nullptr)
		{
			SpawnedOverlay->SetDinoInfo(Species);
		}

		SpawnedOverlay->FinishSpawning(ImageTransform);

		// 4. 중복 스폰 방지
		bIsAnchored = true;

		// 5. 마커 앵커 대신 월드 앵커에 고정 (TrackedImage 대신 nullptr 전달).
		// 진동(Jitter)과 움찔거림(Twitch)을 완벽히 없애는 대신, 
		// 기기 이동 시 미세하게 밀리는 현상(Drift)만 남기는 3번 방식입니다.
		OverlayPin = UARBlueprintLibrary::PinComponent(
			SpawnedOverlay->GetRootComponent(), ImageTransform, nullptr, FName("DinoHybridAnchor"));

		UE_LOG(LogTemp, Log, TEXT("[AR] 마커 '%s' -> 공룡 '%s'"),
			MarkerCode.IsEmpty() ? TEXT("(이름없음)") : *MarkerCode,
			Species != nullptr ? *Species->NameKo.ToString() : TEXT("(BP 기본값)"));

		// 6. 찾았으니 스캔을 끝낸다. UI 가 "스캔 중" 표시를 지울 수 있게 알린다.
		//    MarkerCode 는 네비게이션이 서버에서 마커 좌표를 조회하는 데도 쓴다.
		StopScan();
		OnMarkerFound.Broadcast(OverlayPin, ImageTransform, MarkerCode);
		break;
	}
}

UDinoInfoData* AARTrackingManager::ResolveSpecies(const FString& MarkerCode) const
{
	if (DinoRegistry == nullptr)
	{
		return nullptr;
	}

	if (UDinoInfoData* Found = DinoRegistry->FindByMarker(MarkerCode))
	{
		return Found;
	}

	// 마커가 아직 안 정해졌거나 새 마커가 대응표에 없는 동안에도 빈 화면이
	// 나오지 않게 한다. 폴백도 비어 있으면 BP_DinoOverlay 의 기본값이 쓰인다.
	return DinoRegistry->FallbackSpecies;
}

void AARTrackingManager::UpdateScanPhase()
{
	EDinoScanPhase NewPhase = EDinoScanPhase::Idle;

	if (bIsAnchored)
	{
		NewPhase = EDinoScanPhase::Recognized;
	}
	else if (bIsScanning)
	{
		NewPhase = (VisibleCandidateCount > 0) ? EDinoScanPhase::Aiming : EDinoScanPhase::Searching;
	}

	ScanPhase = NewPhase;
}
