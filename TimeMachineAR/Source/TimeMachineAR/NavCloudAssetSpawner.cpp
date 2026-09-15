// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavCloudAssetSpawner.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NavArCoreConfig.h"                             // 마커 트리거 스위치 · Cloud Anchor 모드 + 마커 이미지 DB 복원
#include "DinoInfoData.h"                                // GetSpeciesOverlayClass(순수 · 플러그인 비의존)
#include "DinoOverlayActor.h"                            // TSubclassOf<ADinoOverlayActor> 비교에 완전한 타입이 필요하다(Mac 빌드 포함)

// ARCore Cloud Anchors 는 Android 전용이라 GoogleARCoreServices 의존도 Android 타깃에만 걸린다
// (TimeMachineAR.Build.cs). NAV_CLOUD_RESOLVE 는 그 조건과 1:1 로 붙어 있는 정의다.
#if NAV_CLOUD_RESOLVE
#include "ARTrackingManager.h"                           // 스캔·마커 인식 델리게이트 — **바인딩**과 공개 ClearOverlay 만 쓴다
#include "DinoOverlayActor.h"                            // 공개 함수(SetDinoInfo·StartReveal)만 부른다 — 수정 0
#include "TimeRevealComponent.h"                         // 회중시계 연출 — 공개 AttachTo 만 부른다(수정 0)
#include "TimeRevealProfile.h"
#include "Components/StaticMeshComponent.h"
#include "NavCloudResolver.h"                            // 라벨 규약 + 토스트 HUD 공유
#include "NavCloudResolveHud.h"
#include "NavClient.h"                                   // ServerBaseUrl
#include "ARBlueprintLibrary.h"
#include "ARPin.h"
#include "ARTrackable.h"                                 // 마커 트리거 — UARTrackedImage
#include "ARTypes.h"
#include "GoogleARCoreServicesFunctionLibrary.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/ConstructorHelpers.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNavAssetSpawn, Log, All);

#if NAV_CLOUD_RESOLVE
// 익명 네임스페이스라도 유니티 빌드에선 NavCloudResolver.cpp 와 한 TU 에 묶이므로 이름을 Spawn* 으로 구분한다.
namespace
{
	/** 한 시도를 포기하고 다시 요청하기까지의 시간(초). 12단계 리졸버와 같은 값. */
	constexpr double kSpawnAttemptTimeoutSeconds = 12.0;
	/** 목록 조회 실패 시 재시도 간격(초). */
	constexpr double kSpawnFetchRetrySeconds = 5.0;
	/**
	 * 한 앵커에 허용할 최대 시도 횟수. 촬영은 앵커 앞에 서서 하므로 12단계(60회=12분)처럼
	 * 길게 붙들 이유가 없다 — 12초×10 ≈ 2분이면 안 잡히는 것이고, 스캔을 다시 누르면 리셋된다.
	 */
	constexpr int32 kSpawnMaxAttempts = 10;

	/** ini 를 못 읽었을 때 쓰는 폴백. Archelon 이 아직 없어도 §C~§G 파이프라인은 돌아야 한다(D25). */
	const TCHAR* kFallbackAssetClassPath =
		TEXT("/Game/Stuff/BluePrint/BP_DinoOverlay_T-Rex.BP_DinoOverlay_T-Rex_C");

	const TCHAR* kConfigSection = TEXT("/Script/TimeMachineAR.NavCloudAssetSpawner");

	/** 서버 주소 폴백 — 폰을 USB 로 맥에 꽂고 `adb reverse tcp:8000 tcp:8000` 하면 이 주소가 맥 서버에 닿는다. */
	const TCHAR* kUsbServerBaseUrl = TEXT("http://127.0.0.1:8000");

	/** 종 데이터 기본값 — ini 에 `AssetDinoInfoPath` 키가 없을 때(팀원 빌드엔 로컬 ini 섹션이 없다). 에셋 앵커·마커 트리거가 띄우는 종. */
	const TCHAR* kSpawnDefaultDinoInfoPath = TEXT("/Game/UI/DinoCard/DA_Dino_Archelon.DA_Dino_Archelon");

	/** 마커 트리거가 본뜨는 에셋 앵커 기본 번호 — neuti4f `asset3`(아르켈론 · 조정 패드로 맞춘 자리). */
	constexpr int32 kSpawnMarkerTemplatePointNo = 203;
	/**
	 * #203 배치 보정 **스냅샷** — 2026-09-15 서버 `GET /maps/385f15c8…/cloud-anchors?state=bound` 값.
	 * 서버에 못 닿는 빌드(팀원 집)는 이 값으로 #203 과 같은 자리·크기에 띄운다. 서버값을 받으면 그걸 쓰고, 다르면 로그로 갱신을 알린다.
	 */
	const TCHAR* kSpawnTemplateSnapshotLabel = TEXT("asset3");
	constexpr double kSpawnTemplateSnapshotXCm = 289.11;
	constexpr double kSpawnTemplateSnapshotYCm = 41.88;
	constexpr double kSpawnTemplateSnapshotZCm = 1.71;
	constexpr float kSpawnTemplateSnapshotYawDeg = 0.f;
	constexpr float kSpawnTemplateSnapshotScale = 1.407f;

	FString SpawnCloudStateName(ECloudARPinCloudState State) { return UEnum::GetValueAsString(State); }
	FString SpawnTaskResultName(EARPinCloudTaskResult Result) { return UEnum::GetValueAsString(Result); }
	FString SpawnQualityReasonName(EARTrackingQualityReason Reason) { return UEnum::GetValueAsString(Reason); }

	/** pydantic 은 Decimal 을 문자열("12.50")로 내보낸다 — 숫자·문자열 둘 다 받는다(테스트 앱 JsonNum 과 같다). */
	double SpawnJsonNum(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Default)
	{
		if (!Obj.IsValid())
		{
			return Default;
		}
		const TSharedPtr<FJsonValue> V = Obj->TryGetField(Field);
		if (!V.IsValid() || V->IsNull())
		{
			return Default;
		}
		if (V->Type == EJson::String)
		{
			const FString Str = V->AsString();
			return Str.IsNumeric() ? FCString::Atod(*Str) : Default;
		}
		double Out = Default;
		return V->TryGetNumber(Out) ? Out : Default;
	}

	/** 목록 행의 `asset_offset`(null 이면 0·0·1) — 앵커 항목과 마커 트리거가 같은 해석을 쓴다. */
	void SpawnReadAssetOffset(const TSharedPtr<FJsonObject>& Row, FVector& OutLoc, float& OutYaw, float& OutScale)
	{
		OutLoc = FVector::ZeroVector;
		OutYaw = 0.f;
		OutScale = 1.f;
		const TSharedPtr<FJsonObject>* OffObj = nullptr;
		if (Row.IsValid() && Row->TryGetObjectField(TEXT("asset_offset"), OffObj) && OffObj != nullptr && OffObj->IsValid())
		{
			OutLoc = FVector(
				SpawnJsonNum(*OffObj, TEXT("x_cm"), 0.0),
				SpawnJsonNum(*OffObj, TEXT("y_cm"), 0.0),
				SpawnJsonNum(*OffObj, TEXT("z_cm"), 0.0));
			OutYaw = static_cast<float>(SpawnJsonNum(*OffObj, TEXT("yaw_deg"), 0.0));
			OutScale = FMath::Clamp(static_cast<float>(SpawnJsonNum(*OffObj, TEXT("scale"), 1.0)), 0.05f, 50.f);
		}
	}

	/**
	 * 핀을 놓는다 — 리졸브가 진행 중이면 **취소까지**(RemoveCloudARPin 만으로는 안 끊겨 ARCore 작업이 쌓인다).
	 * 로직은 리졸버 한 곳에 둔다(NavCloudResolver.h "진행 중 리졸브는 취소해야 끝난다").
	 */
	void ReleaseCloudPin(UWorld* World, UARPin* Pin)
	{
		if (UNavCloudResolverSubsystem* Resolver = World ? World->GetSubsystem<UNavCloudResolverSubsystem>() : nullptr)
		{
			Resolver->ReleaseCloudPin(Pin);
		}
		else if (UCloudARPin* CloudPin = Cast<UCloudARPin>(Pin))
		{
			UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(CloudPin);
		}
	}

	/** 이 상태면 이번 시도는 끝났다(성공 못 함) — 다시 요청해야 한다. */
	bool SpawnIsCloudError(ECloudARPinCloudState State)
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

FTransform UNavCloudAssetSpawner::ComposeAssetTransform(const FTransform& AnchorXf, const FVector& OffLoc, float OffYaw,
	float OffScale, float IniYawDeg, float IniZCm, float IniScale)
{
	// 테스트 앱 ComposeTarget 과 같은 식(헤더 주석) — 위치 보정은 앵커 축, ini z 는 월드 위, yaw 는 더하고 배율은 곱한다.
	FRotator Rot = AnchorXf.Rotator();
	Rot.Yaw += IniYawDeg + OffYaw;
	const FVector Loc = AnchorXf.GetLocation() + AnchorXf.GetRotation().RotateVector(OffLoc) + FVector(0.f, 0.f, IniZCm);
	return FTransform(Rot, Loc, FVector(IniScale * OffScale));
}

UClass* UNavCloudAssetSpawner::GetSpeciesOverlayClass(const UDinoInfoData* Info)
{
	// 마커 흐름(ARTrackingManager.cpp CheckForTrackedImages)과 같은 선택 — 종이 지정한 전용 오버레이가 있으면 그것.
	return (Info != nullptr && Info->CustomOverlayClass != nullptr) ? Info->CustomOverlayClass.Get() : nullptr;
}

FTransform UNavCloudAssetSpawner::GravityAlignMarkerTransform(const FTransform& MarkerXf)
{
	// 증강 이미지 pose(UE) — X = 인쇄물 위쪽 · Y = 오른쪽 · Z = 인쇄면 법선(엔진 GoogleARCoreAPI.cpp ARCoreToUnrealTransform 기저 변환).
	// 앞 = (X − Z) 의 수평 성분: 눕히면 X, 세우면 −Z(보는 사람이 바라보는 쪽). 둘을 더해 기울기가 바뀌어도 방향이 튀지 않게 한다.
	const FQuat Rot = MarkerXf.GetRotation();
	FVector Forward = Rot.GetAxisX() - Rot.GetAxisZ();
	Forward.Z = 0.0;
	if (!Forward.Normalize(1e-4))
	{
		// 인쇄면이 거의 아래를 향한 퇴화 자세 — 위쪽 축만 투영한다.
		Forward = Rot.GetAxisX();
		Forward.Z = 0.0;
		if (!Forward.Normalize(1e-4))
		{
			Forward = FVector::ForwardVector;
		}
	}
	return FTransform(FRotator(0.0, Forward.Rotation().Yaw, 0.0), MarkerXf.GetLocation());
}

bool UNavCloudAssetSpawner::ShouldCreateSubsystem(UObject* Outer) const
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

bool UNavCloudAssetSpawner::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavCloudAssetSpawner::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if NAV_CLOUD_RESOLVE
	LoadSpawnConfig();
	UE_LOG(LogNavAssetSpawn, Log,
		TEXT("[NavAssetSpawn] 에셋 스포너 시작(13-1) — 에셋='%s' 종='%s' DA위치=%s yaw=%.1f° z=%.0fcm scale=%.2f · 에셋지도=%s · 회중시계 보충=%s"),
		*AssetClassPath, AssetDinoInfoPath.IsEmpty() ? TEXT("(BP 기본값)") : *AssetDinoInfoPath,
		bApplyDinoInfoLocation ? TEXT("적용") : TEXT("무시"), SpawnYawOffsetDeg, SpawnZOffsetCm, SpawnScale,
		AssetMapId.IsEmpty() ? TEXT("(네비 지도와 같음)") : *AssetMapId,
		AssetTimeRevealProfilePath.IsEmpty() ? TEXT("(없음 — 종 데이터만)") : *AssetTimeRevealProfilePath);
	UE_LOG(LogNavAssetSpawn, Log,
		TEXT("[NavAssetSpawn] 마커 트리거 %s — 등록 마커를 #%d '%s' 과 같은 조립으로 띄운다(보정 (%.1f,%.1f,%.1f)cm yaw=%.1f scale=%.3f · 서버값을 받으면 교체)"),
		bMarkerTriggers ? TEXT("ON") : TEXT("OFF"), MarkerTemplatePointNo, *TemplateLabel,
		TemplateOffLoc.X, TemplateOffLoc.Y, TemplateOffLoc.Z, TemplateOffYaw, TemplateOffScale);
#endif
}

void UNavCloudAssetSpawner::Deinitialize()
{
#if NAV_CLOUD_RESOLVE
	if (AARTrackingManager* Manager = TrackingManager.Get())
	{
		Manager->OnScanStateChanged.RemoveDynamic(this, &UNavCloudAssetSpawner::HandleScanStateChanged);
		Manager->OnMarkerFound.RemoveDynamic(this, &UNavCloudAssetSpawner::HandleMarkerFound);
	}
	ClearAll();
#endif
	Entries.Reset();
	MarkerEntries.Reset();
	Super::Deinitialize();
}

TStatId UNavCloudAssetSpawner::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNavCloudAssetSpawner, STATGROUP_Tickables);
}

void UNavCloudAssetSpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if NAV_CLOUD_RESOLVE
	// 카메라 전환 등으로 AR 세션이 다시 시작되면 플러그인이 마커 이미지 DB 를 또 버린다 — 세션이 멈춘 틱도 봐야 재시작을 안다.
	if (const UWorld* World = GetWorld())
	{
		NavArCoreConfig::TickKeepMarkerImages(World->GetTimeSeconds());
	}

	// AR 세션이 돌기 전엔 플러그인 호출이 무의미하다. 매니저 탐색도 레벨이 뜬 뒤에 하면 된다.
	if (UARBlueprintLibrary::GetARSessionStatus().Status != EARSessionStatus::Running)
	{
		return;
	}

	TryBindTrackingManager();
	TryConfigureCloudMode();

	// 마커 트리거 — 스캔 중 보이는 등록 마커를 #203 조립으로 띄우고(회차당 하나), 뜬 것은 그 핀을 따라가게 한다.
	if (bScanArmed && bMarkerTriggers && MarkerEntries.Num() == 0)
	{
		PollMarkerTriggers();
	}
	for (FNavAssetAnchorEntry& Marker : MarkerEntries)
	{
		if (!Marker.bGaveUp)
		{
			SpawnOrFollow(Marker);
		}
	}

	// 스캔을 누르기 전엔 아무것도 하지 않는다(D22 — 트리거는 「AR 스캔」 버튼뿐).
	if (Entries.Num() == 0 && !bScanArmed)
	{
		return;
	}

	if (bScanArmed)
	{
		if (!bConfigResolved)
		{
			bConfigResolved = ResolveServerConfig();
			if (!bConfigResolved)
			{
				return; // 서버 주소가 없으면 앵커 목록을 받을 수 없다(조용히 쉰다).
			}
		}
		if (Entries.Num() == 0)
		{
			FetchAssetAnchors();
			return;
		}
	}

	PollResolves();
#endif
}

void UNavCloudAssetSpawner::HandleScanStateChanged(bool bScanning)
{
#if NAV_CLOUD_RESOLVE
	if (bScanning)
	{
		// 스캔 ON = 이 회차의 시작. 지난 회차의 에셋·핀(마커 트리거 포함)을 먼저 걷어낸다(중복 방지).
		// 매니저도 StartScan 에서 자기 오버레이를 먼저 지운다 — 같은 자리에 맞춰 둔다.
		ClearAll();
		bScanArmed = true;
		bFetchInFlight = false;
		NextFetchTime = 0.0;
		FetchFailuresThisScan = 0;
		bToastedFetchFail = false;
		bToastedNoAnchors = false;
		UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] 스캔 ON — asset* 앵커를 찾는다"));
		return;
	}

	// 스캔 OFF = 새 시도를 더 걸지 않는다. 진행 중이던 리졸브와 이미 뜬 에셋은 그대로 둔다
	// (마커를 찾으면 매니저가 스스로 StopScan 을 부른다 — 헤더 주석 참조).
	bScanArmed = false;
	UE_LOG(LogNavAssetSpawn, Log,
		TEXT("[NavAssetSpawn] 스캔 OFF — 새 시도 중단(진행 중 %d건은 끝까지 간다)"), Entries.Num());
#endif
}

void UNavCloudAssetSpawner::HandleMarkerFound(UARPin* Pin, const FTransform& /*MarkerPose*/, const FString& MarkerCode)
{
#if NAV_CLOUD_RESOLVE
	if (!bMarkerTriggers)
	{
		return; // 끄면 매니저가 띄운 것을 그대로 둔다(끈 빌드는 마커 이미지 DB 도 안 되돌려 보통 여기까지 오지 않는다).
	}

	// 매니저가 마커 자리에 종별 공룡을 띄우고 StopScan 을 부른 직후다(ARTrackingManager.cpp CheckForTrackedImages) —
	// #203 조립으로 먼저 띄우고, 띄웠으면 매니저 것을 공개 ClearOverlay 로 걷는다. 같은 틱 안이라 걷기 전 모습은 화면에 안 나온다.
	// 매니저는 스캔이 꺼져 있어 다시 띄우지 않는다. 이미지는 매니저 핀에서 먼저 떠 둔다 — ClearOverlay 가 그 핀을 지운다.
	if (MarkerEntries.Num() == 0)
	{
		UARTrackedImage* Image = (Pin != nullptr) ? Cast<UARTrackedImage>(Pin->GetTrackedGeometry()) : nullptr;
		if (Image == nullptr)
		{
			Image = FindTrackedMarker(MarkerCode);
		}
		if (Image != nullptr)
		{
			SpawnMarkerTrigger(Image);
		}
	}
	if (MarkerEntries.Num() == 0)
	{
		UE_LOG(LogNavAssetSpawn, Warning,
			TEXT("[NavAssetSpawn] 마커 '%s' 를 #%d 조립으로 못 띄웠다(추적 이미지·핀 없음) — 매니저 공룡을 그대로 둔다"),
			*MarkerCode, MarkerTemplatePointNo);
		return;
	}
	if (AARTrackingManager* Manager = TrackingManager.Get())
	{
		Manager->ClearOverlay();
		UE_LOG(LogNavAssetSpawn, Log,
			TEXT("[NavAssetSpawn] 마커 '%s' — 매니저 종별 공룡을 걷고 #%d 조립으로 바꿨다"), *MarkerCode, MarkerTemplatePointNo);
	}
#endif
}

#if NAV_CLOUD_RESOLVE

void UNavCloudAssetSpawner::TryBindTrackingManager()
{
	if (TrackingManager.IsValid())
	{
		return;
	}
	AARTrackingManager* Manager = AARTrackingManager::GetARTrackingManager(this);
	if (Manager == nullptr)
	{
		return; // 레벨이 아직 안 떴다 — 다음 틱에 다시.
	}
	TrackingManager = Manager;
	Manager->OnScanStateChanged.AddDynamic(this, &UNavCloudAssetSpawner::HandleScanStateChanged);
	Manager->OnMarkerFound.AddDynamic(this, &UNavCloudAssetSpawner::HandleMarkerFound);

	// 붙기 전에 이미 스캔 중이었으면(자동 스캔 설정 등) 놓친 ON 을 여기서 따라잡는다.
	if (Manager->IsScanning())
	{
		HandleScanStateChanged(true);
	}
	UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] 스캔 버튼·마커 인식에 연결됨(매니저 수정 0)"));
}

void UNavCloudAssetSpawner::TryConfigureCloudMode()
{
	if (bCloudConfigured)
	{
		return;
	}
	// 플러그인이 이때 버리는 마커 이미지 DB 는 NavArCoreConfig 가 되돌린다(리졸버와 같은 경로 — 먼저 부른 쪽이 켠다).
	if (NavArCoreConfig::EnableCloudAnchorMode(TEXT("NavAssetSpawn")))
	{
		bCloudConfigured = true;
		UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] Cloud Anchor 모드 ON"));
	}
}

bool UNavCloudAssetSpawner::ResolveServerConfig()
{
	// NavClient 는 **읽기만** 한다(공지 트리거 6 회피 — 기존 public 시그니처 변경 없음).
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
	// 13-3 — 에셋 앵커는 네비 지도와 **다른 지도**에 둘 수 있다(AssetMapId). 비우면 예전처럼 네비 지도.
	LoadSpawnConfig();
	if (!AssetMapId.IsEmpty())
	{
		MapId = AssetMapId;
	}

	if (MapId.IsEmpty())
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogNavAssetSpawn, Warning,
				TEXT("[NavAssetSpawn] AssetMapId·DefaultMapId 둘 다 없음 — 에셋 앵커를 받을 수 없다"));
		}
		return false;
	}

	// 현장 네트워크가 자주 바뀐다(Wi-Fi 불량 · IPv6 전용 핫스팟). APK 에 박힌 주소가 틀려도
	// 리빌드 없이 USB + adb reverse 로 살릴 수 있게 127.0.0.1 을 다음 후보로 둔다.
	ServerCandidates.Reset();
	if (!ServerBaseUrl.IsEmpty())
	{
		ServerCandidates.Add(ServerBaseUrl);
	}
	ServerCandidates.AddUnique(FString(kUsbServerBaseUrl));
	ServerCandidateIndex = 0;
	ServerBaseUrl = ServerCandidates[0];
	UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] 서버 후보=[%s] map=%s%s"),
		*FString::Join(ServerCandidates, TEXT(", ")), *MapId,
		AssetMapId.IsEmpty() ? TEXT(" (네비 지도와 같음)") : TEXT(" (에셋 지도 AssetMapId — 네비와 별개)"));
	return true;
}

void UNavCloudAssetSpawner::LoadSpawnConfig()
{
	if (bSpawnConfigLoaded)
	{
		return;
	}
	bSpawnConfigLoaded = true;

	// 키가 없을 때의 기본값 — GetString·GetInt 는 키가 없으면 값을 건드리지 않는다.
	AssetDinoInfoPath = kSpawnDefaultDinoInfoPath;
	MarkerTemplatePointNo = kSpawnMarkerTemplatePointNo;
	if (GConfig != nullptr)
	{
		GConfig->GetString(kConfigSection, TEXT("AssetClassPath"), AssetClassPath, GGameIni);
		GConfig->GetFloat(kConfigSection, TEXT("SpawnYawOffsetDeg"), SpawnYawOffsetDeg, GGameIni);
		GConfig->GetFloat(kConfigSection, TEXT("SpawnZOffsetCm"), SpawnZOffsetCm, GGameIni);
		GConfig->GetFloat(kConfigSection, TEXT("SpawnScale"), SpawnScale, GGameIni);
		GConfig->GetString(kConfigSection, TEXT("AssetDinoInfoPath"), AssetDinoInfoPath, GGameIni);
		GConfig->GetBool(kConfigSection, TEXT("bApplyDinoInfoLocation"), bApplyDinoInfoLocation, GGameIni);
		GConfig->GetString(kConfigSection, TEXT("AssetMapId"), AssetMapId, GGameIni);
		GConfig->GetString(kConfigSection, TEXT("AssetTimeRevealProfilePath"), AssetTimeRevealProfilePath, GGameIni);
		GConfig->GetInt(kConfigSection, TEXT("MarkerTemplatePointNo"), MarkerTemplatePointNo, GGameIni);
	}
	bMarkerTriggers = NavArCoreConfig::AreMarkerTriggersEnabled();
	AssetClassPath.TrimStartAndEndInline();
	AssetDinoInfoPath.TrimStartAndEndInline();
	AssetMapId.TrimStartAndEndInline();
	AssetTimeRevealProfilePath.TrimStartAndEndInline();
	if (AssetClassPath.IsEmpty())
	{
		AssetClassPath = kFallbackAssetClassPath;
		UE_LOG(LogNavAssetSpawn, Warning,
			TEXT("[NavAssetSpawn] ini 에 AssetClassPath 가 없다 — T-Rex 폴백으로 파이프라인만 검증한다"));
	}
	if (SpawnScale <= 0.f)
	{
		SpawnScale = 1.f;
	}

	// 마커 트리거가 서버값을 받기 전에 쓸 보정 — #203 이면 스냅샷(팀원 집에서도 #203 과 같은 자리·크기), 다른 번호면 보정 없음.
	const bool bSnapshot = (MarkerTemplatePointNo == kSpawnMarkerTemplatePointNo);
	TemplateLabel = bSnapshot ? FString(kSpawnTemplateSnapshotLabel) : FString::Printf(TEXT("#%d"), MarkerTemplatePointNo);
	TemplateOffLoc = bSnapshot
		? FVector(kSpawnTemplateSnapshotXCm, kSpawnTemplateSnapshotYCm, kSpawnTemplateSnapshotZCm) : FVector::ZeroVector;
	TemplateOffYaw = bSnapshot ? kSpawnTemplateSnapshotYawDeg : 0.f;
	TemplateOffScale = bSnapshot ? kSpawnTemplateSnapshotScale : 1.f;
}

UClass* UNavCloudAssetSpawner::LoadAssetClass()
{
	if (AssetClass != nullptr)
	{
		return AssetClass;
	}
	LoadSpawnConfig();

	// 문자열 경로 런타임 로드라 하드참조가 없다 — 쿡에서 빠지지 않게 DefaultGame.ini 의
	// +DirectoriesToAlwaysCook 에 에셋 폴더가 들어 있어야 한다(§D 함정).
	AssetClass = LoadClass<AActor>(nullptr, *AssetClassPath);
	if (AssetClass == nullptr && AssetClassPath != kFallbackAssetClassPath)
	{
		UE_LOG(LogNavAssetSpawn, Error,
			TEXT("[NavAssetSpawn] 에셋 로드 실패 '%s' — 경로 오타이거나 쿡에서 빠졌다. T-Rex 로 폴백"),
			*AssetClassPath);
		Toast(TEXT("에셋을 못 찾음 — 경로/쿡 확인"));
		AssetClass = LoadClass<AActor>(nullptr, kFallbackAssetClassPath);
	}
	if (AssetClass == nullptr)
	{
		UE_LOG(LogNavAssetSpawn, Error, TEXT("[NavAssetSpawn] 폴백 에셋도 로드 실패 — 스폰할 것이 없다"));
	}
	return AssetClass;
}

UDinoInfoData* UNavCloudAssetSpawner::LoadDinoInfo()
{
	if (bDinoInfoResolved)
	{
		return DinoInfoAsset;
	}
	bDinoInfoResolved = true;
	LoadSpawnConfig();
	if (AssetDinoInfoPath.IsEmpty())
	{
		return nullptr; // BP 기본값대로 띄운다.
	}
	DinoInfoAsset = LoadObject<UDinoInfoData>(nullptr, *AssetDinoInfoPath);
	if (DinoInfoAsset == nullptr)
	{
		// develop 병합(Archelon 반입) 전이면 DA 가 아예 없다 — BP 기본값으로 뜬다.
		UE_LOG(LogNavAssetSpawn, Error,
			TEXT("[NavAssetSpawn] 종 데이터 로드 실패 '%s' — 병합·경로 확인. BP 기본값으로 띄운다"),
			*AssetDinoInfoPath);
		Toast(TEXT("종 데이터(DA)를 못 찾음 — BP 기본값으로 표시"));
	}
	return DinoInfoAsset;
}

UTimeRevealProfile* UNavCloudAssetSpawner::LoadTimeRevealProfile()
{
	if (bTimeRevealProfileResolved)
	{
		return TimeRevealProfileAsset;
	}
	bTimeRevealProfileResolved = true;
	LoadSpawnConfig();
	if (AssetTimeRevealProfilePath.IsEmpty())
	{
		return nullptr; // 종 데이터에도 ini 에도 없으면 회중시계 없이 띄운다.
	}
	TimeRevealProfileAsset = LoadObject<UTimeRevealProfile>(nullptr, *AssetTimeRevealProfilePath);
	if (TimeRevealProfileAsset == nullptr)
	{
		UE_LOG(LogNavAssetSpawn, Error,
			TEXT("[NavAssetSpawn] 회중시계 설정 로드 실패 '%s' — 경로·쿡 확인. 회중시계 없이 띄운다"), *AssetTimeRevealProfilePath);
	}
	return TimeRevealProfileAsset;
}

void UNavCloudAssetSpawner::OnFetchFailed(const FString& Url, int32 Code)
{
	++FetchFailuresThisScan;
	UE_LOG(LogNavAssetSpawn, Warning, TEXT("[NavAssetSpawn] GET bound 실패 code=%d url=%s"), Code, *Url);

	// 다음 후보로 넘긴다. 폴백으로 넘어갈 땐 바로, 한 바퀴 돌아 처음으로 올 땐 간격(5초)을 지킨다.
	if (ServerCandidates.Num() > 1)
	{
		ServerCandidateIndex = (ServerCandidateIndex + 1) % ServerCandidates.Num();
		ServerBaseUrl = ServerCandidates[ServerCandidateIndex];
		if (ServerCandidateIndex != 0)
		{
			NextFetchTime = 0.0;
		}
	}

	// 후보를 한 바퀴 다 실패했을 때 한 번만 알린다.
	if (bScanArmed && !bToastedFetchFail && FetchFailuresThisScan >= FMath::Max(1, ServerCandidates.Num()))
	{
		bToastedFetchFail = true;
		Toast(TEXT("서버 연결 실패 — 주소 확인 (USB 면 adb reverse tcp:8000 tcp:8000)"));
	}
}

void UNavCloudAssetSpawner::FetchAssetAnchors()
{
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (bFetchInFlight || Now < NextFetchTime)
	{
		return;
	}
	bFetchInFlight = true;
	NextFetchTime = Now + kSpawnFetchRetrySeconds;

	const FString Url = FString::Printf(
		TEXT("%s/maps/%s/cloud-anchors?state=bound"), *ServerBaseUrl, *MapId);

	TWeakObjectPtr<UNavCloudAssetSpawner> WeakThis(this);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	Req->SetTimeout(5.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis, Url](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			UNavCloudAssetSpawner* Self = WeakThis.Get();
			if (Self == nullptr) { return; }
			Self->bFetchInFlight = false;
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
				Self->OnFetchFailed(Url, Code);
				return;
			}
			Self->ApplyAnchorsJson(Resp->GetContentAsString());
		});
	Req->ProcessRequest();
	UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] GET %s"), *Url);
}

void UNavCloudAssetSpawner::ApplyAnchorsJson(const FString& Body)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Arr))
	{
		UE_LOG(LogNavAssetSpawn, Warning, TEXT("[NavAssetSpawn] 앵커 목록 파싱 실패"));
		return;
	}
	// 마커 트리거가 본뜨는 앵커(#203)의 저장값은 스캔이 꺼졌어도 받는다 — 대응표 마커를 잡은 매니저가 StopScan 을 부른다.
	CaptureTemplateOffset(Arr);
	if (!bScanArmed)
	{
		return; // 응답이 오는 사이 스캔이 꺼졌다 — 새로 걸지 않는다.
	}

	int32 Skipped = 0;
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
		FString Label;
		(*Obj)->TryGetStringField(TEXT("label"), Label);

		// D23 — **에셋 앵커만** 여기서 가져간다. 네비 포인트는 리졸버(측위) 몫이다.
		if (!NavCloudAnchorRoles::IsAssetAnchorLabel(Label))
		{
			++Skipped;
			continue;
		}

		const int32 PointNo = static_cast<int32>((*Obj)->GetIntegerField(TEXT("point_no")));
		if (Entries.ContainsByPredicate(
				[PointNo](const FNavAssetAnchorEntry& X) { return X.PointNo == PointNo; }))
		{
			continue;
		}

		FNavAssetAnchorEntry& E = Entries.AddDefaulted_GetRef();
		E.PointNo = PointNo;
		E.CloudId = CloudId;
		E.Label = Label;
		// 서버 배치 보정(테스트 앱 조정 패드가 저장한 값). 없으면(null) 0·0·1 — 예전과 같은 자리에 뜬다.
		SpawnReadAssetOffset(*Obj, E.OffLoc, E.OffYaw, E.OffScale);
		UE_LOG(LogNavAssetSpawn, Log,
			TEXT("[NavAssetSpawn] #%d '%s' cloud_id=%s 보정=(%.1f,%.1f,%.1f)cm yaw=%.1f scale=%.3f"),
			E.PointNo, *E.Label, *E.CloudId, E.OffLoc.X, E.OffLoc.Y, E.OffLoc.Z, E.OffYaw, E.OffScale);
	}

	if (Entries.Num() == 0)
	{
		// 201 이 enabled=false 면 ?state=bound 에 아예 안 나온다(서버가 enabled 로 거른다).
		UE_LOG(LogNavAssetSpawn, Warning,
			TEXT("[NavAssetSpawn] asset* 앵커 0개 (네비 앵커 %d개는 건너뜀) — 서버에서 enabled/label 확인"),
			Skipped);
		if (!bToastedNoAnchors)
		{
			bToastedNoAnchors = true;
			Toast(TEXT("에셋 앵커가 서버에 없음 — enabled/label 확인"));
		}
		return;
	}

	Entries.Sort([](const FNavAssetAnchorEntry& A, const FNavAssetAnchorEntry& B)
		{ return A.PointNo < B.PointNo; });
	UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] asset 앵커 %d개 — 리졸브 시작 (서버=%s)"),
		Entries.Num(), *ServerBaseUrl);
	// 진단: 이 토스트가 떴는데 인식이 안 되면 서버·주소가 아니라 장면·각도·폰 인터넷 쪽이다.
	TArray<FString> Labels;
	for (const FNavAssetAnchorEntry& Entry : Entries)
	{
		Labels.Add(Entry.Label);
	}
	Toast(FString::Printf(TEXT("%s 찾는 중 — 등록한 자리·각도로 비춰 주세요"), *FString::Join(Labels, TEXT(", "))));
	for (FNavAssetAnchorEntry& E : Entries)
	{
		StartResolve(E);
	}
}

void UNavCloudAssetSpawner::CaptureTemplateOffset(const TArray<TSharedPtr<FJsonValue>>& Rows)
{
	if (!bMarkerTriggers)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : Rows)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj->IsValid())
		{
			continue;
		}
		int32 PointNo = 0;
		if (!(*Obj)->TryGetNumberField(TEXT("point_no"), PointNo) || PointNo != MarkerTemplatePointNo)
		{
			continue;
		}
		FVector Loc = FVector::ZeroVector;
		float Yaw = 0.f;
		float Scale = 1.f;
		SpawnReadAssetOffset(*Obj, Loc, Yaw, Scale);
		FString Label;
		(*Obj)->TryGetStringField(TEXT("label"), Label);

		if (!bTemplateFromServer)
		{
			UE_LOG(LogNavAssetSpawn, Log,
				TEXT("[NavAssetSpawn] 마커 트리거 기준 #%d '%s' 서버 저장값 — 보정=(%.2f,%.2f,%.2f)cm yaw=%.2f scale=%.3f"),
				PointNo, *Label, Loc.X, Loc.Y, Loc.Z, Yaw, Scale);
			// 서버에 못 닿는 빌드(팀원 집)는 코드 스냅샷으로 뜬다 — 다르면 스냅샷을 갱신해야 두 곳이 같은 자리에 뜬다.
			const bool bSameAsSnapshot = (MarkerTemplatePointNo != kSpawnMarkerTemplatePointNo)
				|| (Loc.Equals(FVector(kSpawnTemplateSnapshotXCm, kSpawnTemplateSnapshotYCm, kSpawnTemplateSnapshotZCm), 0.05)
					&& FMath::IsNearlyEqual(Yaw, kSpawnTemplateSnapshotYawDeg, 0.05f)
					&& FMath::IsNearlyEqual(Scale, kSpawnTemplateSnapshotScale, 0.0005f));
			if (!bSameAsSnapshot)
			{
				UE_LOG(LogNavAssetSpawn, Warning,
					TEXT("[NavAssetSpawn] #%d 서버 저장값이 코드 스냅샷과 다르다 — 서버 없는 빌드는 옛 자리에 뜬다. NavCloudAssetSpawner.cpp kSpawnTemplateSnapshot* 갱신"),
					PointNo);
			}
		}
		TemplateLabel = Label;
		TemplateOffLoc = Loc;
		TemplateOffYaw = Yaw;
		TemplateOffScale = Scale;
		bTemplateFromServer = true;
		for (FNavAssetAnchorEntry& Marker : MarkerEntries)
		{
			ApplyTemplateOffset(Marker); // 이미 떠 있으면 다음 틱 SpawnOrFollow 가 새 자리로 옮긴다.
		}
		return;
	}
}

void UNavCloudAssetSpawner::StartResolve(FNavAssetAnchorEntry& Entry)
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
		UE_LOG(LogNavAssetSpawn, Log, TEXT("[NavAssetSpawn] #%d 리졸브 요청 (%d번째 시도) %s"),
			Entry.PointNo, Entry.Attempts, *Entry.CloudId);
	}
	else
	{
		Entry.Pin = nullptr; // 다음 틱이 다시 시도한다.
		UE_LOG(LogNavAssetSpawn, Warning, TEXT("[NavAssetSpawn] #%d 리졸브 시작 실패: %s"),
			Entry.PointNo, *SpawnTaskResultName(Result));
	}
}

void UNavCloudAssetSpawner::PollResolves()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	for (FNavAssetAnchorEntry& E : Entries)
	{
		if (E.bGaveUp)
		{
			continue;
		}
		if (IsValid(E.SpawnedActor))
		{
			SpawnOrFollow(E); // 이미 떠 있다 — 앵커를 따라가게만 한다.
			continue;
		}
		E.SpawnedActor = nullptr; // 밖에서 지워졌으면 비워 두고 다시 띄울 수 있게 한다.

		UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin);
		if (Pin == nullptr)
		{
			// 시작 자체가 실패했던 건 — 스캔이 살아 있을 때만 간격을 지켜 다시 요청한다.
			if (bScanArmed && E.Attempts < kSpawnMaxAttempts && Now - E.AttemptStart > kSpawnAttemptTimeoutSeconds)
			{
				StartResolve(E);
			}
			continue;
		}

		const ECloudARPinCloudState CState = Pin->GetARPinCloudState();
		const bool bTracking = (Pin->GetTrackingState() == EARTrackingState::Tracking);

		// 인식 확정 = 클라우드 상태 Success **그리고** 지금 트래킹 중(12단계와 같은 규칙).
		if (CState == ECloudARPinCloudState::Success && bTracking)
		{
			const float AttemptLatency = static_cast<float>(Now - E.AttemptStart);
			const float TotalLatency = static_cast<float>(Now - E.FirstRequest);
			SpawnOrFollow(E);
			if (E.SpawnedActor != nullptr)
			{
				Toast(FString::Printf(TEXT("%s 앵커 인식 (%.1f초) — 에셋 표시"), *E.Label, AttemptLatency));
				UE_LOG(LogNavAssetSpawn, Log,
					TEXT("[NavAssetSpawn] #%d '%s' 인식 — 시도지연 %.1f초 / 전체 %.1f초 / 시도 %d회"),
					E.PointNo, *E.Label, AttemptLatency, TotalLatency, E.Attempts);
			}
			continue;
		}

		// 실패/타임아웃이면 이번 시도를 버리고 다시 요청한다.
		const bool bTimedOut = (Now - E.AttemptStart > kSpawnAttemptTimeoutSeconds);
		if (SpawnIsCloudError(CState) || bTimedOut)
		{
			UE_LOG(LogNavAssetSpawn, Warning,
				TEXT("[NavAssetSpawn] #%d 시도 %d 실패 — state=%s tracking=%d 이유=%s%s"),
				E.PointNo, E.Attempts, *SpawnCloudStateName(CState), bTracking ? 1 : 0,
				*SpawnQualityReasonName(UARBlueprintLibrary::GetTrackingQualityReason()),
				bTimedOut ? TEXT(" (타임아웃)") : TEXT(""));

			ReleaseCloudPin(World, Pin); // 타임아웃이면 아직 진행 중이다 — 취소까지 해야 끊긴다
			E.Pin = nullptr;
			if (bScanArmed && E.Attempts < kSpawnMaxAttempts)
			{
				StartResolve(E);
			}
			else if (E.Attempts >= kSpawnMaxAttempts)
			{
				E.bGaveUp = true;
				UE_LOG(LogNavAssetSpawn, Error,
					TEXT("[NavAssetSpawn] #%d 포기 — %d회 시도 실패"), E.PointNo, E.Attempts);
				Toast(TEXT("앵커 인식 실패 — 등록한 각도로 다시 비춰 주세요"));
			}
		}
	}
}

void UNavCloudAssetSpawner::PollMarkerTriggers()
{
	if (UARTrackedImage* Image = FindTrackedMarker(FString()))
	{
		SpawnMarkerTrigger(Image);
	}
}

UARTrackedImage* UNavCloudAssetSpawner::FindTrackedMarker(const FString& MarkerCode) const
{
	for (UARTrackedGeometry* Geometry : UARBlueprintLibrary::GetAllGeometriesByClass(UARTrackedImage::StaticClass()))
	{
		UARTrackedImage* Image = Cast<UARTrackedImage>(Geometry);
		if (Image == nullptr || Image->GetTrackingState() != EARTrackingState::Tracking || Image->GetDetectedImage() == nullptr)
		{
			continue;
		}
		if (MarkerCode.IsEmpty() || Image->GetDetectedImage()->GetFriendlyName() == MarkerCode)
		{
			return Image;
		}
	}
	return nullptr;
}

bool UNavCloudAssetSpawner::SpawnMarkerTrigger(UARTrackedImage* Image)
{
	const UWorld* World = GetWorld();
	if (Image == nullptr || World == nullptr)
	{
		return false;
	}
	const FString MarkerName = (Image->GetDetectedImage() != nullptr)
		? Image->GetDetectedImage()->GetFriendlyName() : FString(TEXT("(이름없음)"));

	// 매니저처럼 이미지에 핀을 붙인다 — 추적이 다듬어지면 따라가고, 회중시계 연출은 이 핀으로 "안 보임"을 판단한다.
	UARPin* Pin = UARBlueprintLibrary::PinComponent(nullptr, Image->GetLocalToWorldTransform(), Image,
		FName(TEXT("NavAssetMarkerTrigger")));
	if (Pin == nullptr)
	{
		UE_LOG(LogNavAssetSpawn, Warning, TEXT("[NavAssetSpawn] 마커 '%s' 핀 생성 실패 — 다음 틱에 다시"), *MarkerName);
		return false;
	}

	FNavAssetAnchorEntry& Marker = MarkerEntries.AddDefaulted_GetRef();
	Marker.MarkerName = MarkerName;
	Marker.Pin = Pin;
	Marker.FirstRequest = World->GetTimeSeconds();
	Marker.AttemptStart = Marker.FirstRequest;
	ApplyTemplateOffset(Marker);
	UE_LOG(LogNavAssetSpawn, Log,
		TEXT("[NavAssetSpawn] 마커 '%s' → #%d '%s' 트리거 — 보정=(%.1f,%.1f,%.1f)cm yaw=%.1f scale=%.3f (%s)"),
		*MarkerName, Marker.PointNo, *Marker.Label, Marker.OffLoc.X, Marker.OffLoc.Y, Marker.OffLoc.Z,
		Marker.OffYaw, Marker.OffScale, bTemplateFromServer ? TEXT("서버 저장값") : TEXT("코드 스냅샷 — 서버값 미수신"));

	SpawnOrFollow(Marker);
	if (Marker.SpawnedActor != nullptr)
	{
		Toast(FString::Printf(TEXT("마커 %s → #%d 조립으로 에셋 표시"), *MarkerName, Marker.PointNo));
	}
	return true;
}

void UNavCloudAssetSpawner::ApplyTemplateOffset(FNavAssetAnchorEntry& Entry) const
{
	Entry.PointNo = MarkerTemplatePointNo;
	Entry.Label = TemplateLabel;
	Entry.OffLoc = TemplateOffLoc;
	Entry.OffYaw = TemplateOffYaw;
	Entry.OffScale = TemplateOffScale;
}

void UNavCloudAssetSpawner::SpawnOrFollow(FNavAssetAnchorEntry& Entry)
{
	// 클라우드 핀이든 마커 이미지에 붙인 일반 핀이든 같은 조립이다 — 마커 트리거가 #203 과 같은 결과를 내는 자리.
	UARPin* Pin = Entry.Pin;
	UWorld* World = GetWorld();
	if (Pin == nullptr || World == nullptr)
	{
		return;
	}
	// 안 보이는 동안은 마지막 자리에 그대로 둔다(추적이 끊겼다고 에셋이 튀면 안 된다).
	if (Pin->GetTrackingState() != EARTrackingState::Tracking)
	{
		return;
	}

	// 앵커 pose + ini 오프셋(D25). 방향은 앵커 yaw 가 정한다(D26) — 마음에 안 들면
	// 리빌드(십몇 분)보다 **원하는 방향을 보고 앵커를 다시 등록**(자바 도구 30초)하는 게 빠르다.
	// 마커 트리거는 이미지 pose 를 앵커처럼 세운 것(yaw 만 — GravityAlignMarkerTransform)을 앵커 자리로 쓴다.
	const FTransform PinXf = Pin->GetLocalToWorldTransform();
	const FTransform AnchorXf = Entry.MarkerName.IsEmpty() ? PinXf : GravityAlignMarkerTransform(PinXf);
	// + 서버 배치 보정(asset_offset) — 테스트 앱 조정 패드와 같은 합성식. 마커 트리거는 본뜬 앵커(#203)의 값이다.
	const FTransform Target = ComposeAssetTransform(AnchorXf, Entry.OffLoc, Entry.OffYaw, Entry.OffScale,
		SpawnYawOffsetDeg, SpawnZOffsetCm, SpawnScale);
	const FVector Loc = Target.GetLocation();
	const FRotator Rot = Target.Rotator();

	if (IsValid(Entry.SpawnedActor))
	{
		Entry.SpawnedActor->SetActorTransform(Target);
		return;
	}

	const FString Who = Entry.MarkerName.IsEmpty()
		? FString::Printf(TEXT("#%d '%s'"), Entry.PointNo, *Entry.Label)
		: FString::Printf(TEXT("마커 '%s'(#%d '%s' 조립)"), *Entry.MarkerName, Entry.PointNo, *Entry.Label);

	// 종이 전용 오버레이 BP(CustomOverlayClass)를 지정하면 그걸 띄운다 — 마커 흐름(ARTrackingManager.cpp
	// CheckForTrackedImages)과 같은 선택. 없으면 ini 의 AssetClassPath(기본 T-Rex 오버레이 + 종 데이터).
	UDinoInfoData* Info = LoadDinoInfo();
	UClass* Class = GetSpeciesOverlayClass(Info);
	if (Class == nullptr)
	{
		Class = LoadAssetClass();
	}
	if (Class == nullptr)
	{
		Entry.bGaveUp = true; // 스폰할 것이 없다 — 리졸브를 더 돌려도 의미가 없다.
		return;
	}

	// 매니저가 마커로 공룡을 띄울 때와 같은 조립 — 지연 스폰 → SetDinoInfo → FinishSpawning
	// (ARTrackingManager.cpp CheckForTrackedImages). 종을 BeginPlay 보다 먼저 넣어야
	// 살점 머티리얼이 그 종의 메시 기준으로 만들어진다.
	AActor* Spawned = World->SpawnActorDeferred<AActor>(
		Class, Target, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Spawned == nullptr)
	{
		UE_LOG(LogNavAssetSpawn, Error, TEXT("[NavAssetSpawn] %s 스폰 실패 (%s)"),
			*Who, *Class->GetPathName());
		Entry.bGaveUp = true;
		return;
	}
	ADinoOverlayActor* Overlay = Cast<ADinoOverlayActor>(Spawned);
	if (Overlay != nullptr && Info != nullptr)
	{
		Overlay->SetDinoInfo(Info);
	}
	else if (Overlay == nullptr && Info != nullptr)
	{
		UE_LOG(LogNavAssetSpawn, Warning,
			TEXT("[NavAssetSpawn] '%s' 는 DinoOverlayActor 가 아니라 종 데이터를 넣지 않는다"), *Class->GetPathName());
	}
	Spawned->FinishSpawning(Target);

	// DA 의 MeshTransform 위치는 마커 기준 오프셋이다 — DA_Dino_Archelon 은 T-Rex 의 X=125.6cm 를 물려받아
	// 그대로 두면 앵커에서 1.2m 앞에 뜬다. 앵커 흐름에선 앵커 자리가 곧 위치라 **DA 가 준 메시의 위치만** 0 으로
	// 되돌리고 회전·배율(메시 보정)은 그대로 쓴다. BeginPlay 의 ApplySpecies 가 다시 덮으므로 FinishSpawning 뒤에 한다.
	bool bZeroedLocation = false;
	if (Overlay != nullptr && Info != nullptr && !bApplyDinoInfoLocation)
	{
		if (Info->FleshMesh != nullptr && Overlay->FleshMesh != nullptr)
		{
			Overlay->FleshMesh->SetRelativeLocation(FVector::ZeroVector);
			bZeroedLocation = true;
		}
		if (Info->BoneMesh != nullptr && Overlay->BoneMesh != nullptr)
		{
			Overlay->BoneMesh->SetRelativeLocation(FVector::ZeroVector);
			bZeroedLocation = true;
		}
	}

	// 살점은 BeginPlay 에서 알파 0 으로 시작하고, 올리는 응시 로직은 디버그 폰에만 있다(AR 게임모드의
	// BP_ARPawn 엔 없다). 여기서 켜지 않으면 살점이 끝내 안 보인다. 알파 파라미터가 없는 머티리얼이면 무해.
	if (Overlay != nullptr)
	{
		Overlay->StartReveal();
	}

	// 회중시계 연출(TimeReveal) — 마커 흐름(ARTrackingManager.cpp)과 같이 종의 TimeRevealProfile 로 붙인다.
	// 붙으면 공룡을 숨기고 시계를 띄워, 위로 던지면 시간의 문이 열리며 나타난다. 추적 핀은 이 클라우드 앵커(마커 트리거면
	// 이미지 핀)다(2초 넘게 안 보이면 연출이 취소되고 공룡이 바로 보인다). 위의 StartReveal 은 연출이 끝나 원래 머티리얼로
	// 돌아왔을 때 살점 알파가 0 에 남지 않게 먼저 걸어 둔다(연출 중엔 메시가 숨겨져 화면에 영향 없음).
	// 종 데이터에 설정이 없으면 ini AssetTimeRevealProfilePath 로 보충한다(develop DA 가 참조를 잃은 동안의 임시 경로 — 헤더 주석).
	UTimeRevealProfile* RevealProfile = (Info != nullptr && Info->TimeRevealProfile != nullptr)
		? Info->TimeRevealProfile.Get() : LoadTimeRevealProfile();
	bool bTimeReveal = false;
	if (Overlay != nullptr && RevealProfile != nullptr)
	{
		bTimeReveal = UTimeRevealComponent::AttachTo(Overlay, RevealProfile, Pin) != nullptr;
	}
	Entry.SpawnedActor = Spawned;
	UE_LOG(LogNavAssetSpawn, Log,
		TEXT("[NavAssetSpawn] %s 스폰(클래스=%s, 종=%s, 살점표시=%d, DA위치무시=%d, 회중시계=%d) — 월드(%.0f, %.0f, %.0f) yaw=%.1f° scale=%.2f"),
		*Who, *Class->GetPathName(), Info != nullptr ? *Info->GetPathName() : TEXT("BP 기본값"),
		Overlay != nullptr ? 1 : 0, bZeroedLocation ? 1 : 0, bTimeReveal ? 1 : 0, Loc.X, Loc.Y, Loc.Z, Rot.Yaw, Target.GetScale3D().X);
}

void UNavCloudAssetSpawner::ClearAll()
{
	for (FNavAssetAnchorEntry& E : Entries)
	{
		// 우리가 만든 것만 우리가 지운다. 매니저의 오버레이(ClearOverlay)는 건드리지 않는다.
		if (IsValid(E.SpawnedActor))
		{
			E.SpawnedActor->Destroy();
		}
		E.SpawnedActor = nullptr;
		ReleaseCloudPin(GetWorld(), E.Pin);
		E.Pin = nullptr;
	}
	Entries.Reset();

	// 마커 트리거 — 이미지에 붙인 일반 핀이다(클라우드 핀이 아니라 RemovePin 으로 뗀다).
	for (FNavAssetAnchorEntry& Marker : MarkerEntries)
	{
		if (IsValid(Marker.SpawnedActor))
		{
			Marker.SpawnedActor->Destroy();
		}
		Marker.SpawnedActor = nullptr;
		if (Marker.Pin != nullptr)
		{
			UARBlueprintLibrary::RemovePin(Marker.Pin);
		}
		Marker.Pin = nullptr;
	}
	MarkerEntries.Reset();
}

void UNavCloudAssetSpawner::Toast(const FString& Message)
{
	UWorld* World = GetWorld();
	UNavCloudResolverSubsystem* Resolver =
		World ? World->GetSubsystem<UNavCloudResolverSubsystem>() : nullptr;
	if (Resolver == nullptr)
	{
		return; // 리졸버가 없는 빌드에선 로그만 남는다.
	}
	if (UNavCloudResolveHud* SharedHud = Resolver->GetSharedHud())
	{
		SharedHud->ShowMessage(Message);
	}
}

#else // !NAV_CLOUD_RESOLVE — Mac 에디터 등 플러그인이 없는 타깃에선 전부 빈 껍데기.

void UNavCloudAssetSpawner::TryBindTrackingManager() {}
void UNavCloudAssetSpawner::TryConfigureCloudMode() {}
bool UNavCloudAssetSpawner::ResolveServerConfig() { return false; }
void UNavCloudAssetSpawner::LoadSpawnConfig() {}
UClass* UNavCloudAssetSpawner::LoadAssetClass() { return nullptr; }
UDinoInfoData* UNavCloudAssetSpawner::LoadDinoInfo() { return nullptr; }
UTimeRevealProfile* UNavCloudAssetSpawner::LoadTimeRevealProfile() { return nullptr; }
void UNavCloudAssetSpawner::OnFetchFailed(const FString& /*Url*/, int32 /*Code*/) {}
void UNavCloudAssetSpawner::FetchAssetAnchors() {}
void UNavCloudAssetSpawner::ApplyAnchorsJson(const FString& /*Body*/) {}
void UNavCloudAssetSpawner::CaptureTemplateOffset(const TArray<TSharedPtr<FJsonValue>>& /*Rows*/) {}
void UNavCloudAssetSpawner::StartResolve(FNavAssetAnchorEntry& /*Entry*/) {}
void UNavCloudAssetSpawner::PollResolves() {}
void UNavCloudAssetSpawner::PollMarkerTriggers() {}
UARTrackedImage* UNavCloudAssetSpawner::FindTrackedMarker(const FString& /*MarkerCode*/) const { return nullptr; }
bool UNavCloudAssetSpawner::SpawnMarkerTrigger(UARTrackedImage* /*Image*/) { return false; }
void UNavCloudAssetSpawner::ApplyTemplateOffset(FNavAssetAnchorEntry& /*Entry*/) const {}
void UNavCloudAssetSpawner::SpawnOrFollow(FNavAssetAnchorEntry& /*Entry*/) {}
void UNavCloudAssetSpawner::ClearAll() {}
void UNavCloudAssetSpawner::Toast(const FString& /*Message*/) {}

#endif
