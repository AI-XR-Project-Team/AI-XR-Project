#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "TimeMachineARDebugPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class UInputMappingContext;
class UInputAction;
class UStaticMeshComponent;

UCLASS()
class TIMEMACHINEAR_API ATimeMachineARDebugPawn : public APawn
{
	GENERATED_BODY()

public:
	ATimeMachineARDebugPawn();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* CameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UFloatingPawnMovement* MovementComponent;

	// Ghost Mesh for Interpolation Test
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* GhostMesh;

	// Enhanced Input
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* RollAction; // For Q/E roll

	// Input handlers
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void Roll(const FInputActionValue& Value);

	// Interpolation & Dead Reckoning Test
	void SimulateServerUpdate();

	FTimerHandle ServerTimerHandle;
	
	FVector TargetLocation;
	FVector PredictedTargetLocation;
	FRotator TargetRotation;
	FVector ServerVelocity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network Simulation")
	bool bEnableDeadReckoning = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network Simulation")
	float InterpolationSpeed = 10.0f;

	// --- Gaze Raycast Logic ---
	void CheckGaze(float DeltaTime);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Interaction")
	float RequiredGazeTime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AR Interaction")
	float MaxGazeDistance = 2000.0f;

private:
	float GazeTimer = 0.0f;
	class ADinoOverlayActor* CurrentGazedActor = nullptr;
};
