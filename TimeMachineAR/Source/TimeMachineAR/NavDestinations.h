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
	/** Stable museum specimen number, independent of graph array order; zero for legacy labels. */
	static int32 DisplayNumber(const FString& Label);
	/** Shared reference palette and truthful native icon fallback for museum destinations. */
	static FLinearColor AccentColor(const FString& NodeType, const FString& Label);
	static FString IconGlyph(const FString& Label);

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

	/**
	 * 바닥 안내 스타일(황금 발자국). 액터가 목적지가 바뀔 때 한 번 조회한다.
	 *
	 *  - exhibit : `/Game/UI/Nav/Floor/Golden/T_Footprint_Left`·`_Right` 한 쌍을 좌우 교대로 깐다.
	 *              시안(02 시트)이 발자국 한 종만 주므로 공룡 4종이 같은 황금 발자국을 쓴다 —
	 *              공룡별 텍스처 분기(`FloorTextureObjectPath`)는 남겨 두었고 공룡별 황금 쌍이
	 *              생기면 여기서만 갈라 주면 된다.
	 *  - facility·entrance : 기존 공용 화살표(`FloorTextureObjectPath`) 한 장을 좌우 없이 경로 위에.
	 *  - 목적지 아님 : 텍스처 경로 비움(액터는 숨긴다).
	 * 도착 링은 종류에 관계없이 `T_NavArrivalRing`.
	 */
	struct FFloorStyle
	{
		FString LeftTexturePath;
		FString RightTexturePath;
		FString ArrivalRingTexturePath;
		/** true 면 StepIndex 짝/홀로 좌우 텍스처를 번갈아 쓰고 가로 오프셋을 건다. */
		bool bAlternate = false;
		bool IsValid() const { return !LeftTexturePath.IsEmpty() && !RightTexturePath.IsEmpty(); }
	};
	static FFloorStyle FloorStyle(const FString& NodeType, const FString& Label);

	/** HUD 전방 셰브론(거리 배지 옆) 텍스처 오브젝트 경로. */
	static FString ForwardChevronObjectPath();

	// ---- 조사(助詞) 헬퍼. 받침 유무로 자연스러운 한국어 문구를 만든다 ----

	/**
	 * 받침 유무로 "으로/로" 를 고른다(ㄹ 받침·받침 없음 → "로", 그 밖 받침 → "으로").
	 * 한글이 아니면 "(으)로". 안내 로그 문구("{목적지}로 안내할게요!")에 쓴다.
	 */
	static FString ToParticle(const FString& Word);

	/**
	 * 받침 유무로 "이/가" 를 고른다. 한글이 아니면 "이(가)".
	 * (TimeRevealComponent.cpp 의 익명 함수를 여기로 옮겼다 — 회중시계 연출 문구도 같은 규칙을 쓴다.)
	 */
	static FString SubjectParticle(const FString& Word);

	// ---- 안내 로그 문구(렉시 말풍선). 안내 수단이 node_type 으로 갈린다 ----
	//
	// 전부 "렉시가 하는 말"로 통일한다(nav-lexi-guide-design.md §2). Label 이 비면
	// exhibit 은 "전시물", 그 밖은 "목적지"로 채운다.

	/** 네비 켬, 아직 측위 전. */
	static FString LexiNavOnText();

	/** 측위 성립, 목적지 미정. */
	static FString LexiLocalizedText();

	/**
	 * 안내 중. RemainingCm ≤ 500 이면 "조금만 더 가면" 문구로 갈아탄다.
	 * 2줄은 서버 원문(Instruction)이 있으면 그걸, 없으면 발자국/화살표 문구로 채운다.
	 */
	static FString LexiGuidingText(const FString& NodeType, const FString& Label, const FNavGuidance& Guidance);

	/** 경로 이탈(§3 OffRoute 단계). */
	static FString LexiOffRouteText();

	/** 목적지 도착. exhibit/facility/entrance 로 마무리 문구가 갈린다. */
	static FString LexiArrivedText(const FString& NodeType, const FString& Label);

	/** 도착 후 잠깐의 마무리 문구(§4 자동 종료의 Ended 단계). */
	static FString LexiEndedText(const FString& NodeType);

	/** 전시물 마커 인식 완료(초록). */
	static FString LexiRecognizedText();

	/**
	 * 그래프 노드 중 목적지 6종을 **버튼 배치 순서**로 골라 인덱스를 준다.
	 * 위 4칸 = 전시물(공룡번호 1..4 순), 아래 2칸 = 화장실 · 입구/출구 (final §B-2).
	 * 목적지가 아닌 노드는 건너뛴다. 있는 것만 담는다(없으면 그 칸은 안 생긴다).
	 */
	static void BuildDestinationOrder(const TArray<FNavMapNode>& Nodes, TArray<int32>& OutOrder);
};
