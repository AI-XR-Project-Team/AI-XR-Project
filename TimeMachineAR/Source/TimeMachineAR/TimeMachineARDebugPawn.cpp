#include "TimeMachineARDebugPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "Math/UnrealMathUtility.h"

ATimeMachineARDebugPawn::ATimeMachineARDebugPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root Component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	// Camera Component
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetupAttachment(RootComponent);
	CameraComponent->bUsePawnControlRotation = true;

	// Movement Component
	MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));

	// Ghost Mesh Component
	GhostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMesh"));
	GhostMesh->SetupAttachment(RootComponent);
	GhostMesh->SetUsingAbsoluteLocation(true);
	GhostMesh->SetUsingAbsoluteRotation(true);
	GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATimeMachineARDebugPawn::BeginPlay()
{
	Super::BeginPlay();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	// Initialize Network Simulation Data
	TargetLocation = GetActorLocation();
	PredictedTargetLocation = TargetLocation;
	TargetRotation = GetActorRotation();
	ServerVelocity = FVector::ZeroVector;

	if (GhostMesh)
	{
		GhostMesh->SetWorldLocationAndRotation(TargetLocation, TargetRotation);
	}

	// Start 30Hz simulated server tick (0.033 seconds)
	GetWorldTimerManager().SetTimer(ServerTimerHandle, this, &ATimeMachineARDebugPawn::SimulateServerUpdate, 0.0333f, true);
}

void ATimeMachineARDebugPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (!GhostMesh) return;

	// Dead Reckoning: predict the target location based on velocity and elapsed time
	if (bEnableDeadReckoning)
	{
		PredictedTargetLocation += ServerVelocity * DeltaTime;
	}
	else
	{
		PredictedTargetLocation = TargetLocation;
	}

	// Interpolate the GhostMesh to the predicted target
	FVector NewLocation = FMath::VInterpTo(GhostMesh->GetComponentLocation(), PredictedTargetLocation, DeltaTime, InterpolationSpeed);
	FRotator NewRotation = FMath::RInterpTo(GhostMesh->GetComponentRotation(), TargetRotation, DeltaTime, InterpolationSpeed);

	GhostMesh->SetWorldLocationAndRotation(NewLocation, NewRotation);
}

void ATimeMachineARDebugPawn::SimulateServerUpdate()
{
	// Simulate receiving a packet from the server at 30Hz
	// Capture the true state of the Pawn
	TargetLocation = GetActorLocation();
	TargetRotation = GetActorRotation();
	ServerVelocity = GetVelocity();

	// Reset predicted target to actual target upon receiving "packet"
	PredictedTargetLocation = TargetLocation;
}

void ATimeMachineARDebugPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATimeMachineARDebugPawn::Move);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATimeMachineARDebugPawn::Look);
		}

		// Rolling (Q/E)
		if (RollAction)
		{
			EnhancedInputComponent->BindAction(RollAction, ETriggerEvent::Triggered, this, &ATimeMachineARDebugPawn::Roll);
		}
	}
}

void ATimeMachineARDebugPawn::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// Forward/Backward
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(ForwardDirection, MovementVector.Y);

		// Right/Left
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void ATimeMachineARDebugPawn::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void ATimeMachineARDebugPawn::Roll(const FInputActionValue& Value)
{
	float RollValue = Value.Get<float>();

	if (Controller != nullptr)
	{
		AddControllerRollInput(RollValue);
	}
}
