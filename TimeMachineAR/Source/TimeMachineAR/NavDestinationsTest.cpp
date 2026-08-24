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

	// (5) 안내 문구 — 발자국/화살표 분기, 도착 문구에 label.
	TestEqual(TEXT("전시물 안내=발자국"),
		FNavDestinations::GuidingText(TEXT("exhibit")), FString(TEXT("공룡발자국을 따라가주세요")));
	TestEqual(TEXT("화장실 안내=화살표"),
		FNavDestinations::GuidingText(TEXT("facility")), FString(TEXT("안내화살표를 따라가주세요")));
	TestEqual(TEXT("입구 안내=화살표"),
		FNavDestinations::GuidingText(TEXT("entrance")), FString(TEXT("안내화살표를 따라가주세요")));
	TestTrue(TEXT("전시물 도착에 label 포함"),
		FNavDestinations::ArrivalText(TEXT("exhibit"), TEXT("트리케라톱스")).Contains(TEXT("트리케라톱스")));
	TestEqual(TEXT("화장실 도착 문구"),
		FNavDestinations::ArrivalText(TEXT("facility"), TEXT("화장실")),
		FString(TEXT("목적지에 도착하였습니다")));

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
