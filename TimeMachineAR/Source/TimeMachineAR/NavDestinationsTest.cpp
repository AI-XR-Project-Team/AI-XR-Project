// FNavDestinations 단위 테스트 (8단계 §B·§D) — 분류·색·아이콘·문구·버튼 순서.
//
// 순수 static 헬퍼라 PIE 없이 검증한다. 커맨드라인:
//   UnrealEditor-Cmd -project=<abs.uproject> \
//     -ExecCmds="Automation RunTests TimeMachineAR.Nav.Destinations; Quit" -unattended -nullrhi -nosplash

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavDestinations.h"
#include "NavTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavDestinationsTest,
	"TimeMachineAR.Nav.Destinations",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavDestinationsTest::RunTest(const FString& Parameters)
{
	// (1) 분류 — 대소문자 무시, 모르는 값은 None. (enum class 라 int32 로 비교)
	auto Kind = [](const TCHAR* T) { return static_cast<int32>(FNavDestinations::Classify(T)); };
	TestEqual(TEXT("exhibit"), Kind(TEXT("exhibit")), static_cast<int32>(ENavDestKind::Exhibit));
	TestEqual(TEXT("EXHIBIT 대문자"), Kind(TEXT("EXHIBIT")), static_cast<int32>(ENavDestKind::Exhibit));
	TestEqual(TEXT("facility"), Kind(TEXT("facility")), static_cast<int32>(ENavDestKind::Facility));
	TestEqual(TEXT("entrance"), Kind(TEXT("entrance")), static_cast<int32>(ENavDestKind::Entrance));
	TestEqual(TEXT("junction=None"), Kind(TEXT("junction")), static_cast<int32>(ENavDestKind::None));
	TestEqual(TEXT("waypoint=None"), Kind(TEXT("waypoint")), static_cast<int32>(ENavDestKind::None));
	TestFalse(TEXT("junction 은 목적지 아님"), FNavDestinations::IsDestination(TEXT("junction")));
	TestTrue(TEXT("exhibit 은 목적지"), FNavDestinations::IsDestination(TEXT("exhibit")));

	// (2) 색 — 세 종류가 서로 다르다(같은 색이 세 UI 에 재사용되므로 값만 확인).
	const FLinearColor Ex = FNavDestinations::AccentColor(TEXT("exhibit"));
	const FLinearColor Fa = FNavDestinations::AccentColor(TEXT("facility"));
	const FLinearColor En = FNavDestinations::AccentColor(TEXT("entrance"));
	TestTrue(TEXT("전시물≠화장실"), !Ex.Equals(Fa));
	TestTrue(TEXT("화장실≠입구"), !Fa.Equals(En));
	// 주황은 R 이 가장 크고, 파랑은 B 가 가장 크다(엉뚱한 채널 매핑 방어).
	TestTrue(TEXT("주황 R>B"), Ex.R > Ex.B);
	TestTrue(TEXT("파랑 B>R"), Fa.B > Fa.R);
	TestTrue(TEXT("초록 G>R,G>B"), En.G > En.R && En.G > En.B);

	// (3) 공룡 번호 — label 로 1..4, 못 가리면 0.
	TestEqual(TEXT("트리케라=1"), FNavDestinations::DinoIndexFromLabel(TEXT("트리케라톱스")), 1);
	TestEqual(TEXT("브라키오=2"), FNavDestinations::DinoIndexFromLabel(TEXT("브라키오사우르스")), 2);
	TestEqual(TEXT("티라노 렉스=3"), FNavDestinations::DinoIndexFromLabel(TEXT("티라노사우루스 렉스")), 3);
	TestEqual(TEXT("안킬로=4"), FNavDestinations::DinoIndexFromLabel(TEXT("안킬로사우르스")), 4);
	TestEqual(TEXT("모르는 이름=0"), FNavDestinations::DinoIndexFromLabel(TEXT("스테고사우루스")), 0);

	// (4) 아이콘 경로 — node_type·label 조합.
	TestEqual(TEXT("EX1 아이콘"),
		FNavDestinations::IconObjectPath(TEXT("exhibit"), TEXT("트리케라톱스")),
		FString(TEXT("/Game/UI/Nav/Icons/T_Icon_EX1_Triceratops.T_Icon_EX1_Triceratops")));
	TestEqual(TEXT("EX3 아이콘(렉스)"),
		FNavDestinations::IconObjectPath(TEXT("exhibit"), TEXT("티라노사우루스 렉스")),
		FString(TEXT("/Game/UI/Nav/Icons/T_Icon_EX3_TRex.T_Icon_EX3_TRex")));
	TestEqual(TEXT("화장실 아이콘"),
		FNavDestinations::IconObjectPath(TEXT("facility"), TEXT("화장실")),
		FString(TEXT("/Game/UI/Nav/Icons/T_Icon_Toilet.T_Icon_Toilet")));
	TestEqual(TEXT("입구 아이콘"),
		FNavDestinations::IconObjectPath(TEXT("entrance"), TEXT("입구·출구")),
		FString(TEXT("/Game/UI/Nav/Icons/T_Icon_Entrance.T_Icon_Entrance")));
	TestTrue(TEXT("junction 아이콘 없음"),
		FNavDestinations::IconObjectPath(TEXT("junction"), TEXT("D지점")).IsEmpty());

	// (5) 조사 — ToParticle("으로/로")·SubjectParticle("이/가").
	TestEqual(TEXT("트리케라톱스→로"), FNavDestinations::ToParticle(TEXT("트리케라톱스")), FString(TEXT("로")));
	TestEqual(TEXT("화장실→로(ㄹ받침)"), FNavDestinations::ToParticle(TEXT("화장실")), FString(TEXT("로")));
	TestEqual(TEXT("입구→로(받침없음)"), FNavDestinations::ToParticle(TEXT("입구")), FString(TEXT("로")));
	TestEqual(TEXT("티라노사우르스 렉스→로"), FNavDestinations::ToParticle(TEXT("티라노사우르스 렉스")), FString(TEXT("로")));
	TestEqual(TEXT("브라키오사우루스→로"), FNavDestinations::ToParticle(TEXT("브라키오사우루스")), FString(TEXT("로")));
	TestEqual(TEXT("안킬로사우루스→로"), FNavDestinations::ToParticle(TEXT("안킬로사우루스")), FString(TEXT("로")));
	TestEqual(TEXT("정문→으로(받침 있음, ㄹ 아님)"), FNavDestinations::ToParticle(TEXT("정문")), FString(TEXT("으로")));
	TestEqual(TEXT("빈 문자열→(으)로"), FNavDestinations::ToParticle(TEXT("")), FString(TEXT("(으)로")));
	TestEqual(TEXT("한글 아님→(으)로"), FNavDestinations::ToParticle(TEXT("Exit")), FString(TEXT("(으)로")));

	TestEqual(TEXT("트리케라톱스→가(받침없음)"), FNavDestinations::SubjectParticle(TEXT("트리케라톱스")), FString(TEXT("가")));
	TestEqual(TEXT("화장실→이(ㄹ받침)"), FNavDestinations::SubjectParticle(TEXT("화장실")), FString(TEXT("이")));
	TestEqual(TEXT("입구→가(받침없음)"), FNavDestinations::SubjectParticle(TEXT("입구")), FString(TEXT("가")));
	TestEqual(TEXT("빈 문자열→이(가)"), FNavDestinations::SubjectParticle(TEXT("")), FString(TEXT("이(가)")));

	// (5b) 렉시 문구 — 상태별 계약(nav-lexi-guide-design.md §2).
	TestTrue(TEXT("NavOn: 마커 안내"), FNavDestinations::LexiNavOnText().Contains(TEXT("마커")));
	TestTrue(TEXT("Localized: 미니맵 안내"), FNavDestinations::LexiLocalizedText().Contains(TEXT("미니맵")));
	TestTrue(TEXT("OffRoute: 이탈 안내"), FNavDestinations::LexiOffRouteText().Contains(TEXT("벗어났")));
	TestTrue(TEXT("Recognized: 인식 완료"), FNavDestinations::LexiRecognizedText().Contains(TEXT("인식 완료")));

	{
		// 멀리 있을 때(Remaining 2800) — 서버 원문(Instruction)이 있으면 2줄에 그대로 실린다.
		FNavGuidance Far;
		Far.bValid = true;
		Far.RemainingCm = 2800.f;
		Far.Instruction = TEXT("앞으로 6m 직진하세요");
		const FString FarText = FNavDestinations::LexiGuidingText(TEXT("exhibit"), TEXT("티라노사우루스 렉스"), Far);
		TestTrue(TEXT("Guiding(멀리): label 포함"), FarText.Contains(TEXT("티라노사우루스 렉스")));
		TestTrue(TEXT("Guiding(멀리): 서버 원문 포함"), FarText.Contains(TEXT("앞으로 6m 직진하세요")));
		TestFalse(TEXT("Guiding(멀리): 재촉 문구 없음"), FarText.Contains(TEXT("조금만 더 가면")));

		// 근접(Remaining 300, ≤500) — "조금만 더 가면" 으로 갈아탄다.
		FNavGuidance Near;
		Near.bValid = true;
		Near.RemainingCm = 300.f;
		const FString NearText = FNavDestinations::LexiGuidingText(TEXT("exhibit"), TEXT("티라노사우루스 렉스"), Near);
		TestTrue(TEXT("Guiding(근접): 조금만 더 가면"), NearText.Contains(TEXT("조금만 더 가면")));

		// 서버 원문이 없으면(경로 시작 직후 등) node_type 으로 발자국/화살표 문구를 대신 쓴다.
		FNavGuidance NoInstruction;
		NoInstruction.bValid = true;
		NoInstruction.RemainingCm = 2000.f;
		TestTrue(TEXT("Guiding(전시물, 원문 없음): 발자국"),
			FNavDestinations::LexiGuidingText(TEXT("exhibit"), TEXT("화장실"), NoInstruction).Contains(TEXT("발자국")));
		TestTrue(TEXT("Guiding(시설, 원문 없음): 화살표"),
			FNavDestinations::LexiGuidingText(TEXT("facility"), TEXT("화장실"), NoInstruction).Contains(TEXT("화살표")));

		// Label 이 비면 exhibit="전시물", 그 밖="목적지".
		TestTrue(TEXT("Guiding(exhibit, label 없음): 전시물"),
			FNavDestinations::LexiGuidingText(TEXT("exhibit"), TEXT(""), Far).Contains(TEXT("전시물")));
		TestTrue(TEXT("Guiding(facility, label 없음): 목적지"),
			FNavDestinations::LexiGuidingText(TEXT("facility"), TEXT(""), Far).Contains(TEXT("목적지")));
	}

	// (5c) 도착·종료 문구 — node_type 별 갈래 + label 포함.
	TestTrue(TEXT("전시물 도착에 label 포함"),
		FNavDestinations::LexiArrivedText(TEXT("exhibit"), TEXT("트리케라톱스")).Contains(TEXT("트리케라톱스")));
	TestTrue(TEXT("전시물 도착: 스캔 안내"),
		FNavDestinations::LexiArrivedText(TEXT("exhibit"), TEXT("트리케라톱스")).Contains(TEXT("스캔")));
	TestTrue(TEXT("시설 도착에 label 포함"),
		FNavDestinations::LexiArrivedText(TEXT("facility"), TEXT("화장실")).Contains(TEXT("화장실")));
	TestTrue(TEXT("입구 도착: 관람 인사"),
		FNavDestinations::LexiArrivedText(TEXT("entrance"), TEXT("입구·출구")).Contains(TEXT("관람")));
	TestTrue(TEXT("도착(label 없음, exhibit): 전시물"),
		FNavDestinations::LexiArrivedText(TEXT("exhibit"), TEXT("")).Contains(TEXT("전시물")));

	TestTrue(TEXT("Ended(exhibit): AR 스캔 안내"), FNavDestinations::LexiEndedText(TEXT("exhibit")).Contains(TEXT("AR 스캔")));
	TestTrue(TEXT("Ended(facility): 미니맵 안내"), FNavDestinations::LexiEndedText(TEXT("facility")).Contains(TEXT("미니맵")));

	// (6) 버튼 순서 — 위 4칸 전시물(EX1..EX4), 아래 화장실·입구. 비목적지는 제외.
	{
		TArray<FNavMapNode> Nodes;
		auto Add = [&Nodes](const FString& Id, const FString& Type, const FString& Label)
		{
			FNavMapNode N; N.NodeId = Id; N.NodeType = Type; N.Label = Label; Nodes.Add(N);
		};
		// 일부러 뒤섞어 넣는다 — 정렬이 자리를 바로잡아야 한다.
		Add(TEXT("id-b"), TEXT("waypoint"), TEXT("B지점"));
		Add(TEXT("id-g"), TEXT("exhibit"),  TEXT("브라키오사우르스"));  // EX2
		Add(TEXT("id-a2"), TEXT("entrance"), TEXT("A지점"));            // 중복 entrance → 제거돼야
		Add(TEXT("id-a"), TEXT("entrance"), TEXT("입구·출구"));
		Add(TEXT("id-l"), TEXT("exhibit"),  TEXT("안킬로사우르스"));    // EX4
		Add(TEXT("id-e"), TEXT("facility"), TEXT("화장실"));
		Add(TEXT("id-f"), TEXT("exhibit"),  TEXT("트리케라톱스"));      // EX1
		Add(TEXT("id-i"), TEXT("exhibit"),  TEXT("티라노사우루스 렉스")); // EX3

		TArray<int32> Order;
		FNavDestinations::BuildDestinationOrder(Nodes, Order);
		TestEqual(TEXT("목적지 6개만(중복 entrance 제거)"), Order.Num(), 6);
		TestEqual(TEXT("1칸=EX1 트리케라톱스"), Nodes[Order[0]].NodeId, FString(TEXT("id-f")));
		TestEqual(TEXT("2칸=EX2 브라키오"),   Nodes[Order[1]].NodeId, FString(TEXT("id-g")));
		TestEqual(TEXT("3칸=EX3 티라노렉스"), Nodes[Order[2]].NodeId, FString(TEXT("id-i")));
		TestEqual(TEXT("4칸=EX4 안킬로"),     Nodes[Order[3]].NodeId, FString(TEXT("id-l")));
		TestEqual(TEXT("5칸=화장실"),         Nodes[Order[4]].NodeId, FString(TEXT("id-e")));
		TestEqual(TEXT("6칸=입구/출구(A지점 아님)"), Nodes[Order[5]].NodeId, FString(TEXT("id-a")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
