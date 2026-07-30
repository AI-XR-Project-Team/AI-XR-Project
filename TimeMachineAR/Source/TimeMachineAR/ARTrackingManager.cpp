#include "ARTrackingManager.h"
#include "ARTrackable.h"
#include "ARPin.h"
#include "DinoOverlayActor.h"
#include "Kismet/GameplayStatics.h"

AARTrackingManager::AARTrackingManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AARTrackingManager::BeginPlay()
{
	Super::BeginPlay();

	// AR 세션 시작
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
				SpawnedOverlay = GetWorld()->SpawnActor<ADinoOverlayActor>(OverlayActorClass, ImageTransform, SpawnParams);

				if (SpawnedOverlay)
				{
					// 3. 스폰된 액터의 루트를 앵커(AR Pin)로 강제 고정시킴 (하이브리드 핸드오프!)
					UARPin* NewAnchor = UARBlueprintLibrary::PinComponent(SpawnedOverlay->GetRootComponent(), ImageTransform, TrackedImage, FName("DinoHybridAnchor"));
					
					if (NewAnchor)
					{
						// 4. 앵커 고정에 성공했으므로 플래그 변경 (더 이상 탐색하지 않음)
						bIsAnchored = true; 
						break;
					}
				}
			}
		}
	}
}
