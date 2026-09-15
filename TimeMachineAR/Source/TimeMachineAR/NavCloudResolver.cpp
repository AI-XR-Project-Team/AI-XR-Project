// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavCloudResolver.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

// ARCore Cloud Anchors 는 Android 전용이라 GoogleARCoreServices 의존도 Android 타깃에만 걸린다
// (TimeMachineAR.Build.cs). NAV_CLOUD_RESOLVE 는 그 조건과 1:1 로 붙어 있는 정의다. Mac 에디터
// 타깃에선 아래 기능 코드가 전부 빠지고 클래스 껍데기만 남는다(ShouldCreateSubsystem=false).
#if NAV_CLOUD_RESOLVE
#include "NavCloudResolveHud.h"
#include "NavClient.h"                                   // ServerBaseUrl
#include "NavLocalizer.h"                                // 앵커로 측위 세우기
#include "NavTypes.h"                                    // FNavMarker
#include "ARBlueprintLibrary.h"
#include "ARPin.h"
#include "ARTypes.h"
#include "GoogleARCoreServicesFunctionLibrary.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNavCloudResolve, Log, All);

#if NAV_CLOUD_RESOLVE
// 13-3 D36 — 측량 로그 전용 카테고리. 줄 앞의 `[NavCloudSurvey]` 가 §G 파서의 기준이다.
DEFINE_LOG_CATEGORY_STATIC(LogNavCloudSurvey, Log, All);

namespace
{
	/** 한 시도를 포기하고 다시 요청하기까지의 시간(초). 런북 §E-2. */
	constexpr double kAttemptTimeoutSeconds = 12.0;
	/** 목록 조회 실패 시 재시도 간격(초). */
	constexpr double kFetchRetrySeconds = 10.0;
	/**
	 * 핀을 이만큼(초) 놓쳤다가 다시 잡으면 "재관측" 으로 보고 변환을 다시 세운다.
	 * 마커 측위의 `UNavLocalizer::ReacquireGapSeconds` 기본값과 맞췄다 — 짧게 잡으면
	 * 화면 가장자리에서 깜빡일 때마다 재래치가 걸려 결국 매 틱 재계산이 된다.
	 */
	constexpr double kReacquireGapSeconds = 1.5;
	/** 이만큼(cm) 이상 밀려 있던 것을 씻어냈을 때만 재보정 토스트를 띄운다(잔소리 방지). */
	constexpr float kRelatchToastCm = 30.f;
	/** 서버 목록 재조회 주기(초). 새로 등록된 앵커·바뀐 heading 을 앱 재시작 없이 받는다. */
	constexpr double kRefreshSeconds = 60.0;

	/** D43 — 재시도 간격의 시작값(초). 12단계의 고정 재시도 간격과 같다. 12 → 24 → 48 → 상한. */
	constexpr double kBackoffBaseSeconds = 12.0;
	/** D44 — 먼 시도를 양보시키려면 그 시도가 최소 이만큼(초)은 돌았어야 한다(막 건 요청을 끊지 않게). */
	constexpr double kMinAttemptBeforePreemptSeconds = 3.0;
	/** RESOURCE_EXHAUSTED 를 받으면 신규 요청 전체를 이만큼(초) 멈춘다(구글 한도 — 명세 §3.6). */
	constexpr double kResourceExhaustedHoldSeconds = 30.0;
	/** 근접 앵커에 허용하는 분당 요청 상한. 일반 상한(ini)이 이보다 작을 때만 의미가 있다. §4 기준 60/분. */
	constexpr int32 kNearRequestsPerMinuteCeiling = 60;
	/** 스케줄러·기준앵커 판정 주기(초). 매 프레임 24×24 거리 계산을 할 필요가 없다. */
	constexpr double kSchedulerIntervalSeconds = 0.2;

	/** D46 합의 — 다른 후보의 점프 벡터가 이 거리(cm) 안이면 "같은 자리를 함의" 로 본다. */
	constexpr double kJumpConsensusCm = 100.0;
	/** D46 — 거절 기록이 합의 판정에 쓰이는 유효 시간(초). */
	constexpr double kJumpRecordWindowSeconds = 10.0;
	/** D46 지속 — 점프 벡터가 이 거리(cm) 안에서 이만큼(초) 유지되면 지금 변환이 틀린 것으로 본다. */
	constexpr double kJumpStableCm = 50.0;
	constexpr double kJumpStableSeconds = 15.0;
	/** 거절 로그 스로틀(초). */
	constexpr double kJumpLogIntervalSeconds = 5.0;
	/** 스케줄러 상태 요약 주기(초). */
	constexpr double kSummaryIntervalSeconds = 30.0;

	/**
	 * 서버는 Decimal 필드를 **문자열**로 직렬화한다(`"pos_x_cm": "2280.00"`). 숫자로 와도
	 * 받도록 둘 다 처리한다 — 여기서 0 으로 떨어지면 측위가 원점으로 튄다.
	 */
	float ReadNumberField(const TSharedPtr<FJsonObject>& Obj, const FString& Field, float Default)
	{
		double Number = 0.0;
		if (Obj->TryGetNumberField(Field, Number))
		{
			return static_cast<float>(Number);
		}
		FString Text;
		if (Obj->TryGetStringField(Field, Text) && !Text.IsEmpty())
		{
			return FCString::Atof(*Text);
		}
		return Default;
	}

	/** 서버가 아는 이 앵커의 맵 좌표를 측위 기준점(FNavMarker) 형태로 만든다. */
	FNavMarker MakeAnchorPose(const FNavCloudResolveEntry& E)
	{
		FNavMarker Pose;
		Pose.Code = FString::Printf(TEXT("CA%d"), E.PointNo);
		Pose.MarkerType = TEXT("cloud_anchor");
		Pose.PosXCm = E.PosXCm;
		Pose.PosYCm = E.PosYCm;
		Pose.PosZCm = E.PosZCm;
		Pose.HeadingDeg = E.HeadingDeg;
		return Pose;
	}

	FString CloudStateName(ECloudARPinCloudState State) { return UEnum::GetValueAsString(State); }
	FString TaskResultName(EARPinCloudTaskResult Result) { return UEnum::GetValueAsString(Result); }
	FString QualityReasonName(EARTrackingQualityReason Reason) { return UEnum::GetValueAsString(Reason); }

	/** 이 상태면 이번 시도는 끝났다(성공 못 함) — 다시 요청해야 한다. */
	bool IsCloudError(ECloudARPinCloudState State)
	{
		return State == ECloudARPinCloudState::ErrorInternalError
			|| State == ECloudARPinCloudState::ErrorLocalizationFailure
			|| State == ECloudARPinCloudState::ErrorServiceUnavailable
			|| State == ECloudARPinCloudState::ErrorResourceExhausted
			|| State == ECloudARPinCloudState::ErrorResolvingCloudIDNotFound
			|| State == ECloudARPinCloudState::ErrorNotAuthorized
			|| State == ECloudARPinCloudState::ErrorSDKVersionTooOld
			|| State == ECloudARPinCloudState::ErrorSDKVersionTooNew
			|| State == ECloudARPinCloudState::Cancelled;
	}

	/** 리졸브 요청이 돌고 있다(인식 전 핀이 있다). 스케줄러 슬롯을 차지한다. */
	bool IsInFlight(const FNavCloudResolveEntry& E)
	{
		return E.Pin != nullptr && !E.bRecognized;
	}

	/** 인식됐고 지금 Tracking 인 핀. 없으면 nullptr. */
	UARPin* GetTrackingRecognizedPin(const FNavCloudResolveEntry& E)
	{
		UARPin* Pin = E.Pin.Get();
		if (!E.bRecognized || Pin == nullptr || Pin->GetTrackingState() != EARTrackingState::Tracking)
		{
			return nullptr;
		}
		return Pin;
	}

	double Distance2D(const FVector& A, const FVector& B)
	{
		return FVector2D::Distance(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y));
	}

	/**
	 * D46 — 거절 기록을 갱신한다. 비교 대상은 **점프 벡터**(함의 위치 − 현재 위치)다 — 사람이 걸어도
	 * 두 위치가 같이 움직이므로 벡터는 그대로다. 벡터가 크게 바뀌거나 기록이 끊겼으면 지속 시계를 새로 켠다.
	 */
	FNavCloudJumpReject& NoteJumpReject(TMap<int32, FNavCloudJumpReject>& Map, int32 PointNo,
		const FVector2D& Offset, double Now)
	{
		FNavCloudJumpReject& R = Map.FindOrAdd(PointNo);
		const bool bFresh = R.LastTime <= 0.0
			|| (Now - R.LastTime) > 2.0
			|| FVector2D::Distance(R.JumpOffset, Offset) > kJumpStableCm;
		if (bFresh)
		{
			R.FirstTime = Now;
		}
		R.JumpOffset = Offset;
		R.LastTime = Now;
		return R;
	}
}
#endif

bool UNavCloudResolverSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
#if NAV_CLOUD_RESOLVE
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
#else
	return false;
#endif
}

bool UNavCloudResolverSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavCloudResolverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if NAV_CLOUD_RESOLVE
	UE_LOG(LogNavCloudResolve, Log,
		TEXT("[NavCloudResolve] 리졸브 서브시스템 시작 — 동시 %d · 분당 %d · 백오프 ≤%.0f초 · 근접 %.0fcm · "
			 "기준전환 ×%.1f/%.0f초 · 점프게이트 %.0fcm · 측량로그 %s"),
		MaxConcurrentResolves, MaxResolveRequestsPerMinute, RetryBackoffMaxSeconds, NearAnchorCm,
		RefSwitchRatio, RefMinDwellSeconds, JumpGateCm, bSurveyLogEnabled ? TEXT("ON") : TEXT("OFF"));
#endif
}

void UNavCloudResolverSubsystem::Deinitialize()
{
#if NAV_CLOUD_RESOLVE
	for (FNavCloudResolveEntry& E : Entries)
	{
		if (UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin))
		{
			UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(Pin);
		}
		E.Pin = nullptr;
	}
	if (Hud != nullptr)
	{
		Hud->RemoveFromParent();
		Hud = nullptr;
	}
#endif
	Entries.Reset();
	JumpRejects.Reset();
	Super::Deinitialize();
}

TStatId UNavCloudResolverSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNavCloudResolverSubsystem, STATGROUP_Tickables);
}

void UNavCloudResolverSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if NAV_CLOUD_RESOLVE
	// AR 세션이 돌기 전엔 아무것도 하지 않는다(플러그인 호출이 무의미).
	if (UARBlueprintLibrary::GetARSessionStatus().Status != EARSessionStatus::Running)
	{
		return;
	}

	TryConfigureCloudMode();

	if (!bConfigResolved)
	{
		bConfigResolved = ResolveServerConfig();
		if (!bConfigResolved)
		{
			return; // 서버 설정이 없으면 리졸브는 성립하지 않는다(조용히 쉰다).
		}
	}

	// D48 — 카메라 궤적은 앵커 목록·측위 상태와 무관하게 유효하다. 목록을 받기 전부터 남긴다.
	TickSurveyLog();

	if (!bAnchorsLoaded)
	{
		FetchBoundAnchors();
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RefreshAnchorsIfDue(Now);
	PollResolves();

	if (Now >= NextSchedulerTickTime)
	{
		NextSchedulerTickTime = Now + kSchedulerIntervalSeconds;
		ScheduleResolves(Now);
		UpdateReferenceAnchor(Now);
	}
	LogSchedulerSummaryIfDue(Now);
#endif
}

#if NAV_CLOUD_RESOLVE

void UNavCloudResolverSubsystem::TryConfigureCloudMode()
{
	if (bCloudConfigured)
	{
		return;
	}
	FGoogleARCoreServicesConfig Config;
	Config.ARPinCloudMode = EARPinCloudMode::Enabled;
	if (UGoogleARCoreServicesFunctionLibrary::ConfigGoogleARCoreServices(Config))
	{
		bCloudConfigured = true;
		UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] Cloud Anchor 모드 ON"));
	}
}

bool UNavCloudResolverSubsystem::ResolveServerConfig()
{
	// NavClient 는 **수정하지 않고 읽기만** 한다(공지 트리거 6 회피 — 기존 public 시그니처 변경 없음).
	const UWorld* World = GetWorld();
	if (const UGameInstance* GI = World ? World->GetGameInstance() : nullptr)
	{
		if (const UNavClient* Nav = GI->GetSubsystem<UNavClient>())
		{
			ServerBaseUrl = Nav->GetServerBaseUrl();
		}
	}
	if (GConfig != nullptr)
	{
		GConfig->GetString(TEXT("/Script/TimeMachineAR.NavClient"),
			TEXT("DefaultMapId"), MapId, GGameIni);
	}
	ServerBaseUrl.TrimStartAndEndInline();
	MapId.TrimStartAndEndInline();

	if (ServerBaseUrl.IsEmpty() || MapId.IsEmpty())
	{
		// 매 틱 로그를 쏟지 않도록 한 번만 남긴다.
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogNavCloudResolve, Warning,
				TEXT("[NavCloudResolve] 서버 설정 없음 (ServerBaseUrl='%s' MapId='%s') — 리졸브 안 함"),
				*ServerBaseUrl, *MapId);
		}
		return false;
	}
	UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] 서버=%s map=%s"), *ServerBaseUrl, *MapId);
	return true;
}

void UNavCloudResolverSubsystem::FetchBoundAnchors()
{
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (bFetchInFlight || Now < NextFetchTime)
	{
		return;
	}
	bFetchInFlight = true;
	NextFetchTime = Now + kFetchRetrySeconds; // 실패해도 곧장 재요청하지 않게 먼저 밀어 둔다.

	const FString Url = FString::Printf(
		TEXT("%s/maps/%s/cloud-anchors?state=bound"), *ServerBaseUrl, *MapId);

	TWeakObjectPtr<UNavCloudResolverSubsystem> WeakThis(this);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	Req->SetTimeout(5.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			UNavCloudResolverSubsystem* Self = WeakThis.Get();
			if (Self == nullptr) { return; }
			Self->bFetchInFlight = false;
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
				UE_LOG(LogNavCloudResolve, Warning,
					TEXT("[NavCloudResolve] GET bound 실패 code=%d — %.0f초 뒤 재시도"),
					Code, kFetchRetrySeconds);
				return;
			}
			Self->ApplyAnchorsJson(Resp->GetContentAsString());
		});
	Req->ProcessRequest();
	UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] GET %s"), *Url);
}

void UNavCloudResolverSubsystem::ApplyAnchorsJson(const FString& Body)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Arr))
	{
		UE_LOG(LogNavCloudResolve, Warning, TEXT("[NavCloudResolve] 앵커 목록 파싱 실패"));
		return;
	}

	// 기존 엔트리를 버리지 않는다 — 진행 중인 리졸브 핀과 추적 이력을 살린 채,
	// 새로 등록된 앵커만 추가하고 좌표·heading 은 서버 값으로 갱신한다.
	TSet<int32> SeenPoints;
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj->IsValid())
		{
			continue;
		}
		FString CloudId;
		if (!(*Obj)->TryGetStringField(TEXT("cloud_id"), CloudId) || CloudId.IsEmpty())
		{
			continue; // state=bound 라 없을 리 없지만 방어.
		}
		const int32 PointNo = static_cast<int32>((*Obj)->GetIntegerField(TEXT("point_no")));

		// 13-1 D23 — `asset*` 은 **에셋 전용 앵커**다. 맵 좌표가 촬영용 임의값이라
		// (201 은 0,0,0) 측위 기준점으로 쓰면 인식하는 순간 앱이 자기 위치를 원점으로 잡는다.
		// 그래서 목록을 통째로 받되 여기서 갈라 버린다 — 에셋 쪽은 NavCloudAssetSpawner 가 맡는다.
		FString Label;
		(*Obj)->TryGetStringField(TEXT("label"), Label);
		if (NavCloudAnchorRoles::IsAssetAnchorLabel(Label))
		{
			// 라벨이 뒤늦게 바뀌었을 수 있다 — 이미 들고 있던 핀이 있으면 여기서 놓는다.
			const int32 Dropped = Entries.IndexOfByPredicate(
				[PointNo](const FNavCloudResolveEntry& X) { return X.PointNo == PointNo; });
			if (Dropped != INDEX_NONE)
			{
				if (UCloudARPin* OldPin = Cast<UCloudARPin>(Entries[Dropped].Pin))
				{
					UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(OldPin);
				}
				Entries.RemoveAt(Dropped);
				if (RefPointNo == PointNo)
				{
					RefPointNo = 0;
				}
			}
			if (!bAnchorsLoaded || Dropped != INDEX_NONE)
			{
				UE_LOG(LogNavCloudResolve, Log,
					TEXT("[NavCloudResolve] #%d '%s' 는 에셋 앵커 — 측위에서 제외(13-1 D23)"),
					PointNo, *Label);
			}
			continue;
		}

		SeenPoints.Add(PointNo);

		FNavCloudResolveEntry* Existing = Entries.FindByPredicate(
			[PointNo](const FNavCloudResolveEntry& X) { return X.PointNo == PointNo; });

		FNavCloudResolveEntry& E = Existing != nullptr ? *Existing : Entries.AddDefaulted_GetRef();
		const bool bIsNew = (Existing == nullptr);
		const bool bReHosted = !bIsNew && !E.CloudId.Equals(CloudId);

		E.PointNo = PointNo;
		E.CloudId = CloudId;
		// 좌표·heading 은 **매번 서버 값으로 덮는다** — 13-3 §G 재시드 값이 앱을 다시 굽지 않아도
		// 다음 기준 전환·재보정부터 쓰인다.
		E.PosXCm = ReadNumberField(*Obj, TEXT("pos_x_cm"), 0.f);
		E.PosYCm = ReadNumberField(*Obj, TEXT("pos_y_cm"), 0.f);
		E.PosZCm = ReadNumberField(*Obj, TEXT("pos_z_cm"), 0.f);
		E.HeadingDeg = ReadNumberField(*Obj, TEXT("heading_deg"), 90.f);

		if (bReHosted)
		{
			// 다른 앵커로 재등록됐다 — 들고 있던 핀은 무효다. 큐로 돌려 처음부터 다시 잡는다.
			if (UCloudARPin* OldPin = Cast<UCloudARPin>(E.Pin))
			{
				UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(OldPin);
			}
			E.Pin = nullptr;
			E.bRecognized = false;
			E.Attempts = 0;
			E.FirstRequest = 0.0;
			E.AttemptStart = 0.0;
			E.ConsecutiveFailures = 0;
			E.NextEligibleTime = 0.0;
			E.LastTrackedTime = 0.0;
			E.LastGapSeconds = 0.0;
			if (RefPointNo == PointNo)
			{
				RefPointNo = 0;
			}
		}

		if (bIsNew || bReHosted)
		{
			UE_LOG(LogNavCloudResolve, Log,
				TEXT("[NavCloudResolve] #%d 맵(%.0f, %.0f, %.0f) heading=%.1f° cloud_id=%s%s"),
				E.PointNo, E.PosXCm, E.PosYCm, E.PosZCm, E.HeadingDeg, *E.CloudId,
				bReHosted ? TEXT(" (재등록)") : TEXT(""));
		}
	}

	// 13-3 §G — 서버에서 enabled=false 로 내렸거나 바인딩을 뗀 앵커는 다음 재조회에서 뺀다.
	// 빈 목록(서버 순간 장애 등)으로 전부 지우지 않도록, 하나라도 받았을 때만 정리한다.
	if (SeenPoints.Num() > 0)
	{
		for (int32 i = Entries.Num() - 1; i >= 0; --i)
		{
			if (SeenPoints.Contains(Entries[i].PointNo))
			{
				continue;
			}
			UE_LOG(LogNavCloudResolve, Log,
				TEXT("[NavCloudResolve] #%d 서버 목록에서 빠짐(enabled=false/바인딩 해제) — 리졸브 대상에서 제외"),
				Entries[i].PointNo);
			if (UCloudARPin* OldPin = Cast<UCloudARPin>(Entries[i].Pin))
			{
				UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(OldPin);
			}
			if (RefPointNo == Entries[i].PointNo)
			{
				RefPointNo = 0;
			}
			JumpRejects.Remove(Entries[i].PointNo);
			Entries.RemoveAt(i);
		}
	}

	Entries.Sort([](const FNavCloudResolveEntry& A, const FNavCloudResolveEntry& B)
		{ return A.PointNo < B.PointNo; });

	if (Entries.Num() == 0)
	{
		// 등록이 아직이면 계속 기다린다(자바 도구가 bind 하면 다음 조회에 잡힌다).
		UE_LOG(LogNavCloudResolve, Warning,
			TEXT("[NavCloudResolve] bound 앵커 0개 — 등록 전이거나 맵이 다르다. 계속 재조회"));
		return;
	}

	const bool bFirstLoad = !bAnchorsLoaded;
	bAnchorsLoaded = true;
	if (bFirstLoad)
	{
		UE_LOG(LogNavCloudResolve, Log,
			TEXT("[NavCloudResolve] bound 앵커 %d개 로드 — 동시 %d개씩 가까울 법한 순으로 리졸브(D42)"),
			Entries.Num(), FMath::Max(1, MaxConcurrentResolves));
	}
	// 리졸브는 여기서 걸지 않는다 — 새 엔트리는 NextEligibleTime=0 으로 큐에 서고 ScheduleResolves 가 배정한다.
}

void UNavCloudResolverSubsystem::StartResolve(FNavCloudResolveEntry& Entry)
{
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	EARPinCloudTaskResult Result = EARPinCloudTaskResult::Failed;
	UCloudARPin* Pin = UGoogleARCoreServicesFunctionLibrary::CreateAndResolveCloudARPin(Entry.CloudId, Result);
	++Entry.Attempts;
	Entry.AttemptStart = Now;
	if (Entry.FirstRequest <= 0.0)
	{
		Entry.FirstRequest = Now;
	}

	if (Pin != nullptr && Result == EARPinCloudTaskResult::Started)
	{
		Entry.Pin = Pin;
		RecentRequestTimes.Add(Now);
		int32 InFlight = 0;
		for (const FNavCloudResolveEntry& X : Entries)
		{
			InFlight += IsInFlight(X) ? 1 : 0;
		}
		// `리졸브 요청` 문구는 §4 검증(분당 요청 수)이 logcat 에서 세는 기준이다 — 바꾸지 말 것.
		UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] #%d 리졸브 요청 (%d번째 시도) %s · 진행 %d/%d"),
			Entry.PointNo, Entry.Attempts, *Entry.CloudId, InFlight, FMath::Max(1, MaxConcurrentResolves));
	}
	else
	{
		// 요청 자체가 안 걸렸다(세션 준비 전 등 — 네트워크 요청이 아니다). 단계적 백오프 대신
		// 12단계와 같은 고정 간격으로 다시 배정한다.
		Entry.Pin = nullptr;
		Entry.NextEligibleTime = Now + kBackoffBaseSeconds;
		UE_LOG(LogNavCloudResolve, Warning, TEXT("[NavCloudResolve] #%d 리졸브 시작 실패: %s — %.0f초 뒤 다시"),
			Entry.PointNo, *TaskResultName(Result), kBackoffBaseSeconds);
	}
}

void UNavCloudResolverSubsystem::FailAttempt(FNavCloudResolveEntry& Entry, double Now, bool bResourceExhausted)
{
	++Entry.ConsecutiveFailures;

	// D43 — **요청 시작 간격**을 12 → 24 → 48 → 상한(60)으로 늘린다. 시도에 이미 쓴 시간은 뺀다
	// (타임아웃 실패면 12초를 이미 썼다 → 첫 재시도는 곧바로 = 12단계와 같은 간격).
	const int32 Exponent = FMath::Clamp(Entry.ConsecutiveFailures - 1, 0, 8);
	const double Interval = FMath::Min(static_cast<double>(RetryBackoffMaxSeconds),
		kBackoffBaseSeconds * FMath::Pow(2.0, static_cast<double>(Exponent)));
	const double Spent = (Entry.AttemptStart > 0.0) ? (Now - Entry.AttemptStart) : 0.0;
	double Delay = FMath::Max(0.0, Interval - Spent);

	// D44 — 근접한 앵커는 기다리게 하지 않는다(사람이 그 앞에 서 있는 동안 계속 건다).
	const float Bound = EstimateDistanceLowerBoundCm(Entry);
	const bool bNear = Bound >= 0.f && Bound <= NearAnchorCm;
	if (bNear)
	{
		Delay = 0.0;
	}

	if (bResourceExhausted)
	{
		Delay = FMath::Max(Delay, static_cast<double>(RetryBackoffMaxSeconds));
		GlobalResolveHoldUntil = Now + kResourceExhaustedHoldSeconds;
		UE_LOG(LogNavCloudResolve, Error,
			TEXT("[NavCloudResolve] RESOURCE_EXHAUSTED (#%d) — 구글 한도. 신규 요청 %.0f초 정지"),
			Entry.PointNo, kResourceExhaustedHoldSeconds);
	}
	Entry.NextEligibleTime = Now + Delay;
}

float UNavCloudResolverSubsystem::EstimateDistanceLowerBoundCm(const FNavCloudResolveEntry& Target) const
{
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Cam == nullptr)
	{
		return -1.f;
	}
	const FVector CamLoc = Cam->GetCameraLocation();

	// 삼각부등식: |cam→t| ≥ | |t−r|맵 − |cam→r|월드 |. 맵 거리는 앵커 **사이** 거리라 맵↔월드 회전과
	// 무관하다 → 1차 시드의 자리표시 heading 에서도 성립한다. 설치 오차만큼 여유를 뺀다.
	double Best = -1.0;
	for (const FNavCloudResolveEntry& R : Entries)
	{
		if (R.PointNo == Target.PointNo)
		{
			continue;
		}
		const UARPin* Pin = GetTrackingRecognizedPin(R);
		if (Pin == nullptr)
		{
			continue;
		}
		const double MapGap = FVector2D::Distance(
			FVector2D(Target.PosXCm, Target.PosYCm), FVector2D(R.PosXCm, R.PosYCm));
		const double CamToRef = Distance2D(CamLoc, Pin->GetLocalToWorldTransform().GetLocation());
		const double Bound = FMath::Max(0.0, FMath::Abs(MapGap - CamToRef) - static_cast<double>(PlanSlackCm));
		Best = FMath::Max(Best, Bound);
	}
	return static_cast<float>(Best);
}

void UNavCloudResolverSubsystem::ScheduleResolves(double Now)
{
	if (!bCloudConfigured)
	{
		return; // Cloud 모드가 켜지기 전에 걸면 시작 실패만 쌓인다.
	}

	RecentRequestTimes.RemoveAll([Now](double T) { return Now - T > 60.0; });

	const int32 Cap = FMath::Max(1, MaxConcurrentResolves);
	TArray<int32> InFlight;
	TArray<int32> Waiting;
	TArray<float> Bounds;
	Bounds.SetNum(Entries.Num());

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		FNavCloudResolveEntry& E = Entries[i];
		if (E.bRecognized)
		{
			E.bNearLast = false;
			Bounds[i] = -1.f;
			continue;
		}
		const float Bound = EstimateDistanceLowerBoundCm(E);
		Bounds[i] = Bound;
		const bool bNear = Bound >= 0.f && Bound <= NearAnchorCm;

		// D44 — 먼 → 가까움으로 바뀌는 순간 백오프를 푼다(포기가 아니라 대기였음을 여기서 되살린다).
		if (bNear && !E.bNearLast && (E.ConsecutiveFailures > 0 || E.NextEligibleTime > Now))
		{
			UE_LOG(LogNavCloudResolve, Log,
				TEXT("[NavCloudResolve] #%d 근접(거리 하한 %.0fcm) — 백오프 해제(D44, 실패 %d회였음)"),
				E.PointNo, Bound, E.ConsecutiveFailures);
			E.ConsecutiveFailures = 0;
			E.NextEligibleTime = 0.0;
		}
		E.bNearLast = bNear;

		if (E.Pin != nullptr)
		{
			InFlight.Add(i);
		}
		else if (Now >= E.NextEligibleTime)
		{
			Waiting.Add(i);
		}
	}

	if (Waiting.Num() == 0 || Now < GlobalResolveHoldUntil)
	{
		return;
	}

	auto IsNearIdx = [this, &Bounds](int32 Idx)
	{
		return Bounds[Idx] >= 0.f && Bounds[Idx] <= NearAnchorCm;
	};
	auto BoundKey = [&Bounds](int32 Idx)
	{
		return Bounds[Idx] >= 0.f ? static_cast<double>(Bounds[Idx]) : TNumericLimits<double>::Max();
	};

	// D42 — 근접 우선 → 거리 하한 오름차순 → 오래 안 건 것 → 번호(설치 우선순위) 순.
	Waiting.Sort([this, &IsNearIdx, &BoundKey](int32 A, int32 B)
		{
			const bool NearA = IsNearIdx(A);
			const bool NearB = IsNearIdx(B);
			if (NearA != NearB) { return NearA; }
			const double KA = BoundKey(A);
			const double KB = BoundKey(B);
			if (KA != KB) { return KA < KB; }
			if (Entries[A].AttemptStart != Entries[B].AttemptStart)
			{
				return Entries[A].AttemptStart < Entries[B].AttemptStart;
			}
			return Entries[A].PointNo < Entries[B].PointNo;
		});

	int32 Free = Cap - InFlight.Num();
	const int32 FarBudgetMax = FMath::Max(1, MaxResolveRequestsPerMinute);
	const int32 NearBudgetMax = FMath::Max(FarBudgetMax, kNearRequestsPerMinuteCeiling);

	for (int32 Idx : Waiting)
	{
		const bool bNear = IsNearIdx(Idx);
		const int32 Budget = (bNear ? NearBudgetMax : FarBudgetMax) - RecentRequestTimes.Num();
		if (Budget <= 0)
		{
			if (bNear) { continue; }
			break; // 정렬상 뒤는 전부 근접이 아니다.
		}

		if (Free <= 0)
		{
			if (!bNear)
			{
				break;
			}
			// D44 양보 — 근접 앵커가 슬롯을 못 얻으면, 근접이 아니고 3초 이상 돈 시도 중 가장 먼 것을 끊는다.
			int32 Victim = INDEX_NONE;
			for (int32 F : InFlight)
			{
				if (IsNearIdx(F) || Now - Entries[F].AttemptStart < kMinAttemptBeforePreemptSeconds)
				{
					continue;
				}
				if (Victim == INDEX_NONE || BoundKey(F) > BoundKey(Victim)
					|| (BoundKey(F) == BoundKey(Victim) && Entries[F].AttemptStart < Entries[Victim].AttemptStart))
				{
					Victim = F;
				}
			}
			if (Victim == INDEX_NONE)
			{
				break;
			}
			FNavCloudResolveEntry& V = Entries[Victim];
			UE_LOG(LogNavCloudResolve, Log,
				TEXT("[NavCloudResolve] #%d 시도 양보 — 근접 #%d 에 슬롯(D44, 실패로 세지 않음)"),
				V.PointNo, Entries[Idx].PointNo);
			if (UCloudARPin* VictimPin = Cast<UCloudARPin>(V.Pin))
			{
				UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(VictimPin);
			}
			V.Pin = nullptr;
			V.NextEligibleTime = Now;
			InFlight.RemoveSingle(Victim);
			++Free;
		}

		StartResolve(Entries[Idx]);
		if (Entries[Idx].Pin != nullptr)
		{
			InFlight.Add(Idx);
			--Free;
		}
	}
}

void UNavCloudResolverSubsystem::PollResolves()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	for (FNavCloudResolveEntry& E : Entries)
	{
		if (E.bRecognized)
		{
			// 마커가 늘 재탐색되던 것처럼, 잡은 뒤에도 이 앵커를 **계속 지켜본다**.
			WatchRecognizedAnchor(E, Now);
			continue;
		}
		UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin);
		if (Pin == nullptr)
		{
			continue; // 큐에서 차례를 기다린다 — ScheduleResolves 가 건다(D42).
		}

		const ECloudARPinCloudState CState = Pin->GetARPinCloudState();
		const bool bTracking = (Pin->GetTrackingState() == EARTrackingState::Tracking);

		// 인식 확정 = 클라우드 상태 Success **그리고** 지금 트래킹 중(런북 §E-2).
		if (CState == ECloudARPinCloudState::Success && bTracking)
		{
			E.bRecognized = true;
			E.ConsecutiveFailures = 0;
			E.NextEligibleTime = 0.0;
			E.LastTrackedTime = Now;
			E.LastGapSeconds = 0.0;
			const float AttemptLatency = static_cast<float>(Now - E.AttemptStart);
			const float TotalLatency = static_cast<float>(Now - E.FirstRequest);
			// 인식된 앵커로 **지도를 띄운다**(QR 마커 대체). 실패해도 인식 실측엔 영향 없다.
			const bool bLocalizedNow = TryLocalizeWithAnchor(E);
			EnsureHud();
			if (Hud != nullptr)
			{
				if (bLocalizedNow)
				{
					Hud->ShowLocalized(E.PointNo, AttemptLatency);
				}
				else
				{
					Hud->ShowRecognized(E.PointNo, AttemptLatency);
				}
			}
			// 실측표에 그대로 옮길 수 있게 두 값을 같이 남긴다. 토스트의 지연은 **이번 시도**
			// 기준이고(요청→확정), 전체는 앱 시작부터 걸린 시간이다(그 사이 걸어왔을 수 있다).
			UE_LOG(LogNavCloudResolve, Log,
				TEXT("[NavCloudResolve] #%d 인식 — 시도지연 %.1f초 / 전체 %.1f초 / 시도 %d회"),
				E.PointNo, AttemptLatency, TotalLatency, E.Attempts);

			if (bSurveyLogEnabled)
			{
				const FTransform X = Pin->GetLocalToWorldTransform();
				const FVector L = X.GetLocation();
				UE_LOG(LogNavCloudSurvey, Log,
					TEXT("[NavCloudSurvey] #%d RESOLVED t=%.2f world=(%.1f,%.1f,%.1f) yaw=%.2f latency=%.1f attempts=%d"),
					E.PointNo, SurveyTime(), L.X, L.Y, L.Z, X.Rotator().Yaw, AttemptLatency, E.Attempts);
			}
			continue;
		}

		// 실패/타임아웃이면 이번 시도를 버리고 백오프 뒤 큐로 돌려보낸다(D43 — 포기는 없다, D44).
		const bool bTimedOut = (Now - E.AttemptStart > kAttemptTimeoutSeconds);
		if (IsCloudError(CState) || bTimedOut)
		{
			UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(Pin);
			E.Pin = nullptr;
			FailAttempt(E, Now, CState == ECloudARPinCloudState::ErrorResourceExhausted);
			UE_LOG(LogNavCloudResolve, Warning,
				TEXT("[NavCloudResolve] #%d 시도 %d 실패 — state=%s tracking=%d 이유=%s%s · 다음 %.0f초 뒤"),
				E.PointNo, E.Attempts, *CloudStateName(CState), bTracking ? 1 : 0,
				*QualityReasonName(UARBlueprintLibrary::GetTrackingQualityReason()),
				bTimedOut ? TEXT(" (타임아웃)") : TEXT(""),
				FMath::Max(0.0, E.NextEligibleTime - Now));
		}
	}
}

bool UNavCloudResolverSubsystem::TryLocalizeWithAnchor(FNavCloudResolveEntry& Entry)
{
	UWorld* World = GetWorld();
	UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	UCloudARPin* Pin = Cast<UCloudARPin>(Entry.Pin);
	if (Localizer == nullptr || Pin == nullptr)
	{
		return false;
	}
	if (Localizer->IsLocalized())
	{
		// 이미 서 있는 측위(QR 마커 또는 다른 앵커)를 덮지 않는다. 앵커 사이 전환은 UpdateReferenceAnchor 몫이다.
		return false;
	}

	// 서버가 아는 이 앵커의 맵 좌표를 기준점으로 넘긴다. 마커 측위와 같은 식이다.
	const FNavMarker AnchorPose = MakeAnchorPose(Entry);

	const FTransform AnchorWorld = Pin->GetLocalToWorldTransform();
	const bool bNewlyLocalized =
		Localizer->LocalizeFromCloudAnchor(AnchorPose.Code, AnchorWorld, AnchorPose);
	Entry.bLocalizeApplied = true;
	RefPointNo = Entry.PointNo;
	RefSince = World->GetTimeSeconds();
	JumpRejects.Reset();

	UE_LOG(LogNavCloudResolve, Log,
		TEXT("[NavCloudResolve] #%d 로 측위 %s — 앵커 월드 yaw=%.1f° / 서버 heading=%.1f°"
			 " (heading 이 실측값이 아니면 지도 회전이 그만큼 틀어진다 — 13-3 §G 재시드)"),
		Entry.PointNo, bNewlyLocalized ? TEXT("성립") : TEXT("재적용"),
		AnchorWorld.Rotator().Yaw, Entry.HeadingDeg);
	if (bSurveyLogEnabled)
	{
		UE_LOG(LogNavCloudSurvey, Log,
			TEXT("[NavCloudSurvey] REF t=%.2f from=#0 to=#%d d_from=-1 d_to=-1 jump_cm=0 reason=localize"),
			SurveyTime(), Entry.PointNo);
	}
	return bNewlyLocalized;
}

void UNavCloudResolverSubsystem::WatchRecognizedAnchor(FNavCloudResolveEntry& Entry, double Now)
{
	UCloudARPin* Pin = Cast<UCloudARPin>(Entry.Pin);

	// ① 핀이 사라졌거나 오류로 죽었으면 **큐로 돌려보낸다** — 마커가 늘 재탐색 상태였던 것처럼,
	//    앵커도 언제나 다시 잡을 준비를 유지한다(동시 상한은 스케줄러가 지킨다).
	if (Pin == nullptr || IsCloudError(Pin->GetARPinCloudState()))
	{
		UE_LOG(LogNavCloudResolve, Warning,
			TEXT("[NavCloudResolve] #%d 핀 무효 (%s) — 큐로 돌려 다시 잡는다"),
			Entry.PointNo,
			Pin != nullptr ? *CloudStateName(Pin->GetARPinCloudState()) : TEXT("null"));
		if (Pin != nullptr)
		{
			UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(Pin);
		}
		Entry.Pin = nullptr;
		Entry.bRecognized = false;
		Entry.Attempts = 0;
		Entry.FirstRequest = 0.0;
		Entry.AttemptStart = 0.0;
		Entry.LastTrackedTime = 0.0;
		Entry.LastGapSeconds = 0.0;
		Entry.ConsecutiveFailures = 0;
		Entry.NextEligibleTime = 0.0;
		if (RefPointNo == Entry.PointNo)
		{
			RefPointNo = 0;
			UE_LOG(LogNavCloudResolve, Warning,
				TEXT("[NavCloudResolve] 기준 앵커 #%d 를 잃음 — 가장 가까운 인식 앵커로 넘긴다(D45)"),
				Entry.PointNo);
		}
		return;
	}

	// ② 지금 안 보이면 아무것도 하지 않는다. LastTrackedTime 을 그대로 둬 gap 이 쌓이게 한다.
	if (Pin->GetTrackingState() != EARTrackingState::Tracking)
	{
		return;
	}

	const double Gap = (Entry.LastTrackedTime > 0.0) ? (Now - Entry.LastTrackedTime) : 0.0;
	Entry.LastTrackedTime = Now;
	if (Gap >= kReacquireGapSeconds)
	{
		Entry.LastGapSeconds = Gap; // 측량 로그가 다음 샘플에 싣는다.
	}

	UWorld* World = GetWorld();
	UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	if (Localizer == nullptr)
	{
		return;
	}

	// ③ 측위가 비어 있으면(네비 버튼의 ResetLocalization 등) UpdateReferenceAnchor 가
	//    **가장 가까운** 인식 앵커로 다시 세운다(D45 — 번호 순 아무 앵커가 아니라).
	if (!Localizer->IsLocalized())
	{
		return;
	}

	// ④ 마커로 잡힌 측위는 덮지 않는다 — 마커가 더 정확하다(7단계 앵커 전환이 우선).
	if (!Localizer->IsAnchoredToCloudAnchor())
	{
		return;
	}

	// ⑤ D45 — 재관측 재보정은 **기준 앵커에만** 한다. 다른 앵커의 재관측으로 기준을 덮지 않는다.
	if (Entry.PointNo != RefPointNo)
	{
		return;
	}

	// ⑥ **재관측일 때만** 변환을 다시 세운다(마커의 bRelatchOnReacquire 와 같은 규칙).
	//    매 틱 재계산하면 추적 노이즈가 실려 지도가 떨린다. 같은 앵커라 점프 게이트는 걸지 않는다 —
	//    추적 재초기화로 월드 원점이 옮겨 갔다면 이 재보정이 곧 복구다.
	if (Gap < kReacquireGapSeconds)
	{
		return;
	}

	const FVector BeforeMap = GetCameraMapLocation();
	Localizer->LocalizeFromCloudAnchor(
		FString::Printf(TEXT("CA%d"), Entry.PointNo),
		Pin->GetLocalToWorldTransform(),
		MakeAnchorPose(Entry));
	const FVector AfterMap = GetCameraMapLocation();

	const float DriftCm = static_cast<float>(Distance2D(BeforeMap, AfterMap));
	++Entry.Relatches;
	UE_LOG(LogNavCloudResolve, Log,
		TEXT("[NavCloudResolve] #%d 재보정 %d회째 — %.1f초 만에 재관측, 드리프트 %.0fcm 보정"),
		Entry.PointNo, Entry.Relatches, Gap, DriftCm);

	// 눈에 띄게 밀렸던 것을 씻어냈을 때만 알린다(토스트 잔소리 방지).
	if (DriftCm >= kRelatchToastCm)
	{
		EnsureHud();
		if (Hud != nullptr)
		{
			Hud->ShowMessage(FString::Printf(
				TEXT("%d번 앵커 재보정 (%.0fcm)"), Entry.PointNo, DriftCm));
		}
	}
}

bool UNavCloudResolverSubsystem::ComputeImpliedCameraMap(const FNavCloudResolveEntry& Entry, FVector& OutMap) const
{
	UARPin* Pin = Entry.Pin.Get();
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Pin == nullptr || Cam == nullptr)
	{
		return false;
	}

	// ⚠️ NavLocalizer::SolveTransform · WorldToMap 과 **같은 식**이다(앵커는 인쇄물 보정각 0).
	//    맵(오른손) Y 를 뒤집어 왼손 프레임으로 만든 뒤 회전+이동, heading 부호도 반전.
	const FTransform AnchorWorld = Pin->GetLocalToWorldTransform();
	const double MapHeadingDeg = -static_cast<double>(Entry.HeadingDeg);
	const double YawOffsetDeg = FRotator::NormalizeAxis(AnchorWorld.Rotator().Yaw - MapHeadingDeg);
	const FRotator Rot(0.0, YawOffsetDeg, 0.0);
	const FVector AnchorMapPrime(Entry.PosXCm, -Entry.PosYCm, Entry.PosZCm);
	const FVector Translation = AnchorWorld.GetLocation() - Rot.RotateVector(AnchorMapPrime);
	const FTransform MapToWorldXf(Rot, Translation, FVector::OneVector);

	const FVector P = MapToWorldXf.InverseTransformPosition(Cam->GetCameraLocation());
	OutMap = FVector(P.X, -P.Y, P.Z);
	return true;
}

bool UNavCloudResolverSubsystem::PassJumpGate(int32 CandidatePointNo, const FVector2D& ImpliedMap,
	const FVector2D& CurrentMap, double Now, FString& OutReason)
{
	OutReason.Reset();
	const FVector2D Offset = ImpliedMap - CurrentMap;
	const double Jump = Offset.Size();
	if (Jump <= JumpGateCm)
	{
		JumpRejects.Remove(CandidatePointNo);
		return true;
	}

	for (auto It = JumpRejects.CreateIterator(); It; ++It)
	{
		if (Now - It.Value().LastTime > kJumpRecordWindowSeconds)
		{
			It.RemoveCurrent();
		}
	}
	FNavCloudJumpReject& Mine = NoteJumpReject(JumpRejects, CandidatePointNo, Offset, Now);

	// ① 합의 — 다른 앵커도 같은 방향·크기로 튀라고 한다면, 틀린 건 지금 변환이다.
	for (const TPair<int32, FNavCloudJumpReject>& Pair : JumpRejects)
	{
		if (Pair.Key != CandidatePointNo
			&& FVector2D::Distance(Pair.Value.JumpOffset, Offset) <= kJumpConsensusCm)
		{
			OutReason = FString::Printf(TEXT("합의 #%d·#%d"), CandidatePointNo, Pair.Key);
			JumpRejects.Reset();
			return true;
		}
	}
	// ② 지속 — 같은 점프를 15초 넘게 계속 요구하면 받아들인다(교착 방지).
	const double Held = Now - Mine.FirstTime;
	if (Held >= kJumpStableSeconds)
	{
		OutReason = FString::Printf(TEXT("지속 %.0f초"), Held);
		JumpRejects.Reset();
		return true;
	}

	if (Mine.LastLogTime < 0.0 || Now - Mine.LastLogTime >= kJumpLogIntervalSeconds)
	{
		Mine.LastLogTime = Now;
		UE_LOG(LogNavCloudResolve, Warning,
			TEXT("[NavCloudResolve] #%d 로 기준 전환 거부(D46) — 함의 위치가 %.0fcm 튄다 · 기준 #%d 유지 (%.0f초째)"),
			CandidatePointNo, Jump, RefPointNo, Held);
		if (bSurveyLogEnabled)
		{
			UE_LOG(LogNavCloudSurvey, Log,
				TEXT("[NavCloudSurvey] JUMP_REJECT t=%.2f cand=#%d ref=#%d jump_cm=%.0f"),
				SurveyTime(), CandidatePointNo, RefPointNo, Jump);
		}
	}
	return false;
}

void UNavCloudResolverSubsystem::UpdateReferenceAnchor(double Now)
{
	UWorld* World = GetWorld();
	UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Localizer == nullptr || Cam == nullptr)
	{
		return;
	}

	if (Localizer->IsLocalized() && !Localizer->IsAnchoredToCloudAnchor())
	{
		RefPointNo = 0; // QR 마커가 기준이다 — 앵커로 덮지 않는다.
		return;
	}

	// D45 — 거리는 **월드**(ARCore 실측)로 잰다. 맵 좌표·heading 이 자리표시여도 누가 가까운지는 맞다.
	const FVector CamLoc = Cam->GetCameraLocation();
	int32 NearestIdx = INDEX_NONE;
	double NearestD = TNumericLimits<double>::Max();
	int32 RefIdx = INDEX_NONE;
	double RefD = -1.0;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const UARPin* Pin = GetTrackingRecognizedPin(Entries[i]);
		if (Pin == nullptr)
		{
			continue;
		}
		const double D = Distance2D(CamLoc, Pin->GetLocalToWorldTransform().GetLocation());
		if (D < NearestD)
		{
			NearestD = D;
			NearestIdx = i;
		}
		if (Entries[i].PointNo == RefPointNo)
		{
			RefIdx = i;
			RefD = D;
		}
	}
	if (NearestIdx == INDEX_NONE)
	{
		return;
	}
	FNavCloudResolveEntry& Nearest = Entries[NearestIdx];

	if (!Localizer->IsLocalized())
	{
		// 네비 버튼의 ResetLocalization 등으로 비었다 — 가장 가까운 앵커로 다시 세운다.
		// 비교할 현재 위치가 없으니 점프 게이트는 걸지 않는다.
		TryLocalizeWithAnchor(Nearest);
		return;
	}

	const bool bRefLost = (RefIdx == INDEX_NONE);
	if (!bRefLost)
	{
		if (NearestIdx == RefIdx)
		{
			return;
		}
		if (Now - RefSince < RefMinDwellSeconds)
		{
			return; // 최소 체류 — 기준을 잡자마자 갈아치우지 않는다.
		}
		if (NearestD * RefSwitchRatio > RefD)
		{
			return; // 히스테리시스 — 1.5배 이상 가까워야 바꾼다.
		}
	}

	FVector ImpliedMap;
	if (!ComputeImpliedCameraMap(Nearest, ImpliedMap))
	{
		return;
	}
	const FVector CurrentMap = Localizer->WorldToMap(CamLoc);
	const FVector2D Current2D(CurrentMap.X, CurrentMap.Y);
	const double Jump = Distance2D(ImpliedMap, CurrentMap);

	if (Jump > JumpGateCm)
	{
		// D46 합의 판정용 — 지금 보이는 다른 인식 앵커들이 함의하는 점프도 기록해 둔다.
		for (int32 j = 0; j < Entries.Num(); ++j)
		{
			if (j == NearestIdx || GetTrackingRecognizedPin(Entries[j]) == nullptr)
			{
				continue;
			}
			FVector OtherMap;
			if (ComputeImpliedCameraMap(Entries[j], OtherMap) && Distance2D(OtherMap, CurrentMap) > JumpGateCm)
			{
				NoteJumpReject(JumpRejects, Entries[j].PointNo,
					FVector2D(OtherMap.X, OtherMap.Y) - Current2D, Now);
			}
		}
	}

	FString GateReason;
	if (!PassJumpGate(Nearest.PointNo, FVector2D(ImpliedMap.X, ImpliedMap.Y), Current2D, Now, GateReason))
	{
		return;
	}

	UARPin* NearestPin = Nearest.Pin.Get();
	const int32 FromNo = RefPointNo;
	Localizer->LocalizeFromCloudAnchor(
		FString::Printf(TEXT("CA%d"), Nearest.PointNo),
		NearestPin->GetLocalToWorldTransform(),
		MakeAnchorPose(Nearest));
	Nearest.bLocalizeApplied = true;
	RefPointNo = Nearest.PointNo;
	RefSince = Now;

	const FVector AfterMap = GetCameraMapLocation();
	const float DriftCm = static_cast<float>(Distance2D(CurrentMap, AfterMap));
	const TCHAR* ReasonToken = !GateReason.IsEmpty()
		? (GateReason.StartsWith(TEXT("합의")) ? TEXT("gate_consensus") : TEXT("gate_stable"))
		: (bRefLost ? TEXT("ref_lost") : TEXT("nearer"));

	// `재보정` 문구는 §H 검증(`adb logcat | grep 재보정`)의 기준이다 — 기준 전환도 드리프트를 씻는 순간이라 같이 잡히게 둔다.
	UE_LOG(LogNavCloudResolve, Log,
		TEXT("[NavCloudResolve] #%d 재보정(기준 전환 #%d→#%d) — 드리프트 %.0fcm 보정 · 거리 %.0f→%.0fcm%s%s"),
		Nearest.PointNo, FromNo, Nearest.PointNo, DriftCm, RefD, NearestD,
		GateReason.IsEmpty() ? TEXT("") : TEXT(" · 게이트 통과: "), *GateReason);
	if (bSurveyLogEnabled)
	{
		UE_LOG(LogNavCloudSurvey, Log,
			TEXT("[NavCloudSurvey] REF t=%.2f from=#%d to=#%d d_from=%.0f d_to=%.0f jump_cm=%.0f reason=%s"),
			SurveyTime(), FromNo, Nearest.PointNo, RefD, NearestD, DriftCm, ReasonToken);
	}

	if (DriftCm >= kRelatchToastCm)
	{
		EnsureHud();
		if (Hud != nullptr)
		{
			Hud->ShowMessage(FString::Printf(
				TEXT("%d번 앵커로 기준 전환 (%.0fcm)"), Nearest.PointNo, DriftCm));
		}
	}
}

void UNavCloudResolverSubsystem::RefreshAnchorsIfDue(double Now)
{
	if (NextRefreshTime <= 0.0)
	{
		NextRefreshTime = Now + kRefreshSeconds;   // 첫 로드 직후엔 다시 부르지 않는다.
		return;
	}
	if (Now < NextRefreshTime)
	{
		return;
	}
	NextRefreshTime = Now + kRefreshSeconds;
	NextFetchTime = 0.0;      // 실패 백오프를 무시하고 지금 조회한다.
	FetchBoundAnchors();
}

FVector UNavCloudResolverSubsystem::GetCameraMapLocation() const
{
	const UWorld* World = GetWorld();
	UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Localizer == nullptr || Cam == nullptr)
	{
		return FVector::ZeroVector;
	}
	return Localizer->WorldToMap(Cam->GetCameraLocation());
}

double UNavCloudResolverSubsystem::SurveyTime() const
{
	if (!bSurveyLogEnabled || SurveyStartPlatformSeconds < 0.0)
	{
		return -1.0;
	}
	return FPlatformTime::Seconds() - SurveyStartPlatformSeconds;
}

void UNavCloudResolverSubsystem::TickSurveyLog()
{
	if (!bSurveyLogEnabled)
	{
		return;
	}

	// 형식은 헤더 주석의 계약 그대로 — §G 파서(survey_parse.py)가 이 줄들을 읽는다.
	if (SurveyStartPlatformSeconds < 0.0)
	{
		SurveyStartPlatformSeconds = FPlatformTime::Seconds();
		NextSurveyCamT = 0.0;
		NextSurveyAnchorT = 0.0;
		UE_LOG(LogNavCloudSurvey, Log,
			TEXT("[NavCloudSurvey] SESSION tag=%s map=%s anchor_s=%.2f cam_s=%.2f"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), *MapId,
			SurveyLogIntervalSeconds, SurveyCamIntervalSeconds);
	}
	const double T = SurveyTime();

	if (T >= NextSurveyCamT)
	{
		const double Step = FMath::Max(0.1, static_cast<double>(SurveyCamIntervalSeconds));
		NextSurveyCamT = (NextSurveyCamT + Step > T) ? NextSurveyCamT + Step : T + Step;
		if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			const FVector L = Cam->GetCameraLocation();
			const FRotator R = Cam->GetCameraRotation();
			UE_LOG(LogNavCloudSurvey, Log,
				TEXT("[NavCloudSurvey] CAM t=%.2f world=(%.1f,%.1f,%.1f) yaw=%.2f pitch=%.2f"),
				T, L.X, L.Y, L.Z, R.Yaw, R.Pitch);
		}
	}

	if (T >= NextSurveyAnchorT)
	{
		const double Step = FMath::Max(0.2, static_cast<double>(SurveyLogIntervalSeconds));
		NextSurveyAnchorT = (NextSurveyAnchorT + Step > T) ? NextSurveyAnchorT + Step : T + Step;
		for (FNavCloudResolveEntry& E : Entries)
		{
			const UARPin* Pin = GetTrackingRecognizedPin(E);
			if (Pin == nullptr)
			{
				continue;
			}
			const FTransform X = Pin->GetLocalToWorldTransform();
			const FVector L = X.GetLocation();
			UE_LOG(LogNavCloudSurvey, Log,
				TEXT("[NavCloudSurvey] #%d world=(%.1f,%.1f,%.1f) yaw=%.2f t=%.2f gap=%.2f"),
				E.PointNo, L.X, L.Y, L.Z, X.Rotator().Yaw, T, E.LastGapSeconds);
			E.LastGapSeconds = 0.0;
		}
	}
}

void UNavCloudResolverSubsystem::LogSchedulerSummaryIfDue(double Now)
{
	if (Now < NextSummaryTime)
	{
		return;
	}
	NextSummaryTime = Now + kSummaryIntervalSeconds;

	RecentRequestTimes.RemoveAll([Now](double T) { return Now - T > 60.0; });
	int32 InFlight = 0, Ready = 0, Backoff = 0, Recognized = 0;
	for (const FNavCloudResolveEntry& E : Entries)
	{
		if (E.bRecognized) { ++Recognized; }
		else if (E.Pin != nullptr) { ++InFlight; }
		else if (Now >= E.NextEligibleTime) { ++Ready; }
		else { ++Backoff; }
	}
	UE_LOG(LogNavCloudResolve, Log,
		TEXT("[NavCloudResolve] 스케줄러(D42~D46) — 진행 %d/%d · 대기 %d · 백오프 %d · 인식 %d/%d · 최근60초 요청 %d · 기준 #%d"),
		InFlight, FMath::Max(1, MaxConcurrentResolves), Ready, Backoff, Recognized, Entries.Num(),
		RecentRequestTimes.Num(), RefPointNo);
}

UNavCloudResolveHud* UNavCloudResolverSubsystem::GetSharedHud()
{
	EnsureHud();
	return Hud;
}

void UNavCloudResolverSubsystem::EnsureHud()
{
	if (Hud != nullptr)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	Hud = CreateWidget<UNavCloudResolveHud>(World, UNavCloudResolveHud::StaticClass());
	if (Hud != nullptr)
	{
		Hud->AddToViewport(250); // 미니맵 100 · 안내로그 200 위, 관리자 오버레이 300 아래.
	}
}

#else // !NAV_CLOUD_RESOLVE — Mac 에디터 등 플러그인이 없는 타깃에선 전부 빈 껍데기.

void UNavCloudResolverSubsystem::TryConfigureCloudMode() {}
bool UNavCloudResolverSubsystem::ResolveServerConfig() { return false; }
void UNavCloudResolverSubsystem::FetchBoundAnchors() {}
void UNavCloudResolverSubsystem::ApplyAnchorsJson(const FString& /*Body*/) {}
void UNavCloudResolverSubsystem::StartResolve(FNavCloudResolveEntry& /*Entry*/) {}
void UNavCloudResolverSubsystem::PollResolves() {}
void UNavCloudResolverSubsystem::ScheduleResolves(double /*Now*/) {}
void UNavCloudResolverSubsystem::FailAttempt(FNavCloudResolveEntry& /*Entry*/, double /*Now*/, bool /*bResourceExhausted*/) {}
float UNavCloudResolverSubsystem::EstimateDistanceLowerBoundCm(const FNavCloudResolveEntry& /*Entry*/) const { return -1.f; }
void UNavCloudResolverSubsystem::EnsureHud() {}
UNavCloudResolveHud* UNavCloudResolverSubsystem::GetSharedHud() { return nullptr; }
bool UNavCloudResolverSubsystem::TryLocalizeWithAnchor(FNavCloudResolveEntry& /*Entry*/) { return false; }
void UNavCloudResolverSubsystem::WatchRecognizedAnchor(FNavCloudResolveEntry& /*Entry*/, double /*Now*/) {}
void UNavCloudResolverSubsystem::UpdateReferenceAnchor(double /*Now*/) {}
bool UNavCloudResolverSubsystem::PassJumpGate(int32 /*CandidatePointNo*/, const FVector2D& /*ImpliedMap*/,
	const FVector2D& /*CurrentMap*/, double /*Now*/, FString& /*OutReason*/) { return false; }
bool UNavCloudResolverSubsystem::ComputeImpliedCameraMap(const FNavCloudResolveEntry& /*Entry*/, FVector& /*OutMap*/) const { return false; }
void UNavCloudResolverSubsystem::RefreshAnchorsIfDue(double /*Now*/) {}
FVector UNavCloudResolverSubsystem::GetCameraMapLocation() const { return FVector::ZeroVector; }
void UNavCloudResolverSubsystem::TickSurveyLog() {}
double UNavCloudResolverSubsystem::SurveyTime() const { return -1.0; }
void UNavCloudResolverSubsystem::LogSchedulerSummaryIfDue(double /*Now*/) {}

#endif
