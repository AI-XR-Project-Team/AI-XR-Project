#include "DinoOverlayActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ADinoOverlayActor::ADinoOverlayActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	BoneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoneMesh"));
	BoneMesh->SetupAttachment(Root);

	FleshMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FleshMesh"));
	FleshMesh->SetupAttachment(Root);
}

void ADinoOverlayActor::BeginPlay()
{
	Super::BeginPlay();

	// 게임 시작 시, 살점 메시에 적용된 첫 번째 머티리얼을 동적(Dynamic)으로 만듦
	if (FleshMesh && FleshMesh->GetMaterial(0))
	{
		FleshDynamicMaterial = UMaterialInstanceDynamic::Create(FleshMesh->GetMaterial(0), this);
		FleshMesh->SetMaterial(0, FleshDynamicMaterial);

		// 초기 상태는 투명(Alpha = 0)
		if (FleshDynamicMaterial)
		{
			FleshDynamicMaterial->SetScalarParameterValue(MaterialAlphaParamName, 0.0f);
		}
	}
}

void ADinoOverlayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 페이드 인(Fade In) 애니메이션 처리
	if (bIsRevealing && FleshDynamicMaterial)
	{
		CurrentRevealTime += DeltaTime;
		
		// 0~1 사이의 값으로 정규화 (진행률)
		float Progress = FMath::Clamp(CurrentRevealTime / RevealDuration, 0.0f, 1.0f);

		// 머티리얼의 파라미터 업데이트
		FleshDynamicMaterial->SetScalarParameterValue(MaterialAlphaParamName, Progress);

		// 애니메이션 완료 체크
		if (Progress >= 1.0f)
		{
			bIsRevealing = false;
			bHasRevealed = true;
		}
	}
}

void ADinoOverlayActor::StartReveal()
{
	if (!bIsRevealing && !bHasRevealed)
	{
		bIsRevealing = true;
		CurrentRevealTime = 0.0f;
	}
}
