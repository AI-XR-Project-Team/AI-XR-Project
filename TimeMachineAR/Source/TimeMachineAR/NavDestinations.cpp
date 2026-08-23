#include "NavDestinations.h"

namespace
{
	/** 아이콘 폴더(final §2 H-2). Content/UI/Nav/Icons → /Game/UI/Nav/Icons. */
	const TCHAR* IconDir = TEXT("/Game/UI/Nav/Icons/");

	/** 바닥 발자국·화살표 폴더(final §2 H-3). Content/UI/Nav/Floor → /Game/UI/Nav/Floor. */
	const TCHAR* FloorDir = TEXT("/Game/UI/Nav/Floor/");

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

FString FNavDestinations::IconObjectPath(const FString& NodeType, const FString& Label)
{
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

FString FNavDestinations::GuidingText(const FString& NodeType)
{
	return (Classify(NodeType) == ENavDestKind::Exhibit)
		? FString(TEXT("공룡발자국을 따라가주세요"))
		: FString(TEXT("안내화살표를 따라가주세요"));
}

FString FNavDestinations::ArrivalText(const FString& NodeType, const FString& Label)
{
	if (Classify(NodeType) == ENavDestKind::Exhibit)
	{
		const FString Name = Label.IsEmpty() ? FString(TEXT("전시물")) : Label;
		return FString::Printf(TEXT("%s 앞에 도착했습니다. 앞에 공룡 발자국을 인식시켜주세요"), *Name);
	}
	return TEXT("목적지에 도착하였습니다");
}

void FNavDestinations::BuildDestinationOrder(const TArray<FNavMapNode>& Nodes, TArray<int32>& OutOrder)
{
	OutOrder.Reset();

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

	// 아래 2칸: 화장실 → 입구/출구.
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Classify(Nodes[i].NodeType) == ENavDestKind::Facility) { OutOrder.Add(i); }
	}
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (Classify(Nodes[i].NodeType) == ENavDestKind::Entrance) { OutOrder.Add(i); }
	}
}
