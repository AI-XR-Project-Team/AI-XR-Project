#include "NavLocalizer.h"

#include "NavClient.h"
#include "ARBlueprintLibrary.h"
#include "ARTrackable.h"
#include "ARTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

// NavClient.cpp 와 같은 이름의 static 카테고리. 파일마다 따로 정의되지만 로그
// 태그는 같아서 `adb logcat -s LogNav:V` 하나로 네비 전체를 볼 수 있다.
DEFINE_LOG_CATEGORY_STATIC(LogNav, Log, All);

UNavLocalizer* UNavLocalizer::GetNavLocalizer(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr)
	{
		return World->GetSubsystem<UNavLocalizer>();
	}
	return nullptr;
}

void UNavLocalizer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CurrentPose = FNavMapPose();
}

void UNavLocalizer::Deinitialize()
{
	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UNavClient* Client = GI->GetSubsystem<UNavClient>())
		{
			Client->OnMarkersReceived.RemoveDynamic(this, &UNavLocalizer::HandleMarkersReceived);
			Client->OnRequestFailed.RemoveDynamic(this, &UNavLocalizer::HandleMarkersFailed);
		}
	}
	Super::Deinitialize();
}

TStatId UNavLocalizer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNavLocalizer, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------- 시작/중지

void UNavLocalizer::StartLocalizing(const FString& MapId)
{
	ResetLocalization();

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNavClient* Client = GI ? GI->GetSubsystem<UNavClient>() : nullptr;
	if (Client == nullptr)
	{
		UE_LOG(LogNav, Error, TEXT("[Localizer] NavClient 를 찾을 수 없다. 측위를 시작하지 못했다."));
		return;
	}

	PendingMapId = MapId;
	bScanning = true;

	// 마커 목록이 이미 있으면 서버를 다시 부르지 않는다. 층을 옮기지 않는 한
	// 마커 위치는 바뀌지 않는다.
	if (bMarkersReady)
	{
		UE_LOG(LogNav, Log, TEXT("[Localizer] 마커 %d 개 보유. 바로 탐색 시작."), KnownMarkers.Num());
		return;
	}

	// OnRequestFailed 는 route 요청과 공유하는 델리게이트다. 마커를 받는 즉시
	// 떼어내 다른 요청의 실패를 측위 실패로 오인하지 않게 한다.
	Client->OnMarkersReceived.AddDynamic(this, &UNavLocalizer::HandleMarkersReceived);
	Client->OnRequestFailed.AddDynamic(this, &UNavLocalizer::HandleMarkersFailed);
	Client->GetMarkers(PendingMapId);

	UE_LOG(LogNav, Log, TEXT("[Localizer] 마커 목록 요청 (map=%s)"),
		PendingMapId.IsEmpty() ? TEXT("<default>") : *PendingMapId);
}

void UNavLocalizer::StopScanning()
{
	bScanning = false;
}

void UNavLocalizer::ResetLocalization()
{
	bScanning = false;
	bLocalized = false;
	bLostReported = false;
	SecondsSinceMarkerSeen = 0.f;
	AnchorImage = nullptr;
	AnchorMarkerCode.Reset();
	AnchorMarker = FNavMarker();
	MapToWorldXf = FTransform::Identity;
	CurrentPose = FNavMapPose();
}

// ---------------------------------------------------------------------- 서버 응답

void UNavLocalizer::HandleMarkersReceived(const TArray<FNavMarker>& Markers)
{
	KnownMarkers.Reset();
	for (const FNavMarker& M : Markers)
	{
		KnownMarkers.Add(M.Code, M);
	}
	bMarkersReady = KnownMarkers.Num() > 0;

	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UNavClient* Client = GI->GetSubsystem<UNavClient>())
		{
			Client->OnMarkersReceived.RemoveDynamic(this, &UNavLocalizer::HandleMarkersReceived);
			Client->OnRequestFailed.RemoveDynamic(this, &UNavLocalizer::HandleMarkersFailed);
		}
	}

	UE_LOG(LogNav, Log, TEXT("[Localizer] 마커 %d 개 수신. 탐색 시작."), KnownMarkers.Num());
}

void UNavLocalizer::HandleMarkersFailed(int32 StatusCode, const FString& Reason)
{
	// 목록을 못 받으면 마커를 찍어도 그게 맵 어디인지 알 길이 없다.
	bScanning = false;
	UE_LOG(LogNav, Error, TEXT("[Localizer] 마커 목록 실패 (%d): %s"), StatusCode, *Reason);
}

// ---------------------------------------------------------------------- Tick

void UNavLocalizer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bScanning && bMarkersReady && !bLocalized)
	{
		if (TryLocalizeFromTrackedImages())
		{
			bScanning = false;
			bLocalized = true;
			bLostReported = false;
			SecondsSinceMarkerSeen = 0.f;
			UE_LOG(LogNav, Log, TEXT("[Localizer] 측위 성립. 기준 마커=%s"), *AnchorMarkerCode);
			OnLocalized.Broadcast(AnchorMarkerCode);
		}
	}

	if (!bLocalized)
	{
		return;
	}

	// 변환을 다시 세울지 판단한다. 기본은 latch — 처음 잡은 변환을 계속 쓰고,
	// 마커를 한동안 못 보다가 다시 마주쳤을 때만 갱신한다(ue-nav-ui-plan.md §2).
	// 매 틱 재계산은 추적 노이즈가 그대로 실려 지도가 부들거리므로 기본이 아니다.
	if (AnchorImage != nullptr && AnchorImage->GetTrackingState() == EARTrackingState::Tracking)
	{
		const bool bReacquired = bRelatchOnReacquire && SecondsSinceMarkerSeen >= ReacquireGapSeconds;
		if (bContinuousRelatch || bReacquired)
		{
			SolveTransform(AnchorImage->GetLocalToWorldTransform());
			if (bReacquired)
			{
				UE_LOG(LogNav, Log, TEXT("[Localizer] 마커 재관측(%.1f초 만). 드리프트 보정."),
					SecondsSinceMarkerSeen);
			}
		}

		SecondsSinceMarkerSeen = 0.f;
		bLostReported = false;
	}
	else
	{
		SecondsSinceMarkerSeen += DeltaTime;
		if (LostAfterSeconds > 0.f && !bLostReported && SecondsSinceMarkerSeen > LostAfterSeconds)
		{
			bLostReported = true;
			UE_LOG(LogNav, Warning, TEXT("[Localizer] 마커를 %.1f 초째 못 봤다. 위치가 밀렸을 수 있다."),
				SecondsSinceMarkerSeen);
			OnLocalizationLost.Broadcast();
		}
	}

	UpdateCurrentPose();
}

// ---------------------------------------------------------------------- 측위

bool UNavLocalizer::TryLocalizeFromTrackedImages()
{
	const TArray<UARTrackedGeometry*> Geometries =
		UARBlueprintLibrary::GetAllGeometriesByClass(UARTrackedImage::StaticClass());

	for (UARTrackedGeometry* Geometry : Geometries)
	{
		UARTrackedImage* Image = Cast<UARTrackedImage>(Geometry);
		if (Image == nullptr || Image->GetTrackingState() != EARTrackingState::Tracking)
		{
			continue;
		}

		// 후보 이미지의 이름이 곧 서버 마커 code 다(ARTrackingManager 와 같은 규약).
		const UARCandidateImage* Candidate = Image->GetDetectedImage();
		if (Candidate == nullptr)
		{
			continue;
		}
		const FString Code = Candidate->GetFriendlyName();

		const FNavMarker* Found = KnownMarkers.Find(Code);
		if (Found == nullptr)
		{
			// 서버가 모르는 마커. 공룡용 마커일 수 있으니 조용히 넘어간다.
			continue;
		}

		AnchorImage = Image;
		AnchorMarkerCode = Code;
		AnchorMarker = *Found;
		SolveTransform(Image->GetLocalToWorldTransform());
		return true;
	}

	return false;
}

void UNavLocalizer::SolveTransform(const FTransform& MarkerWorld)
{
	// 두 방위의 차이가 곧 맵→월드 회전이다.
	//   서버 heading : 맵 +X 축 기준 CCW(도)
	//   UE yaw       : 월드 +X 축 기준 CCW(도)  ← 규약이 같아서 그냥 뺀다
	// 축 규약이 같은 것은 우연이 아니라, 서버 좌표계를 처음부터 UE 기준으로
	// 정해 두었기 때문이다(docs/nav-server-integration-guide.md).
	const float MapHeadingDeg = AnchorMarker.HeadingDeg + MarkerHeadingOffsetDeg;
	const float YawOffsetDeg = FRotator::NormalizeAxis(MarkerWorld.Rotator().Yaw - MapHeadingDeg);

	const FRotator Rot(0.f, YawOffsetDeg, 0.f);
	const FVector MarkerMap(AnchorMarker.PosXCm, AnchorMarker.PosYCm, AnchorMarker.PosZCm);

	// 회전만 걸면 마커가 원점 근처에 놓인다. 실제 마커 자리로 밀어 준다.
	const FVector Translation = MarkerWorld.GetLocation() - Rot.RotateVector(MarkerMap);

	MapToWorldXf = FTransform(Rot, Translation, FVector::OneVector);
}

void UNavLocalizer::UpdateCurrentPose()
{
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Cam == nullptr)
	{
		return;   // AR 세션이 아직 카메라를 못 붙였다.
	}

	const FVector MapPoint = WorldToMap(Cam->GetCameraLocation());

	CurrentPose.PosXCm = MapPoint.X;
	CurrentPose.PosYCm = MapPoint.Y;
	// 카메라는 눈높이에 있다. 바닥 렌더가 공중에 뜨지 않도록 마커 높이로 눌러 준다.
	CurrentPose.PosZCm = bSnapHeightToMarker ? AnchorMarker.PosZCm : MapPoint.Z;
	CurrentPose.HeadingDeg = FRotator::NormalizeAxis(
		Cam->GetCameraRotation().Yaw - MapToWorldXf.Rotator().Yaw);
	CurrentPose.bHasHeading = true;

	OnPoseUpdated.Broadcast(CurrentPose);
}

// ---------------------------------------------------------------------- 변환

FVector UNavLocalizer::MapToWorld(float PosXCm, float PosYCm, float PosZCm) const
{
	const FVector P(PosXCm, PosYCm, PosZCm);
	return bLocalized ? MapToWorldXf.TransformPosition(P) : P;
}

FVector UNavLocalizer::WorldToMap(const FVector& WorldLocation) const
{
	return bLocalized ? MapToWorldXf.InverseTransformPosition(WorldLocation) : WorldLocation;
}
