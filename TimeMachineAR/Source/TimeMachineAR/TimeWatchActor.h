#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimeWatchActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * 회중시계 조립 액터(design.md §2/§3) — **시각 전용, 상태 없음**. 던지기/포털 등 연출 상태 전이는
 * UTimeRevealComponent(Sonnet 작업 2)가 소유하고, 이 액터는 그저 "본체+바늘"을 들고 회전만 한다.
 *
 * 계층:
 *   Root(SceneComponent)     — 문자판 중심. 문자판 법선 = **액터 +X**(design.md §2).
 *    ├ BodyMesh               — SM_Watch_Body. Yaw −90(문자판 법선 +Y[메시 로컬] → 액터 +X).
 *    ├ HourPivot(SceneComponent) ─ HourMesh   — SM_Watch_HandHour
 *    └ MinutePivot(SceneComponent) ─ MinuteMesh — SM_Watch_HandMinute
 *
 * 바늘은 **피벗의 로컬 X(=문자판 법선, Root 의 회전이 항등이라 Root 의 +X 와 같다)** 를 축으로
 * 돈다. Pivot 의 RelativeRotation 은 Roll 성분만 쓴다(Pitch=Yaw=0 이면 Roll 은 로컬 X 회전 —
 * world Z 가 아니다). 손 메시 자신의 상대 회전(Yaw −90, 허브를 피벗 원점으로 당기는 오프셋
 * 계산에 쓰인 것과 동일 축)은 고정이며 각도에 따라 바뀌지 않는다.
 *
 * 메시는 생성자에서 `ConstructorHelpers::FObjectFinder` 로 `/Game/TimeReveal/Clock/...`(파생
 * 자산, Scripts/import_time_watch.py 가 만든다)을 찾는다. 아직 임포트 전이거나 에셋이 빠졌으면
 * nullptr 로 조용히 넘어간다(에디터에서 빈 액터로 뜨는 것을 막지 않는다 — 로그로만 경고).
 */
UCLASS()
class TIMEMACHINEAR_API ATimeWatchActor : public AActor
{
	GENERATED_BODY()

public:
	ATimeWatchActor();

	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

public:
	// ------------------------------------------------------------------ 컴포넌트

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watch")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watch")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watch")
	TObjectPtr<USceneComponent> HourPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watch")
	TObjectPtr<UStaticMeshComponent> HourMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watch")
	TObjectPtr<USceneComponent> MinutePivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watch")
	TObjectPtr<UStaticMeshComponent> MinuteMesh;

	// ------------------------------------------------------------------ 조립 수치(design.md §3.2, 초기값)
	// BP 자식이 렌더 검수 후 조정할 수 있게 전부 EditAnywhere. 여기 적힌 기본값이
	// WatchAssemblyTest 렌더로 확정한 최종 수치다(handoff.md §B 에도 기록).

	/** 본체 상대 위치(cm). 문자판 중심(0.6,37.5,-25.5)을 Root 원점으로 당기는 오프셋, Yaw−90 반영. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FVector BodyRelativeLocation = FVector(-37.5f, 0.6f, 25.5f);

	/** 본체 상대 회전. Yaw −90 = 메시 로컬 문자판 법선(+Y) → 액터 +X. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FRotator BodyRelativeRotation = FRotator(0.f, -90.f, 0.f);

	/** 시침 피벗 위치(cm). 문자판 위 여유(액터 +X 쪽) 1.4 — 분침보다 문자판에 가깝다. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FVector HourPivotRelativeLocation = FVector(1.4f, 0.f, 0.f);

	/** 시침 메시 상대 위치(피벗 기준). 허브(-2.7,0,-79.9)×Scale 을 피벗 원점으로 당긴 값. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FVector HourMeshRelativeLocation = FVector(0.f, -0.459f, 13.583f);   // = (0, -2.7*0.17, 79.9*0.17)

	/** 시침 메시 상대 회전(피벗 기준, 고정 — 각도는 피벗 Roll 로만 바뀐다). */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FRotator HourMeshRelativeRotation = FRotator(0.f, -90.f, 0.f);

	/** 시침 스케일(원본 190cm 기준 균등). 끝이 숫자 링 안쪽(35~48cm)에 오도록 0.17 ≈ 30cm. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	float HourHandScale = 0.17f;

	/** 분침 피벗 위치(cm). 시침보다 문자판에서 더 떨어뜨려 관통/틱 없음(측면 렌더로 확정). */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FVector MinutePivotRelativeLocation = FVector(3.5f, 0.f, 0.f);

	/** 분침 메시 상대 위치(피벗 기준). 허브(0,0,-79.8)×Scale 을 피벗 원점으로 당긴 값. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FVector MinuteMeshRelativeLocation = FVector(0.f, 0.f, 19.95f);      // = (0, 0, 79.8*0.25)

	/** 분침 메시 상대 회전(피벗 기준, 고정). */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	FRotator MinuteMeshRelativeRotation = FRotator(0.f, -90.f, 0.f);

	/** 분침 스케일. 끝이 분 트랙(48cm) 안쪽에 오도록 0.25 ≈ 44cm. */
	UPROPERTY(EditAnywhere, Category = "Watch|Assembly")
	float MinuteHandScale = 0.25f;

	// ------------------------------------------------------------------ API(design.md §2)

	/** 시침/분침 회전 속도(도/초). 피벗 로컬 X(문자판 법선) 기준, 부호는 렌더로 확정한 방향. */
	UFUNCTION(BlueprintCallable, Category = "Watch")
	void SetHandSpeeds(float HourDegPerSec, float MinuteDegPerSec);

	/** 본체 전체(Root 이하 전부, 바늘 포함) 스핀 속도(도/초). 축 = 액터 로컬 Z(보우/크라운을 지나는 세로축). */
	UFUNCTION(BlueprintCallable, Category = "Watch")
	void SetBodySpin(float DegPerSec);

	/** 바늘 각도를 절대값으로 지정(도, 0 = 12시, 피벗 로컬 X 기준 Roll). 다음 Tick 부터는 다시 SetHandSpeeds 속도로 누적. */
	UFUNCTION(BlueprintCallable, Category = "Watch")
	void SetHandsAngle(float HourDeg, float MinuteDeg);

	/** GoldGlow(0~1) — 본체+두 바늘 MID 공통. 어두운 배경에서 금속이 죽을 때만 쓰는 보조 Emissive. */
	UFUNCTION(BlueprintCallable, Category = "Watch")
	void SetGlow(float Glow01);

	/** Tint — 본체+두 바늘 MID 공통. */
	UFUNCTION(BlueprintCallable, Category = "Watch")
	void SetTint(FLinearColor Tint);

private:
	void ApplyAssembly();
	float CurrentHourDegPerSec = 0.f;
	float CurrentMinuteDegPerSec = 0.f;
	float CurrentBodySpinDegPerSec = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HourMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MinuteMID;

	/** BeginPlay 에서 한 번. 메시가 없으면(슬롯 0 없음) 만들지 않는다. */
	void CreateHandDynamicMaterials();
};
