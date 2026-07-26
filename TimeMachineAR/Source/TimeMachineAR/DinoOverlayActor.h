#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DinoOverlayActor.generated.h"

UCLASS()
class TIMEMACHINEAR_API ADinoOverlayActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ADinoOverlayActor();

protected:
	virtual void BeginPlay() override;

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
