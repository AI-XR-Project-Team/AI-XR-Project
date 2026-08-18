#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DinoInfoData.generated.h"

class UTexture2D;

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

	/** 제목 위 아이콘. 비우면 그 칸이 접힌다. 예: straighten(자) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	TObjectPtr<UTexture2D> Icon;
};

/**
 * 카드 가운데 탭 하나 (소개 / 특징 / 서식지 / 발견).
 *
 * 탭 개수를 넷으로 고정하지 않는다. 종마다 할 얘기가 다르다 — 화석이 한 점만
 * 나온 종에 "발견" 탭을 억지로 채우는 것보다, 그 종에 있는 탭만 두는 편이 낫다.
 * 카드가 이 배열을 그대로 순회해 탭 버튼을 만든다.
 */
USTRUCT(BlueprintType)
struct FDinoTab
{
	GENERATED_BODY()

	/** 탭 버튼에 찍히는 글자. 예: 특징 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	FText Label;

	/**
	 * 탭 글자 앞 아이콘. 예: menu_book(소개)
	 *
	 * 레퍼런스는 선택된 탭에만 아이콘을 보여 준다. 그래서 이 값이 있어도
	 * 탭이 꺼져 있으면 접힌다(UDinoTabButton::Apply).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	TObjectPtr<UTexture2D> Icon;

	/** 본문 위 굵은 제목. 비우면 줄이 접힌다. 예: 강력한 턱과 이빨 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	FText Heading;

	/** 탭 본문. 두세 문장 분량을 전제로 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino", meta = (MultiLine = "true"))
	FText Body;

	/** 본문 위 사진. 없으면 이미지 칸이 통째로 접힌다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino")
	TObjectPtr<UTexture2D> Image;
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

	/** 식성 태그 앞 아이콘. 예: eco(잎) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	TObjectPtr<UTexture2D> DietIcon;

	/** 시대 태그의 윗줄. 예: 백악기 후기 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	FText PeriodTag;

	/** 시대 태그의 아랫줄. 예: 약 6,800만 년 전 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	FText PeriodSub;

	/** 시대 태그 앞 아이콘. 예: history */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|태그")
	TObjectPtr<UTexture2D> PeriodIcon;

	/**
	 * 카드 위쪽 대표 사진. 비우면 그 칸이 통째로 접힌다.
	 *
	 * 시안에서 카드 면적을 가장 많이 차지하는 자리다. 나중에 손으로 돌릴 수 있는
	 * 3D 뷰로 바꾸더라도 레이아웃은 그대로 두고 브러시만 렌더 타깃으로 갈아 끼우면
	 * 되도록, 먼저 정적 사진으로 자리를 잡아 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|본문")
	TObjectPtr<UTexture2D> HeroImage;

	/**
	 * 소개 탭 본문.
	 *
	 * Tabs 가 비었을 때만 쓴다. 탭 기능이 없던 시절의 필드라 남겨 두었다 —
	 * 이미 만들어 둔 DA 가 탭을 채우기 전까지 빈 카드가 되지 않게 하려는 것이다.
	 * 새로 만드는 DA 는 Tabs 에 "소개" 탭을 넣는 쪽을 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|본문", meta = (MultiLine = "true"))
	FText IntroText;

	/**
	 * 카드 가운데 탭. 첫 번째 탭이 카드를 열었을 때 선택된다.
	 *
	 * 비워 두면 카드가 IntroText 로 탭 하나를 만들어 낸다. 그래서 탭을 안 쓰는
	 * DA 도 그대로 동작한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dino|본문")
	TArray<FDinoTab> Tabs;

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
