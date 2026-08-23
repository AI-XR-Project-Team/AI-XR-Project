#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavFloorGuideActor.generated.h"

class UNavLocalizer;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;

/**
 * 경로를 **바닥의 공룡 발자국/화살표**로 그리는 액터(9단계 §C-1).
 *
 * 미니맵(Follow)이 C++ 로 스폰·소유하고 매 프레임 `UpdateGuide` 로 먹인다 — 8단계 GuideLog·
 * FullMap 과 같은 방식이라 **에디터/BP 배선이 0** 이다(spec §F). 레벨에 배치하지 않는다
 * (`.umap` 병합 불가 회피).
 *
 * ## 어떻게 그리나
 *
 * `FNavFloorGuide::BuildPlacements`(순수 로직)가 정적 경로 위, 사용자 앞 RangeCm 까지 SpacingCm
 * 간격의 발자국 자리(맵 좌표+진행 yaw)를 낸다. 이 액터는 그 자리를 `UNavLocalizer::MapToWorld()`
 * 로 월드에 얹어 엔진 기본 `Plane` 스태틱메시를 바닥에 눕힌다. 메시는 **풀**(재사용)로 돌려
 * 매 프레임 스폰/파괴하지 않는다. 텍스처는 목적지 `node_type`·`label` 로 갈린다
 * (`FNavDestinations::FloorTextureObjectPath`) — 전시물=그 공룡 발자국, 시설·입구=화살표.
 *
 * ## 3중 안전장치 (retired §2.3)
 *  - 앞 `FloorGuideRangeCm` 만 그린다(먼 쪽 드리프트가 벽을 뚫는 걸 안 보이게).
 *  - 측위 저하/상실 시 즉시 숨긴다(어긋난 3D 는 없는 것보다 나쁘다).
 *  - Z 는 마커 평면 기준(`FloorZOffsetCm`) — ARCore 평면 추정에 안 맡긴다.
 *
 * ## 에셋 (사람 몫, final §2 H-3)
 *  - 텍스처 5장: `/Game/UI/Nav/Floor/` (발자국 4 + 화살표 1). 경로 로드 → 손 할당 불필요.
 *  - 머티리얼 `M_NavFloorArrow`: Unlit+Translucent, `TextureSampleParameter2D` 이름 **FootprintTex**.
 *    아직 없으면 메시 기본 머티리얼로 렌더(보이긴 함) + 경고 1회.
 */
UCLASS(Config = Game)
class TIMEMACHINEAR_API ANavFloorGuideActor : public AActor
{
	GENERATED_BODY()

public:
	ANavFloorGuideActor();

	/**
	 * 발자국을 다시 깐다. 미니맵(Follow)이 측위 중·경로 있을 때 매 프레임 부른다.
	 * 측위 전/추적 저하면 스스로 숨긴다.
	 *
	 * @param RoutePtsMap 정적 경로 폴리라인(맵 cm), 시작→목적지.
	 * @param UserXY      현재 사용자 위치(맵 cm).
	 * @param NodeType    목적지 node_type(exhibit/facility/entrance) — 발자국/화살표 분기.
	 * @param Label       목적지 label — 전시물이면 공룡 4종 발자국 선택.
	 */
	void UpdateGuide(const TArray<FVector2D>& RoutePtsMap, const FVector2D& UserXY,
		const FString& NodeType, const FString& Label);

	/** 발자국을 전부 숨긴다(측위 상실·경로 없음). */
	void HideGuide();

	// ------------------------------------------------------------------ 캘리브레이션(ini)

	/** 사용자 앞으로 발자국을 낼 최대 거리(cm). 먼 쪽 드리프트 방지(안전장치 ①). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "50.0"))
	float FloorGuideRangeCm = 600.f;

	/** 발자국 간격(cm). final §C-1 보강 = 1m. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "20.0"))
	float FloorGuideSpacingCm = 100.f;

	/** 바닥에서 띄우는 높이(cm). 떠/잠겨 보이면 현장에서 조정(안전장치 ③). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	float FloorZOffsetCm = 1.0f;

	/** 발자국 방향 보정각(도). 텍스처 +V(위)가 진행 방향과 어긋나면 현장에서 맞춘다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	float FloorYawOffsetDeg = 0.f;

	/** 발자국 한 변 크기(cm). Plane 권장 60(발자국 실물 약 30, final §C-1). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "5.0"))
	float FloorPlaneSizeCm = 60.f;

	/** 발자국 머티리얼(사람 몫, Unlit+Translucent). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	FString FloorMaterialPath = TEXT("/Game/UI/Nav/Floor/M_NavFloorArrow.M_NavFloorArrow");

	/** 발자국 텍스처를 갈아끼울 머티리얼 파라미터 이름(사람이 머티리얼에서 이 이름으로 만든다). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	FName FloorTextureParam = TEXT("FootprintTex");

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Root;

	/** 재사용하는 Plane 메시 풀. 필요한 만큼만 보이고 나머지는 숨긴다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Pool;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> CurrentTexture;

	/** 지금 물린 발자국 텍스처 경로. 같은 목적지면 다시 로드/세팅하지 않는다. */
	FString CurrentTexturePath;

	/** 에셋 로드가 끝났나(1회). */
	bool bAssetsReady = false;

	/** 머티리얼이 없어 경고를 한 번 냈나. */
	bool bWarnedNoMaterial = false;

	UNavLocalizer* GetLocalizer() const;

	/** 에셋(메시·머티리얼)을 아직 안 했으면 로드한다. */
	void EnsureAssets();

	/** 풀에서 Index 번째 컴포넌트를 얻는다. 없으면 만들어 등록한다. */
	UStaticMeshComponent* GetOrCreatePlane(int32 Index);

	/** 목적지에 맞는 발자국 텍스처를 물린다(바뀌었을 때만). */
	void ApplyTexture(const FString& NodeType, const FString& Label);
};
