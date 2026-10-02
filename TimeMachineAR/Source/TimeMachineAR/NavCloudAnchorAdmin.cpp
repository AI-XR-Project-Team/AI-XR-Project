// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavCloudAnchorAdmin.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

// ARCore Cloud Anchors 는 Android 전용이고 NAV_ADMIN_MODE 도 Android 빌드에만 정의된다
// (TimeMachineAR.Build.cs). 그 외(예: Mac 에디터)에선 아래 기능 코드가 전부 빠지고
// 클래스 껍데기만 남아, ShouldCreateSubsystem 이 false 라 생성조차 되지 않는다.
#if NAV_ADMIN_MODE
#include "NavCloudAdminOverlay.h"
#include "NavClient.h"                                   // ServerBaseUrl
#include "ARBlueprintLibrary.h"
#include "ARPin.h"
#include "ARTypes.h"
#include "ARTraceResult.h"
#include "ARTrackable.h"                                 // UARPlaneGeometry
#include "GoogleARCoreServicesFunctionLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNavCloud, Log, All);

#if NAV_ADMIN_MODE
namespace
{
	FString CloudStateName(ECloudARPinCloudState State) { return UEnum::GetValueAsString(State); }
	FString TaskResultName(EARPinCloudTaskResult Result) { return UEnum::GetValueAsString(Result); }

	// 등록 가능해지기까지 필요한 누적 스캔량(이동cm + 회전°*0.4). 실제 품질 수치가
	// 아니라 "충분히 둘러봤는지" proxy. 이동에 가중치를 줘 제자리 회전만으론 안 차게 한다.
	constexpr float kScanTarget = 500.f;
	// 최소 이동량(cm). 이만큼 걸어서 앵커 주위를 돌지 않으면 등록을 막는다(360° 커버리지).
	constexpr float kMinTranslationCm = 200.f;
}
#endif

bool UNavCloudAnchorAdminSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
#if NAV_ADMIN_MODE
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
#else
	return false;
#endif
}

bool UNavCloudAnchorAdminSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavCloudAnchorAdminSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if NAV_ADMIN_MODE
	LoadOffsets(); // 저장된 에셋 오프셋 복원(재시작·재설치에도 유지)
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] 관리자 Cloud Anchor 등록 모드 활성화."));
#endif
}

void UNavCloudAnchorAdminSubsystem::Deinitialize()
{
	PendingPin = nullptr;
#if NAV_ADMIN_MODE
	ExitPreview(); // 리졸브 핀·스폰 에셋 정리
	if (Overlay != nullptr)
	{
		Overlay->RemoveFromParent();
		Overlay = nullptr;
	}
#endif
	Super::Deinitialize();
}

TStatId UNavCloudAnchorAdminSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNavCloudAnchorAdminSubsystem, STATGROUP_Tickables);
}

void UNavCloudAnchorAdminSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if NAV_ADMIN_MODE
	if (UARBlueprintLibrary::GetARSessionStatus().Status != EARSessionStatus::Running)
	{
		return;
	}

	EnsureOverlay();
	TryConfigureCloudMode();

	if (!bConfigResolved)
	{
		bConfigResolved = ResolveServerConfig();
	}
	if (bConfigResolved && !bPointsRequested)
	{
		bPointsRequested = true;
		FetchPoints();
	}

	PollPendingHost();
	if (bPreviewMode)
	{
		TickPreview();       // 리졸브→에셋 배치→위치 보정
	}
	else if (PendingPin == nullptr && !bBindInFlight)
	{
		AccumulateScan();    // 처리 중이 아닐 때만 스캔량 누적
	}
	UpdateLiveStatus();      // 매 틱 스캔/호스팅/미리보기 상태 표시
#endif
}

// ── 버튼 핸들러: UFUNCTION 이라 모든 플랫폼에서 정의돼야 한다(UHT 썽크 참조). ──────
void UNavCloudAnchorAdminSubsystem::OnPrevClicked()
{
#if NAV_ADMIN_MODE
	SelectAdjacent(-1);
#endif
}

void UNavCloudAnchorAdminSubsystem::OnNextClicked()
{
#if NAV_ADMIN_MODE
	SelectAdjacent(+1);
#endif
}

void UNavCloudAnchorAdminSubsystem::OnRegisterClicked()
{
#if NAV_ADMIN_MODE
	RegisterAtCrosshair();
#endif
}

void UNavCloudAnchorAdminSubsystem::OnApplyServerUrl()
{
#if NAV_ADMIN_MODE
	if (Overlay == nullptr) { return; }
	FString Url = Overlay->GetServerUrlText();
	Url.TrimStartAndEndInline();
	while (Url.EndsWith(TEXT("/"))) { Url.LeftChopInline(1); }
	if (!Url.IsEmpty() && !Url.StartsWith(TEXT("http")))
	{
		Url = TEXT("http://") + Url; // 스킴 생략 시 보정
	}
	SavedServerUrl = Url;
	SaveOffsets(); // 서버주소도 같은 SaveGame 에 저장
	// 설정 다시 풀어 새 주소로 재연결 + 목록 재요청.
	bConfigResolved = false;
	bPointsRequested = false;
	bPointsLoaded = false;
	Points.Reset();
	FlashResult(Url.IsEmpty() ? TEXT("서버 주소 비움 — 기본값 사용") : FString::Printf(TEXT("서버 주소 적용: %s"), *Url), FColor::Cyan);
#endif
}

void UNavCloudAnchorAdminSubsystem::OnPreviewClicked()
{
#if NAV_ADMIN_MODE
	bPreviewMode = !bPreviewMode;
	if (bPreviewMode) { EnterPreview(); }
	else              { ExitPreview(); }
	if (Overlay != nullptr)
	{
		Overlay->SetPreviewActive(bPreviewMode);
		Overlay->SetNudgeVisible(bPreviewMode);
	}
#endif
}

// 오프셋 보정 버튼 — UFUNCTION 이라 모든 플랫폼에서 정의(썽크 참조).
void UNavCloudAnchorAdminSubsystem::OnNudgeFwd()   {
#if NAV_ADMIN_MODE
	ApplyNudge(0);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnNudgeBack()  {
#if NAV_ADMIN_MODE
	ApplyNudge(1);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnNudgeLeft()  {
#if NAV_ADMIN_MODE
	ApplyNudge(2);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnNudgeRight() {
#if NAV_ADMIN_MODE
	ApplyNudge(3);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnNudgeUp()    {
#if NAV_ADMIN_MODE
	ApplyNudge(4);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnNudgeDown()  {
#if NAV_ADMIN_MODE
	ApplyNudge(5);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnRotCCW()     {
#if NAV_ADMIN_MODE
	ApplyNudge(6);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnRotCW()      {
#if NAV_ADMIN_MODE
	ApplyNudge(7);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnScaleUp()    {
#if NAV_ADMIN_MODE
	ApplyNudge(8);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnScaleDown()  {
#if NAV_ADMIN_MODE
	ApplyNudge(9);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnResetOffset(){
#if NAV_ADMIN_MODE
	ApplyNudge(10);
#endif
}
void UNavCloudAnchorAdminSubsystem::OnStepToggle() {
#if NAV_ADMIN_MODE
	ApplyNudge(11);
#endif
}

#if NAV_ADMIN_MODE

void UNavCloudAnchorAdminSubsystem::EnsureOverlay()
{
	if (Overlay != nullptr)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	Overlay = CreateWidget<UNavCloudAdminOverlay>(World, UNavCloudAdminOverlay::StaticClass());
	if (Overlay == nullptr)
	{
		return;
	}
	Overlay->AddToViewport(300); // 앱 UI(미니맵 100·로그 200) 위

	// 버튼 OnClicked 구독(버튼은 오버레이가 RebuildWidget 에서 생성).
	if (Overlay->PrevButton)     { Overlay->PrevButton->OnClicked.AddDynamic(this, &UNavCloudAnchorAdminSubsystem::OnPrevClicked); }
	if (Overlay->NextButton)     { Overlay->NextButton->OnClicked.AddDynamic(this, &UNavCloudAnchorAdminSubsystem::OnNextClicked); }
	if (Overlay->RegisterButton) { Overlay->RegisterButton->OnClicked.AddDynamic(this, &UNavCloudAnchorAdminSubsystem::OnRegisterClicked); }
	if (Overlay->PreviewButton)  { Overlay->PreviewButton->OnClicked.AddDynamic(this, &UNavCloudAnchorAdminSubsystem::OnPreviewClicked); }

	// 오프셋 보정 버튼 구독.
	if (Overlay->NudgeFwd)   { Overlay->NudgeFwd->OnClicked.AddDynamic(this,   &UNavCloudAnchorAdminSubsystem::OnNudgeFwd); }
	if (Overlay->NudgeBack)  { Overlay->NudgeBack->OnClicked.AddDynamic(this,  &UNavCloudAnchorAdminSubsystem::OnNudgeBack); }
	if (Overlay->NudgeLeft)  { Overlay->NudgeLeft->OnClicked.AddDynamic(this,  &UNavCloudAnchorAdminSubsystem::OnNudgeLeft); }
	if (Overlay->NudgeRight) { Overlay->NudgeRight->OnClicked.AddDynamic(this, &UNavCloudAnchorAdminSubsystem::OnNudgeRight); }
	if (Overlay->NudgeUp)    { Overlay->NudgeUp->OnClicked.AddDynamic(this,    &UNavCloudAnchorAdminSubsystem::OnNudgeUp); }
	if (Overlay->NudgeDown)  { Overlay->NudgeDown->OnClicked.AddDynamic(this,  &UNavCloudAnchorAdminSubsystem::OnNudgeDown); }
	if (Overlay->RotCCW)     { Overlay->RotCCW->OnClicked.AddDynamic(this,     &UNavCloudAnchorAdminSubsystem::OnRotCCW); }
	if (Overlay->RotCW)      { Overlay->RotCW->OnClicked.AddDynamic(this,      &UNavCloudAnchorAdminSubsystem::OnRotCW); }
	if (Overlay->ScaleUp)    { Overlay->ScaleUp->OnClicked.AddDynamic(this,    &UNavCloudAnchorAdminSubsystem::OnScaleUp); }
	if (Overlay->ScaleDown)  { Overlay->ScaleDown->OnClicked.AddDynamic(this,  &UNavCloudAnchorAdminSubsystem::OnScaleDown); }
	if (Overlay->ResetBtn)   { Overlay->ResetBtn->OnClicked.AddDynamic(this,   &UNavCloudAnchorAdminSubsystem::OnResetOffset); }
	if (Overlay->StepBtn)    { Overlay->StepBtn->OnClicked.AddDynamic(this,    &UNavCloudAnchorAdminSubsystem::OnStepToggle); }
	if (Overlay->ApplyUrlBtn){ Overlay->ApplyUrlBtn->OnClicked.AddDynamic(this, &UNavCloudAnchorAdminSubsystem::OnApplyServerUrl); }

	// 서버주소 입력칸을 현재 사용 중인 주소로 채운다(저장값 우선, 없으면 ini 기본).
	{
		FString Shown = SavedServerUrl;
		if (Shown.IsEmpty())
		{
			const UWorld* W = GetWorld();
			if (const UGameInstance* GI = W ? W->GetGameInstance() : nullptr)
			{
				if (const UNavClient* Nav = GI->GetSubsystem<UNavClient>()) { Shown = Nav->GetServerBaseUrl(); }
			}
		}
		Overlay->SetServerUrlText(Shown);
	}

	ComposeBody(TEXT("초기화 중…"), FColor::Silver);
}

bool UNavCloudAnchorAdminSubsystem::ResolveServerConfig()
{
	// 사용자가 앱에서 입력한 주소가 있으면 우선(IP 안 굳게). 없으면 NavClient(ini) 기본값.
	if (!SavedServerUrl.IsEmpty())
	{
		ServerBaseUrl = SavedServerUrl;
	}
	else
	{
		const UWorld* World = GetWorld();
		if (const UGameInstance* GI = World ? World->GetGameInstance() : nullptr)
		{
			if (const UNavClient* Nav = GI->GetSubsystem<UNavClient>())
			{
				ServerBaseUrl = Nav->GetServerBaseUrl();
			}
		}
	}
	if (GConfig != nullptr)
	{
		GConfig->GetString(TEXT("/Script/TimeMachineAR.NavClient"),
			TEXT("DefaultMapId"), MapId, GGameIni);
	}
	ServerBaseUrl.TrimEndInline();
	MapId.TrimStartAndEndInline();

	if (ServerBaseUrl.IsEmpty() || MapId.IsEmpty())
	{
		UE_LOG(LogNavCloud, Warning,
			TEXT("[NavCloudAdmin] 서버 설정 없음 (ServerBaseUrl='%s' MapId='%s')"), *ServerBaseUrl, *MapId);
		return false;
	}
	return true;
}

void UNavCloudAnchorAdminSubsystem::FetchPoints()
{
	if (ServerBaseUrl.IsEmpty() || MapId.IsEmpty())
	{
		return;
	}
	const FString Url = FString::Printf(
		TEXT("%s/maps/%s/cloud-anchors?state=all"), *ServerBaseUrl, *MapId);

	TWeakObjectPtr<UNavCloudAnchorAdminSubsystem> WeakThis(this);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	Req->SetTimeout(5.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			UNavCloudAnchorAdminSubsystem* Self = WeakThis.Get();
			if (Self == nullptr) { return; }
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
				Self->FlashResult(FString::Printf(TEXT("포인트 목록 실패 (HTTP %d) — 서버/시딩 확인"), Code), FColor::Orange);
				UE_LOG(LogNavCloud, Warning, TEXT("[NavCloudAdmin] GET points 실패 code=%d"), Code);
				return;
			}
			Self->ApplyPointsJson(Resp->GetContentAsString());
		});
	Req->ProcessRequest();
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] GET %s"), *Url);
}

void UNavCloudAnchorAdminSubsystem::ApplyPointsJson(const FString& Body)
{
	Points.Reset();

	TArray<TSharedPtr<FJsonValue>> Arr;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Arr))
	{
		FlashResult(TEXT("포인트 목록 파싱 실패"), FColor::Orange);
		return;
	}

	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj->IsValid())
		{
			continue;
		}
		FNavCloudAdminPoint P;
		P.PointNo = static_cast<int32>((*Obj)->GetIntegerField(TEXT("point_no")));
		(*Obj)->TryGetStringField(TEXT("edge"), P.Edge);
		(*Obj)->TryGetStringField(TEXT("label"), P.Label);
		FString CloudId;
		P.bBound = (*Obj)->TryGetStringField(TEXT("cloud_id"), CloudId) && !CloudId.IsEmpty();
		P.CloudId = CloudId;
		bool bIsBound = false;
		if ((*Obj)->TryGetBoolField(TEXT("is_bound"), bIsBound)) { P.bBound = bIsBound; }
		Points.Add(P);
	}
	Points.Sort([](const FNavCloudAdminPoint& A, const FNavCloudAdminPoint& B) { return A.PointNo < B.PointNo; });
	bPointsLoaded = true;

	SelectedIdx = INDEX_NONE;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		if (!Points[i].bBound) { SelectedIdx = i; break; }
	}
	if (SelectedIdx == INDEX_NONE && Points.Num() > 0) { SelectedIdx = 0; }

	FlashResult(FString::Printf(TEXT("포인트 %d개 로드됨"), Points.Num()), FColor::Cyan);
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] 포인트 %d개 로드"), Points.Num());
}

void UNavCloudAnchorAdminSubsystem::SelectAdjacent(int32 Dir)
{
	if (Points.Num() == 0) { return; }
	const int32 N = Points.Num();
	int32 Idx = (SelectedIdx == INDEX_NONE) ? 0 : ((SelectedIdx + Dir) % N + N) % N;
	SelectedIdx = Idx;
	// 새 지점이므로 스캔을 처음부터 다시 하게 초기화.
	ScanAmount = 0.f;
	TransTotal = 0.f;
	bHasLastCam = false;
	if (const FNavCloudAdminPoint* P = SelectedPoint())
	{
		FlashResult(FString::Printf(TEXT("선택: #%d %s — 그 자리 주변을 둘러보며 스캔하세요"), P->PointNo, *P->Edge), FColor::Yellow);
	}
}

void UNavCloudAnchorAdminSubsystem::AccumulateScan()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
	{
		return;
	}
	// 트래킹이 끊기면 이동량을 이어붙이지 않는다(튀는 값 방지).
	if (UARBlueprintLibrary::GetTrackingQuality() != EARTrackingQuality::OrientationAndPosition)
	{
		bHasLastCam = false;
		return;
	}
	const FVector Pos = PC->PlayerCameraManager->GetCameraLocation();
	const float   Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
	if (bHasLastCam)
	{
		const float dYaw = FMath::Abs(FRotator::NormalizeAxis(Yaw - LastCamYaw)); // °
		const float dPos = FVector::Dist(Pos, LastCamPos);                        // cm
		// 이동(걸어 돌기)에 큰 가중, 회전은 작게 → 제자리 회전만으론 잘 안 참.
		ScanAmount = FMath::Min(ScanAmount + dPos * 1.0f + dYaw * 0.4f, kScanTarget * 1.5f);
		TransTotal += dPos;
	}
	LastCamPos = Pos;
	LastCamYaw = Yaw;
	bHasLastCam = true;
}

void UNavCloudAnchorAdminSubsystem::RegisterAtCrosshair()
{
	if (PendingPin != nullptr || bBindInFlight)
	{
		FlashResult(TEXT("이미 진행 중…"), FColor::Yellow);
		return;
	}
	if (!bCloudConfigured)
	{
		FlashResult(TEXT("Cloud 모드 준비 중 — 잠시 후 다시"), FColor::Orange);
		return;
	}
	// ── 스캔 게이트: 충분히 둘러봤을 때만 등록 허용(품질 막대 대용) ──
	if (UARBlueprintLibrary::GetTrackingQuality() != EARTrackingQuality::OrientationAndPosition)
	{
		FlashResult(TEXT("트래킹 약함 — 천천히 움직여 주변을 잡으세요"), FColor::Orange);
		return;
	}
	const float Progress = FMath::Clamp(ScanAmount / kScanTarget, 0.f, 1.f);
	if (Progress < 1.f)
	{
		FlashResult(FString::Printf(TEXT("더 둘러보며 스캔하세요 (%.0f%%)"), Progress * 100.f), FColor::Orange);
		return;
	}
	if (TransTotal < kMinTranslationCm)
	{
		// 제자리 회전만으론 360° 커버리지가 안 나온다 — 걸어서 돌게 강제.
		FlashResult(TEXT("제자리 회전 말고 앵커 주위를 걸어서 한 바퀴 도세요"), FColor::Orange);
		return;
	}

	// 화면 중앙(조준점) 좌표.
	FVector2D ViewportSize(0.f, 0.f);
	if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(ViewportSize); }
	const FVector2D Center = ViewportSize * 0.5f;

	const TArray<FARTraceResult> Hits = UARBlueprintLibrary::LineTraceTrackedObjects(
		Center, /*bTestFeaturePoints=*/false, /*bTestGroundPlane=*/true,
		/*bTestPlaneExtents=*/true, /*bTestPlaneBoundaryPolygon=*/true);
	if (Hits.Num() == 0)
	{
		FlashResult(TEXT("바닥을 못 찾음 — 조준점을 바닥에 두고 더 비추세요"), FColor::Orange);
		return;
	}

	UARPin* Pin = UARBlueprintLibrary::PinComponentToTraceResult(nullptr, Hits[0], FName(TEXT("NavCloudHostPin")));
	if (Pin == nullptr)
	{
		FlashResult(TEXT("앵커 핀 생성 실패"), FColor::Red);
		return;
	}

	HostAnchorYawDeg = Pin->GetLocalToWorldTransform().Rotator().Yaw;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	HostCameraYawDeg = (PC && PC->PlayerCameraManager) ? PC->PlayerCameraManager->GetCameraRotation().Yaw : 0.f;

	EARPinCloudTaskResult TaskResult = EARPinCloudTaskResult::Failed;
	UCloudARPin* Cloud = UGoogleARCoreServicesFunctionLibrary::CreateAndHostCloudARPin(Pin, /*LifetimeInDays=*/1, TaskResult);
	if (TaskResult != EARPinCloudTaskResult::Started || Cloud == nullptr)
	{
		FlashResult(FString::Printf(TEXT("호스팅 시작 실패: %s"), *TaskResultName(TaskResult)), FColor::Red);
		UARBlueprintLibrary::RemovePin(Pin);
		return;
	}

	PendingPin = Cloud;
	HostStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] 호스팅 시작 anchorYaw=%.1f camYaw=%.1f"), HostAnchorYawDeg, HostCameraYawDeg);
}

void UNavCloudAnchorAdminSubsystem::PollPendingHost()
{
	UCloudARPin* Cloud = Cast<UCloudARPin>(PendingPin);
	if (Cloud == nullptr)
	{
		return;
	}

	const ECloudARPinCloudState State = Cloud->GetARPinCloudState();
	if (State == ECloudARPinCloudState::NotHosted || State == ECloudARPinCloudState::InProgress)
	{
		return;
	}

	if (State == ECloudARPinCloudState::Success)
	{
		const FString CloudId = Cloud->GetCloudID();
		const float DeltaDeg = FRotator::NormalizeAxis(90.f - HostCameraYawDeg);
		const float HeadingDeg = FRotator::NormalizeAxis(HostAnchorYawDeg + DeltaDeg);
		UE_LOG(LogNavCloud, Log,
			TEXT("[NavCloudAdmin] HOST OK cloud_id=%s anchorYaw=%.2f camYaw=%.2f delta=%.2f heading=%.2f"),
			*CloudId, HostAnchorYawDeg, HostCameraYawDeg, DeltaDeg, HeadingDeg);

		const FNavCloudAdminPoint* P = SelectedPoint();
		if (P != nullptr && bConfigResolved)
		{
			FlashResult(FString::Printf(TEXT("✅ 호스팅 성공 → #%d 서버 등록 중…"), P->PointNo), FColor::Green);
			BindSelectedPoint(CloudId, HeadingDeg);
		}
		else
		{
			FlashResult(FString::Printf(TEXT("✅ 호스팅 성공 (포인트 미선택 — bind 생략) cloud_id=%s"), *CloudId), FColor::Green);
		}
	}
	else
	{
		FlashResult(FString::Printf(TEXT("❌ 호스팅 실패: %s — 더 스캔하고 다시 등록"), *CloudStateName(State)), FColor::Red);
		UE_LOG(LogNavCloud, Warning, TEXT("[NavCloudAdmin] HOST FAIL state=%s"), *CloudStateName(State));
	}
	PendingPin = nullptr;
	// 다음 등록을 위해 스캔량 초기화(다시 둘러봐야 게이트가 열린다).
	ScanAmount = 0.f;
	TransTotal = 0.f;
	bHasLastCam = false;
}

void UNavCloudAnchorAdminSubsystem::BindSelectedPoint(const FString& CloudId, float HeadingDeg)
{
	const FNavCloudAdminPoint* P = SelectedPoint();
	if (P == nullptr || ServerBaseUrl.IsEmpty() || MapId.IsEmpty())
	{
		return;
	}
	const int32 PointNo = P->PointNo;
	const FString Url = FString::Printf(
		TEXT("%s/maps/%s/cloud-anchors/points/%d/bind"), *ServerBaseUrl, *MapId, PointNo);

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("cloud_id"), CloudId);
	Json->SetNumberField(TEXT("heading_deg"), HeadingDeg);
	FString OutStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Json, Writer);

	bBindInFlight = true;
	const FString BoundCloudId = CloudId; // 콜백에서 로컬 목록에 저장(미리보기 리졸브에 필요)
	TWeakObjectPtr<UNavCloudAnchorAdminSubsystem> WeakThis(this);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("PUT"));
	Req->SetURL(Url);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetContentAsString(OutStr);
	Req->SetTimeout(5.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis, PointNo, BoundCloudId](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			UNavCloudAnchorAdminSubsystem* Self = WeakThis.Get();
			if (Self == nullptr) { return; }
			Self->bBindInFlight = false;
			const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
			if (bOk && Code == 200)
			{
				for (FNavCloudAdminPoint& Pt : Self->Points)
				{
					if (Pt.PointNo == PointNo) { Pt.bBound = true; Pt.CloudId = BoundCloudId; break; }
				}
				Self->FlashResult(FString::Printf(TEXT("✅ #%d 등록 완료!"), PointNo), FColor::Green);
				UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] BIND OK point=%d"), PointNo);
			}
			else
			{
				Self->FlashResult(FString::Printf(TEXT("⚠️ #%d 서버 등록 실패 (HTTP %d) — 다시 등록"), PointNo, Code), FColor::Orange);
				UE_LOG(LogNavCloud, Warning, TEXT("[NavCloudAdmin] BIND FAIL point=%d code=%d"), PointNo, Code);
			}
		});
	Req->ProcessRequest();
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] PUT %s heading=%.2f"), *Url, HeadingDeg);
}

void UNavCloudAnchorAdminSubsystem::TryConfigureCloudMode()
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
		UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] Cloud Anchor 모드 ON"));
	}
}

const FNavCloudAdminPoint* UNavCloudAnchorAdminSubsystem::SelectedPoint() const
{
	return Points.IsValidIndex(SelectedIdx) ? &Points[SelectedIdx] : nullptr;
}

void UNavCloudAnchorAdminSubsystem::FlashResult(const FString& Msg, const FColor& Color)
{
	ResultMsg = Msg;
	ResultColor = Color;
	ResultUntil = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + 6.0; // 6초 고정 표시
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] %s"), *Msg);
}

void UNavCloudAnchorAdminSubsystem::UpdateLiveStatus()
{
	if (Overlay == nullptr)
	{
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// ── 조준점 색 = 스캔/준비 상태 직관 표시 ──
	const EARTrackingQuality Quality = UARBlueprintLibrary::GetTrackingQuality();
	const bool bTracking = (Quality == EARTrackingQuality::OrientationAndPosition);
	int32 PlaneCount = 0;
	for (UARTrackedGeometry* G : UARBlueprintLibrary::GetAllGeometriesByClass(UARPlaneGeometry::StaticClass()))
	{
		if (G && G->GetTrackingState() == EARTrackingState::Tracking) { ++PlaneCount; }
	}

	// 미리보기 중 표시된 에셋 수.
	int32 PreviewShown = 0;
	for (const FNavCloudPreviewEntry& E : PreviewEntries) { if (E.Asset != nullptr) { ++PreviewShown; } }

	const float ScanProgress = FMath::Clamp(ScanAmount / kScanTarget, 0.f, 1.f);
	const bool  bScanReady = bTracking && ScanProgress >= 1.f && TransTotal >= kMinTranslationCm;
	FColor CrossColor;
	if (bPreviewMode)                                    { CrossColor = PreviewShown > 0 ? FColor::Green : FColor::Cyan; }
	else if (PendingPin != nullptr || bBindInFlight)     { CrossColor = FColor::Cyan; }       // 처리 중
	else if (bScanReady)                                 { CrossColor = FColor::Green; }      // 등록 준비됨
	else if (bTracking)                                  { CrossColor = FColor::Yellow; }     // 스캔 중
	else                                                 { CrossColor = FColor(255,80,80); }  // 트래킹 약함
	Overlay->SetCrosshairColor(CrossColor);

	// ── 하단 줄: 결과 고정 표시(6초) > 처리 중 애니메이션 > 스캔 안내 ──
	FString Bottom;
	FColor  BottomColor;
	if (Now < ResultUntil)
	{
		Bottom = ResultMsg;
		BottomColor = ResultColor;
	}
	else if (bPreviewMode)
	{
		Bottom = FString::Printf(TEXT("👁 미리보기 ON — 에셋 %d개 표시 중 (등록한 자리를 비추세요 · 👁 다시 누르면 끔)"), PreviewShown);
		BottomColor = PreviewShown > 0 ? FColor::Green : FColor::Cyan;
	}
	else if (PendingPin != nullptr)
	{
		static const TCHAR* Dots[4] = { TEXT("●○○"), TEXT("○●○"), TEXT("○○●"), TEXT("○●○") };
		const int32 Frame = static_cast<int32>(Now * 3.0) % 4;
		Bottom = FString::Printf(TEXT("⏳ 등록(스캔) 중 %s  (%.0f초) — 그대로 들고 계세요"),
			Dots[Frame], Now - HostStartTime);
		BottomColor = FColor::Yellow;
	}
	else if (bBindInFlight)
	{
		Bottom = TEXT("서버에 등록 중…");
		BottomColor = FColor::Cyan;
	}
	else if (!bPointsLoaded)
	{
		Bottom = TEXT("서버에서 포인트 목록 불러오는 중…");
		BottomColor = FColor::Silver;
	}
	else if (!bTracking)
	{
		Bottom = TEXT("트래킹 약함 — 폰을 천천히 움직여 바닥·주변을 잡으세요");
		BottomColor = FColor(255,140,0);
	}
	else
	{
		// 스캔 게이지(품질 막대 대용): 카메라 움직인 양으로 채운다.
		const int32 Filled = FMath::RoundToInt(ScanProgress * 10.f);
		FString Bar;
		for (int32 i = 0; i < 10; ++i) { Bar += (i < Filled) ? TEXT("▓") : TEXT("░"); }
		if (bScanReady)
		{
			Bottom = FString::Printf(TEXT("스캔 충분 %s 100%% — 바닥을 ＋에 맞추고 [여기 등록]"), *Bar);
			BottomColor = FColor::Green;
		}
		else if (ScanProgress >= 1.f) // 많이 찍었지만 제자리 — 걸어야 함
		{
			Bottom = FString::Printf(TEXT("앵커 주위를 걸어서 도세요 %s (이동 %.0f/%.0fcm)"),
				*Bar, TransTotal, kMinTranslationCm);
			BottomColor = FColor(255,140,0);
		}
		else
		{
			Bottom = FString::Printf(TEXT("주변을 걸어 돌며 스캔 중 %s %.0f%% (평면 %d개)"),
				*Bar, ScanProgress * 100.f, PlaneCount);
			BottomColor = FColor::Yellow;
		}
	}

	ComposeBody(Bottom, BottomColor);
}

void UNavCloudAnchorAdminSubsystem::ComposeBody(const FString& BottomLine, const FColor& Color)
{
	if (Overlay == nullptr)
	{
		return;
	}
	FString Text = TEXT("[Cloud Anchor 등록 — 관리자]\n");
	if (!bPointsLoaded)
	{
		Text += TEXT("포인트 목록 불러오는 중…\n");
	}
	else if (Points.Num() == 0)
	{
		Text += TEXT("포인트 없음 — 서버 시딩 필요\n");
	}
	else
	{
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const FNavCloudAdminPoint& P = Points[i];
			const TCHAR* Mark = (i == SelectedIdx) ? TEXT("▶") : TEXT("  ");
			const TCHAR* Done = P.bBound ? TEXT("✓등록") : TEXT("·미등록");
			Text += FString::Printf(TEXT("%s #%d %s  %s\n"), Mark, P.PointNo,
				P.Edge.IsEmpty() ? TEXT("-") : *P.Edge, Done);
		}
	}
	Text += TEXT("\n") + BottomLine;
	Overlay->SetBody(Text, Color);
}

// ── 에셋 미리보기 (임시: 에셋 정렬 검증) ─────────────────────────────────────
FString UNavCloudAnchorAdminSubsystem::AssetClassPathForLabel(const FString& Label) const
{
	// test1 = 검증용 기존 티라노. test2 = 팀원이 만들 에셋(없으면 티라노로 폴백).
	if (Label.Equals(TEXT("test1"), ESearchCase::IgnoreCase))
	{
		// 쿡에 확실히 포함된 T-Rex 변형(base BP_DinoOverlay 는 패키지에 안 담길 수 있음).
		return TEXT("/Game/Stuff/BluePrint/BP_DinoOverlay_T-Rex.BP_DinoOverlay_T-Rex_C");
	}
	if (Label.Equals(TEXT("test2"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/Stuff/BP_Test2Asset.BP_Test2Asset_C");
	}
	if (Label.Equals(TEXT("test3"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/Stuff/BP_Test3Asset.BP_Test3Asset_C");
	}
	return FString();
}

void UNavCloudAnchorAdminSubsystem::EnterPreview()
{
	ExitPreview(); // 중복 방지
	int32 Started = 0;
	for (const FNavCloudAdminPoint& P : Points)
	{
		if (!P.bBound || P.CloudId.IsEmpty()) { continue; }
		if (AssetClassPathForLabel(P.Label).IsEmpty()) { continue; } // 에셋 매핑 있는 것만(test1/test2)

		EARPinCloudTaskResult Result = EARPinCloudTaskResult::Failed;
		UCloudARPin* Pin = UGoogleARCoreServicesFunctionLibrary::CreateAndResolveCloudARPin(P.CloudId, Result);
		if (Pin != nullptr && Result == EARPinCloudTaskResult::Started)
		{
			FNavCloudPreviewEntry E;
			E.PointNo = P.PointNo;
			E.Label = P.Label;
			E.CloudId = P.CloudId;
			E.Pin = Pin;
			E.ResolveStart = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			PreviewEntries.Add(E);
			++Started;
		}
		else
		{
			UE_LOG(LogNavCloud, Warning, TEXT("[NavCloudAdmin] 리졸브 시작 실패 %s: %s"),
				*P.Label, *TaskResultName(Result));
		}
	}
	// 조정 대상 일관성: 리졸브 시작한 첫 앵커를 선택 상태로.
	if (PreviewEntries.Num() > 0)
	{
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			if (Points[i].PointNo == PreviewEntries[0].PointNo) { SelectedIdx = i; break; }
		}
	}
	FlashResult(Started > 0
		? FString::Printf(TEXT("미리보기 ON — 앵커 %d개 리졸브 중, 등록한 자리를 카메라로 비추세요"), Started)
		: TEXT("미리보기 ON — 리졸브할 test 앵커가 없음(먼저 등록하세요)"), FColor::Cyan);
}

void UNavCloudAnchorAdminSubsystem::ExitPreview()
{
	for (FNavCloudPreviewEntry& E : PreviewEntries)
	{
		if (UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin))
		{
			UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(Pin);
		}
		if (E.Asset != nullptr)
		{
			E.Asset->Destroy();
		}
	}
	PreviewEntries.Reset();
}

void UNavCloudAnchorAdminSubsystem::TickPreview()
{
	UWorld* World = GetWorld();
	if (World == nullptr) { return; }

	const double Now = World->GetTimeSeconds();
	for (FNavCloudPreviewEntry& E : PreviewEntries)
	{
		UCloudARPin* Pin = Cast<UCloudARPin>(E.Pin);
		if (Pin == nullptr) { continue; }

		const ECloudARPinCloudState CState = Pin->GetARPinCloudState();

		// ── 리졸브 자동 재시도: 아직 에셋 안 뜬 상태에서 실패/타임아웃이면 다시 시도 ──
		if (E.Asset == nullptr)
		{
			const bool bError =
				CState == ECloudARPinCloudState::ErrorInternalError ||
				CState == ECloudARPinCloudState::ErrorLocalizationFailure ||
				CState == ECloudARPinCloudState::ErrorServiceUnavailable ||
				CState == ECloudARPinCloudState::ErrorResourceExhausted ||
				CState == ECloudARPinCloudState::ErrorResolvingCloudIDNotFound ||
				CState == ECloudARPinCloudState::Cancelled;
			const bool bTimeout = (CState != ECloudARPinCloudState::Success) && (Now - E.ResolveStart > 12.0);
			if ((bError || bTimeout) && E.Retries < 5 && !E.CloudId.IsEmpty())
			{
				UGoogleARCoreServicesFunctionLibrary::RemoveCloudARPin(Pin);
				EARPinCloudTaskResult R = EARPinCloudTaskResult::Failed;
				UCloudARPin* NewPin = UGoogleARCoreServicesFunctionLibrary::CreateAndResolveCloudARPin(E.CloudId, R);
				E.Pin = NewPin;
				E.ResolveStart = Now;
				++E.Retries;
				UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] 리졸브 재시도 %s (#%d, %d회)"), *E.Label, E.PointNo, E.Retries);
				continue;
			}
		}

		// 리졸브가 성공했고 현재 트래킹 중일 때만 위치를 신뢰한다.
		const bool bResolved = (CState == ECloudARPinCloudState::Success);
		const bool bTracking = (Pin->GetTrackingState() == EARTrackingState::Tracking);
		if (!bResolved || !bTracking) { continue; }

		// 앵커별 오프셋을 얹는다. 회전은 **앵커 지점을 중심**으로 돈다(에셋을 앵커 위로
		// 맞춘 뒤엔 제자리 회전처럼 보인다). 위치 오프셋도 같은 회전을 받아 강체 회전.
		const FNavCloudOffset O = Offsets.FindRef(E.PointNo);
		const FTransform Anchor = Pin->GetLocalToWorldTransform();
		const FQuat FinalRot = Anchor.GetRotation() * FRotator(0.f, O.Yaw, 0.f).Quaternion();
		const FVector FinalPos = Anchor.GetLocation() + FinalRot.RotateVector(O.Loc);
		const FTransform PinTf(FinalRot, FinalPos, FVector(O.Scale));

		if (E.Asset == nullptr)
		{
			const FString Path = AssetClassPathForLabel(E.Label);
			UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, *Path);
			if (Cls == nullptr && !E.Label.Equals(TEXT("test1"), ESearchCase::IgnoreCase))
			{
				// test2 BP 가 아직 없으면 티라노로 폴백(뭐라도 보이게).
				Cls = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Game/Stuff/BluePrint/BP_DinoOverlay_T-Rex.BP_DinoOverlay_T-Rex_C"));
			}
			if (Cls == nullptr)
			{
				UE_LOG(LogNavCloud, Warning, TEXT("[NavCloudAdmin] 에셋 클래스 로드 실패: %s"), *Path);
				continue;
			}
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			E.Asset = World->SpawnActor<AActor>(Cls, PinTf, SP);
			UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] 에셋 스폰 %s (point %d)"), *E.Label, E.PointNo);
			FlashResult(FString::Printf(TEXT("✅ %s 에셋 표시 — 앵커 위에 고정, 계속 보정됨"), *E.Label), FColor::Green);
		}
		else
		{
			// 앵커가 갱신될 때마다 따라가 위치를 계속 보정(+실시간 오프셋 반영).
			E.Asset->SetActorTransform(PinTf);
		}
	}
}

void UNavCloudAnchorAdminSubsystem::ApplyNudge(int32 Code)
{
	static const float Mults[3] = { 1.f, 5.f, 10.f };
	const float Mult = Mults[FMath::Clamp(StepLevel, 0, 2)];
	const float PosStep = Mult;          // cm
	const float RotStep = Mult;          // °
	const float SclStep = Mult * 0.01f;  // %

	// 스텝 전환(선택 포인트 없어도 동작).
	if (Code == 11)
	{
		StepLevel = (StepLevel + 1) % 3;
		if (Overlay != nullptr)
		{
			Overlay->SetStepLabel(FString::Printf(TEXT("스텝×%d"), static_cast<int32>(Mults[StepLevel])));
		}
		FlashResult(FString::Printf(TEXT("스텝 ×%d"), static_cast<int32>(Mults[StepLevel])), FColor::White);
		return;
	}

	// 조정 대상 결정: 미리보기 중이면 **화면에 떠 있는 에셋의 앵커**를 조정한다
	// (그래야 보이는 에셋이 움직인다). 선택 포인트가 표시 중이면 그걸, 아니면 표시 중인
	// 첫 에셋을 대상으로. 미리보기가 아니면 ◀▶ 선택 포인트.
	int32 TargetPointNo = INDEX_NONE;
	if (bPreviewMode)
	{
		const FNavCloudAdminPoint* Sel = SelectedPoint();
		for (const FNavCloudPreviewEntry& E : PreviewEntries)
		{
			if (E.Asset != nullptr && Sel != nullptr && E.PointNo == Sel->PointNo) { TargetPointNo = E.PointNo; break; }
		}
		if (TargetPointNo == INDEX_NONE)
		{
			for (const FNavCloudPreviewEntry& E : PreviewEntries)
			{
				if (E.Asset != nullptr) { TargetPointNo = E.PointNo; break; }
			}
		}
	}
	if (TargetPointNo == INDEX_NONE)
	{
		if (const FNavCloudAdminPoint* P = SelectedPoint()) { TargetPointNo = P->PointNo; }
	}
	if (TargetPointNo == INDEX_NONE)
	{
		FlashResult(TEXT("조정할 에셋이 없어요 — 미리보기로 앵커를 띄우세요"), FColor::Orange);
		return;
	}
	FNavCloudOffset& O = Offsets.FindOrAdd(TargetPointNo);

	switch (Code)
	{
	case 0:  O.Loc.X += PosStep; break;  // 앞(+X)
	case 1:  O.Loc.X -= PosStep; break;  // 뒤
	case 2:  O.Loc.Y -= PosStep; break;  // 좌(-Y)
	case 3:  O.Loc.Y += PosStep; break;  // 우(+Y)
	case 4:  O.Loc.Z += PosStep; break;  // 위
	case 5:  O.Loc.Z -= PosStep; break;  // 아래
	case 6:  O.Yaw -= RotStep; break;    // ↺
	case 7:  O.Yaw += RotStep; break;    // ↻
	case 8:  O.Scale = FMath::Min(O.Scale + SclStep, 50.f); break;
	case 9:  O.Scale = FMath::Max(O.Scale - SclStep, 0.05f); break;
	case 10: O = FNavCloudOffset(); break; // 리셋
	default: break;
	}
	O.Yaw = FRotator::NormalizeAxis(O.Yaw);
	SaveOffsets(); // 변경 즉시 자동 저장

	FlashResult(FString::Printf(
		TEXT("#%d 오프셋 X%.0f Y%.0f Z%.0fcm · 회전 %.0f° · 크기 %.0f%% (스텝×%d·자동저장)"),
		TargetPointNo, O.Loc.X, O.Loc.Y, O.Loc.Z, O.Yaw, O.Scale * 100.f, static_cast<int32>(Mult)),
		FColor::White);
	UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] OFFSET #%d loc=(%.1f,%.1f,%.1f) yaw=%.1f scale=%.3f"),
		TargetPointNo, O.Loc.X, O.Loc.Y, O.Loc.Z, O.Yaw, O.Scale);
}

void UNavCloudAnchorAdminSubsystem::SaveOffsets()
{
	UNavCloudAdminSave* Save = Cast<UNavCloudAdminSave>(
		UGameplayStatics::CreateSaveGameObject(UNavCloudAdminSave::StaticClass()));
	if (Save == nullptr) { return; }
	Save->Offsets = Offsets;
	Save->ServerUrl = SavedServerUrl;
	UGameplayStatics::SaveGameToSlot(Save, TEXT("NavCloudAdminOffsets"), 0);
}

void UNavCloudAnchorAdminSubsystem::LoadOffsets()
{
	if (!UGameplayStatics::DoesSaveGameExist(TEXT("NavCloudAdminOffsets"), 0))
	{
		return;
	}
	if (UNavCloudAdminSave* Save = Cast<UNavCloudAdminSave>(
		UGameplayStatics::LoadGameFromSlot(TEXT("NavCloudAdminOffsets"), 0)))
	{
		Offsets = Save->Offsets;
		SavedServerUrl = Save->ServerUrl;
		UE_LOG(LogNavCloud, Log, TEXT("[NavCloudAdmin] 오프셋 %d개·서버주소='%s' 로드"), Offsets.Num(), *SavedServerUrl);
	}
}

#endif // NAV_ADMIN_MODE
