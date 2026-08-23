#pragma once

#include "CoreMinimal.h"
#include "NavTypes.h"

/**
 * 목적지 분류·색·아이콘·안내 문구를 한곳에 모은 **순수 헬퍼**(8단계 §B·§D).
 *
 * 지도 마름모·하단 버튼·안내 로그 세 곳이 같은 규칙(색·아이콘·문구)을 써야 하므로
 * (final §B "같은 색이 세 곳에 똑같이 쓰인다") 규칙을 위젯마다 복붙하지 않고 여기서
 * 한 번만 정의한다. 전부 static 이라 UObject·월드 없이 헤드리스 자동화 테스트로 검증한다
 * (`TimeMachineAR.Nav.Destinations`).
 *
 * 분기 기준은 서버가 이미 주는 `node_type`·`label` 뿐이다(새 데이터 0). 공룡 4종 구별은
 * `label` 로만 한다(final §2 E-3) — node_type 은 전부 "exhibit" 라 서로 못 가린다.
 */

/** 목적지 종류. node_type 을 UI 관점으로 줄인 것. 순수 C++ 전용이라 리플렉션 없음. */
enum class ENavDestKind : uint8
{
	/** 목적지가 아니다(junction·waypoint). 아이콘·버튼을 만들지 않는다. */
	None,
	/** 전시물(공룡). 주황. 발자국으로 안내. */
	Exhibit,
	/** 편의시설(화장실). 파랑. 화살표로 안내. */
	Facility,
	/** 입구·출구. 초록. 화살표로 안내. */
	Entrance,
};

struct TIMEMACHINEAR_API FNavDestinations
{
	/** node_type 문자열 → 종류. 대소문자 무시. 모르는 값은 None. */
	static ENavDestKind Classify(const FString& NodeType);

	/** 지도 마름모·버튼 테두리·AR 마름모에 쓰는 강조색(final §B 표). */
	static FLinearColor AccentColor(ENavDestKind Kind);
	static FLinearColor AccentColor(const FString& NodeType)
	{
		return AccentColor(Classify(NodeType));
	}

	/** 목적지(아이콘을 그릴 대상)면 true. exhibit·facility·entrance 만 참. */
	static bool IsDestination(const FString& NodeType)
	{
		return Classify(NodeType) != ENavDestKind::None;
	}

	/**
	 * label 로 공룡을 가려 1..4 를 준다(트리케라톱스=1 … 안킬로사우르스=4). 못 가리면 0.
	 * EX1~EX4 아이콘 선택과 버튼 정렬 순서를 이 번호 하나로 정한다.
	 */
	static int32 DinoIndexFromLabel(const FString& Label);

	/**
	 * node_type·label 에 맞는 아이콘 텍스처 오브젝트 경로(`/Game/UI/Nav/Icons/...`).
	 * 목적지가 아니거나 공룡을 못 가리면 빈 문자열. 위젯이 이 경로로 LoadObject 한다
	 * (에디터에서 에셋을 손으로 할당할 필요가 없다, final §2 H-2).
	 */
	static FString IconObjectPath(const FString& NodeType, const FString& Label);

	/**
	 * 바닥 AR 발자국·화살표 텍스처 오브젝트 경로(`/Game/UI/Nav/Floor/...`, 9단계 §C-1).
	 * 전시물(exhibit) → 그 공룡의 발자국 4종, 화장실·입구/출구 → 공용 화살표 셰브론.
	 * 목적지가 아니거나 전시물인데 공룡을 못 가리면 빈 문자열. 액터가 이 경로로 LoadObject 한다
	 * (에디터에서 손 할당 불필요, final §2 H-3). 아이콘(Icons)과 폴더·설정이 다른 별도 텍스처다.
	 */
	static FString FloorTextureObjectPath(const FString& NodeType, const FString& Label);

	// ---- 안내 로그 문구(final §D 표). 안내 수단이 node_type 으로 갈린다 ----

	/** 안내 중 문구. exhibit → 발자국, facility·entrance → 화살표. */
	static FString GuidingText(const FString& NodeType);

	/** 도착 문구. exhibit 는 "(공룡) 앞에 도착…", 그 밖은 "목적지에 도착하였습니다". */
	static FString ArrivalText(const FString& NodeType, const FString& Label);

	/** 전시물 마커 인식 완료(초록). */
	static FString RecognizedText()
	{
		return TEXT("인식이 완료되었습니다. 이제 공룡을 관찰해주세요");
	}

	/**
	 * 그래프 노드 중 목적지 6종을 **버튼 배치 순서**로 골라 인덱스를 준다.
	 * 위 4칸 = 전시물(공룡번호 1..4 순), 아래 2칸 = 화장실 · 입구/출구 (final §B-2).
	 * 목적지가 아닌 노드는 건너뛴다. 있는 것만 담는다(없으면 그 칸은 안 생긴다).
	 */
	static void BuildDestinationOrder(const TArray<FNavMapNode>& Nodes, TArray<int32>& OutOrder);
};
