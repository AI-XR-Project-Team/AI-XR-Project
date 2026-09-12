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

	Entries.Reset();
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
		FNavCloudResolveEntry E;
		E.PointNo = static_cast<int32>((*Obj)->GetIntegerField(TEXT("point_no")));
		E.CloudId = CloudId;
		Entries.Add(E);
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

	bAnchorsLoaded = true;
	UE_LOG(LogNavCloudResolve, Log, TEXT("[NavCloudResolve] bound 앵커 %d개 로드"), Entries.Num());
	for (FNavCloudResolveEntry& E : Entries)
	{
		StartResolve(E);
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
		if (E.bRecognized)
		{
			continue; // 이 단계의 산출물은 "인식됨 + 지연" 뿐이다(D18) — 잡은 뒤엔 손대지 않는다.
		}

		UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin);
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
			EnsureHud();
			if (Hud != nullptr)
			{
				Hud->ShowRecognized(E.PointNo, AttemptLatency);
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
				E.bRecognized = true; // 더 시도하지 않는다(포기) — 로그로 남기고 조용히 끝낸다.
				UE_LOG(LogNavCloudResolve, Error,
					TEXT("[NavCloudResolve] #%d 포기 — %d회 시도 실패"), E.PointNo, E.Attempts);
			}
		}
	}
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

#endif
