#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DinoInfoData.generated.h"

/**
 * 카드 아래쪽 요약 타일 한 칸.
 *
 * 값을 문자열로 두는 이유는 "12~13 m", "6~9 ton", "약 200만 년" 처럼
 * 범위·근사 표기가 대부분이라서다. 숫자로 저장하면 매번 포맷 규칙을
 * 따로 들고 다녀야 하는데, 전시 문구는 종마다 표기가 제각각이다.
 */
USTRUCT(BlueprintType)
struct FDinoStat
{
	GENERATED_BODY()

	/** 타일 제목. 예: 크기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	FText Label;

	/** 굵게 표시되는 값. 예: 12~13 m */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	FText Value;

	/** 값 아래 작은 글씨. 예: 길이 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	FText Sub;
};

/**
 * 공룡 한 종의 정보 카드 내용.
 *
 * 서버(dinosaurs 테이블)에는 이름·학명·시대·전장 정도만 있고 식성·무게·
 * 소개문 같은 카드용 필드가 없다. 지금은 스키마를 건드리지 않고 이 에셋에
 * 채워 넣는다. 나중에 서버로 옮길 때는 이 구조 그대로 응답에 매핑하면 된다.
 *
 * 전시물마다 하나씩 만들어 BP_DinoOverlay 의 DinoInfo 에 지정한다.
 */
UCLASS(BlueprintType)
class TIMEMACHINEAR_API UDinoInfoData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 카드 왼쪽 위 번호. 전시 순번이라 자릿수를 맞춰 적는다. 예: 01 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|이름")
	FText DisplayNumber;

	/** 예: 티라노사우루스 렉스 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|이름")
	FText NameKo;

	/** 예: Tyrannosaurus rex */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|이름")
	FText NameSci;

	/** 분류 태그. 예: 육식동물 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	FText DietTag;

	/** 시대 태그의 윗줄. 예: 백악기 후기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	FText PeriodTag;

	/** 시대 태그의 아랫줄. 예: 약 6,800만 년 전 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	FText PeriodSub;

	/** 소개 탭 본문. 두세 문장 분량을 전제로 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|본문", meta = (MultiLine = "true"))
	FText IntroText;

	/**
	 * 요약 타일. 레퍼런스 기준 네 칸(크기·무게·식성·기간)이지만 개수는 자유다.
	 * 카드가 배열을 그대로 순회해 타일을 만든다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|본문")
	TArray<FDinoStat> Stats;

	/**
	 * 도슨트 대화를 열 때 넘길 전시물 UUID.
	 *
	 * 서버의 exhibits.id 와 같아야 한다. 비워 두면 카드가 도슨트 버튼을
	 * 눌러도 아무 일이 없으니, 대화까지 이으려면 반드시 채운다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|도슨트")
	FString ExhibitId;
};
