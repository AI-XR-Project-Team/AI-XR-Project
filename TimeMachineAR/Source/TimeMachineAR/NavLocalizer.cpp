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

// LogNav 는 NavClient.h 에서 선언하고 NavClient.cpp 에서 정의한다. 여기서 따로
// DEFINE_LOG_CATEGORY_STATIC 을 두면 unity 빌드에서 중복 정의로 깨진다.

namespace
{
	/** 추적 품질 저하 이유(AR enum) → 사람이 읽는 안내 문구(5-B2 경고 배너용). */
	FString QualityReasonText(EARTrackingQualityReason Reason)
	{
		switch (Reason)
		{
		case EARTrackingQualityReason::ExcessiveMotion:
			return TEXT("너무 빠르게 움직였습니다. QR 을 다시 찍어 주세요");
		case EARTrackingQualityReason::InsufficientFeatures:
			return TEXT("주변이 밋밋해 추적이 어렵습니다. QR 을 다시 찍어 주세요");
		case EARTrackingQualityReason::InsufficientLight:
			return TEXT("주변이 어둡습니다. 밝은 곳에서 QR 을 다시 찍어 주세요");
		case EARTrackingQualityReason::Relocalizing:
			return TEXT("위치를 다시 잡는 중입니다. QR 을 다시 찍어 주세요");
		case EARTrackingQualityReason::Initializing:
			return TEXT("추적을 준비 중입니다. 잠시 후 QR 을 다시 찍어 주세요");
		default:
			return TEXT("위치가 흔들렸습니다. QR 을 다시 찍어 주세요");
		}
	}
}

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
	LastMapId = MapId;   // RescanFromUser 가 같은 맵으로 다시 시작할 수 있게.
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
	// 품질 감지 상태도 함께 리셋한다. 재탐색 중에는 경고를 띄우지 않는다.
	SecondsPoorQuality = 0.f;
	bTrackingDegraded = false;
}

void UNavLocalizer::RescanFromUser()
{
	UE_LOG(LogNav, Log, TEXT("[Localizer] 사용자 재스캔 요청. 변환을 버리고 다시 탐색한다."));
	// ResetLocalization 이 상태를 비우고, StartLocalizing 이 마지막 맵으로 다시 탐색한다
	// (마커 목록이 이미 있으면 서버는 다시 부르지 않는다).
	StartLocalizing(LastMapId);
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

	// 안내 중(측위 후)에만 추적 품질을 지켜본다. 흔들려서 센서가 틀어지면 경고를 띄운다.
	MonitorTrackingQuality(DeltaTime);
}

void UNavLocalizer::MonitorTrackingQuality(float DeltaTime)
{
	// 좋은 품질 = 위치까지 잡히는 상태. 그 외(NotTracking / OrientationOnly)는 나쁨.
	const EARTrackingQuality Quality = UARBlueprintLibrary::GetTrackingQuality();
	const bool bGood = (Quality == EARTrackingQuality::OrientationAndPosition);

	if (bGood)
	{
		SecondsPoorQuality = 0.f;
		if (bTrackingDegraded)
		{
			bTrackingDegraded = false;
			UE_LOG(LogNav, Log, TEXT("[Localizer] 추적 품질 회복."));
			OnTrackingRecovered.Broadcast();
		}
		return;
	}

	// 나쁜 상태가 이어진 시간을 쌓는다. 지속 시간 게이트를 넘는 순간 한 번만 경고한다
	// (한 프레임 튐으로 배너가 깜빡이지 않게).
	SecondsPoorQuality += DeltaTime;
	if (!bTrackingDegraded && SecondsPoorQuality >= PoorQualityHoldSeconds)
	{
		bTrackingDegraded = true;
		const FString Reason = QualityReasonText(UARBlueprintLibrary::GetTrackingQualityReason());
		UE_LOG(LogNav, Warning, TEXT("[Localizer] 추적 품질 저하 %.1f초 지속: %s"),
			SecondsPoorQuality, *Reason);
		OnTrackingDegraded.Broadcast(Reason);
	}
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
	// 서버 맵은 오른손 좌표계(+X=오른쪽, +Y=앞), UE 월드는 왼손 좌표계다.
	// 둘은 거울상이라 순수 회전만으로는 못 맞춘다 — 좌우(측면)가 뒤집힌다.
	// 그래서 맵 Y 축을 반전해 왼손 프레임(맵')으로 바꾼 뒤 회전+평행이동한다.
	// Y 를 뒤집으면 회전 방향도 반대가 되므로 heading 부호도 반전한다.
	const float MapHeadingDeg = -(AnchorMarker.HeadingDeg + MarkerHeadingOffsetDeg);
	const float YawOffsetDeg = FRotator::NormalizeAxis(MarkerWorld.Rotator().Yaw - MapHeadingDeg);

	const FRotator Rot(0.f, YawOffsetDeg, 0.f);
	const FVector MarkerMap(AnchorMarker.PosXCm, -AnchorMarker.PosYCm, AnchorMarker.PosZCm);

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
	// 맵' 는 Y 를 뒤집은 왼손 프레임이라, 서버 맵 heading 은 부호가 반대다.
	CurrentPose.HeadingDeg = FRotator::NormalizeAxis(
		MapToWorldXf.Rotator().Yaw - Cam->GetCameraRotation().Yaw);
	CurrentPose.bHasHeading = true;

	OnPoseUpdated.Broadcast(CurrentPose);
}

// ---------------------------------------------------------------------- 변환

FVector UNavLocalizer::MapToWorld(float PosXCm, float PosYCm, float PosZCm) const
{
	// 맵(오른손) → 맵'(왼손): Y 반전 후 변환.
	const FVector P(PosXCm, -PosYCm, PosZCm);
	return bLocalized ? MapToWorldXf.TransformPosition(P) : FVector(PosXCm, PosYCm, PosZCm);
}

FVector UNavLocalizer::WorldToMap(const FVector& WorldLocation) const
{
	if (!bLocalized)
	{
		return WorldLocation;
	}
	// 맵'(왼손) → 맵(오른손): 역변환 후 Y 를 되돌린다.
	const FVector P = MapToWorldXf.InverseTransformPosition(WorldLocation);
	return FVector(P.X, -P.Y, P.Z);
}
