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
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNavCloudResolve, Log, All);

#if NAV_CLOUD_RESOLVE
namespace
{
	/** 한 시도를 포기하고 다시 요청하기까지의 시간(초). 런북 §E-2. */
	constexpr double kAttemptTimeoutSeconds = 12.0;
	/** 목록 조회 실패 시 재시도 간격(초). */
	constexpr double kFetchRetrySeconds = 10.0;
	/** 한 앵커에 허용할 최대 시도 횟수. 12초×60 ≈ 12분 — 현장 실측 한 세션을 덮는다. */
	constexpr int32 kMaxAttempts = 60;
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
	UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] 리졸브 서브시스템 시작(12단계 §E)."));
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
			return; // 서버 설정이 없으면 12단계 리졸브는 성립하지 않는다(조용히 쉰다).
		}
	}

	if (!bAnchorsLoaded)
	{
		FetchBoundAnchors();
		return;
	}

	RefreshAnchorsIfDue(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	PollResolves();
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
		SeenPoints.Add(PointNo);

		FNavCloudResolveEntry* Existing = Entries.FindByPredicate(
			[PointNo](const FNavCloudResolveEntry& X) { return X.PointNo == PointNo; });

		FNavCloudResolveEntry& E = Existing != nullptr ? *Existing : Entries.AddDefaulted_GetRef();
		const bool bIsNew = (Existing == nullptr);
		const bool bReHosted = !bIsNew && !E.CloudId.Equals(CloudId);

		E.PointNo = PointNo;
		E.CloudId = CloudId;
		// 좌표·heading 은 **매번 서버 값으로 덮는다** — 방향 보정을 서버에서 고치면
		// 앱을 다시 굽지 않아도 다음 재보정부터 그 값이 쓰인다.
		E.PosXCm = ReadNumberField(*Obj, TEXT("pos_x_cm"), 0.f);
		E.PosYCm = ReadNumberField(*Obj, TEXT("pos_y_cm"), 0.f);
		E.PosZCm = ReadNumberField(*Obj, TEXT("pos_z_cm"), 0.f);
		E.HeadingDeg = ReadNumberField(*Obj, TEXT("heading_deg"), 90.f);

		if (bReHosted)
		{
			// 다른 앵커로 재등록됐다 — 들고 있던 핀은 무효다. 처음부터 다시 잡는다.
			if (UCloudARPin* OldPin = Cast<UCloudARPin>(E.Pin))
			{
				UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(OldPin);
			}
			E.Pin = nullptr;
			E.bRecognized = false;
			E.bGaveUp = false;
			E.Attempts = 0;
			E.FirstRequest = 0.0;
		}

		if (bIsNew || bReHosted)
		{
			UE_LOG(LogNavCloudResolve, Log,
				TEXT("[NavCloudResolve] #%d 맵(%.0f, %.0f, %.0f) heading=%.1f° cloud_id=%s%s"),
				E.PointNo, E.PosXCm, E.PosYCm, E.PosZCm, E.HeadingDeg, *E.CloudId,
				bReHosted ? TEXT(" (재등록)") : TEXT(""));
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
		UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] bound 앵커 %d개 로드"), Entries.Num());
	}
	// 아직 핀이 없는 앵커(새로 등록됐거나 재등록된 것)만 리졸브를 건다.
	for (FNavCloudResolveEntry& E : Entries)
	{
		if (E.Pin == nullptr && !E.bRecognized && E.Attempts == 0)
		{
			StartResolve(E);
		}
	}
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
		UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] #%d 리졸브 요청 (%d번째 시도) %s"),
			Entry.PointNo, Entry.Attempts, *Entry.CloudId);
	}
	else
	{
		Entry.Pin = nullptr; // 다음 틱이 다시 시도한다.
		UE_LOG(LogNavCloudResolve, Warning, TEXT("[NavCloudResolve] #%d 리졸브 시작 실패: %s"),
			Entry.PointNo, *TaskResultName(Result));
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
		UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin);

		if (E.bGaveUp)
		{
			continue;
		}
		if (E.bRecognized)
		{
			// 마커가 늘 재탐색되던 것처럼, 잡은 뒤에도 이 앵커를 **계속 지켜본다**.
			WatchRecognizedAnchor(E, Now);
			continue;
		}
		if (Pin == nullptr)
		{
			// 시작 자체가 실패했던 건 — 타임아웃 간격을 지켜 다시 요청한다.
			if (E.Attempts < kMaxAttempts && Now - E.AttemptStart > kAttemptTimeoutSeconds)
			{
				StartResolve(E);
			}
			continue;
		}

		const ECloudARPinCloudState CState = Pin->GetARPinCloudState();
		const bool bTracking = (Pin->GetTrackingState() == EARTrackingState::Tracking);

		// 인식 확정 = 클라우드 상태 Success **그리고** 지금 트래킹 중(런북 §E-2).
		if (CState == ECloudARPinCloudState::Success && bTracking)
		{
			E.bRecognized = true;
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
			continue;
		}

		// 실패/타임아웃이면 이번 시도를 버리고 다시 요청한다.
		const bool bTimedOut = (Now - E.AttemptStart > kAttemptTimeoutSeconds);
		if (IsCloudError(CState) || bTimedOut)
		{
			UE_LOG(LogNavCloudResolve, Warning,
				TEXT("[NavCloudResolve] #%d 시도 %d 실패 — state=%s tracking=%d 이유=%s%s"),
				E.PointNo, E.Attempts, *CloudStateName(CState), bTracking ? 1 : 0,
				*QualityReasonName(UARBlueprintLibrary::GetTrackingQualityReason()),
				bTimedOut ? TEXT(" (타임아웃)") : TEXT(""));

			UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(Pin);
			E.Pin = nullptr;
			if (E.Attempts < kMaxAttempts)
			{
				StartResolve(E);
			}
			else
			{
				E.bGaveUp = true; // 더 시도하지 않는다 — 로그로 남기고 조용히 끝낸다.
				UE_LOG(LogNavCloudResolve, Error,
					TEXT("[NavCloudResolve] #%d 포기 — %d회 시도 실패"), E.PointNo, E.Attempts);
			}
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
		// 이미 서 있는 측위(대개 QR 마커)를 덮지 않는다. 마커가 더 정확하다.
		return false;
	}

	// 서버가 아는 이 앵커의 맵 좌표를 기준점으로 넘긴다. 마커 측위와 같은 식이다.
	const FNavMarker AnchorPose = MakeAnchorPose(Entry);

	const FTransform AnchorWorld = Pin->GetLocalToWorldTransform();
	const bool bNewlyLocalized =
		Localizer->LocalizeFromCloudAnchor(AnchorPose.Code, AnchorWorld, AnchorPose);
	Entry.bLocalizeApplied = true;

	UE_LOG(LogNavCloudResolve, Log,
		TEXT("[NavCloudResolve] #%d 로 측위 %s — 앵커 월드 yaw=%.1f° / 서버 heading=%.1f°"
			 " (heading 이 실측값이 아니면 지도 회전이 그만큼 틀어진다 — 13단계 보정)"),
		Entry.PointNo, bNewlyLocalized ? TEXT("성립") : TEXT("재적용"),
		AnchorWorld.Rotator().Yaw, Entry.HeadingDeg);
	return bNewlyLocalized;
}

void UNavCloudResolverSubsystem::WatchRecognizedAnchor(FNavCloudResolveEntry& Entry, double Now)
{
	UCloudARPin* Pin = Cast<UCloudARPin>(Entry.Pin);

	// ① 핀이 사라졌거나 오류로 죽었으면 **처음부터 다시 잡는다** — 마커가 늘 재탐색
	//    상태였던 것처럼, 앵커도 언제나 다시 잡을 준비를 유지한다.
	if (Pin == nullptr || IsCloudError(Pin->GetARPinCloudState()))
	{
		UE_LOG(LogNavCloudResolve, Warning,
			TEXT("[NavCloudResolve] #%d 핀 무효 (%s) — 리졸브를 다시 건다"),
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
		Entry.LastTrackedTime = 0.0;
		StartResolve(Entry);
		return;
	}

	// ② 지금 안 보이면 아무것도 하지 않는다. LastTrackedTime 을 그대로 둬 gap 이 쌓이게 한다.
	if (Pin->GetTrackingState() != EARTrackingState::Tracking)
	{
		return;
	}

	const double Gap = (Entry.LastTrackedTime > 0.0) ? (Now - Entry.LastTrackedTime) : 0.0;
	Entry.LastTrackedTime = Now;

	UWorld* World = GetWorld();
	UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	if (Localizer == nullptr)
	{
		return;
	}

	// ③ 측위가 비어 있으면(네비 버튼의 ResetLocalization 등) 이 앵커로 다시 세운다.
	if (!Localizer->IsLocalized())
	{
		TryLocalizeWithAnchor(Entry);
		return;
	}

	// ④ 마커로 잡힌 측위는 덮지 않는다 — 마커가 더 정확하다(7단계 앵커 전환이 우선).
	if (!Localizer->IsAnchoredToCloudAnchor())
	{
		return;
	}

	// ⑤ **재관측일 때만** 변환을 다시 세운다(마커의 bRelatchOnReacquire 와 같은 규칙).
	//    매 틱 재계산하면 추적 노이즈가 실려 지도가 떨린다.
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

	const float DriftCm = FVector2D::Distance(
		FVector2D(BeforeMap.X, BeforeMap.Y), FVector2D(AfterMap.X, AfterMap.Y));
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
void UNavCloudResolverSubsystem::EnsureHud() {}
bool UNavCloudResolverSubsystem::TryLocalizeWithAnchor(FNavCloudResolveEntry& /*Entry*/) { return false; }
void UNavCloudResolverSubsystem::WatchRecognizedAnchor(FNavCloudResolveEntry& /*Entry*/, double /*Now*/) {}
void UNavCloudResolverSubsystem::RefreshAnchorsIfDue(double /*Now*/) {}
FVector UNavCloudResolverSubsystem::GetCameraMapLocation() const { return FVector::ZeroVector; }

#endif
