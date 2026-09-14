#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavDestinations.h"
#include "NavFloorGuideActor.generated.h"

class UNavLocalizer;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;
struct FNavFloorPlacement;

/** 미니맵(소유자)이 매 프레임 넘겨 주는 도착 상태. 액터는 스스로 도착을 판정하지 않는다. */
struct FNavFloorArrival
{
	/** NavRouteProgress 의 bArrived(히스테리시스 포함) 그대로. */
	bool bArrived = false;
	/** 목적지 맵 좌표가 있으면 true. 없으면 링을 못 그린다. */
	bool bHasDestination = false;
	/** 목적지 맵 좌표(cm). */
	FVector2D DestMapXY = FVector2D::ZeroVector;
};

/**
 * 경로를 **바닥의 황금빛 공룡 발자국/화살표**로 그리는 액터(9단계 §C-1, 황금 발자국 시안).
 *
 * 미니맵(Follow)이 C++ 로 스폰·소유하고 매 프레임 `UpdateGuide` 로 먹인다 — 8단계 GuideLog·
 * FullMap 과 같은 방식이라 **에디터/BP 배선이 0** 이다(spec §F). 레벨에 배치하지 않는다
 * (`.umap` 병합 불가 회피).
 *
 * ## 어떻게 그리나
 *
 * `FNavFloorGuide::BuildPlacements`(순수 로직)가 정적 경로 위, 사용자 앞 RangeCm 까지 SpacingCm
 * 간격의 발자국 자리(맵 좌표+진행 yaw+StepIndex)를 낸다. 이 액터는 그 자리를
 * `UNavLocalizer::MapToWorld()` 로 월드에 얹어 엔진 기본 `Plane` 스태틱메시를 바닥에 눕힌다.
 *  - 좌우 교대: `StepIndex` 짝수=왼발, 홀수=오른발. 좌/우는 **MID 두 개**로 갈라 한 MID 에
 *    텍스처를 번갈아 세팅해 전부 마지막 텍스처로 바뀌는 문제를 피한다.
 *  - 가로 오프셋: 월드 접선 기준 왼발은 왼쪽, 오른발은 오른쪽으로 `FloorLateralOffsetCm`
 *    (급회전에서는 LateralScale 로 0 까지 줄어든다).
 *  - 메시는 **풀**(재사용). 매 프레임 스폰/파괴/MID 생성/텍스처 로드를 하지 않는다.
 *  - 텍스처는 목적지 종류·라벨로 갈린다(`FNavDestinations::FloorStyle`) — 전시물=황금 발자국
 *    한 쌍, 시설·입구=기존 화살표.
 *
 * ## 도착 상태 (소유자가 명시적으로 넘긴다)
 *
 * 도착하면 사용자 앞 창에 그리드 점이 남지 않아 발자국은 자연히 사라진다. 그 자리에
 * 목적지 좌표 위 **도착 링 + 대표 발자국** 을 그린다. 미니맵이 `FNavFloorArrival` 로 도착
 * 여부·목적지 좌표를 넘겨야만 그린다(액터 안에서 거리로 재판정하지 않는다 — 도착 판정의
 * 출처는 NavRouteProgress 하나). 링은 측위가 유효하고 목적지가 `FloorGuideRangeCm` 안일 때만.
 *
 * ## 3중 안전장치 (retired §2.3)
 *  - 앞 `FloorGuideRangeCm` 만 그린다(먼 쪽 드리프트가 벽을 뚫는 걸 안 보이게).
 *  - 측위 저하/상실 시 즉시 숨긴다(어긋난 3D 는 없는 것보다 나쁘다).
 *  - Z 는 마커 평면 기준(`FloorZOffsetCm`) — ARCore 평면 추정에 안 맡긴다.
 *
 * ## 에셋
 *  - `/Game/UI/Nav/Floor/Golden/` : T_Footprint_Left/Right, T_NavArrivalRing, T_NavForwardChevron
 *    (Scripts/import_nav_footprints.py 가 만든다). 경로 로드 → 손 할당 불필요.
 *  - 머티리얼 `M_NavFootprintGolden`(같은 스크립트가 만든다): Unlit+Translucent,
 *    `FootprintTex`(Texture) · `Tint`(Vector) · `Opacity`·`BreathAmp`·`BreathPeriod`(Scalar).
 *    없으면 기존 `M_NavFloorArrow`(FootprintTex 만)로 폴백, 그것도 없으면 숨기고 경고 1회.
 *  - 텍스처가 없으면 흰 Plane 을 보여 주지 않고 숨긴다 + 경고 1회.
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
	 * @param Arrival     도착 여부·목적지 좌표(소유자 = NavRouteProgress 판정).
	 */
	void UpdateGuide(const TArray<FVector2D>& RoutePtsMap, const FVector2D& UserXY,
		const FString& NodeType, const FString& Label, const FNavFloorArrival& Arrival = FNavFloorArrival());

	/**
	 * 에디터 프리뷰/테스트용: 측위 없이 **맵 = 월드(항등)** 로 깐다. 실제 앱 경로는 아니다.
	 * `TimeMachineAR.Nav.FloorGuidePreview` 가 방향·교대·머티리얼을 오프스크린 렌더로 확인한다.
	 */
	void UpdateGuidePreview(const TArray<FVector2D>& RoutePtsWorld, const FVector2D& UserXY,
		const FString& NodeType, const FString& Label, const FNavFloorArrival& Arrival = FNavFloorArrival());

	/** 발자국·링을 전부 숨긴다(측위 상실·경로 없음·취소). */
	void HideGuide();

	/**
	 * HUD 전방 셰브론을 띄울 월드 위치(가장 먼 보이는 발자국, 바닥에서 `GuideHeadHeightCm` 위).
	 * 이번 프레임에 발자국을 하나도 안 그렸으면 false.
	 */
	bool GetGuideHeadWorld(FVector& OutWorld) const;

	/** 이번 프레임에 도착 링을 그렸으면 그 월드 위치(바닥). */
	bool GetArrivalWorld(FVector& OutWorld) const;

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

	/**
	 * 발자국 방향 보정각(도). 텍스처 +V(위, 발가락)가 진행 방향과 어긋나면 맞춘다.
	 * 엔진 Plane 의 UV: 텍스처 위(row 0)가 로컬 -Y 쪽이라 +X 진행에 맞추려면 +90(오프스크린
	 * 프리뷰 `Nav.FloorGuidePreview` 로 +X/+Y/-X/-Y 네 방향 확인). 황금 발자국도 발가락이 row 0.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	float FloorYawOffsetDeg = 90.f;

	/** 발자국 Plane 한 변(cm). 글로우 여백 포함 — 실제 발 길이는 그 70%(시트 크롭 기준 ≈42cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "5.0"))
	float FloorPlaneSizeCm = 60.f;

	/** 왼발/오른발을 경로 중심에서 좌우로 벌리는 거리(cm). 디자인 제안 8~12. 급회전에서는 자동 축소. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "0.0"))
	float FloorLateralOffsetCm = 10.f;

	/** 도착 링 Plane 한 변(cm). 링 지름은 그 ~92%. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "20.0"))
	float ArrivalRingSizeCm = 120.f;

	/** 도착 링 안 대표 발자국 Plane 한 변(cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "5.0"))
	float ArrivalFootSizeCm = 70.f;

	/** HUD 전방 셰브론을 가장 먼 발자국 위 이 높이(cm)에 투영한다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	float GuideHeadHeightCm = 60.f;

	/** 발광 호흡 진폭(0=고정 밝기). 약한 1.5~2초 호흡만 — 빠른 점멸 금지. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FloorBreathAmplitude = 0.12f;

	/** 발광 호흡 주기(초). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor",
		meta = (ClampMin = "0.5"))
	float FloorBreathPeriodSec = 1.8f;

	/** 발자국 머티리얼(Unlit+Translucent, param FootprintTex/Tint/Opacity/BreathAmp/BreathPeriod). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	FString FloorMaterialPath = TEXT("/Game/UI/Nav/Floor/Golden/M_NavFootprintGolden.M_NavFootprintGolden");

	/** 위 머티리얼이 없을 때의 폴백(기존, FootprintTex 만). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Floor")
	FString FloorFallbackMaterialPath = TEXT("/Game/UI/Nav/Floor/M_NavFloorArrow.M_NavFloorArrow");

	/** 발자국 텍스처를 갈아끼울 머티리얼 파라미터 이름. */
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

	/** 도착 링·링 안 대표 발자국(풀과 별개, 각 1개). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RingComp;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ArrivalFootComp;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	/** 좌/우/링 MID. 풀 생성 시 한 번 만들고 텍스처만 갈아끼운다. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LeftMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RightMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMID;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LeftTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> RightTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> RingTexture;

	/** 지금 물린 스타일 키(좌|우|링 경로). 같은 목적지면 다시 로드/세팅하지 않는다. */
	FString CurrentStyleKey;
	FNavDestinations::FFloorStyle CurrentStyle;
	bool bStyleReady = false;

	/** 이번 프레임 결과(HUD 투영용). */
	bool bHasGuideHead = false;
	FVector GuideHeadWorld = FVector::ZeroVector;
	bool bHasArrival = false;
	FVector ArrivalWorld = FVector::ZeroVector;

	/** 에셋 로드가 끝났나(1회). */
	bool bAssetsReady = false;

	/** 이미 경고를 낸 경로(경로별 1회). */
	TSet<FString> WarnedPaths;

	UNavLocalizer* GetLocalizer() const;

	/** 에셋(메시·머티리얼·MID)을 아직 안 했으면 로드한다. */
	void EnsureAssets();

	/** Plane 컴포넌트 하나를 만들어 등록한다(풀·링·대표 발자국 공용). */
	UStaticMeshComponent* CreatePlane(const TCHAR* DebugName, int32 SortPriority);

	/** 풀에서 Index 번째 컴포넌트를 얻는다. 없으면 만들어 등록한다. */
	UStaticMeshComponent* GetOrCreatePlane(int32 Index);

	/** 목적지에 맞는 텍스처 세트를 물린다(바뀌었을 때만). 하나라도 없으면 false + 경고 1회. */
	bool ApplyStyle(const FString& NodeType, const FString& Label);

	UTexture* LoadTextureOnce(const FString& Path);

	/** 공통 본체. MapToWorld 만 다르다(실제 측위 vs 프리뷰 항등). */
	void UpdateGuideInternal(const TArray<FVector2D>& RoutePtsMap, const FVector2D& UserXY,
		const FString& NodeType, const FString& Label, const FNavFloorArrival& Arrival,
		TFunctionRef<FVector(const FVector2D&)> MapToWorld);

	/** 발자국 자리들을 풀에 깐다. */
	void PlaceFootprints(const TArray<FNavFloorPlacement>& Placements,
		TFunctionRef<FVector(const FVector2D&)> MapToWorld);

	/** 도착 링 + 대표 발자국. */
	void PlaceArrival(const FVector& DestWorld, float YawDeg);

	void HideFootprints();
	void HideArrival();

	/** 맵 자리·맵 yaw → 월드 위치·월드 진행 방향(두 점 변환). */
	static void MapPoseToWorld(const FVector2D& MapPos, float MapYawDeg,
		TFunctionRef<FVector(const FVector2D&)> MapToWorld, FVector& OutWorld, FVector& OutDir);
};
