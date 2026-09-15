#include "NavDestinations.h"

namespace
{
	/** 아이콘 폴더(final §2 H-2). Content/UI/Nav/Icons → /Game/UI/Nav/Icons. */
	const TCHAR* IconDir = TEXT("/Game/UI/Nav/Icons/");

	/** 바닥 발자국·화살표 폴더(final §2 H-3). Content/UI/Nav/Floor → /Game/UI/Nav/Floor. */
	const TCHAR* FloorDir = TEXT("/Game/UI/Nav/Floor/");

	/** 황금 발자국 시트에서 뽑은 텍스처 폴더(Scripts/import_nav_footprints.py 가 만든다). */
	const TCHAR* GoldenDir = TEXT("/Game/UI/Nav/Floor/Golden/");

	/** `/Game/.../T_Name` → `/Game/.../T_Name.T_Name` (텍스처 오브젝트 경로). */
	FString AssetObjectPath(const FString& Dir, const FString& Name)
	{
		return FString::Printf(TEXT("%s%s.%s"), *Dir, *Name, *Name);
	}
}

ENavDestKind FNavDestinations::Classify(const FString& NodeType)
{
	if (NodeType.Equals(TEXT("exhibit"), ESearchCase::IgnoreCase))  { return ENavDestKind::Exhibit; }
	if (NodeType.Equals(TEXT("facility"), ESearchCase::IgnoreCase)) { return ENavDestKind::Facility; }
	if (NodeType.Equals(TEXT("entrance"), ESearchCase::IgnoreCase)) { return ENavDestKind::Entrance; }
	if (NodeType.Equals(TEXT("exit"), ESearchCase::IgnoreCase)) { return ENavDestKind::Entrance; }
	return ENavDestKind::None;
}

FLinearColor FNavDestinations::AccentColor(ENavDestKind Kind)
{
	// final §B 표 — sRGB 16진값을 그대로 옮긴다(디코드해 선형으로).
	switch (Kind)
	{
	case ENavDestKind::Exhibit:  return FLinearColor::FromSRGBColor(FColor(0xEF, 0x7D, 0x1E)); // 주황 #ef7d1e
	case ENavDestKind::Facility: return FLinearColor::FromSRGBColor(FColor(0x2A, 0x78, 0xD6)); // 파랑 #2a78d6
	case ENavDestKind::Entrance: return FLinearColor::FromSRGBColor(FColor(0x0C, 0xA3, 0x0C)); // 초록 #0ca30c
	default:                     return FLinearColor::Gray;
	}
}

int32 FNavDestinations::DinoIndexFromLabel(const FString& Label)
{
	// label 부분일치로 공룡을 가린다(final §2 E-3). "티라노"·"렉스" 둘 다 3 으로 받는다.
	if (Label.Contains(TEXT("트리케라"))) { return 1; }   // Triceratops
	if (Label.Contains(TEXT("브라키오"))) { return 2; }   // Brachiosaurus
	if (Label.Contains(TEXT("티라노")) || Label.Contains(TEXT("렉스"))) { return 3; } // T-Rex
	if (Label.Contains(TEXT("안킬로"))) { return 4; }   // Ankylosaurus
	return 0;
}

int32 FNavDestinations::DisplayNumber(const FString& Label)
{
	FString Name = Label.TrimStartAndEnd().Replace(TEXT(" "), TEXT(""));
	const TCHAR* Names[] = { TEXT("자수정"), TEXT("입구"), TEXT("삼엽충"), TEXT("고사리잎"),
		TEXT("아르켈론"), TEXT("알로사우루스"), TEXT("화석"), TEXT("물고기화석"), TEXT("진주화석"),
		TEXT("포유류화석"), TEXT("거북이화석"), TEXT("출구"), TEXT("탄생석") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		if (Name == Names[Index]) { return Index + 1; }
	}
	return 0;
}

FLinearColor FNavDestinations::AccentColor(const FString& NodeType, const FString& Label)
{
	switch (DisplayNumber(Label))
	{
	case 1: case 13: return FLinearColor(0.56f, 0.13f, 1.f);
	case 2: case 5: case 8: return FLinearColor(0.02f, 0.95f, 1.f);
	case 4: case 11: return FLinearColor(0.18f, 1.f, 0.38f);
	case 12: return FLinearColor(1.f, 0.04f, 0.3f);
	case 3: case 6: case 7: case 9: case 10: return FLinearColor(1.f, 0.43f, 0.08f);
	default:
		if (NodeType.Equals(TEXT("exit"), ESearchCase::IgnoreCase)) { return FLinearColor(1.f, 0.04f, 0.3f); }
		return AccentColor(NodeType);
	}
}

FString FNavDestinations::IconGlyph(const FString& Label)
{
	switch (DisplayNumber(Label))
	{
	case 1: case 13: return TEXT("◇");
	case 2: return TEXT("⇧");
	case 3: return TEXT("삼엽충");
	case 4: return TEXT("잎");
	case 5: case 11: return TEXT("거북");
	case 6: return TEXT("두개골");
	case 7: return TEXT("화석");
	case 8: return TEXT("물고기");
	case 9: return TEXT("조개");
	case 10: return TEXT("포유류");
	case 12: return TEXT("⇨");
	default: return TEXT("•");
	}
}

FString FNavDestinations::IconObjectPath(const FString& NodeType, const FString& Label)
{
	// No matching raster specimen kit is installed. Never substitute old dinosaur artwork.
	if (DisplayNumber(Label) != 0 || NodeType.Equals(TEXT("exit"), ESearchCase::IgnoreCase)) { return FString(); }
	switch (Classify(NodeType))
	{
	case ENavDestKind::Facility: return AssetObjectPath(IconDir, TEXT("T_Icon_Toilet"));
	case ENavDestKind::Entrance: return AssetObjectPath(IconDir, TEXT("T_Icon_Entrance"));
	case ENavDestKind::Exhibit:
	{
		switch (DinoIndexFromLabel(Label))
		{
		case 1: return AssetObjectPath(IconDir, TEXT("T_Icon_EX1_Triceratops"));
		case 2: return AssetObjectPath(IconDir, TEXT("T_Icon_EX2_Brachiosaurus"));
		case 3: return AssetObjectPath(IconDir, TEXT("T_Icon_EX3_TRex"));
		case 4: return AssetObjectPath(IconDir, TEXT("T_Icon_EX4_Ankylosaurus"));
		default: return FString();   // exhibit 인데 공룡을 못 가림 → 아이콘 없음.
		}
	}
	default: return FString();
	}
}

FString FNavDestinations::FloorTextureObjectPath(const FString& NodeType, const FString& Label)
{
	switch (Classify(NodeType))
	{
	// 화장실·입구/출구는 공용 화살표 셰브론 하나로 안내한다(final §C-1 표).
	case ENavDestKind::Facility:
	case ENavDestKind::Entrance:
		return AssetObjectPath(FloorDir, TEXT("T_Floor_Arrow_Facility"));
	case ENavDestKind::Exhibit:
	{
		switch (DinoIndexFromLabel(Label))
		{
		case 1: return AssetObjectPath(FloorDir, TEXT("T_Floor_FP_EX1_Triceratops"));
		case 2: return AssetObjectPath(FloorDir, TEXT("T_Floor_FP_EX2_Brachiosaurus"));
		case 3: return AssetObjectPath(FloorDir, TEXT("T_Floor_FP_EX3_TRex"));
		case 4: return AssetObjectPath(FloorDir, TEXT("T_Floor_FP_EX4_Ankylosaurus"));
		default: return FString();   // exhibit 인데 공룡을 못 가림 → 발자국 없음.
		}
	}
	default: return FString();
	}
}

FNavDestinations::FFloorStyle FNavDestinations::FloorStyle(const FString& NodeType, const FString& Label)
{
	FFloorStyle S;
	S.ArrivalRingTexturePath = AssetObjectPath(GoldenDir, TEXT("T_NavArrivalRing"));
	switch (Classify(NodeType))
	{
	case ENavDestKind::Exhibit:
		S.LeftTexturePath = AssetObjectPath(GoldenDir, TEXT("T_Footprint_Left"));
		S.RightTexturePath = AssetObjectPath(GoldenDir, TEXT("T_Footprint_Right"));
		S.bAlternate = true;
		break;
	case ENavDestKind::Facility:
	case ENavDestKind::Entrance:
		S.LeftTexturePath = S.RightTexturePath = FloorTextureObjectPath(NodeType, Label);
		S.bAlternate = false;
		break;
	default:
		break;
	}
	return S;
}

FString FNavDestinations::ForwardChevronObjectPath()
{
	return AssetObjectPath(GoldenDir, TEXT("T_NavForwardChevron"));
}

FString FNavDestinations::ToParticle(const FString& Word)
{
	if (Word.IsEmpty()) { return TEXT("(으)로"); }
	const TCHAR C = Word[Word.Len() - 1];
	if (C < 0xAC00 || C > 0xD7A3) { return TEXT("(으)로"); }
	// 종성 인덱스 0=받침 없음, 8=ㄹ. 둘 다 "로", 그 밖 받침은 "으로"(국어 조사 규칙).
	const int32 BatchimIndex = (C - 0xAC00) % 28;
	return (BatchimIndex == 0 || BatchimIndex == 8) ? TEXT("로") : TEXT("으로");
}

FString FNavDestinations::SubjectParticle(const FString& Word)
{
	if (Word.IsEmpty()) { return TEXT("이(가)"); }
	const TCHAR C = Word[Word.Len() - 1];
	if (C < 0xAC00 || C > 0xD7A3) { return TEXT("이(가)"); }
	return ((C - 0xAC00) % 28 == 0) ? TEXT("가") : TEXT("이");
}

namespace
{
	/** Label 이 비었을 때의 기본 이름(final §2: exhibit 은 "전시물", 그 밖은 "목적지"). */
	FString ResolveDestName(const FString& NodeType, const FString& Label)
	{
		if (!Label.IsEmpty()) { return Label; }
		return (FNavDestinations::Classify(NodeType) == ENavDestKind::Exhibit)
			? FString(TEXT("전시물")) : FString(TEXT("목적지"));
	}
}

FString FNavDestinations::LexiNavOnText()
{
	return TEXT("바닥의 마커를 비춰주세요!\n제가 지금 위치를 찾아볼게요.");
}

FString FNavDestinations::LexiLocalizedText()
{
	return TEXT("위치를 찾았어요!\n오른쪽 미니맵을 눌러 목적지를 골라주세요.");
}

FString FNavDestinations::LexiGuidingText(const FString& NodeType, const FString& Label, const FNavGuidance& Guidance)
{
	const FString Name = ResolveDestName(NodeType, Label);

	// 1줄: 거의 다 왔으면(bValid 인 유효한 값일 때만) 재촉 문구로 갈아탄다.
	FString Line1;
	if (Guidance.bValid && Guidance.RemainingCm <= 500.f)
	{
		Line1 = TEXT("조금만 더 가면 도착해요!");
	}
	else
	{
		Line1 = FString::Printf(TEXT("%s%s 안내할게요!"), *Name, *ToParticle(Name));
	}

	// 2줄: 서버가 준 턴바이턴 원문이 있으면 그대로, 없으면(경로 시작 직후 등) 안내 수단으로 대체.
	FString Line2;
	if (Guidance.bValid && !Guidance.Instruction.IsEmpty())
	{
		Line2 = Guidance.Instruction;
	}
	else if (Classify(NodeType) == ENavDestKind::Exhibit)
	{
		Line2 = TEXT("바닥의 발자국을 따라오세요.");
	}
	else
	{
		Line2 = TEXT("바닥의 화살표를 따라오세요.");
	}

	return Line1 + TEXT("\n") + Line2;
}

FString FNavDestinations::LexiOffRouteText()
{
	return TEXT("경로에서 조금 벗어났어요.\n발자국이 보이는 곳으로 돌아와 주세요.");
}

FString FNavDestinations::LexiArrivedText(const FString& NodeType, const FString& Label)
{
	const FString Name = ResolveDestName(NodeType, Label);
	switch (Classify(NodeType))
	{
	case ENavDestKind::Exhibit:
		return FString::Printf(
			TEXT("%s 앞에 도착했어요!\n지금 이 공룡의 마커를 스캔하면 AR로 더 자세히 볼 수 있어요."), *Name);
	case ENavDestKind::Facility:
		return FString::Printf(TEXT("%s에 도착했어요!\n안내를 마칠게요."), *Name);
	case ENavDestKind::Entrance:
	default:
		return FString::Printf(TEXT("%s에 도착했어요!\n즐거운 관람 되셨나요?"), *Name);
	}
}

FString FNavDestinations::LexiEndedText(const FString& NodeType)
{
	return (Classify(NodeType) == ENavDestKind::Exhibit)
		? FString(TEXT("안내를 마칠게요.\nAR 스캔 탭에서 마커를 비춰보세요!"))
		: FString(TEXT("안내를 마칠게요.\n또 필요하면 미니맵을 눌러주세요."));
}

FString FNavDestinations::LexiRecognizedText()
{
	return TEXT("인식 완료!\n이제 공룡을 자세히 살펴보세요.");
}

void FNavDestinations::BuildDestinationOrder(const TArray<FNavMapNode>& Nodes, TArray<int32>& OutOrder)
{
	OutOrder.Reset();
	bool bMuseumSpecimens = false;
	for (const FNavMapNode& Node : Nodes)
	{
		const int32 Number = DisplayNumber(Node.Label);
		if (Number != 0 && Number != 2 && Number != 12) { bMuseumSpecimens = true; break; }
	}
	if (bMuseumSpecimens)
	{
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (IsDestination(Nodes[Index].NodeType)) { OutOrder.Add(Index); }
		}
		OutOrder.Sort([&Nodes](int32 A, int32 B)
		{
			const int32 ANumber = DisplayNumber(Nodes[A].Label);
			const int32 BNumber = DisplayNumber(Nodes[B].Label);
			const int32 ARank = ANumber == 0 ? MAX_int32 : ANumber;
			const int32 BRank = BNumber == 0 ? MAX_int32 : BNumber;
			return ARank != BRank ? ARank < BRank : Nodes[A].NodeId < Nodes[B].NodeId;
		});
		return;
	}

	// 위 4칸: 전시물. 공룡번호(1..4) 순으로 정렬해 항상 같은 자리에 온다.
	TArray<int32> Exhibits;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Classify(Nodes[i].NodeType) == ENavDestKind::Exhibit)
		{
			Exhibits.Add(i);
		}
	}
	Exhibits.Sort([&Nodes](int32 A, int32 B)
	{
		return DinoIndexFromLabel(Nodes[A].Label) < DinoIndexFromLabel(Nodes[B].Label);
	});
	OutOrder.Append(Exhibits);

	// 아래 칸: 화장실(들).
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Classify(Nodes[i].NodeType) == ENavDestKind::Facility) { OutOrder.Add(i); }
	}

	// 입구/출구는 **하나만**. 시드에 시작점(예: "A지점")과 "입구·출구" 가 둘 다 entrance 로
	// 들어와 겹치므로, "입구"/"출구" 라벨을 우선 고르고 없으면 첫 entrance 만 남긴다.
	int32 EntranceIdx = INDEX_NONE;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Classify(Nodes[i].NodeType) != ENavDestKind::Entrance) { continue; }
		if (EntranceIdx == INDEX_NONE) { EntranceIdx = i; }
		if (Nodes[i].Label.Contains(TEXT("입구")) || Nodes[i].Label.Contains(TEXT("출구")))
		{
			EntranceIdx = i;
			break;
		}
	}
	if (EntranceIdx != INDEX_NONE) { OutOrder.Add(EntranceIdx); }
}
