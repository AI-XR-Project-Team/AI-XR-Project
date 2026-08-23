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

	// 발자국 머티리얼(사람 몫). 없으면 메시 기본 머티리얼로 렌더된다(보이긴 함).
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, *FloorMaterialPath);
	if (BaseMaterial != nullptr)
	{
		DynMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	}
	else if (!bWarnedNoMaterial)
	{
		bWarnedNoMaterial = true;
		UE_LOG(LogNav, Warning,
			TEXT("[floor] 머티리얼 '%s' 이(가) 없습니다. 사람이 M_NavFloorArrow(Unlit+Translucent, ")
			TEXT("param '%s')를 만들어야 발자국 텍스처가 뜹니다(spec §I). 그전엔 기본 머티리얼로 보입니다."),
			*FloorMaterialPath, *FloorTextureParam.ToString());
	}
}

UStaticMeshComponent* ANavFloorGuideActor::GetOrCreatePlane(int32 Index)
{
	if (Pool.IsValidIndex(Index) && Pool[Index] != nullptr)
	{
		return Pool[Index];
	}

	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(PlaneMesh);
	if (DynMaterial != nullptr)
	{
		C->SetMaterial(0, DynMaterial);
	}
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetMobility(EComponentMobility::Movable);
	C->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	C->RegisterComponent();
	C->SetVisibility(false);

	if (!Pool.IsValidIndex(Index))
	{
		Pool.SetNum(Index + 1);
	}
	Pool[Index] = C;
	return C;
}

void ANavFloorGuideActor::ApplyTexture(const FString& NodeType, const FString& Label)
{
	const FString Path = FNavDestinations::FloorTextureObjectPath(NodeType, Label);
	if (Path == CurrentTexturePath)
	{
		return;   // 같은 목적지 — 다시 로드하지 않는다.
	}
	CurrentTexturePath = Path;

	CurrentTexture = Path.IsEmpty() ? nullptr : LoadObject<UTexture>(nullptr, *Path);
	if (DynMaterial != nullptr && CurrentTexture != nullptr)
	{
		DynMaterial->SetTextureParameterValue(FloorTextureParam, CurrentTexture);
	}
}

void ANavFloorGuideActor::HideGuide()
{
	for (UStaticMeshComponent* C : Pool)
	{
		if (C != nullptr)
		{
			C->SetVisibility(false);
		}
	}
}

void ANavFloorGuideActor::UpdateGuide(const TArray<FVector2D>& RoutePtsMap, const FVector2D& UserXY,
	const FString& NodeType, const FString& Label)
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

	TArray<FNavFloorPlacement> Placements;
	FNavFloorGuide::BuildPlacements(RoutePtsMap, UserXY,
		FloorGuideSpacingCm, FloorGuideRangeCm, Placements);

	if (Placements.Num() == 0)
	{
		HideGuide();
		return;
	}

	ApplyTexture(NodeType, Label);

	const float Scale = FloorPlaneSizeCm / 100.f;   // 엔진 Plane 이 1m 라 60cm → 0.6.

	for (int32 i = 0; i < Placements.Num(); ++i)
	{
		const FNavFloorPlacement& P = Placements[i];

		// 맵(cm) → 월드. 진행 방향은 두 점을 변환해 월드 상의 방향으로 뽑는다
		// (MapToWorld 가 맵→월드 yaw 회전을 품고 있어 맵 yaw 를 그대로 쓰면 어긋난다).
		const FVector World = Loc->MapToWorld(P.MapPos.X, P.MapPos.Y, 0.f)
			+ FVector(0.f, 0.f, FloorZOffsetCm);

		const float Rad = FMath::DegreesToRadians(P.YawDeg);
		const FVector2D AheadMap = P.MapPos + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * 20.f;
		const FVector WorldAhead = Loc->MapToWorld(AheadMap.X, AheadMap.Y, 0.f);
		const FVector WorldDir = (WorldAhead - Loc->MapToWorld(P.MapPos.X, P.MapPos.Y, 0.f));
		const float WorldYaw = FMath::RadiansToDegrees(FMath::Atan2(WorldDir.Y, WorldDir.X))
			+ FloorYawOffsetDeg;

		// Plane 은 XY 평면(법선 +Z)에 눕는다. yaw 만 돌려 진행 방향을 향하게 한다.
		const FRotator Rot(0.f, WorldYaw, 0.f);

		UStaticMeshComponent* C = GetOrCreatePlane(i);
		C->SetWorldLocationAndRotation(World, Rot);
		C->SetWorldScale3D(FVector(Scale, Scale, Scale));
		C->SetVisibility(true);
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
