// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavMapArOverlay.h"

#include "NavClient.h"                                   // ServerBaseUrl — 읽기만
#include "NavCloudResolver.h"                            // 인식된 핀 pose · 에셋 라벨 규약
#include "NavLocalizer.h"                                // MapToWorld · WorldToMap — 공개 함수만
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogNavMapOverlay, Log, All);

namespace
{
	/** 조회 실패 시 재시도 간격(초). */
	constexpr double kMapFetchRetrySeconds = 10.0;
	/** 앵커 목록 재조회 주기(초) — 서버 보정이 앱 재시작 없이도 들어오게. */
	constexpr double kAnchorRefreshSeconds = 60.0;
	/** 그리기 주기(초). 선 수명은 이보다 조금 길게 잡아 깜빡이지 않게 한다. */
	constexpr double kDrawIntervalSeconds = 0.1;

	const FColor kWallColor(0, 200, 255);
	const FColor kObstacleColor(255, 150, 0);
	const FColor kAnchorColor(255, 225, 0);
	const FColor kResidualColor(255, 40, 40);
	const FColor kGraphColor(60, 255, 120);

	/** 서버 Decimal 은 문자열로 온다(`"2280.00"`). 숫자로 와도 받는다. */
	double ReadNumber(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return 0.0;
		}
		double Number = 0.0;
		if (Value->TryGetNumber(Number))
		{
			return Number;
		}
		FString Text;
		return Value->TryGetString(Text) ? FCString::Atod(*Text) : 0.0;
	}

	double ReadField(const TSharedPtr<FJsonObject>& Obj, const FString& Field)
	{
		return Obj.IsValid() && Obj->HasField(Field) ? ReadNumber(Obj->TryGetField(Field)) : 0.0;
	}
}

bool UNavMapArOverlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || !bEnabled)
	{
		return false; // ini 스위치가 꺼져 있으면 틱조차 돌지 않는다(방문객 빌드 영향 0).
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

bool UNavMapArOverlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNavMapArOverlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNavMapArOverlaySubsystem, STATGROUP_Tickables);
}

void UNavMapArOverlaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	if (!bConfigured)
	{
		TryResolveConfig();
		if (!bConfigured)
		{
			return;
		}
	}
	if (!bGraphInFlight && Now >= NextGraphFetch)
	{
		FetchGraph();
	}
	if (!bAnchorsInFlight && Now >= NextAnchorFetch)
	{
		FetchAnchors();
	}

	const UNavLocalizer* Localizer = World->GetSubsystem<UNavLocalizer>();
	if (Localizer == nullptr || !Localizer->IsLocalized())
	{
		return; // 변환이 서기 전엔 맵 좌표를 월드에 옮길 수 없다.
	}

	if (Now >= NextDraw)
	{
		NextDraw = Now + kDrawIntervalSeconds;
		DrawOverlay();
	}
	if (Now >= NextResidualLog)
	{
		NextResidualLog = Now + FMath::Max(1.0, static_cast<double>(ResidualLogIntervalSeconds));
		LogResiduals(Now);
	}
	if (bLogCameraMapPose && Now >= NextPoseLog)
	{
		NextPoseLog = Now + 1.0;
		if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			const FVector P = Localizer->WorldToMap(Cam->GetCameraLocation());
			// 맵 방위(+X=0°, CCW) = phi − 월드 yaw — 앵커 heading 과 같은 규약(LogResiduals 주석).
			const FVector AlongX = Localizer->MapToWorld(100.0f, 0.0f, 0.0f) - Localizer->MapToWorld(0.0f, 0.0f, 0.0f);
			const double PhiDeg = FMath::RadiansToDegrees(FMath::Atan2(AlongX.Y, AlongX.X));
			const double MapYaw = FMath::Fmod(FMath::Fmod(PhiDeg - Cam->GetCameraRotation().Yaw, 360.0) + 360.0, 360.0);
			UE_LOG(LogNavMapOverlay, Log, TEXT("[NavMapOverlay] POS t=%.1f x=%.0f y=%.0f yaw=%.0f"), Now, P.X, P.Y, MapYaw);
		}
	}
}

void UNavMapArOverlaySubsystem::TryResolveConfig()
{
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
		GConfig->GetString(TEXT("/Script/TimeMachineAR.NavClient"), TEXT("DefaultMapId"), MapId, GGameIni);
	}
	ServerBaseUrl.TrimStartAndEndInline();
	MapId.TrimStartAndEndInline();

	if (ServerBaseUrl.IsEmpty() || MapId.IsEmpty())
	{
		if (!bConfigWarned)
		{
			bConfigWarned = true;
			UE_LOG(LogNavMapOverlay, Warning,
				TEXT("[NavMapOverlay] 서버 설정 없음 (ServerBaseUrl='%s' MapId='%s') — 겹쳐보기 안 함"), *ServerBaseUrl, *MapId);
		}
		return;
	}
	bConfigured = true;
	UE_LOG(LogNavMapOverlay, Log,
		TEXT("[NavMapOverlay] 켜짐 — 서버=%s map=%s · 반경 %.0fcm · 벽 높이 %.0f/%.0fcm · 그래프 %s"),
		*ServerBaseUrl, *MapId, MaxDrawDistanceCm, WallLowCm, WallHighCm, bDrawGraph ? TEXT("ON") : TEXT("OFF"));
}

void UNavMapArOverlaySubsystem::FetchGraph()
{
	bGraphInFlight = true;
	const FString Url = FString::Printf(TEXT("%s/maps/%s/graph"), *ServerBaseUrl, *MapId);

	TWeakObjectPtr<UNavMapArOverlaySubsystem> WeakThis(this);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	Req->SetTimeout(8.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			UNavMapArOverlaySubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bGraphInFlight = false;
			const UWorld* World = Self->GetWorld();
			const double Now = World ? World->GetTimeSeconds() : 0.0;

			TSharedPtr<FJsonObject> Root;
			const bool bHttpOk = bOk && Resp.IsValid() && Resp->GetResponseCode() == 200;
			if (bHttpOk)
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, Root);
			}
			if (!bHttpOk || !Root.IsValid())
			{
				Self->NextGraphFetch = Now + kMapFetchRetrySeconds;
				UE_LOG(LogNavMapOverlay, Warning, TEXT("[NavMapOverlay] GET graph 실패 code=%d — %.0f초 뒤 재시도"),
					Resp.IsValid() ? Resp->GetResponseCode() : -1, kMapFetchRetrySeconds);
				return;
			}

			Self->Outline.Reset();
			Self->Obstacles.Reset();
			Self->GraphEdges.Reset();

			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Root->TryGetArrayField(TEXT("outline"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& Point : *Arr)
				{
					const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
					if (Point.IsValid() && Point->TryGetArray(XY) && XY->Num() >= 2)
					{
						Self->Outline.Add(FVector2D(ReadNumber((*XY)[0]), ReadNumber((*XY)[1])));
					}
				}
			}
			if (Root->TryGetArrayField(TEXT("obstacles"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (V.IsValid() && V->TryGetObject(O))
					{
						FNavOverlayObstacle& Ob = Self->Obstacles.AddDefaulted_GetRef();
						Ob.Min = FVector2D(ReadField(*O, TEXT("x0")), ReadField(*O, TEXT("y0")));
						Ob.Max = FVector2D(ReadField(*O, TEXT("x1")), ReadField(*O, TEXT("y1")));
					}
				}
			}
			TMap<FString, FVector2D> NodePos;
			Self->GraphNodes.Reset();
			if (Root->TryGetArrayField(TEXT("nodes"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					FString NodeId;
					if (V.IsValid() && V->TryGetObject(O) && (*O)->TryGetStringField(TEXT("node_id"), NodeId))
					{
						const FVector2D Pos(ReadField(*O, TEXT("pos_x_cm")), ReadField(*O, TEXT("pos_y_cm")));
						NodePos.Add(NodeId, Pos);
						FNavOverlayNode& Node = Self->GraphNodes.AddDefaulted_GetRef();
						Node.Id = NodeId;
						// UUID 앞 4자리 = 현장 대화용 코드(docs graph_common.py · 2D 미리보기와 같은 값).
						// 한글 라벨은 디버그 글꼴에 없어 네모로 깨진다 — 목적지는 '*' 만 붙인다.
						FString NodeLabel;
						const bool bDestination = (*O)->TryGetStringField(TEXT("label"), NodeLabel) && !NodeLabel.IsEmpty();
						Node.ShortLabel = NodeId.Left(4) + (bDestination ? TEXT("*") : TEXT(""));
						Node.Pos = Pos;
					}
				}
			}
			if (Root->TryGetArrayField(TEXT("edges"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					FString From, To;
					if (V.IsValid() && V->TryGetObject(O)
						&& (*O)->TryGetStringField(TEXT("from_node_id"), From) && (*O)->TryGetStringField(TEXT("to_node_id"), To))
					{
						const FVector2D* A = NodePos.Find(From);
						const FVector2D* B = NodePos.Find(To);
						if (A != nullptr && B != nullptr)
						{
							Self->GraphEdges.Emplace(*A, *B);
						}
					}
				}
			}
			Self->bGraphLoaded = Self->Outline.Num() >= 3;
			Self->NextGraphFetch = Now + (Self->bGraphLoaded ? static_cast<double>(Self->GraphRefreshSeconds) : kMapFetchRetrySeconds);

			// 같은 그래프를 20초마다 받으니, 바뀌었을 때만 로그를 남긴다(현장 보정이 들어왔는지 확인용).
			uint32 Hash = HashCombine(GetTypeHash(Self->Outline.Num()), GetTypeHash(Self->GraphEdges.Num()));
			for (const FNavOverlayNode& Node : Self->GraphNodes)
			{
				Hash = HashCombine(Hash, GetTypeHash(Node.Id));
				Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Node.Pos.X)));
				Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Node.Pos.Y)));
			}
			if (Hash != Self->GraphHash)
			{
				Self->GraphHash = Hash;
				UE_LOG(LogNavMapOverlay, Log, TEXT("[NavMapOverlay] graph 갱신 — 벽 %d점 · 전시섬 %d · 노드 %d · 엣지 %d"),
					Self->Outline.Num(), Self->Obstacles.Num(), Self->GraphNodes.Num(), Self->GraphEdges.Num());
			}
		});
	Req->ProcessRequest();
}

void UNavMapArOverlaySubsystem::FetchAnchors()
{
	bAnchorsInFlight = true;
	const FString Url = FString::Printf(TEXT("%s/maps/%s/cloud-anchors?state=bound"), *ServerBaseUrl, *MapId);

	TWeakObjectPtr<UNavMapArOverlaySubsystem> WeakThis(this);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("GET"));
	Req->SetURL(Url);
	Req->SetTimeout(5.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			UNavMapArOverlaySubsystem* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bAnchorsInFlight = false;
			const UWorld* World = Self->GetWorld();
			const double Now = World ? World->GetTimeSeconds() : 0.0;

			TArray<TSharedPtr<FJsonValue>> Arr;
			const bool bHttpOk = bOk && Resp.IsValid() && Resp->GetResponseCode() == 200;
			if (bHttpOk)
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, Arr);
			}
			if (!bHttpOk)
			{
				Self->NextAnchorFetch = Now + kMapFetchRetrySeconds;
				return;
			}
			Self->NextAnchorFetch = Now + kAnchorRefreshSeconds;

			TArray<FNavOverlayAnchor> Fresh;
			for (const TSharedPtr<FJsonValue>& V : Arr)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O))
				{
					continue;
				}
				FString Label;
				(*O)->TryGetStringField(TEXT("label"), Label);
				if (NavCloudAnchorRoles::IsAssetAnchorLabel(Label))
				{
					continue; // 에셋 앵커는 좌표가 촬영용 임의값이다(13-1 D23).
				}
				FNavOverlayAnchor& A = Fresh.AddDefaulted_GetRef();
				A.PointNo = static_cast<int32>((*O)->GetIntegerField(TEXT("point_no")));
				A.Pos = FVector2D(ReadField(*O, TEXT("pos_x_cm")), ReadField(*O, TEXT("pos_y_cm")));
				A.HeadingDeg = static_cast<float>(ReadField(*O, TEXT("heading_deg")));
			}
			const bool bChanged = Fresh.Num() != Self->Anchors.Num();
			Self->Anchors = MoveTemp(Fresh);
			if (bChanged)
			{
				UE_LOG(LogNavMapOverlay, Log, TEXT("[NavMapOverlay] 앵커 %d개 받음"), Self->Anchors.Num());
			}
		});
	Req->ProcessRequest();
}

void UNavMapArOverlaySubsystem::DrawOverlay()
{
	UWorld* World = GetWorld();
	const UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (Localizer == nullptr || Cam == nullptr)
	{
		return;
	}

	const FVector CamMap = Localizer->WorldToMap(Cam->GetCameraLocation());
	const FVector2D Cam2D(CamMap.X, CamMap.Y);
	const float Life = static_cast<float>(kDrawIntervalSeconds * 1.6);
	const double MaxD = MaxDrawDistanceCm;
	auto Near = [&Cam2D, MaxD](const FVector2D& P) { return FVector2D::Distance(P, Cam2D) <= MaxD; };
	auto ToWorld = [Localizer](const FVector2D& P, float ZCm) { return Localizer->MapToWorld(P.X, P.Y, ZCm); };

	if (!bAnnounced && bGraphLoaded)
	{
		bAnnounced = true;
		UE_LOG(LogNavMapOverlay, Log, TEXT("[NavMapOverlay] 그리기 시작 — 기준 %s · 벽 %d점 · 전시섬 %d · 앵커 %d"),
			*Localizer->GetAnchorMarkerCode(), Outline.Num(), Obstacles.Num(), Anchors.Num());
	}

	// ① 벽 — 바닥선·눈높이선 두 겹 + 꼭짓점 기둥(거리감)
	for (int32 i = 0; i < Outline.Num(); ++i)
	{
		const FVector2D& A = Outline[i];
		const FVector2D& B = Outline[(i + 1) % Outline.Num()];
		const bool bNearA = Near(A);
		if (!bNearA && !Near(B))
		{
			continue;
		}
		DrawDebugLine(World, ToWorld(A, WallLowCm), ToWorld(B, WallLowCm), kWallColor, false, Life, 0, 2.0f);
		DrawDebugLine(World, ToWorld(A, WallHighCm), ToWorld(B, WallHighCm), kWallColor, false, Life, 0, 2.0f);
		if (bNearA)
		{
			DrawDebugLine(World, ToWorld(A, WallLowCm), ToWorld(A, WallHighCm), kWallColor, false, Life, 0, 1.0f);
		}
	}

	// ② 전시섬
	for (const FNavOverlayObstacle& Ob : Obstacles)
	{
		const FVector2D C[4] = { Ob.Min, FVector2D(Ob.Max.X, Ob.Min.Y), Ob.Max, FVector2D(Ob.Min.X, Ob.Max.Y) };
		if (!Near(C[0]) && !Near(C[1]) && !Near(C[2]) && !Near(C[3]))
		{
			continue;
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector2D& P = C[k];
			const FVector2D& Q = C[(k + 1) % 4];
			DrawDebugLine(World, ToWorld(P, WallLowCm), ToWorld(Q, WallLowCm), kObstacleColor, false, Life, 0, 2.0f);
			DrawDebugLine(World, ToWorld(P, WallHighCm), ToWorld(Q, WallHighCm), kObstacleColor, false, Life, 0, 1.5f);
			DrawDebugLine(World, ToWorld(P, WallLowCm), ToWorld(P, WallHighCm), kObstacleColor, false, Life, 0, 1.0f);
		}
	}

	// ③ 노드·엣지 — 거리와 무관하게 전부(현장에서 길 전체를 보며 노드를 옮긴다). 이름표만 가까운 노드에
	if (bDrawGraph)
	{
		for (const TPair<FVector2D, FVector2D>& E : GraphEdges)
		{
			DrawDebugLine(World, ToWorld(E.Key, 2.0f), ToWorld(E.Value, 2.0f), kGraphColor, false, Life, 0, 3.0f);
		}
		for (const FNavOverlayNode& Node : GraphNodes)
		{
			const FVector P = ToWorld(Node.Pos, 2.0f);
			DrawDebugSphere(World, P, 10.0f, 8, kGraphColor, false, Life, 0, 1.5f);
			if (Near(Node.Pos))
			{
				DrawDebugLine(World, P, P + FVector(0.0, 0.0, 40.0), kGraphColor, false, Life, 0, 1.5f);
				DrawDebugString(World, P + FVector(0.0, 0.0, 50.0), Node.ShortLabel, nullptr, kGraphColor, Life, true, 1.0f);
			}
		}
	}

	// ④ 앵커 DB 좌표
	for (const FNavOverlayAnchor& A : Anchors)
	{
		if (!Near(A.Pos))
		{
			continue;
		}
		const FVector P = ToWorld(A.Pos, 0.0f);
		DrawDebugSphere(World, P, 12.0f, 8, kAnchorColor, false, Life, 0, 1.0f);
		DrawDebugLine(World, P, P + FVector(0.0, 0.0, 150.0), kAnchorColor, false, Life, 0, 1.5f);
		DrawDebugString(World, P + FVector(0.0, 0.0, 165.0), FString::Printf(TEXT("#%d"), A.PointNo), nullptr, kAnchorColor, Life, true, 1.3f);
	}

	// ⑤ 인식된 앵커 — 실제 핀 ↔ DB 좌표 차이
	const UNavCloudResolverSubsystem* Resolver = World->GetSubsystem<UNavCloudResolverSubsystem>();
	if (Resolver == nullptr)
	{
		return;
	}
	TArray<FNavCloudAnchorWorldPose> Poses;
	Resolver->GetRecognizedAnchorWorldPoses(Poses);
	for (const FNavCloudAnchorWorldPose& Pose : Poses)
	{
		const FNavOverlayAnchor* Row = Anchors.FindByPredicate(
			[&Pose](const FNavOverlayAnchor& X) { return X.PointNo == Pose.PointNo; });
		if (Row == nullptr || !Near(Row->Pos))
		{
			continue;
		}
		const FVector Real = Pose.World.GetLocation();
		const FVector RealMap = Localizer->WorldToMap(Real);
		const double Gap = FVector2D::Distance(FVector2D(RealMap.X, RealMap.Y), Row->Pos);
		const FVector Db = ToWorld(Row->Pos, 0.0f);
		DrawDebugSphere(World, Real, 8.0f, 8, kResidualColor, false, Life, 0, 1.0f);
		DrawDebugLine(World, Db, FVector(Real.X, Real.Y, Db.Z), kResidualColor, false, Life, 0, 3.0f);
		DrawDebugString(World, Real + FVector(0.0, 0.0, 70.0), FString::Printf(TEXT("%.0fcm"), Gap), nullptr, kResidualColor, Life, true, 1.1f);
	}
}

void UNavMapArOverlaySubsystem::LogResiduals(double Now)
{
	UWorld* World = GetWorld();
	const UNavLocalizer* Localizer = World ? World->GetSubsystem<UNavLocalizer>() : nullptr;
	const UNavCloudResolverSubsystem* Resolver = World ? World->GetSubsystem<UNavCloudResolverSubsystem>() : nullptr;
	if (Localizer == nullptr || Resolver == nullptr || !Localizer->IsAnchoredToCloudAnchor() || Anchors.Num() == 0)
	{
		return; // 잔차는 앵커 기준 측위일 때만 의미가 있다(QR 기준이면 다른 규약).
	}

	// 맵 +X 방향이 월드에서 가리키는 yaw = phi. 앵커 k 의 기대 핀 yaw = phi − heading_k
	// (NavCloudResolver::ComputeImpliedCameraMap · NavLocalizer::SolveTransform 과 같은 규약).
	const FVector Origin = Localizer->MapToWorld(0.0f, 0.0f, 0.0f);
	const FVector AlongX = Localizer->MapToWorld(100.0f, 0.0f, 0.0f) - Origin;
	const double PhiDeg = FMath::RadiansToDegrees(FMath::Atan2(AlongX.Y, AlongX.X));

	TArray<FNavCloudAnchorWorldPose> Poses;
	Resolver->GetRecognizedAnchorWorldPoses(Poses);
	const FString Ref = Localizer->GetAnchorMarkerCode();
	for (const FNavCloudAnchorWorldPose& Pose : Poses)
	{
		const FNavOverlayAnchor* Row = Anchors.FindByPredicate(
			[&Pose](const FNavOverlayAnchor& X) { return X.PointNo == Pose.PointNo; });
		if (Row == nullptr)
		{
			continue;
		}
		const FVector RealMap = Localizer->WorldToMap(Pose.World.GetLocation());
		const double Dx = RealMap.X - Row->Pos.X;
		const double Dy = RealMap.Y - Row->Pos.Y;
		const double DyawDeg = FRotator::NormalizeAxis(Pose.World.Rotator().Yaw - (PhiDeg - Row->HeadingDeg));
		UE_LOG(LogNavMapOverlay, Log,
			TEXT("[NavMapOverlay] RESID t=%.1f ref=%s #%d dx=%.0f dy=%.0f d=%.0f dyaw=%.1f"),
			Now, *Ref, Pose.PointNo, Dx, Dy, FMath::Sqrt(Dx * Dx + Dy * Dy), DyawDeg);
	}
}
