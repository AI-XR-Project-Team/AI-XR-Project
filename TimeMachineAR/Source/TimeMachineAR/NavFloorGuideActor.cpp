#include "NavFloorGuideActor.h"

#include "NavFloorGuide.h"
#include "NavDestinations.h"
#include "NavLocalizer.h"
#include "NavClient.h"                     // LogNav
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName ParamTint(TEXT("Tint"));
	const FName ParamOpacity(TEXT("Opacity"));
	const FName ParamBreathAmp(TEXT("BreathAmp"));
	const FName ParamBreathPeriod(TEXT("BreathPeriod"));

	/** 투명 정렬 우선순위: 큰 링이 발자국을 덮지 않게 발이 위. */
	constexpr int32 SortFootprint = 10;
	constexpr int32 SortRing = 5;
	constexpr int32 SortArrivalFoot = 12;
}

ANavFloorGuideActor::ANavFloorGuideActor()
{
	// 외부(미니맵)가 UpdateGuide 로 구동한다 — 스스로 틱하지 않는다.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ANavFloorGuideActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureAssets();
}

UNavLocalizer* ANavFloorGuideActor::GetLocalizer() const
{
	return UNavLocalizer::GetNavLocalizer(this);
}

void ANavFloorGuideActor::EnsureAssets()
{
	if (bAssetsReady)
	{
		return;
	}
	bAssetsReady = true;

	// 엔진 기본 Plane(100×100 언리얼 유닛 = 1m×1m, 법선 +Z). 모듈 추가 0.
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh == nullptr)
	{
		UE_LOG(LogNav, Warning, TEXT("[floor] 엔진 Plane 메시를 못 찾았습니다. 발자국을 못 그립니다."));
		return;
	}

	// 황금 발자국 머티리얼 → 없으면 기존 화살표 머티리얼(FootprintTex 만) → 그것도 없으면 숨김.
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, *FloorMaterialPath);
	if (BaseMaterial == nullptr)
	{
		UE_LOG(LogNav, Warning,
			TEXT("[floor] 머티리얼 '%s' 이(가) 없습니다. Scripts/import_nav_footprints.py 를 돌려 만드세요. ")
			TEXT("폴백 '%s' 을(를) 시도합니다."), *FloorMaterialPath, *FloorFallbackMaterialPath);
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, *FloorFallbackMaterialPath);
	}
	if (BaseMaterial == nullptr)
	{
		UE_LOG(LogNav, Warning, TEXT("[floor] 폴백 머티리얼도 없습니다. 바닥 안내를 숨깁니다(흰 Plane 을 내지 않음)."));
		PlaneMesh = nullptr;   // 이후 UpdateGuide 가 전부 숨긴다.
		return;
	}

	// 좌/우/링 MID 세 개를 여기서 한 번만 만든다. 프레임마다 만들지 않는다.
	LeftMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	RightMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	RingMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	for (UMaterialInstanceDynamic* M : { LeftMID.Get(), RightMID.Get() })
	{
		M->SetScalarParameterValue(ParamBreathAmp, FloorBreathAmplitude);
		M->SetScalarParameterValue(ParamBreathPeriod, FMath::Max(0.5f, FloorBreathPeriodSec));
	}
	RingMID->SetScalarParameterValue(ParamBreathAmp, FloorBreathAmplitude * 0.5f);
	RingMID->SetScalarParameterValue(ParamBreathPeriod, FMath::Max(0.5f, FloorBreathPeriodSec));
}

UStaticMeshComponent* ANavFloorGuideActor::CreatePlane(const TCHAR* DebugName, int32 SortPriority)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this,
		MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(DebugName)));
	C->SetStaticMesh(PlaneMesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->bReceivesDecals = false;
	C->SetMobility(EComponentMobility::Movable);
	C->SetTranslucentSortPriority(SortPriority);
	C->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	C->RegisterComponent();
	C->SetVisibility(false);
	return C;
}

UStaticMeshComponent* ANavFloorGuideActor::GetOrCreatePlane(int32 Index)
{
	if (Pool.IsValidIndex(Index) && Pool[Index] != nullptr)
	{
		return Pool[Index];
	}
	UStaticMeshComponent* C = CreatePlane(*FString::Printf(TEXT("Footprint%d"), Index), SortFootprint);
	if (!Pool.IsValidIndex(Index))
	{
		Pool.SetNum(Index + 1);
	}
	Pool[Index] = C;
	return C;
}

UTexture* ANavFloorGuideActor::LoadTextureOnce(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	UTexture* Tex = LoadObject<UTexture>(nullptr, *Path);
	if (Tex == nullptr && !WarnedPaths.Contains(Path))
	{
		WarnedPaths.Add(Path);
		UE_LOG(LogNav, Warning,
			TEXT("[floor] 텍스처 '%s' 이(가) 없습니다. 바닥 안내를 숨깁니다(직전 텍스처/흰 Plane 을 내지 않음). ")
			TEXT("Scripts/import_nav_footprints.py 로 임포트하고 패키지에 포함됐는지 확인하세요."), *Path);
	}
	return Tex;
}

bool ANavFloorGuideActor::ApplyStyle(const FString& NodeType, const FString& Label)
{
	const FNavDestinations::FFloorStyle Style = FNavDestinations::FloorStyle(NodeType, Label);
	const FString Key = Style.LeftTexturePath + TEXT("|") + Style.RightTexturePath + TEXT("|") + Style.ArrivalRingTexturePath;
	if (Key == CurrentStyleKey)
	{
		return bStyleReady;   // 같은 목적지 — 다시 로드하지 않는다.
	}
	CurrentStyleKey = Key;
	CurrentStyle = Style;
	bStyleReady = false;

	if (!Style.IsValid() || LeftMID == nullptr)
	{
		return false;   // 목적지가 아니거나 머티리얼이 없다 → 숨김.
	}

	LeftTexture = LoadTextureOnce(Style.LeftTexturePath);
	RightTexture = (Style.RightTexturePath == Style.LeftTexturePath)
		? LeftTexture.Get() : LoadTextureOnce(Style.RightTexturePath);
	RingTexture = LoadTextureOnce(Style.ArrivalRingTexturePath);
	if (LeftTexture == nullptr || RightTexture == nullptr)
	{
		return false;
	}

	LeftMID->SetTextureParameterValue(FloorTextureParam, LeftTexture);
	RightMID->SetTextureParameterValue(FloorTextureParam, RightTexture);
	if (RingTexture != nullptr)
	{
		RingMID->SetTextureParameterValue(FloorTextureParam, RingTexture);
	}
	bStyleReady = true;
	return true;
}

void ANavFloorGuideActor::HideFootprints()
{
	for (UStaticMeshComponent* C : Pool)
	{
		if (C != nullptr)
		{
			C->SetVisibility(false);
		}
	}
	bHasGuideHead = false;
}

void ANavFloorGuideActor::HideArrival()
{
	if (RingComp != nullptr) { RingComp->SetVisibility(false); }
	if (ArrivalFootComp != nullptr) { ArrivalFootComp->SetVisibility(false); }
	bHasArrival = false;
}

void ANavFloorGuideActor::HideGuide()
{
	HideFootprints();
	HideArrival();
}

bool ANavFloorGuideActor::GetGuideHeadWorld(FVector& OutWorld) const
{
	OutWorld = GuideHeadWorld;
	return bHasGuideHead;
}

bool ANavFloorGuideActor::GetArrivalWorld(FVector& OutWorld) const
{
	OutWorld = ArrivalWorld;
	return bHasArrival;
}

void ANavFloorGuideActor::MapPoseToWorld(const FVector2D& MapPos, float MapYawDeg,
	TFunctionRef<FVector(const FVector2D&)> MapToWorld, FVector& OutWorld, FVector& OutDir)
{
	// 맵(cm) → 월드. 진행 방향은 두 점을 변환해 월드 상의 방향으로 뽑는다
	// (MapToWorld 가 맵→월드 yaw 회전·Y 반전을 품고 있어 맵 yaw 를 그대로 쓰면 어긋난다).
	OutWorld = MapToWorld(MapPos);
	const float Rad = FMath::DegreesToRadians(MapYawDeg);
	const FVector2D AheadMap = MapPos + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * 20.f;
	OutDir = MapToWorld(AheadMap) - OutWorld;
	OutDir.Z = 0.f;
}

void ANavFloorGuideActor::UpdateGuide(const TArray<FVector2D>& RoutePtsMap, const FVector2D& UserXY,
	const FString& NodeType, const FString& Label, const FNavFloorArrival& Arrival)
{
	EnsureAssets();

	UNavLocalizer* Loc = GetLocalizer();

	// 안전장치 ②: 측위 전이거나 추적이 저하됐으면 아무것도 안 그린다.
	// 어긋난 3D 바닥 그래픽은 없는 것보다 나쁘다(2D 미니맵과 다른 점).
	if (PlaneMesh == nullptr || Loc == nullptr || !Loc->IsLocalized() || Loc->IsTrackingDegraded())
	{
		HideGuide();
		return;
	}

	UpdateGuideInternal(RoutePtsMap, UserXY, NodeType, Label, Arrival,
		[Loc](const FVector2D& M) { return Loc->MapToWorld(M.X, M.Y, 0.f); });
}

void ANavFloorGuideActor::UpdateGuidePreview(const TArray<FVector2D>& RoutePtsWorld, const FVector2D& UserXY,
	const FString& NodeType, const FString& Label, const FNavFloorArrival& Arrival)
{
	EnsureAssets();
	if (PlaneMesh == nullptr)
	{
		HideGuide();
		return;
	}
	UpdateGuideInternal(RoutePtsWorld, UserXY, NodeType, Label, Arrival,
		[](const FVector2D& M) { return FVector(M.X, M.Y, 0.f); });
}

void ANavFloorGuideActor::UpdateGuideInternal(const TArray<FVector2D>& RoutePtsMap, const FVector2D& UserXY,
	const FString& NodeType, const FString& Label, const FNavFloorArrival& Arrival,
	TFunctionRef<FVector(const FVector2D&)> MapToWorld)
{
	if (!ApplyStyle(NodeType, Label))
	{
		HideGuide();   // 에셋 누락/목적지 아님 — 직전 텍스처나 흰 Plane 을 계속 보여 주지 않는다.
		return;
	}

	// ---- 도착: 발자국 대신 목적지 위 링 + 대표 발자국 (소유자가 넘긴 판정만 믿는다).
	if (Arrival.bArrived && Arrival.bHasDestination)
	{
		HideFootprints();
		const FVector DestWorld = MapToWorld(Arrival.DestMapXY);
		const FVector UserWorld = MapToWorld(UserXY);
		// 링은 목적지가 표시 범위 안일 때만 — 벽 너머 원격 목적지에 무조건 그리지 않는다.
		if (FVector::Dist2D(DestWorld, UserWorld) > FloorGuideRangeCm)
		{
			HideArrival();
			return;
		}
		// 대표 발자국 방향 = 경로 마지막 구간의 진행 방향(없으면 사용자→목적지).
		float MapYaw = 0.f;
		bool bHasYaw = false;
		for (int32 i = RoutePtsMap.Num() - 1; i >= 1 && !bHasYaw; --i)
		{
			const FVector2D D = RoutePtsMap[i] - RoutePtsMap[i - 1];
			if (D.SizeSquared() > KINDA_SMALL_NUMBER)
			{
				MapYaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
				bHasYaw = true;
			}
		}
		if (!bHasYaw)
		{
			const FVector2D D = Arrival.DestMapXY - UserXY;
			MapYaw = D.SizeSquared() > KINDA_SMALL_NUMBER ? FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) : 0.f;
		}
		FVector World, Dir;
		MapPoseToWorld(Arrival.DestMapXY, MapYaw, MapToWorld, World, Dir);
		const float WorldYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + FloorYawOffsetDeg;
		PlaceArrival(World, WorldYaw);
		return;
	}

	// ---- 이동 중: 정적 경로 위 사용자 앞 창의 고정 그리드 발자국.
	HideArrival();

	TArray<FNavFloorPlacement> Placements;
	FNavFloorGuide::BuildPlacements(RoutePtsMap, UserXY, FloorGuideSpacingCm, FloorGuideRangeCm, Placements);
	if (Placements.Num() == 0)
	{
		HideFootprints();
		return;
	}
	PlaceFootprints(Placements, MapToWorld);
}

void ANavFloorGuideActor::PlaceFootprints(const TArray<FNavFloorPlacement>& Placements,
	TFunctionRef<FVector(const FVector2D&)> MapToWorld)
{
	const float Scale = FloorPlaneSizeCm / 100.f;   // 엔진 Plane 이 1m 라 60cm → 0.6.
	const float Lateral = CurrentStyle.bAlternate ? FloorLateralOffsetCm : 0.f;

	for (int32 i = 0; i < Placements.Num(); ++i)
	{
		const FNavFloorPlacement& P = Placements[i];

		FVector Center, Dir;
		MapPoseToWorld(P.MapPos, P.YawDeg, MapToWorld, Center, Dir);

		// 좌우 교대는 화면 배열 i 가 아니라 경로 기준 StepIndex 로(걸어도 같은 자리는 같은 발).
		const bool bLeft = CurrentStyle.bAlternate ? P.IsLeft() : true;
		const FVector World = FNavFloorGuide::OffsetForStep(Center, Dir, bLeft, Lateral, P.LateralScale)
			+ FVector(0.f, 0.f, FloorZOffsetCm);

		const float WorldYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + FloorYawOffsetDeg;

		// Plane 은 XY 평면(법선 +Z)에 눕는다. yaw 만 돌려 진행 방향을 향하게 한다.
		UStaticMeshComponent* C = GetOrCreatePlane(i);
		C->SetMaterial(0, bLeft ? LeftMID : RightMID);   // 포인터 세팅만 — MID 를 새로 만들지 않는다.
		C->SetWorldLocationAndRotation(World, FRotator(0.f, WorldYaw, 0.f));
		C->SetWorldScale3D(FVector(Scale, Scale, Scale));
		C->SetVisibility(true);

		if (i == Placements.Num() - 1)
		{
			GuideHeadWorld = Center + FVector(0.f, 0.f, GuideHeadHeightCm);
			bHasGuideHead = true;
		}
	}

	// 남는 풀 컴포넌트는 숨긴다(직전 프레임보다 발자국이 줄었을 때).
	for (int32 i = Placements.Num(); i < Pool.Num(); ++i)
	{
		if (Pool[i] != nullptr)
		{
			Pool[i]->SetVisibility(false);
		}
	}
}

void ANavFloorGuideActor::PlaceArrival(const FVector& DestWorld, float WorldYawDeg)
{
	if (RingTexture == nullptr)
	{
		HideArrival();   // 링 텍스처가 없으면 흰 Plane 대신 아무것도 안 그린다(경고는 로드 시 1회).
		return;
	}
	if (RingComp == nullptr)
	{
		RingComp = CreatePlane(TEXT("ArrivalRing"), SortRing);
		RingComp->SetMaterial(0, RingMID);
	}
	if (ArrivalFootComp == nullptr)
	{
		ArrivalFootComp = CreatePlane(TEXT("ArrivalFoot"), SortArrivalFoot);
	}
	ArrivalFootComp->SetMaterial(0, RightMID);   // 대표 발자국 = 오른발(같은 MID 재사용).

	const FRotator Rot(0.f, WorldYawDeg, 0.f);
	const float RingScale = ArrivalRingSizeCm / 100.f;
	const float FootScale = ArrivalFootSizeCm / 100.f;

	// 링은 바닥 보정 높이, 발자국은 그보다 0.5cm 위(Z-fighting 회피 + 정렬 우선순위).
	RingComp->SetWorldLocationAndRotation(DestWorld + FVector(0.f, 0.f, FloorZOffsetCm), Rot);
	RingComp->SetWorldScale3D(FVector(RingScale, RingScale, RingScale));
	RingComp->SetVisibility(true);

	ArrivalFootComp->SetWorldLocationAndRotation(DestWorld + FVector(0.f, 0.f, FloorZOffsetCm + 0.5f), Rot);
	ArrivalFootComp->SetWorldScale3D(FVector(FootScale, FootScale, FootScale));
	ArrivalFootComp->SetVisibility(true);

	ArrivalWorld = DestWorld;
	bHasArrival = true;
}
