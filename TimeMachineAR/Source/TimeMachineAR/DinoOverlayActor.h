#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DinoOverlayActor.generated.h"

class UDinoInfoData;

/**
 * 공룡을 탭했을 때. 정보 카드가 여기에 붙는다.
 *
 * 액터가 카드 위젯을 직접 들고 있지 않은 이유는, 카드가 화면 어디에 어떤
 * 모양으로 붙어 있는지를 액터가 알 필요가 없기 때문이다. 연결은 BP 에서 한다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDinoClicked, UDinoInfoData*, Info);

UCLASS()
class TIMEMACHINEAR_API ADinoOverlayActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ADinoOverlayActor();

protected:
	virtual void BeginPlay() override;

	// 엔진이 클릭/터치를 이 액터로 넘겨 주는 지점. 둘 다 HandleTapped 로 모은다.
	// PlayerController 의 bEnableClickEvents / bEnableTouchEvents 가 켜져 있어야
	// 호출된다(ATimeMachineARPlayerController 생성자에서 켠다).
	virtual void NotifyActorOnClicked(FKey ButtonPressed = EKeys::LeftMouseButton) override;
	virtual void NotifyActorOnInputTouchBegin(const ETouchIndex::Type FingerIndex) override;

public:	
	virtual void Tick(float DeltaTime) override;

	// 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlay")
	class USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlay")
	class UStaticMeshComponent* BoneMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Overlay")
	class UStaticMeshComponent* FleshMesh;

	// 렌더링 전환 시작 함수 (3초 응시 완료 시 호출됨)
	UFUNCTION(BlueprintCallable, Category = "Overlay")
	void StartReveal();

	// ------------------------------------------------------------ 정보 카드

	/**
	 * 이 오버레이가 나타내는 공룡. 카드에 뿌릴 내용이 전부 여기 들어 있다.
	 * BP_DinoOverlay 의 클래스 기본값에서 DA_Dino_* 를 지정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay|Info")
	UDinoInfoData* DinoInfo;

	UPROPERTY(BlueprintAssignable, Category = "Overlay|Info")
	FOnDinoClicked OnDinoClicked;

	/**
	 * 탭을 받을지. 살점이 드러나기 전(뼈만 보일 때)에도 카드를 열지 여부를
	 * 전시 연출에 맞춰 끌 수 있게 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay|Info")
	bool bClickable = true;

	/** 탭 처리 진입점. 마우스 클릭·터치·BP 호출이 모두 여기로 모인다. */
	UFUNCTION(BlueprintCallable, Category = "Overlay|Info")
	void HandleTapped();

	// 렌더링 트랜지션 (Material Parameter)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	float RevealDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FName MaterialAlphaParamName = FName("Alpha");

private:
	bool bIsRevealing = false;
	bool bHasRevealed = false;
	float CurrentRevealTime = 0.0f;

	// 동적 머티리얼 인스턴스 보관용
	UPROPERTY()
	class UMaterialInstanceDynamic* FleshDynamicMaterial;
};
