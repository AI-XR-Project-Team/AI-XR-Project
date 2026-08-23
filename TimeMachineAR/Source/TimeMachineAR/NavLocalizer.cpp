#include "NavLocalizer.h"

#include "NavClient.h"
#include "ARBlueprintLibrary.h"
#include "ARSessionConfig.h"
#include "ARTrackable.h"
#include "ARTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"   // FIntProperty — MaxNumSimultaneousImagesTracked 리플렉션 설정
// 현장 로그(bFieldLogEnabled)용 — 서버 POST + 폰 폴백 저장
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
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

	// 7단계 §A: AR 세션이 켜지기 "전"에 마커 후보를 세션 설정에 얹는다. 서브시스템
	// Initialize 는 액터 BeginPlay(ARTrackingManager 가 세션을 켜는 곳)보다 먼저 돈다 →
	// 세션 재시작이 필요 없다(공룡 핀 보존, spec §1 D-4'). 여기선 재시작을 허용하지 않는다.
	if (bRegisterMarkerImages)
	{
		RegisterMarkerImages(/*bAllowSessionRestart=*/false);
	}

	// 현장 로그: 세션 태그를 실행 시각으로 한 번 만들고(예: "nav-20260823-153207") 헤더 줄을 쌓는다.
	if (bFieldLogEnabled)
	{
		FieldLogSessionTag = FString::Printf(TEXT("%s-%s"),
			*FieldLogSessionPrefix, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
		FieldLogBuf.Add(TEXT("t_sec,event,from,to,map_x_cm,map_y_cm,heading_deg,extra"));
	}
}

void UNavLocalizer::Deinitialize()
{
	// 앱 종료 시 버퍼에 남은 현장 로그를 마지막으로 업로드한다(베스트 에포트).
	if (bFieldLogEnabled)
	{
		FieldLogFlush(true);
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

	// 안전망: Initialize 에서 후보 등록이 안 됐으면(에셋 로드 실패 등) 여기서 재시도한다.
	// 이 시점엔 세션이 이미 떠 있어 재시작이 필요하지만, 7~9단계엔 공룡을 스폰하지 않아
	// 무해하다(spec §1 D-2). 정상 경로에선 이미 등록돼 있어 이 블록을 타지 않는다.
	if (bRegisterMarkerImages && !bMarkerImagesRegistered)
	{
		RegisterMarkerImages(/*bAllowSessionRestart=*/true);
	}

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

	if (bScanning && bMarkersReady && !bLocalized)
	{
		if (TryLocalizeFromTrackedImages())
		{
			bScanning = false;
			bLocalized = true;
			bLostReported = false;
			SecondsSinceMarkerSeen = 0.f;
			UE_LOG(LogNav, Log, TEXT("[Localizer] 측위 성립. 기준 마커=%s"), *AnchorMarkerCode);
			FieldLogEvent(TEXT("LOCALIZE"), FString(), AnchorMarkerCode,
				CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg, FString());
			OnLocalized.Broadcast(AnchorMarkerCode);
		}
	}

	if (!bLocalized)
	{
		return;
	}

	// 7단계 §A 앵커 전환. 걸어가다 다음 전시물 마커를 잡으면 그쪽으로 앵커를 옮겨
	// 드리프트를 씻는다. 매 프레임 훑으면 비싸므로 AnchorScanIntervalSeconds 로 throttle.
	SecondsSinceAnchorScan += DeltaTime;
	if (SecondsSinceAnchorScan >= AnchorScanIntervalSeconds)
	{
		SecondsSinceAnchorScan = 0.f;
		TryTransitionAnchor();   // 앵커가 바뀌면 아래 relatch 블록이 새 AnchorImage 로 돈다.
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
			FieldLogEvent(TEXT("LOST"), AnchorMarkerCode, FString(),
				CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg,
				FString::Printf(TEXT("gap=%.1f"), SecondsSinceMarkerSeen));
			OnLocalizationLost.Broadcast();
		}
	}

	UpdateCurrentPose();

	// 현장 로그: 주기적 위치 샘플(걸어간 경로·드리프트 추적). bFieldLogEnabled 일 때만.
	FieldLogSincePose += DeltaTime;
	if (bFieldLogEnabled && FieldLogSincePose >= FieldLogPoseIntervalSeconds)
	{
		FieldLogSincePose = 0.f;
		FieldLogEvent(TEXT("POSE"), AnchorMarkerCode, FString(),
			CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg,
			FString::Printf(TEXT("gap=%.1f"), SecondsSinceMarkerSeen));
	}

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

// ==================================================================== 마커 이미지 등록 (7단계 §A)
//
// 6-1a 프로브의 런타임 후보 등록을 정식으로 승격한 것이다(spec §A D-4). AR 세션이 켜지기
// "전"(Initialize)에 후보를 세션 설정 객체에 얹으므로 세션 재시작이 필요 없다(spec §1 D-4').

void UNavLocalizer::RegisterMarkerImages(bool bAllowSessionRestart)
{
	bMarkerImagesRegistered = true;   // 성공 경로에서 다시 안 타도록. 로드 실패 시 아래서 되돌린다.

	// ARTrackingManager 가 BP 프로퍼티로 쥔 것과 같은 세션 설정 인스턴스(같은 에셋 경로).
	// 여기에 후보를 얹으면 그쪽 StartARSession 에도 그대로 반영된다.
	UARSessionConfig* Cfg = LoadObject<UARSessionConfig>(nullptr, *MarkerSessionConfigPath);
	if (Cfg == nullptr)
	{
		bMarkerImagesRegistered = false;   // 에셋 로드 실패 → StartLocalizing 에서 재시도한다.
		UE_LOG(LogNav, Error, TEXT("[Markers] ARSessionConfig 로드 실패: %s"), *MarkerSessionConfigPath);
		return;
	}

	// F-1: 동시 추적 이미지 수를 올린다(기본 1이면 앵커 전환 불가). 공개 setter 가 없어
	// UPROPERTY 를 리플렉션으로 in-place 설정한다 — .uasset 은 디스크에 저장 안 함(공지 0건).
	if (FIntProperty* Prop = FindFProperty<FIntProperty>(
			UARSessionConfig::StaticClass(), TEXT("MaxNumSimultaneousImagesTracked")))
	{
		const int32 Cur = Prop->GetPropertyValue_InContainer(Cfg);
		if (Cur < MaxMarkerImagesTracked)
		{
			Prop->SetPropertyValue_InContainer(Cfg, MaxMarkerImagesTracked);
			UE_LOG(LogNav, Log, TEXT("[Markers] MaxNumSimultaneousImagesTracked %d → %d (앵커 전환용)"),
				Cur, MaxMarkerImagesTracked);
		}
	}
	else
	{
		UE_LOG(LogNav, Warning, TEXT("[Markers] MaxNumSimultaneousImagesTracked 프로퍼티를 못 찾았다 "
			"— 동시추적 1 이면 앵커 전환이 안 될 수 있다."));
	}

	// 등록 목록. ini 로 안 주면 과도기 기본 7장(구 2 + A2 신 5, spec §A 표)을 쓴다.
	TArray<FString> Entries = MarkerImageEntries;
	if (Entries.Num() == 0)
	{
		Entries = {
			TEXT("MARK-NEUTI4-START|/Game/UI/Nav/Markers/T_Marker_A1_Old.T_Marker_A1_Old"),
			TEXT("MARK-NEUTI4-START|/Game/UI/Nav/Markers/T_Marker_A1_A2.T_Marker_A1_A2"),
			TEXT("EX1-TRICERATOPS|/Game/UI/Nav/Markers/T_Marker_EX1_Old.T_Marker_EX1_Old"),
			TEXT("EX1-TRICERATOPS|/Game/UI/Nav/Markers/T_Marker_EX1_A2.T_Marker_EX1_A2"),
			TEXT("EX2-BRACHIO|/Game/UI/Nav/Markers/T_Marker_EX2.T_Marker_EX2"),
			TEXT("EX3-TREX|/Game/UI/Nav/Markers/T_Marker_EX3.T_Marker_EX3"),
			TEXT("EX4-ANKYLO|/Game/UI/Nav/Markers/T_Marker_EX4.T_Marker_EX4"),
		};
	}

	int32 Added = 0;
	for (const FString& Entry : Entries)
	{
		FString FriendlyName, TexPath;
		if (!ParseMarkerEntry(Entry, FriendlyName, TexPath))
		{
			UE_LOG(LogNav, Warning, TEXT("[Markers] 항목 형식 오류('이름|경로' 아님): %s"), *Entry);
			continue;
		}

		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexPath);
		if (Tex == nullptr)
		{
			UE_LOG(LogNav, Error, TEXT("[Markers] 텍스처 로드 실패: %s (%s) — H-1 임포트 확인"),
				*TexPath, *FriendlyName);
			continue;
		}

		// ⚠️ ARCore 는 무압축 PF_B8G8R8A8 / PF_G8 만 받는다. 텍스처 Compression 이 압축(DXT/ASTC)이면
		// nullptr 를 돌려주고 조용히 실패한다 → "VectorDisplacementmap (RGBA8)" 로 임포트해야 한다(§A H-1).
		UARCandidateImage* Cand = UARBlueprintLibrary::AddRuntimeCandidateImage(
			Cfg, Tex, FriendlyName, MarkerPhysicalWidthCm);
		if (Cand == nullptr)
		{
			// 같은 FriendlyName 을 두 번 넣을 때 UE 래퍼가 거부할 수 있다(spec §A "미검증").
			// 그러면 구 마커에만 -OLD 를 붙여 이름을 나누고 markers.csv 에 같은 좌표 행을 판다.
			UE_LOG(LogNav, Error, TEXT("[Markers] 후보 등록 거부(ARCore): %s ← %s. 텍스처 압축이거나 "
				"중복 이름이면 실패 — Compression=VectorDisplacementmap(RGBA8) 또는 이름 분리."),
				*FriendlyName, *TexPath);
			continue;
		}
		++Added;
		UE_LOG(LogNav, Log, TEXT("[Markers] 후보 등록: %s ← %s (폭 %.1f cm)"),
			*FriendlyName, *TexPath, MarkerPhysicalWidthCm);
	}

	// 정상 경로(Initialize): 세션이 아직 안 켜졌으므로 재시작 불필요.
	// 안전망 경로(StartLocalizing): 세션이 이미 떠 있어 후보 반영에 재시작이 필요하지만,
	// 7~9단계엔 공룡을 스폰하지 않아(spec §1 D-2) 무해하다.
	if (bAllowSessionRestart)
	{
		UARBlueprintLibrary::StopARSession();
		UARBlueprintLibrary::StartARSession(Cfg);
		UE_LOG(LogNav, Log, TEXT("[Markers] 후보 %d 개 등록 + AR 세션 재시작(안전망 경로)."), Added);
	}
	else
	{
		UE_LOG(LogNav, Log, TEXT("[Markers] 후보 %d 개 등록(세션 시작 전, 재시작 없음)."), Added);
	}
}

bool UNavLocalizer::ParseMarkerEntry(const FString& Entry, FString& OutName, FString& OutPath)
{
	if (!Entry.Split(TEXT("|"), &OutName, &OutPath))
	{
		return false;
	}
	OutName.TrimStartAndEndInline();
	OutPath.TrimStartAndEndInline();
	return !OutName.IsEmpty() && !OutPath.IsEmpty();
}

// ==================================================================== 앵커 전환 (7단계 §A)
//
// 6-1a 프로브의 GetAllGeometriesByClass 순회를 여기로 승격했다(spec §0). 측위 후에도
// 추적 이미지를 훑어, 걸어가다 다음 전시물 마커를 잡으면 그쪽으로 앵커를 옮겨 드리프트를 씻는다.

FString UNavLocalizer::DecideAnchorTransition(
	const FString& CurrentAnchorCode, bool bAnchorTracking,
	const TArray<FString>& TrackedKnownNow, const TSet<FString>& TrackedKnownLast)
{
	if (TrackedKnownNow.Num() == 0)
	{
		return FString();   // 볼 수 있는 known 마커가 없다 → 그대로 둔다(상실 판정은 별도).
	}

	// 앵커가 추적 불가(멀어져 놓침)면 지금 보이는 아무 known 마커로 재측위한다.
	// 현재와 다른 것을 우선하되, 없으면 같은 것으로라도 다시 잡는다.
	if (!bAnchorTracking)
	{
		for (const FString& Code : TrackedKnownNow)
		{
			if (Code != CurrentAnchorCode)
			{
				return Code;
			}
		}
		return TrackedKnownNow[0];
	}

	// 앵커가 살아 있으면 **이번에 새로 잡힌**(지난 스캔엔 없던) 다른 마커에만 옮긴다.
	// 전시물 도착 = 드리프트 보정 순간. 계속 보이던 마커로는 안 옮겨 요동(thrash)을 막는다.
	for (const FString& Code : TrackedKnownNow)
	{
		if (Code != CurrentAnchorCode && !TrackedKnownLast.Contains(Code))
		{
			return Code;
		}
	}
	return FString();
}

bool UNavLocalizer::TryTransitionAnchor()
{
	const TArray<UARTrackedGeometry*> Geometries =
		UARBlueprintLibrary::GetAllGeometriesByClass(UARTrackedImage::StaticClass());

	// 지금 추적 중인 known 마커 code 목록 + code→대표 이미지 1개.
	TArray<FString> TrackedKnownNow;
	TMap<FString, UARTrackedImage*> ImageByCode;
	for (UARTrackedGeometry* Geometry : Geometries)
	{
		UARTrackedImage* Image = Cast<UARTrackedImage>(Geometry);
		if (Image == nullptr || Image->GetTrackingState() != EARTrackingState::Tracking)
		{
			continue;
		}
		const UARCandidateImage* Candidate = Image->GetDetectedImage();
		if (Candidate == nullptr)
		{
			continue;
		}
		const FString Code = Candidate->GetFriendlyName();
		if (!KnownMarkers.Contains(Code))
		{
			continue;   // 서버가 모르는 마커(공룡용일 수 있음)는 앵커 후보가 아니다.
		}
		if (!ImageByCode.Contains(Code))
		{
			ImageByCode.Add(Code, Image);
			TrackedKnownNow.Add(Code);
		}
	}

	const bool bAnchorTracking =
		AnchorImage != nullptr && AnchorImage->GetTrackingState() == EARTrackingState::Tracking;

	const FString Switch = DecideAnchorTransition(
		AnchorMarkerCode, bAnchorTracking, TrackedKnownNow, TrackedMarkerCodesLastScan);

	// 다음 스캔의 ACQUIRE 에지 판정을 위해 이번 집합을 저장한다(전환 여부와 무관).
	TrackedMarkerCodesLastScan = TSet<FString>(TrackedKnownNow);

	if (Switch.IsEmpty() || Switch == AnchorMarkerCode)
	{
		return false;
	}

	UARTrackedImage* NewImage = ImageByCode.FindRef(Switch);
	const FNavMarker* Found = KnownMarkers.Find(Switch);
	if (NewImage == nullptr || Found == nullptr)
	{
		return false;
	}

	const FString Prev = AnchorMarkerCode;
	// 재보정 "전"의 드리프트된 추정 위치(직전 틱까지 데드레코닝된 값).
	const FVector2D DriftedXY(CurrentPose.PosXCm, CurrentPose.PosYCm);
	FieldLogEvent(TEXT("PRE_TRANSITION"), Prev, Switch,
		DriftedXY.X, DriftedXY.Y, CurrentPose.HeadingDeg, FString());

	AnchorImage = NewImage;
	AnchorMarkerCode = Switch;
	AnchorMarker = *Found;
	SolveTransform(NewImage->GetLocalToWorldTransform());
	SecondsSinceMarkerSeen = 0.f;
	bLostReported = false;

	// 재보정 "후"의 위치(새 마커 기준). 두 위치 차 = A→새마커 구간 누적 드리프트.
	float DriftCm = 0.f;
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const FVector CorrectedMap = WorldToMap(Cam->GetCameraLocation());
		DriftCm = FVector2D::Distance(DriftedXY, FVector2D(CorrectedMap.X, CorrectedMap.Y));
		FieldLogEvent(TEXT("TRANSITION"), Prev, Switch,
			CorrectedMap.X, CorrectedMap.Y, CurrentPose.HeadingDeg,
			FString::Printf(TEXT("drift_cm=%.0f"), DriftCm));
	}

	UE_LOG(LogNav, Log, TEXT("[Localizer] 앵커 전환: %s → %s. 드리프트 재보정 %.0fcm."),
		Prev.IsEmpty() ? TEXT("(없음)") : *Prev, *Switch, DriftCm);
	OnAnchorChanged.Broadcast(Switch);
	return true;
}

// ==================================================================== 현장 로그 (7단계 검증)
//
// A→F 앵커 전환·드리프트를 테더링 없이 검증하려고 이벤트를 CSV 로 모아 서버(/debug/probe-log)
// 로 보낸다(6-1a 프로브와 같은 경로). 서버가 없으면 폰 Saved/NavLog 에 남긴다. 검증 종료 시 제거.

void UNavLocalizer::FieldLogEvent(const FString& Event, const FString& FromCode, const FString& ToCode,
	float MapXCm, float MapYCm, float HeadingDeg, const FString& Extra)
{
	if (!bFieldLogEnabled)
	{
		return;
	}
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	FieldLogBuf.Add(FString::Printf(TEXT("%.2f,%s,%s,%s,%.0f,%.0f,%.1f,%s"),
		T, *Event, *FromCode, *ToCode, MapXCm, MapYCm, HeadingDeg, *Extra));
	FieldLogFlush(false);
}

void UNavLocalizer::FieldLogFlush(bool bFinal)
{
	if (FieldLogBuf.Num() == 0)
	{
		return;
	}
	if (!bFinal && FieldLogBuf.Num() < FieldLogUploadEveryLines)
	{
		return;
	}

	// 스냅샷을 떠서 버퍼를 즉시 비운다(비동기 응답 대기 중 중복/무한증가 방지).
	const FString Payload = FString::Join(FieldLogBuf, TEXT("\n")) + TEXT("\n");
	FieldLogBuf.Reset();

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UNavClient* Client = GI ? GI->GetSubsystem<UNavClient>() : nullptr;
	const FString Base = Client ? Client->GetServerBaseUrl() : FString();

	// 폴백: 서버 주소가 없거나 요청 실패 시 폰 Saved/NavLog 에 append. this 를 잡지 않는다.
	const FString Tag = FieldLogSessionTag;
	auto SaveFallback = [Payload, Tag]()
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("NavLog");
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		const FString File = Dir / FString::Printf(TEXT("navlog_%s.csv"), *Tag);
		FFileHelper::SaveStringToFile(Payload, *File,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	};

	if (Base.IsEmpty())
	{
		++FieldLogUploadFail;
		SaveFallback();
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/debug/probe-log?session=%s"), *Base, *FieldLogSessionTag);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("text/plain"));
	Request->SetContentAsString(Payload);

	TWeakObjectPtr<UNavLocalizer> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, SaveFallback](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
		{
			const bool bSuccess = bOk && Response.IsValid()
				&& Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300;
			if (UNavLocalizer* Self = WeakThis.Get())
			{
				if (bSuccess) { ++Self->FieldLogUploadOk; }
				else          { ++Self->FieldLogUploadFail; }
			}
			if (!bSuccess)
			{
				SaveFallback();
			}
		});
	Request->ProcessRequest();
}

