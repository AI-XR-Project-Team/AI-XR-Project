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
// 13-4 재측위 — 앱 복귀 감지(C) · 테스트 콘솔 명령
#include "Misc/CoreDelegates.h"
#include "HAL/IConsoleManager.h"

// LogNav 는 NavClient.h 에서 선언하고 NavClient.cpp 에서 정의한다. 여기서 따로
// DEFINE_LOG_CATEGORY_STATIC 을 두면 unity 빌드에서 중복 정의로 깨진다.

namespace
{
	/**
	 * 추적 품질 저하 이유(AR enum) → 사람이 읽는 안내 문구(5-B2 경고 배너용).
	 * 13-4 — 13단계엔 QR 이 없다. 행동 지시를 "주변을 천천히 둘러봐 주세요" 로 통일한다(명세 §3.4).
	 */
	FString QualityReasonText(EARTrackingQualityReason Reason)
	{
		switch (Reason)
		{
		case EARTrackingQualityReason::ExcessiveMotion:
			return TEXT("너무 빠르게 움직였습니다. 주변을 천천히 둘러봐 주세요");
		case EARTrackingQualityReason::InsufficientFeatures:
			return TEXT("주변이 밋밋해 추적이 어렵습니다. 주변을 천천히 둘러봐 주세요");
		case EARTrackingQualityReason::InsufficientLight:
			return TEXT("주변이 어둡습니다. 밝은 곳에서 주변을 천천히 둘러봐 주세요");
		case EARTrackingQualityReason::Relocalizing:
			return TEXT("위치를 다시 잡는 중입니다. 주변을 천천히 둘러봐 주세요");
		case EARTrackingQualityReason::Initializing:
			return TEXT("추적을 준비 중입니다. 주변을 천천히 둘러봐 주세요");
		default:
			return TEXT("위치가 흔들렸습니다. 주변을 천천히 둘러봐 주세요");
		}
	}

	// ── 13-4 재측위 상수(ini 로 뺄 만큼 현장 의존적이지 않은 것만) ──
	/** Recovered(✓ "측위가 잡혔습니다")를 보여 주는 시간(초). 오버레이는 이 시각에 페이드를 시작한다. */
	constexpr double kRelocRecoveredShowSeconds = 1.5;
	/** 판정 한 번에 쌓는 시간 상한(초). GC·앱 복귀 한 틱이 "지속" 문턱을 한 번에 넘기지 않게. */
	constexpr float kRelocMaxJudgeStepSeconds = 0.1f;
	/** 창 속도 계산 창(초). 창이 이 비율 이상 찼을 때만 속도를 믿는다(시작 직후 짧은 창의 과대 속도 방지). */
	constexpr double kRelocSpeedWindowSeconds = 0.5;
	constexpr double kRelocSpeedWindowMinFill = 0.8;
	/** REJECT 튜닝 로그 하한(cm) · 분당 줄 수 상한(명세 §3.6). */
	constexpr float kRelocRejectMinCm = 30.f;
	constexpr int32 kRelocRejectLinesPerMinute = 10;
	/** 카메라가 이보다 위·아래를 보면 yaw 가 뒤집힌다(짐벌) — 그 틱은 yaw 순간이동을 보지 않는다. */
	constexpr float kRelocYawMaxPitchDeg = 70.f;
	/** 경로 F 배너 문구(오버레이 아님). */
	const TCHAR* const kRelocSoftHintText = TEXT("주변을 천천히 둘러봐 주세요");

#if !UE_BUILD_SHIPPING
	/**
	 * 13-4 테스트용 — `nav.reloc enter <reason>` / `nav.reloc exit`. PIE 콘솔, 또는 폰에서
	 * `adb shell am broadcast -a android.intent.action.RUN -e cmd "nav.reloc enter shake"`(개발 빌드의 GameActivity 수신기).
	 */
	void HandleNavRelocConsole(const TArray<FString>& Args, UWorld* World)
	{
		UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
		if (Localizer == nullptr)
		{
			UE_LOG(LogNav, Warning, TEXT("[NavReloc] 콘솔: 이 월드에 NavLocalizer 가 없다"));
			return;
		}
		const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : FString();
		if (Verb == TEXT("enter"))
		{
			Localizer->RequestRelocalization(Args.Num() > 1 ? Args[1].ToLower() : FString(TEXT("jump")));
		}
		else if (Verb == TEXT("exit"))
		{
			Localizer->ForceRelocalizationRecovered(TEXT("console"));
		}
		else
		{
			UE_LOG(LogNav, Display,
				TEXT("[NavReloc] 사용법: nav.reloc enter <shake|occluded|dark|featureless|jump|resume> · nav.reloc exit"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GNavRelocConsoleCommand(
		TEXT("nav.reloc"),
		TEXT("13-4 재측위 오버레이 강제: nav.reloc enter <reason> | nav.reloc exit"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleNavRelocConsole));
#endif
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

	// 13-4 감지 C — 화면 꺼짐·전화·앱 전환에서 돌아오면 ARCore 월드 프레임이 이어진다는 보장이 없다.
	// 콜백에선 표시만 하고 판정은 Tick 에서 한다(측위 중일 때만 의미가 있다).
	ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(
		this, &UNavLocalizer::HandleAppForeground);

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
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
	ForegroundHandle.Reset();

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

bool UNavLocalizer::LocalizeFromCloudAnchor(const FString& SourceCode, const FTransform& AnchorWorld,
	const FNavMarker& AnchorMapPose)
{
	// 기준점을 클라우드 핀으로 바꾼다. 추적 이미지는 없으므로 AnchorImage 를 비운다 —
	// Tick 의 재래치 블록은 AnchorImage 가 null 이면 건너뛰고(latch 유지), LostAfterSeconds
	// 가 0(기본)이라 "마커를 못 봤다" 로 측위를 잃지도 않는다.
	AnchorImage = nullptr;
	bAnchorFromCloud = true;
	AnchorMarkerCode = SourceCode;
	AnchorMarker = AnchorMapPose;
	SolveTransform(AnchorWorld);

	const bool bWasLocalized = bLocalized;
	bLocalized = true;
	bLostReported = false;
	SecondsSinceMarkerSeen = 0.f;
	if (!bWasLocalized)
	{
		ResetRelocDetection();   // 13-4 — 지난 측위 때의 카메라 표본과 비교하지 않는다(첫 틱 가짜 순간이동 방지).
	}
	// 13-4 — 재측위 중이면 이 변환이 복구 후보다. 상태 전환은 최소 체류·품질 양호 뒤(MonitorRelocalization).
	NoteRelocalizationFix(SourceCode);
	const bool bRelocalizing = IsRelocalizing();
	UpdateCurrentPose();   // 재측위 중이면 안에서 건너뛴다(마지막 정상 pose 유지 — 명세 §3.3)

	if (!bRelocalizing)   // 복구 재래치는 0.2초마다 올 수 있다 — `[NavReloc] FIX`·`EXIT` 줄이 대신한다.
	{
		UE_LOG(LogNav, Log,
			TEXT("[Localizer] Cloud Anchor 측위 %s. 기준=%s 맵(%.0f, %.0f) heading=%.1f° → 현재 위치 (%.0f, %.0f)"),
			bWasLocalized ? TEXT("기준점 전환") : TEXT("성립"), *SourceCode,
			AnchorMapPose.PosXCm, AnchorMapPose.PosYCm, AnchorMapPose.HeadingDeg,
			CurrentPose.PosXCm, CurrentPose.PosYCm);
	}

	if (bWasLocalized)
	{
		if (!bRelocalizing)   // 재측위 중엔 화면을 멈춰 둔다 — 복구는 OnRelocalized 가 알린다.
		{
			OnAnchorChanged.Broadcast(SourceCode);
		}
		return false;
	}

	FieldLogEvent(TEXT("LOCALIZE"), FString(), SourceCode,
		CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg, TEXT("src=cloud_anchor"));
	OnLocalized.Broadcast(SourceCode);
	return true;
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
	bAnchorFromCloud = false;
	MapToWorldXf = FTransform::Identity;
	CurrentPose = FNavMapPose();
	// 품질 감지 상태도 함께 리셋한다. 재탐색 중에는 경고를 띄우지 않는다.
	SecondsPoorQuality = 0.f;
	SecondsGoodQuality = 0.f;
	bTrackingDegraded = false;

	// 13-4 — 재측위 상태도 버린다. 떠 있던 오버레이는 서브시스템이 상태를 보고 ✓ 없이 조용히 내린다.
	if (LocState != ENavLocState::Localized)
	{
		UE_LOG(LogNav, Log, TEXT("[NavReloc] RESET state=%d — 측위 초기화로 재측위 종료(복구 아님)"),
			static_cast<int32>(LocState));
	}
	LocState = ENavLocState::Localized;
	RelocReason.Reset();
	PendingRecoverySource.Reset();
	ResetRelocDetection();
	bPendingResume = false;
	SoftHintSeconds = 0.f;
	bSoftHintShown = false;
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
			ResetRelocDetection();   // 13-4 — 지난 측위 때의 카메라 표본과 비교하지 않는다.
			NoteRelocalizationFix(AnchorMarkerCode);
			UE_LOG(LogNav, Log, TEXT("[Localizer] 측위 성립. 기준 마커=%s"), *AnchorMarkerCode);
			FieldLogEvent(TEXT("LOCALIZE"), FString(), AnchorMarkerCode,
				CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg, FString());
			OnLocalized.Broadcast(AnchorMarkerCode);
		}
	}

	if (!bLocalized)
	{
		// 13-4 — 측위 전이어도 재측위 상태기계(콘솔 강제 진입 등)는 끝까지 굴린다.
		if (LocState != ENavLocState::Localized)
		{
			MonitorRelocalization(DeltaTime);
		}
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
		// 13-4 — 재측위 중이면 계속 보이던 마커로도 다시 세운다(리졸버 ⑥ 과 같은 규칙). 품질이 안정된 뒤에만.
		const bool bRelocRelatch = IsRelocalizing() && RelocGoodSeconds >= RelocPinStableSeconds;
		if (bContinuousRelatch || bReacquired || bRelocRelatch)
		{
			SolveTransform(AnchorImage->GetLocalToWorldTransform());
			if (bReacquired)
			{
				UE_LOG(LogNav, Log, TEXT("[Localizer] 마커 재관측(%.1f초 만). 드리프트 보정."),
					SecondsSinceMarkerSeen);
			}
			if (bReacquired || bRelocRelatch)
			{
				NoteRelocalizationFix(AnchorMarkerCode);
			}
		}

		SecondsSinceMarkerSeen = 0.f;
		bLostReported = false;
		NoteAnchorObserved();   // 13-4 경로 F — 기준 마커가 보인다
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

	// 13-4 D50 — 감지는 위치 갱신 **앞에서** 한다. 재측위 중이면 UpdateCurrentPose 가 갱신·방송을 건너뛴다.
	MonitorRelocalization(DeltaTime);
	UpdateCurrentPose();

	// 13-4 경로 F — 앵커를 오래 가까이서 못 만나면 배너 한 줄(오버레이 아님). 틀렸다는 증거는 아니라서
	// IsTrackingDegraded 는 건드리지 않는다(바닥 그래픽·리라우트는 그대로 돈다).
	if (LocState == ENavLocState::Localized && RelocSoftHintSeconds > 0.f)
	{
		SoftHintSeconds += DeltaTime;
		if (!bSoftHintShown && SoftHintSeconds >= RelocSoftHintSeconds)
		{
			bSoftHintShown = true;
			UE_LOG(LogNav, Log, TEXT("[NavReloc] HINT %.0f초 동안 가까운 앵커 없음 — 배너만(오버레이 아님)"),
				SoftHintSeconds);
			OnTrackingDegraded.Broadcast(kRelocSoftHintText);
		}
	}

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
		bAnchorFromCloud = false;   // 기준점은 마커다(앵커 측위로 서 있었더라도 마커가 이긴다).
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
	// 인쇄물 축 보정각은 QR 마커에만 해당한다(앵커는 인쇄물이 아니다).
	const float HeadingOffsetDeg = bAnchorFromCloud ? 0.f : MarkerHeadingOffsetDeg;
	const float MapHeadingDeg = -(AnchorMarker.HeadingDeg + HeadingOffsetDeg);
	const float YawOffsetDeg = FRotator::NormalizeAxis(MarkerWorld.Rotator().Yaw - MapHeadingDeg);

	const FRotator Rot(0.f, YawOffsetDeg, 0.f);
	const FVector MarkerMap(AnchorMarker.PosXCm, -AnchorMarker.PosYCm, AnchorMarker.PosZCm);

	// 회전만 걸면 마커가 원점 근처에 놓인다. 실제 마커 자리로 밀어 준다.
	const FVector Translation = MarkerWorld.GetLocation() - Rot.RotateVector(MarkerMap);

	MapToWorldXf = FTransform(Rot, Translation, FVector::OneVector);

	// 13-4 §3.7 함정 1 — 이번 판정 구간에 재래치가 있었다. 순간이동(B) 비교에서 뺀다.
	bSolvedSinceMonitor = true;
}

void UNavLocalizer::ApplyCloudBlendTransform(const FTransform& NewMapToWorld)
{
	// 13-3 D74 — 앵커 측위의 정상 상태에서만. 재측위(Relocalizing)·복구 표시(Recovered) 중엔 13-4 흐름이 변환을 쥔다.
	if (!bLocalized || !bAnchorFromCloud || LocState != ENavLocState::Localized)
	{
		return;
	}
	MapToWorldXf = NewMapToWorld;
}

void UNavLocalizer::UpdateCurrentPose()
{
	// 13-4 — 재측위 중엔 위치를 갱신·방송하지 않는다(마지막 정상 pose 유지 → 미니맵이 저절로 멈춘다).
	if (LocState == ENavLocState::Relocalizing)
	{
		return;
	}

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

// ==================================================================== 13-4 재측위 (D50~D53)
//
// 측위가 틀어질 정도면 안내를 멈추고 "가만히 서서 천천히 스캔" 을 시킨다(오버레이는 NavRelocalizeOverlaySubsystem).
// 앵커 하나가 다시 안정되게 잡히면 리졸버가 그 앵커로 강제 재래치하고(RelatchForRelocalization), 여기서
// 최소 체류·품질을 확인한 뒤 "측위가 잡혔습니다" 로 넘어간다.

bool UNavLocalizer::IsRelocBadReason(EARTrackingQualityReason Reason)
{
	return Reason == EARTrackingQualityReason::ExcessiveMotion
		|| Reason == EARTrackingQualityReason::InsufficientFeatures
		|| Reason == EARTrackingQualityReason::InsufficientLight;
}

FNavRelocVerdict UNavLocalizer::JudgeRelocEntry(const FNavRelocSample& Sample, const FNavRelocParams& Params,
	FNavRelocAccum& Accum)
{
	FNavRelocVerdict Verdict;

	// 한 틱 끊김(GC·앱 복귀)이 "지속" 문턱을 한 번에 넘기지 않게 누적 간격을 자른다.
	const float Dt = FMath::Clamp(Sample.DeltaTime, 0.f, kRelocMaxJudgeStepSeconds);
	Accum.Clock += Dt;
	const bool bCooldown = Accum.CooldownSeconds > 0.f;
	Accum.CooldownSeconds = FMath::Max(0.f, Accum.CooldownSeconds - Dt);

	// ── A. 센서·시야 ──
	const bool bNotTracking = Sample.Quality == EARTrackingQuality::NotTracking;
	const bool bBadReason = IsRelocBadReason(Sample.Reason);
	if (bNotTracking || bBadReason)
	{
		Accum.GoodSeconds = 0.f;
		Accum.BadSeconds += Dt;
		if (bNotTracking)
		{
			Accum.NotTrackingSeconds += Dt;
		}
		switch (Sample.Reason)
		{
		case EARTrackingQualityReason::ExcessiveMotion:      Accum.ShakeSeconds += Dt; break;
		case EARTrackingQualityReason::InsufficientLight:    Accum.DarkSeconds += Dt; break;
		case EARTrackingQualityReason::InsufficientFeatures: Accum.FeaturelessSeconds += Dt; break;
		default:                                             Accum.OccludedSeconds += Dt; break;   // 이유 없는 NotTracking
		}
	}
	else
	{
		Accum.GoodSeconds += Dt;
		if (Accum.GoodSeconds >= Params.GoodResetSeconds)
		{
			Accum.NotTrackingSeconds = 0.f;
			Accum.BadSeconds = 0.f;
			Accum.OccludedSeconds = Accum.ShakeSeconds = Accum.DarkSeconds = Accum.FeaturelessSeconds = 0.f;
		}
	}

	const bool bEnterLost = Params.NotTrackingSeconds > 0.f && Accum.NotTrackingSeconds >= Params.NotTrackingSeconds;
	const bool bEnterHold = !bCooldown && Params.ReasonHoldSeconds > 0.f && Accum.BadSeconds >= Params.ReasonHoldSeconds;
	if (bEnterLost || bEnterHold)
	{
		// 이유 = 이번 "나쁨" 구간에서 가장 오래 쌓인 것. 같으면 occluded → shake → dark → featureless 순.
		struct FRelocLabel { float Seconds; const TCHAR* Code; };
		const FRelocLabel Labels[] = {
			{ Accum.OccludedSeconds, TEXT("occluded") }, { Accum.ShakeSeconds, TEXT("shake") },
			{ Accum.DarkSeconds, TEXT("dark") }, { Accum.FeaturelessSeconds, TEXT("featureless") } };
		const FRelocLabel* Best = &Labels[0];
		for (const FRelocLabel& Label : Labels)
		{
			if (Label.Seconds > Best->Seconds)
			{
				Best = &Label;
			}
		}
		Verdict.Reason = Best->Code;
		Accum.NotTrackingSeconds = Accum.BadSeconds = Accum.GoodSeconds = 0.f;
		Accum.OccludedSeconds = Accum.ShakeSeconds = Accum.DarkSeconds = Accum.FeaturelessSeconds = 0.f;
	}

	// ── B. 좌표 순간이동 — 카메라 **월드** pose 의 틱 간 변화 ──
	if (!Sample.bHasCamera)
	{
		Accum.bHasPrev = false;
		Accum.Window.Reset();
		return Verdict;
	}

	if (Sample.bSolvedThisTick)
	{
		Accum.Window.Reset();   // 재래치 틱 — 비교하지 않고 창도 새로 연다(재보정이 순간이동으로 잡히지 않게)
	}
	else if (Accum.bHasPrev && Params.JumpCm > 0.f)
	{
		while (Accum.Window.Num() > 0 && Accum.Clock - Accum.Window[0].Key > kRelocSpeedWindowSeconds + KINDA_SMALL_NUMBER)
		{
			Accum.Window.RemoveAt(0, 1, EAllowShrinking::No);
		}

		const float DeltaCm = static_cast<float>(FVector::Dist(Sample.CameraWorld, Accum.PrevCameraWorld));
		// 위·아래를 보면 yaw 가 뒤집힌다(짐벌) — 그때는 yaw 를 보지 않는다.
		const bool bYawUsable = FMath::Abs(Sample.CameraPitchDeg) <= kRelocYawMaxPitchDeg
			&& FMath::Abs(Accum.PrevPitchDeg) <= kRelocYawMaxPitchDeg;
		const float DeltaYaw = bYawUsable
			? FMath::Abs(FRotator::NormalizeAxis(Sample.CameraYawDeg - Accum.PrevYawDeg)) : 0.f;
		// 창 속도 — 창이 충분히 찼을 때만(시작 직후 짧은 창은 한 걸음도 과속으로 보인다).
		float SpeedMps = 0.f;
		if (Accum.Window.Num() > 0)
		{
			const double Span = Accum.Clock - Accum.Window[0].Key;
			if (Span >= kRelocSpeedWindowSeconds * kRelocSpeedWindowMinFill)
			{
				SpeedMps = static_cast<float>(FVector::Dist(Sample.CameraWorld, Accum.Window[0].Value) / 100.0 / Span);
			}
		}
		Verdict.JumpCm = DeltaCm;
		Verdict.JumpDeg = DeltaYaw;
		Verdict.SpeedMps = SpeedMps;

		const bool bJump = DeltaCm >= Params.JumpCm
			|| (Params.JumpDeg > 0.f && DeltaYaw >= Params.JumpDeg)
			|| (Params.SpeedMps > 0.f && SpeedMps >= Params.SpeedMps);
		if (bJump)
		{
			if (bCooldown)
			{
				Verdict.bCooldownBlocked = true;   // 복구 직후 — 튜닝 로그로만 남긴다
				Verdict.bNearMiss = true;
			}
			else if (Verdict.Reason.IsEmpty())
			{
				Verdict.Reason = TEXT("jump");
				Accum.Window.Reset();
			}
		}
		else
		{
			Verdict.bNearMiss = DeltaCm >= kRelocRejectMinCm
				|| (Params.JumpDeg > 0.f && DeltaYaw >= Params.JumpDeg * 0.5f)
				|| (Params.SpeedMps > 0.f && SpeedMps >= Params.SpeedMps * 0.5f);
		}
	}

	Accum.Window.Add(TPair<double, FVector>(Accum.Clock, Sample.CameraWorld));
	Accum.PrevCameraWorld = Sample.CameraWorld;
	Accum.PrevYawDeg = Sample.CameraYawDeg;
	Accum.PrevPitchDeg = Sample.CameraPitchDeg;
	Accum.bHasPrev = true;
	return Verdict;
}

bool UNavLocalizer::JudgeRelocExit(float StaySeconds, float GoodQualitySeconds, bool bHasFix,
	float MinStaySeconds, float InQualityRecoverSeconds)
{
	return bHasFix && StaySeconds >= MinStaySeconds && GoodQualitySeconds >= InQualityRecoverSeconds;
}

int32 UNavLocalizer::PickRelocCandidate(const TArray<FNavRelocCandidate>& Candidates, float PinStableSeconds)
{
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (Candidates[i].StableSeconds >= PinStableSeconds
			&& (Best == INDEX_NONE || Candidates[i].DistanceCm < Candidates[Best].DistanceCm))
		{
			Best = i;
		}
	}
	return Best;
}

FNavRelocParams UNavLocalizer::MakeRelocParams() const
{
	FNavRelocParams Params;
	Params.NotTrackingSeconds = RelocNotTrackingSeconds;
	Params.ReasonHoldSeconds = RelocReasonHoldSeconds;
	Params.JumpCm = RelocJumpCm;
	Params.JumpDeg = RelocJumpDeg;
	Params.SpeedMps = RelocSpeedMps;
	Params.GoodResetSeconds = QualityRecoverSeconds;   // "좋은 프레임 0.5초 연속이면 0" — 5-B 회복 기준과 같은 값
	return Params;
}

void UNavLocalizer::MonitorRelocalization(float DeltaTime)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	FlushRelocRejects(Now);

	const EARTrackingQuality Quality = UARBlueprintLibrary::GetTrackingQuality();
	const EARTrackingQualityReason Reason = UARBlueprintLibrary::GetTrackingQualityReason();
	const bool bGood = Quality == EARTrackingQuality::OrientationAndPosition && !IsRelocBadReason(Reason);
	// 복구 조건 ① — 상태와 무관하게 센다(리졸버가 복구 후보 핀의 안정 판정에 쓴다).
	RelocGoodSeconds = bGood ? RelocGoodSeconds + DeltaTime : 0.f;

	const bool bSolved = bSolvedSinceMonitor;
	bSolvedSinceMonitor = false;

	if (LocState == ENavLocState::Relocalizing)
	{
		bPendingResume = false;   // 이번 복구가 앱 복귀까지 덮는다
		if (!bGood)
		{
			// 틀어진 채로 받은 변환은 믿지 않는다 — 양호해진 뒤 리졸버가 안정된 핀으로 다시 세운다.
			PendingRecoverySource.Reset();
		}
		else if (JudgeRelocExit(static_cast<float>(Now - RelocEnterTime), RelocGoodSeconds,
			!PendingRecoverySource.IsEmpty(), RelocMinStaySeconds, QualityRecoverSeconds))
		{
			EnterRecovered(PendingRecoverySource);
		}
		return;
	}

	if (LocState == ENavLocState::Recovered)
	{
		if (Now - RecoveredTime >= kRelocRecoveredShowSeconds)
		{
			LocState = ENavLocState::Localized;
			ResetRelocDetection();   // 재측위 전 카메라 표본과 비교하지 않는다
			RelocAccum.CooldownSeconds = RelocCooldownSeconds;
			UE_LOG(LogNav, Log, TEXT("[NavReloc] LOCALIZED cooldown=%.1fs"), RelocCooldownSeconds);
		}
		return;
	}

	if (!bLocalized)
	{
		return;   // 측위 전엔 감지하지 않는다
	}

	// C. 앱 복귀 — 월드 프레임이 이어진다는 보장이 없다.
	if (bPendingResume)
	{
		bPendingResume = false;
		if (bRelocOnResume)
		{
			EntryDetail = FNavRelocVerdict();
			RequestRelocalization(TEXT("resume"));
			return;
		}
		ResetRelocDetection();   // 조용 모드 — 멈춘 사이의 이동을 순간이동으로 세지 않는다
	}

	// B 확정 — 지난 틱에 잡은 순간이동은 그 사이 재래치가 없었을 때만 진입한다(리졸버 틱 순서와 무관).
	if (bPendingJump)
	{
		bPendingJump = false;
		if (!bSolved)
		{
			EntryDetail = PendingJumpVerdict;
			RequestRelocalization(TEXT("jump"));
			return;
		}
		UE_LOG(LogNav, Log, TEXT("[NavReloc] SKIP jump dx=%.0fcm — 같은 순간 재래치(재보정)가 왔다"),
			PendingJumpVerdict.JumpCm);
	}

	FNavRelocSample Sample;
	Sample.Quality = Quality;
	Sample.Reason = Reason;
	Sample.DeltaTime = DeltaTime;
	Sample.bSolvedThisTick = bSolved;
	if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		Sample.CameraWorld = Cam->GetCameraLocation();
		const FRotator CamRot = Cam->GetCameraRotation();
		Sample.CameraYawDeg = static_cast<float>(CamRot.Yaw);
		Sample.CameraPitchDeg = static_cast<float>(CamRot.Pitch);
	}
	else
	{
		Sample.bHasCamera = false;
	}

	const FNavRelocVerdict Verdict = JudgeRelocEntry(Sample, MakeRelocParams(), RelocAccum);
	if (Verdict.bNearMiss)
	{
		LogRelocReject(Verdict);
	}
	if (Verdict.Reason == TEXT("jump"))
	{
		PendingJumpVerdict = Verdict;
		bPendingJump = true;
	}
	else if (!Verdict.Reason.IsEmpty())
	{
		EntryDetail = FNavRelocVerdict();
		RequestRelocalization(Verdict.Reason);
	}
}

void UNavLocalizer::RequestRelocalization(const FString& Reason)
{
	if (LocState == ENavLocState::Relocalizing)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const EARTrackingQuality Quality = UARBlueprintLibrary::GetTrackingQuality();
	const EARTrackingQualityReason ArReason = UARBlueprintLibrary::GetTrackingQualityReason();

	LocState = ENavLocState::Relocalizing;
	RelocReason = Reason.IsEmpty() ? FString(TEXT("jump")) : Reason;
	RelocEnterTime = Now;
	PendingRecoverySource.Reset();
	bPendingJump = false;
	bPendingResume = false;

	// 5-B 배너·경로 F 힌트가 떠 있으면 내린다 — 같은 안내를 오버레이가 화면 가운데서 한다.
	// IsTrackingDegraded() 는 재측위 동안 LocState 로 true 를 유지하므로 바닥 그래픽·리라우트 게이트는 그대로다.
	if (bTrackingDegraded || bSoftHintShown)
	{
		bTrackingDegraded = false;
		bSoftHintShown = false;
		SecondsPoorQuality = 0.f;
		SecondsGoodQuality = 0.f;
		OnTrackingRecovered.Broadcast();
	}
	SoftHintSeconds = 0.f;

	UE_LOG(LogNav, Warning, TEXT("[NavReloc] ENTER reason=%s dx=%.0fcm dyaw=%.0f° v=%.1fm/s q=%d r=%d t=%.1f"),
		*RelocReason, EntryDetail.JumpCm, EntryDetail.JumpDeg, EntryDetail.SpeedMps,
		static_cast<int32>(Quality), static_cast<int32>(ArReason), Now);
	FieldLogEvent(TEXT("RELOC_ENTER"), AnchorMarkerCode, FString(),
		CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg,
		FString::Printf(TEXT("reason=%s dx=%.0f dyaw=%.0f"), *RelocReason, EntryDetail.JumpCm, EntryDetail.JumpDeg));
	EntryDetail = FNavRelocVerdict();
	OnRelocalizationStarted.Broadcast(RelocReason);
}

void UNavLocalizer::ForceRelocalizationRecovered(const FString& SourceCode)
{
	if (LocState != ENavLocState::Relocalizing)
	{
		UE_LOG(LogNav, Log, TEXT("[NavReloc] exit 무시 — 재측위 중이 아니다(state=%d)"), static_cast<int32>(LocState));
		return;
	}
	EnterRecovered(SourceCode);
}

void UNavLocalizer::EnterRecovered(const FString& SourceCode)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float StaySeconds = static_cast<float>(Now - RelocEnterTime);

	// drift = 멈춰 둔 마지막 정상 위치 ↔ 새 변환으로 본 지금 위치(측위 전이면 -1).
	float DriftCm = -1.f;
	const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (bLocalized && Cam != nullptr)
	{
		const FVector NowMap = WorldToMap(Cam->GetCameraLocation());
		DriftCm = static_cast<float>(FVector2D::Distance(
			FVector2D(CurrentPose.PosXCm, CurrentPose.PosYCm), FVector2D(NowMap.X, NowMap.Y)));
	}

	LocState = ENavLocState::Recovered;
	RecoveredTime = Now;
	PendingRecoverySource.Reset();

	UE_LOG(LogNav, Log, TEXT("[NavReloc] EXIT via=%s stay=%.1fs drift=%.0fcm reason=%s"),
		*SourceCode, StaySeconds, DriftCm, *RelocReason);
	FieldLogEvent(TEXT("RELOC_EXIT"), RelocReason, SourceCode,
		CurrentPose.PosXCm, CurrentPose.PosYCm, CurrentPose.HeadingDeg,
		FString::Printf(TEXT("stay=%.1f drift=%.0f"), StaySeconds, DriftCm));

	if (bLocalized)
	{
		UpdateCurrentPose();   // 복구 직후 첫 방송 — 새 위치로 점프한다(의도, 보간하지 않는다 — 명세 §3.3)
	}
	OnRelocalized.Broadcast(SourceCode);
}

void UNavLocalizer::NoteRelocalizationFix(const FString& SourceCode)
{
	NoteAnchorObserved();
	if (LocState != ENavLocState::Relocalizing)
	{
		return;
	}
	if (PendingRecoverySource.IsEmpty())
	{
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		UE_LOG(LogNav, Log, TEXT("[NavReloc] FIX via=%s stay=%.1fs good=%.1fs — 최소 체류·품질 양호 뒤 복구"),
			*SourceCode, Now - RelocEnterTime, RelocGoodSeconds);
	}
	PendingRecoverySource = SourceCode;
}

void UNavLocalizer::NoteAnchorObserved()
{
	SoftHintSeconds = 0.f;
	if (bSoftHintShown)
	{
		bSoftHintShown = false;
		UE_LOG(LogNav, Log, TEXT("[NavReloc] HINT off — 앵커를 다시 만났다"));
		if (!bTrackingDegraded)
		{
			OnTrackingRecovered.Broadcast();   // 5-B 저하 배너가 따로 떠 있으면 그건 5-B 회복이 내린다
		}
	}
}

void UNavLocalizer::ResetRelocDetection()
{
	RelocAccum = FNavRelocAccum();
	bPendingJump = false;
	bSolvedSinceMonitor = false;
}

float UNavLocalizer::GetRelocElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	if (LocState != ENavLocState::Relocalizing || World == nullptr)
	{
		return 0.f;
	}
	return static_cast<float>(World->GetTimeSeconds() - RelocEnterTime);
}

void UNavLocalizer::HandleAppForeground()
{
	if (!bLocalized && LocState == ENavLocState::Localized)
	{
		return;   // 측위 전이면 복귀해도 틀어질 것이 없다
	}
	bPendingResume = true;
	UE_LOG(LogNav, Log, TEXT("[NavReloc] RESUME 앱 복귀 — %s"),
		bRelocOnResume ? TEXT("재측위 예정") : TEXT("조용 모드(감지 표본만 초기화)"));
}

void UNavLocalizer::LogRelocReject(const FNavRelocVerdict& Verdict)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	FlushRelocRejects(Now);
	if (RejectWindowStart < 0.0)
	{
		RejectWindowStart = Now;
	}
	if (RejectLinesInWindow < kRelocRejectLinesPerMinute)
	{
		++RejectLinesInWindow;
		UE_LOG(LogNav, Log, TEXT("[NavReloc] REJECT reason=jump dx=%.0fcm dyaw=%.0f° v=%.1fm/s%s"),
			Verdict.JumpCm, Verdict.JumpDeg, Verdict.SpeedMps, Verdict.bCooldownBlocked ? TEXT(" cooldown") : TEXT(""));
		return;
	}
	// 분당 상한을 넘은 줄은 최대값만 모아 둔다 — T4 여유폭 채점에 필요한 건 최대값이다.
	++RejectSuppressed;
	RejectSuppressedMax.JumpCm = FMath::Max(RejectSuppressedMax.JumpCm, Verdict.JumpCm);
	RejectSuppressedMax.JumpDeg = FMath::Max(RejectSuppressedMax.JumpDeg, Verdict.JumpDeg);
	RejectSuppressedMax.SpeedMps = FMath::Max(RejectSuppressedMax.SpeedMps, Verdict.SpeedMps);
}

void UNavLocalizer::FlushRelocRejects(double Now)
{
	if (RejectWindowStart < 0.0 || Now - RejectWindowStart < 60.0)
	{
		return;
	}
	if (RejectSuppressed > 0)
	{
		UE_LOG(LogNav, Log, TEXT("[NavReloc] REJECT_MAX dx=%.0fcm dyaw=%.0f° v=%.1fm/s suppressed=%d"),
			RejectSuppressedMax.JumpCm, RejectSuppressedMax.JumpDeg, RejectSuppressedMax.SpeedMps, RejectSuppressed);
	}
	RejectWindowStart = -1.0;
	RejectLinesInWindow = 0;
	RejectSuppressed = 0;
	RejectSuppressedMax = FNavRelocVerdict();
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
	bAnchorFromCloud = false;   // 마커로 옮겨 왔다.
	SolveTransform(NewImage->GetLocalToWorldTransform());
	SecondsSinceMarkerSeen = 0.f;
	bLostReported = false;
	NoteRelocalizationFix(Switch);   // 13-4 — 재측위 중이면 마커 전환도 복구 후보다(명세 §3.3)

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
	if (!IsRelocalizing())   // 재측위 중엔 화면을 멈춰 둔다 — 복구는 OnRelocalized 가 알린다
	{
		OnAnchorChanged.Broadcast(Switch);
	}
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

