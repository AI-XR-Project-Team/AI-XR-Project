#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DinoRegistry.generated.h"

class UDinoInfoData;

/**
 * 마커 이름 → 공룡 종 대응표.
 *
 * 전시물을 늘릴 때 C++ 도 BP 도 건드리지 않게 하려고 둔다. 새 공룡은
 *   1. DA_Dino_* 를 만들어 카드 내용·메시·MarkerCode 를 채우고
 *   2. 이 에셋의 Species 배열에 추가
 * 하면 끝난다. 마커가 아직 안 정해졌다면 MarkerCode 만 나중에 채우면 된다.
 *
 * 레벨의 AARTrackingManager 가 이 에셋을 들고 있다가, 마커를 찾는 순간
 * 그 이름으로 종을 골라 오버레이에 넣는다.
 */
UCLASS(BlueprintType)
class TIMEMACHINEAR_API UDinoRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 전시 중인 공룡 전부. 순서는 상관없다 — MarkerCode 로 찾는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	TArray<TObjectPtr<UDinoInfoData>> Species;

	/**
	 * 어느 종에도 MarkerCode 가 안 맞을 때 쓸 공룡.
	 *
	 * 마커를 아직 정하지 않은 동안에도 앱이 빈 화면을 띄우지 않게 하는 안전망이다.
	 * 비워 두면 BP_DinoOverlay 의 DinoInfo 기본값(티라노)이 그대로 쓰인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	TObjectPtr<UDinoInfoData> FallbackSpecies;

	/**
	 * 마커 이름으로 종을 찾는다. 대소문자는 무시한다.
	 *
	 * 마커 이름은 사람이 손으로 적는 값이라(후보 이미지 FriendlyName 과 DA 양쪽)
	 * 대소문자가 어긋나기 쉽다. 그걸로 공룡이 안 나오는 건 원인을 찾기 어렵다.
	 *
	 * @return 못 찾으면 nullptr. 폴백 적용은 호출부가 판단한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino")
	UDinoInfoData* FindByMarker(const FString& MarkerCode) const;

	/** MarkerCode 가 채워진 종이 하나라도 있는지. 마커 대응을 쓸 수 있는 상태인지 판단용. */
	UFUNCTION(BlueprintPure, Category = "Dino")
	bool HasAnyMarker() const;
};
