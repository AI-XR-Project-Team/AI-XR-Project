#include "TimeWatchActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName ParamGoldGlow(TEXT("GoldGlow"));
	const FName ParamTint(TEXT("Tint"));
}

ATimeWatchActor::ATimeWatchActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root = 문자판 중심, 문자판 법선 = 액터 +X(design.md §2). 회전을 걸지 않는다 — Pivot 의
	// Roll(로컬 X 회전)이 곧 이 축을 기준으로 돌게 하기 위함(§3.2 "world Z 를 쓰지 않는다").
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetMobility(EComponentMobility::Movable);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCastShadow(false);

	HourPivot = CreateDefaultSubobject<USceneComponent>(TEXT("HourPivot"));
	HourPivot->SetupAttachment(Root);

	HourMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HourMesh"));
	HourMesh->SetupAttachment(HourPivot);
	HourMesh->SetMobility(EComponentMobility::Movable);
	HourMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HourMesh->SetCastShadow(false);

	MinutePivot = CreateDefaultSubobject<USceneComponent>(TEXT("MinutePivot"));
	MinutePivot->SetupAttachment(Root);

	MinuteMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MinuteMesh"));
	MinuteMesh->SetupAttachment(MinutePivot);
	MinuteMesh->SetMobility(EComponentMobility::Movable);
	MinuteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MinuteMesh->SetCastShadow(false);

	// 파생 자산(Scripts/import_time_watch.py 가 만든다). 아직 임포트 전이면 nullptr — 경고만
	// 남기고 빈 액터로 둔다(에디터가 죽지 않게).
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyFinder(TEXT("/Game/TimeReveal/Clock/SM_Watch_Body.SM_Watch_Body"));
		if (BodyFinder.Succeeded())
		{
			BodyMesh->SetStaticMesh(BodyFinder.Object);
		}
	}
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> HourFinder(TEXT("/Game/TimeReveal/Clock/SM_Watch_HandHour.SM_Watch_HandHour"));
		if (HourFinder.Succeeded())
		{
			HourMesh->SetStaticMesh(HourFinder.Object);
		}
	}
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> MinuteFinder(TEXT("/Game/TimeReveal/Clock/SM_Watch_HandMinute.SM_Watch_HandMinute"));
		if (MinuteFinder.Succeeded())
		{
			MinuteMesh->SetStaticMesh(MinuteFinder.Object);
		}
	}

	ApplyAssembly();
}

void ATimeWatchActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAssembly();
}

void ATimeWatchActor::ApplyAssembly()
{
	// Apply after Blueprint/instance properties have been deserialized as well.
	// 조립 수치(design.md §3.2) 적용. BP 자식이 EditAnywhere 프로퍼티로 재조정할 수 있으므로
	// 생성자에서는 위 UPROPERTY 기본값을 그대로 컴포넌트에 반영만 한다(값 자체는 헤더에).
	BodyMesh->SetRelativeLocation(BodyRelativeLocation);
	BodyMesh->SetRelativeRotation(BodyRelativeRotation);

	HourPivot->SetRelativeLocation(HourPivotRelativeLocation);
	HourMesh->SetRelativeLocation(HourMeshRelativeLocation);
	HourMesh->SetRelativeRotation(HourMeshRelativeRotation);
	HourMesh->SetRelativeScale3D(FVector(HourHandScale));

	MinutePivot->SetRelativeLocation(MinutePivotRelativeLocation);
	MinuteMesh->SetRelativeLocation(MinuteMeshRelativeLocation);
	MinuteMesh->SetRelativeRotation(MinuteMeshRelativeRotation);
	MinuteMesh->SetRelativeScale3D(FVector(MinuteHandScale));
}

void ATimeWatchActor::BeginPlay()
{
	Super::BeginPlay();
	CreateHandDynamicMaterials();
}

void ATimeWatchActor::CreateHandDynamicMaterials()
{
	if (BodyMesh && BodyMesh->GetStaticMesh())
	{
		BodyMID = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (HourMesh && HourMesh->GetStaticMesh())
	{
		HourMID = HourMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (MinuteMesh && MinuteMesh->GetStaticMesh())
	{
		MinuteMID = MinuteMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
}

void ATimeWatchActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentHourDegPerSec != 0.f && HourPivot)
	{
		HourPivot->AddLocalRotation(FRotator(0.f, 0.f, CurrentHourDegPerSec * DeltaTime));
	}
	if (CurrentMinuteDegPerSec != 0.f && MinutePivot)
	{
		MinutePivot->AddLocalRotation(FRotator(0.f, 0.f, CurrentMinuteDegPerSec * DeltaTime));
	}
	if (CurrentBodySpinDegPerSec != 0.f && Root)
	{
		// Root 를 통째로 돌린다 — 본체+두 바늘이 같이 돌고, 바늘은 그 위에서 자기 속도로 더 돈다.
		Root->AddLocalRotation(FRotator(0.f, CurrentBodySpinDegPerSec * DeltaTime, 0.f));
	}
}

void ATimeWatchActor::SetHandSpeeds(float HourDegPerSec, float MinuteDegPerSec)
{
	CurrentHourDegPerSec = HourDegPerSec;
	CurrentMinuteDegPerSec = MinuteDegPerSec;
}

void ATimeWatchActor::SetBodySpin(float DegPerSec)
{
	CurrentBodySpinDegPerSec = DegPerSec;
}

void ATimeWatchActor::SetHandsAngle(float HourDeg, float MinuteDeg)
{
	if (HourPivot)
	{
		HourPivot->SetRelativeRotation(FRotator(0.f, 0.f, HourDeg));
	}
	if (MinutePivot)
	{
		MinutePivot->SetRelativeRotation(FRotator(0.f, 0.f, MinuteDeg));
	}
}

void ATimeWatchActor::SetGlow(float Glow01)
{
	if (!BodyMID) CreateHandDynamicMaterials();
	Glow01 = FMath::Clamp(Glow01, 0.f, 1.f);
	if (BodyMID) { BodyMID->SetScalarParameterValue(ParamGoldGlow, Glow01); }
	if (HourMID) { HourMID->SetScalarParameterValue(ParamGoldGlow, Glow01); }
	if (MinuteMID) { MinuteMID->SetScalarParameterValue(ParamGoldGlow, Glow01); }
}

void ATimeWatchActor::SetTint(FLinearColor Tint)
{
	if (BodyMID) { BodyMID->SetVectorParameterValue(ParamTint, Tint); }
	if (HourMID) { HourMID->SetVectorParameterValue(ParamTint, Tint); }
	if (MinuteMID) { MinuteMID->SetVectorParameterValue(ParamTint, Tint); }
}
