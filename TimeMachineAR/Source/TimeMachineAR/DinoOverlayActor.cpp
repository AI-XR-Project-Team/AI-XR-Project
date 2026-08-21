#include "DinoOverlayActor.h"
#include "DinoInfoData.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogDinoOverlay, Log, All);

ADinoOverlayActor::ADinoOverlayActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	BoneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoneMesh"));
	BoneMesh->SetupAttachment(Root);

	FleshMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FleshMesh"));
	FleshMesh->SetupAttachment(Root);

	// 탭을 받으려면 트레이스에 걸려야 한다. 물리는 끈다(QueryOnly) — AR 앵커에
	// 핀으로 고정된 액터라 물리가 개입하면 위치가 흔들린다.
	// 클릭 판정은 기본 트레이스 채널인 Visibility 로 들어온다.
	for (UStaticMeshComponent* Mesh : { BoneMesh, FleshMesh })
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetGenerateOverlapEvents(false);
	}
}

void ADinoOverlayActor::SetDinoInfo(UDinoInfoData* InInfo)
{
	DinoInfo = InInfo;
	ApplySpecies();
}

void ADinoOverlayActor::ApplySpecies()
{
	if (DinoInfo == nullptr)
	{
		return;
	}

	// 메시가 비어 있으면 건드리지 않는다. 티라노는 BP_DinoOverlay 컴포넌트에
	// 메시와 배치가 이미 맞춰져 있어서, 여기서 덮으면 오히려 망가진다.
	if (DinoInfo->BoneMesh != nullptr && BoneMesh != nullptr)
	{
		BoneMesh->SetStaticMesh(DinoInfo->BoneMesh);
		BoneMesh->SetRelativeTransform(DinoInfo->MeshTransform);
	}

	if (DinoInfo->FleshMesh != nullptr && FleshMesh != nullptr)
	{
		FleshMesh->SetStaticMesh(DinoInfo->FleshMesh);
		FleshMesh->SetRelativeTransform(DinoInfo->MeshTransform);
	}
}

void ADinoOverlayActor::BeginPlay()
{
	Super::BeginPlay();

	// 레벨에 직접 놓거나 BP 에서 DinoInfo 만 지정해 스폰한 경우를 위해 한 번 더.
	// SetDinoInfo 로 이미 반영됐다면 같은 값을 다시 쓰는 것뿐이라 무해하다.
	ApplySpecies();

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

void ADinoOverlayActor::NotifyActorOnClicked(FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);

	HandleTapped();
}

void ADinoOverlayActor::NotifyActorOnInputTouchBegin(const ETouchIndex::Type FingerIndex)
{
	Super::NotifyActorOnInputTouchBegin(FingerIndex);

	HandleTapped();
}

void ADinoOverlayActor::HandleTapped()
{
	if (!bClickable)
	{
		return;
	}

	if (DinoInfo == nullptr)
	{
		// 카드를 열 수 없다는 뜻이지 탭이 안 먹은 게 아니다. 구분이 안 되면
		// 콜리전 문제인지 데이터 누락인지 현장에서 가릴 수가 없다.
		UE_LOG(LogDinoOverlay, Warning,
			TEXT("공룡을 탭했지만 DinoInfo 가 비어 있습니다. "
				 "BP_DinoOverlay 의 DinoInfo 에 DA_Dino_* 를 지정하세요."));
		return;
	}

	UE_LOG(LogDinoOverlay, Log, TEXT("[overlay] 탭: %s"), *DinoInfo->NameKo.ToString());

	OnDinoClicked.Broadcast(DinoInfo);
}
