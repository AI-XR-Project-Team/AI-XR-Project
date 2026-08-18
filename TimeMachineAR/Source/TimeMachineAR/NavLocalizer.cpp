#include "NavLocalizer.h"

#include "NavClient.h"
#include "ARBlueprintLibrary.h"
#include "ARSessionConfig.h"
#include "ARTrackable.h"
#include "ARTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	// ─── 6-1a 프로브용 CSV 문자열 헬퍼 (6단계 종료 시 함께 제거) ───

	/** 추적 품질 → CSV 셀(짧게). 서버 스키마의 quality 열. */
	FString ProbeQualityStr(EARTrackingQuality Q)
	{
		switch (Q)
		{
		case EARTrackingQuality::OrientationAndPosition: return TEXT("Good");
		case EARTrackingQuality::OrientationOnly:        return TEXT("OrientOnly");
		default:                                         return TEXT("NotTracking");
		}
	}

	/** 추적 품질 저하 이유 → CSV 셀(enum 이름). quality_reason 열. */
	FString ProbeReasonStr(EARTrackingQualityReason R)
	{
		switch (R)
		{
		case EARTrackingQualityReason::ExcessiveMotion:      return TEXT("ExcessiveMotion");
		case EARTrackingQualityReason::InsufficientFeatures: return TEXT("InsufficientFeatures");
		case EARTrackingQualityReason::InsufficientLight:    return TEXT("InsufficientLight");
		case EARTrackingQualityReason::Relocalizing:         return TEXT("Relocalizing");
		case EARTrackingQualityReason::Initializing:         return TEXT("Initializing");
		default:                                             return TEXT("None");
		}
	}

	/** 이미지 추적 상태 → CSV 셀. state 열. */
	FString ProbeStateStr(EARTrackingState S)
	{
		switch (S)
		{
		case EARTrackingState::Tracking:        return TEXT("Tracking");
		case EARTrackingState::NotTracking:     return TEXT("NotTracking");
		case EARTrackingState::StoppedTracking: return TEXT("StoppedTracking");
		default:                                return TEXT("Unknown");
		}
	}

	/** 두 단위벡터 사이 각(도). 시선벡터와 마커 로컬축 사이 각을 잰다. */
	float ProbeAngleDeg(const FVector& A, const FVector& Axis)
	{
		const float Dot = FMath::Clamp(FVector::DotProduct(A, Axis.GetSafeNormal()), -1.f, 1.f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
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

	// 6-1a: 세션 태그를 실행 시각으로 한 번 만든다(예: "a1-20260818-143207"). 앱을 껐다
	// 켜면 새 태그 = 서버에 새 CSV 파일. ini 로 사람이 바꾸는 방식이 아니다(§E-1b).
	ProbeSessionTag = FString::Printf(TEXT("%s-%s"),
		*ProbeSessionPrefix, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
}

void UNavLocalizer::Deinitialize()
{
	// 6-1a: 앱 종료 시 버퍼에 남은 CSV 를 마지막으로 업로드한다(베스트 에포트).
	if (bProbeEnabled)
	{
		ProbeFlush(true);
	}

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
	SecondsGoodQuality = 0.f;
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

	// ─── 6-1a 프로브. 반드시 아래 측위 early return 앞에 둔다(측위 전에도 찍어야 한다) ───
	if (bProbeEnabled)
	{
		ProbeElapsed   += DeltaTime;
		ProbeSinceScan += DeltaTime;
		if (!bProbeRegistered && bProbeRegisterRuntimeImages
			&& ProbeElapsed >= ProbeRegisterDelaySeconds)
		{
			ProbeRegisterImages();
		}
		if (ProbeSinceScan >= ProbeIntervalSeconds)
		{
			ProbeSinceScan = 0.f;
			ProbeScan();
		}
	}

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
	// ⚠️ ARCore 함정: GetTrackingQuality() 는 이분법이다 — pose 가 있으면 무조건
	// OrientationAndPosition, 완전히 잃으면 NotTracking(OrientationOnly 는 안 나옴,
	// GoogleARCoreXRTrackingSystem.cpp). 흔들림은 Quality 가 아니라
	// GetTrackingQualityReason()(ExcessiveMotion 등)에 담긴다. 그래서 Quality 저하
	// "또는" Reason≠None 을 저하로 본다 — Quality 만 보면 흔들림을 절대 못 잡는다.
	const EARTrackingQuality Quality = UARBlueprintLibrary::GetTrackingQuality();
	const EARTrackingQualityReason Reason = UARBlueprintLibrary::GetTrackingQualityReason();
	const bool bBad = (Quality != EARTrackingQuality::OrientationAndPosition)
		|| (Reason != EARTrackingQualityReason::None);

	if (bBad)
	{
		// 나쁜 상태가 이어진 시간을 쌓는다. 임계를 넘는 순간 한 번만 경고.
		SecondsPoorQuality += DeltaTime;
		SecondsGoodQuality = 0.f;
		if (!bTrackingDegraded && SecondsPoorQuality >= PoorQualityHoldSeconds)
		{
			bTrackingDegraded = true;
			const FString Msg = QualityReasonText(Reason);
			UE_LOG(LogNav, Warning, TEXT("[Localizer] 추적 품질 저하 %.1f초 지속(Q=%d,R=%d): %s"),
				SecondsPoorQuality, static_cast<int32>(Quality), static_cast<int32>(Reason), *Msg);
			OnTrackingDegraded.Broadcast(Msg);
		}
		return;
	}

	// 좋은 상태. 단 좋은 프레임 하나로 곧바로 지우지 않는다 — 흔들 때 품질이 좋음↔나쁨을
	// 깜빡이므로, QualityRecoverSeconds 만큼 "연속으로" 좋아야 누적 저하를 지우고 회복 처리.
	SecondsGoodQuality += DeltaTime;
	if (SecondsGoodQuality >= QualityRecoverSeconds)
	{
		SecondsPoorQuality = 0.f;
		if (bTrackingDegraded)
		{
			bTrackingDegraded = false;
			UE_LOG(LogNav, Log, TEXT("[Localizer] 추적 품질 회복(%.1f초 연속 양호)."), SecondsGoodQuality);
			OnTrackingRecovered.Broadcast();
		}
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

// ==================================================================== 6-1a 프로브
//
// 아래 4개 함수(ProbeRegisterImages/ProbeScan/ProbeFlush/ProbePush)는 6-1a 인식 계측
// 전용이다. 6단계 종료 시 통째로 제거한다 — 단 ProbeScan 의 GetAllGeometriesByClass 순회는
// 6-2 앵커 전환이 물려받으므로 그때 승격시킨다(spec §0 "6단계 종료 시 제거 목록").

void UNavLocalizer::ProbeRegisterImages()
{
	bProbeRegistered = true;   // 성패와 무관하게 한 번만 시도한다(매 틱 재시도 방지).

	// ARTrackingManager 가 쓰는 것과 같은 세션 설정 객체를 얻는다.
	UARSessionConfig* Cfg = LoadObject<UARSessionConfig>(nullptr, *ProbeSessionConfigPath);
	if (Cfg == nullptr)
	{
		UE_LOG(LogNav, Error, TEXT("[Probe] ARSessionConfig 로드 실패: %s"), *ProbeSessionConfigPath);
		return;
	}

	int32 Added = 0;
	for (const FString& Entry : ProbeImageEntries)
	{
		FString FriendlyName, TexPath;
		if (!Entry.Split(TEXT("|"), &FriendlyName, &TexPath))
		{
			UE_LOG(LogNav, Warning, TEXT("[Probe] 항목 형식 오류(‘이름|경로’ 아님): %s"), *Entry);
			continue;
		}
		FriendlyName.TrimStartAndEndInline();
		TexPath.TrimStartAndEndInline();

		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexPath);
		if (Tex == nullptr)
		{
			UE_LOG(LogNav, Error, TEXT("[Probe] 텍스처 로드 실패: %s (%s)"), *TexPath, *FriendlyName);
			continue;
		}

		// PhysicalWidth 단위(cm/m)가 UE 버전마다 불확실하다 → M-1 에서 와이어프레임 크기로 검증한다.
		UARBlueprintLibrary::AddRuntimeCandidateImage(
			Cfg, Tex, FriendlyName, ProbeMarkerWidthCm);
		++Added;
		UE_LOG(LogNav, Log, TEXT("[Probe] 후보 등록: %s ← %s (폭 %.1f)"),
			*FriendlyName, *TexPath, ProbeMarkerWidthCm);
	}

	// 후보를 반영하려면 세션을 다시 시작해야 한다(§E-3). 팀원1 공룡 오버레이 핀은 날아가지만
	// 프로브 테스트 중에는 무방하다. DA_ARSession 은 디스크에 저장하지 않는다(런타임 등록만).
	UARBlueprintLibrary::StopARSession();
	UARBlueprintLibrary::StartARSession(Cfg);

	UE_LOG(LogNav, Log, TEXT("[Probe] 런타임 후보 %d 개 등록 + AR 세션 재시작. 세션태그=%s"),
		Added, *ProbeSessionTag);
}

void UNavLocalizer::ProbeScan()
{
	UWorld* World = GetWorld();
	const TArray<UARTrackedGeometry*> Geometries =
		UARBlueprintLibrary::GetAllGeometriesByClass(UARTrackedImage::StaticClass());

	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector CamLoc = Cam ? Cam->GetCameraLocation() : FVector::ZeroVector;

	// 품질/이유는 5-B1 과 같은 정적 조회. "멀어서 놓친 것"과 "흔들려서 놓친 것"을 가른다.
	const FString QualityCell = ProbeQualityStr(UARBlueprintLibrary::GetTrackingQuality());
	const FString ReasonCell  = ProbeReasonStr(UARBlueprintLibrary::GetTrackingQualityReason());

	// 이번 스캔에서 본 마커를 모아 두고, 지난 스캔의 bVisible 과 비교해 LOST 에지를 잡는다.
	TSet<FString> SeenThisScan;
	int32 Simultaneous = 0;

	for (UARTrackedGeometry* Geometry : Geometries)
	{
		UARTrackedImage* Image = Cast<UARTrackedImage>(Geometry);
		if (Image == nullptr)
		{
			continue;
		}
		const EARTrackingState State = Image->GetTrackingState();
		const bool bTracking = (State == EARTrackingState::Tracking);
		if (bTracking)
		{
			++Simultaneous;
		}

		const UARCandidateImage* Candidate = Image->GetDetectedImage();
		const FString Code = Candidate ? Candidate->GetFriendlyName() : TEXT("?");

		const FTransform Xf = Image->GetLocalToWorldTransform();
		const FVector MarkerLoc = Xf.GetLocation();
		const float DistCm = (CamLoc - MarkerLoc).Size();
		const FVector View = (CamLoc - MarkerLoc).GetSafeNormal();
		const float AngX = ProbeAngleDeg(View, Xf.GetUnitAxis(EAxis::X));
		const float AngY = ProbeAngleDeg(View, Xf.GetUnitAxis(EAxis::Y));
		const float AngZ = ProbeAngleDeg(View, Xf.GetUnitAxis(EAxis::Z));

		FProbeSeen& Seen = ProbeSeen.FindOrAdd(Code);

		// ACQUIRE / LOST 에지 판정. 추적 중일 때만 "보인다"로 친다.
		FString Event = TEXT("SAMPLE");
		if (bTracking)
		{
			SeenThisScan.Add(Code);
			if (!Seen.bVisible)
			{
				Event = TEXT("ACQUIRE");
				++Seen.Acquires;
			}
			Seen.bVisible   = true;
			Seen.LastDistCm = DistCm;
			Seen.MinDistCm  = FMath::Min(Seen.MinDistCm, DistCm);
			Seen.MaxDistCm  = FMath::Max(Seen.MaxDistCm, DistCm);
		}

		// 추적 중이 아니면 이번 스캔 SAMPLE 로 남기지 않고, 아래 LOST 처리에 맡긴다.
		if (!bTracking)
		{
			continue;
		}

		if (bProbeDrawDebugBox && World)
		{
			const FVector Extent(ProbeMarkerWidthCm * 0.5f, ProbeMarkerHeightCm * 0.5f, 1.f);
			DrawDebugBox(World, MarkerLoc, Extent, Xf.GetRotation(),
				Event == TEXT("ACQUIRE") ? FColor::Green : FColor::Cyan, false, -1.f, 0, 2.f);
			DrawDebugCoordinateSystem(World, MarkerLoc, Xf.Rotator(), 30.f, false, -1.f, 0, 2.f);
		}

		const FString Csv = FString::Printf(
			TEXT("%.2f,%s,%s,%.0f,%.0f,%.0f,%.0f,%s,%d,%s,%s"),
			ProbeElapsed, *Event, *Code, DistCm, AngX, AngY, AngZ,
			*ProbeStateStr(State), Simultaneous, *QualityCell, *ReasonCell);

		const int32 Min = static_cast<int32>(ProbeElapsed) / 60;
		const int32 Sec = static_cast<int32>(ProbeElapsed) % 60;
		const FString Human = FString::Printf(TEXT("[%02d:%04.1f] %s %s  %.2f m"),
			Min, ProbeElapsed - Min * 60, *Code,
			Event == TEXT("ACQUIRE") ? TEXT("획득") : (Event == TEXT("LOST") ? TEXT("상실") : TEXT("추적")),
			DistCm * 0.01f);

		// ACQUIRE 와 SAMPLE 을 CSV 로 남긴다. 화면에는 에지(획득)만 띄워 소음을 줄인다.
		ProbePush(Csv, Event == TEXT("ACQUIRE") ? Human : FString());
	}

	// 지난 스캔엔 보였는데 이번엔 안 보이는 마커 → LOST 에지 한 번.
	for (TPair<FString, FProbeSeen>& Pair : ProbeSeen)
	{
		FProbeSeen& Seen = Pair.Value;
		if (Seen.bVisible && !SeenThisScan.Contains(Pair.Key))
		{
			Seen.bVisible = false;
			const FString Csv = FString::Printf(
				TEXT("%.2f,LOST,%s,%.0f,0,0,0,NotTracking,%d,%s,%s"),
				ProbeElapsed, *Pair.Key, Seen.LastDistCm, Simultaneous, *QualityCell, *ReasonCell);
			const int32 Min = static_cast<int32>(ProbeElapsed) / 60;
			const FString Human = FString::Printf(TEXT("[%02d:%04.1f] %s 상실  %.2f m"),
				Min, ProbeElapsed - Min * 60, *Pair.Key, Seen.LastDistCm * 0.01f);
			ProbePush(Csv, Human);
		}
	}

	ProbeSimultaneous = Simultaneous;

	// 화면 요약 + 최근 이벤트. 고정 Key 로 매 스캔 갱신(Development 빌드에서만 보인다).
	if (GEngine)
	{
		const FString Summary = FString::Printf(
			TEXT("■ 프로브  동시추적 %d   버퍼 %d   업로드 OK×%d 실패×%d"),
			ProbeSimultaneous, ProbeCsv.Num(), ProbeUploadOk, ProbeUploadFail);
		GEngine->AddOnScreenDebugMessage(920001, 0.3f, FColor::Yellow, Summary);
		for (int32 i = 0; i < ProbeEvents.Num(); ++i)
		{
			GEngine->AddOnScreenDebugMessage(920010 + i, 0.3f, FColor::White, ProbeEvents[i]);
		}
	}

	ProbeFlush(false);
}

void UNavLocalizer::ProbePush(const FString& CsvLine, const FString& HumanLine)
{
	ProbeCsv.Add(CsvLine);
	if (!HumanLine.IsEmpty())
	{
		ProbeEvents.Add(HumanLine);
		// 최근 10줄만 화면에 남긴다.
		while (ProbeEvents.Num() > 10)
		{
			ProbeEvents.RemoveAt(0);
		}
	}
}

void UNavLocalizer::ProbeFlush(bool bFinal)
{
	if (ProbeCsv.Num() == 0)
	{
		return;
	}
	if (!bFinal && ProbeCsv.Num() < ProbeUploadEveryLines)
	{
		return;
	}

	// 스냅샷을 떠서 버퍼를 즉시 비운다 — 비동기 응답을 기다리는 동안 버퍼가 무한정 커지거나
	// 같은 줄이 두 번 올라가는 것을 막는다. 실패하면 이 스냅샷을 폰에 폴백 저장한다.
	const FString Payload = FString::Join(ProbeCsv, TEXT("\n")) + TEXT("\n");
	ProbeCsv.Reset();

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNavClient* Client = GI ? GI->GetSubsystem<UNavClient>() : nullptr;
	const FString Base = Client ? Client->GetServerBaseUrl() : FString();

	// 폴백: 서버 주소가 없으면(또는 요청 실패) 폰의 Saved/Probe 에 append 로 남긴다.
	// 태그를 값으로 캡처한다 — 비동기 응답이 서브시스템 파괴(종료 시 최종 flush) 뒤에
	// 와도 안전하도록 this 를 잡지 않는다.
	const FString Tag = ProbeSessionTag;
	auto SaveFallback = [Payload, Tag]()
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Probe");
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		const FString File = Dir / FString::Printf(TEXT("probe_%s.csv"), *Tag);
		FFileHelper::SaveStringToFile(Payload, *File,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	};

	if (Base.IsEmpty())
	{
		++ProbeUploadFail;
		SaveFallback();
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/debug/probe-log?session=%s"), *Base, *ProbeSessionTag);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("text/plain"));
	Request->SetContentAsString(Payload);

	TWeakObjectPtr<UNavLocalizer> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Payload, SaveFallback](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
		{
			const bool bSuccess = bOk && Response.IsValid()
				&& Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300;
			if (UNavLocalizer* Self = WeakThis.Get())
			{
				if (bSuccess) { ++Self->ProbeUploadOk; }
				else          { ++Self->ProbeUploadFail; }
			}
			if (!bSuccess)
			{
				// 서버가 안 떠 있어도 데이터를 잃지 않는다(현장에서 유일하게 되돌릴 수 있는 신호).
				SaveFallback();
			}
		});
	Request->ProcessRequest();
}
