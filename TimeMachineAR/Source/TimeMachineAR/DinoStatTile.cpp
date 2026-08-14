#include "DinoStatTile.h"

#include "Components/TextBlock.h"

void UDinoStatTile::SetStat(const FDinoStat& InStat)
{
	Stat = InStat;
	Apply();
}

void UDinoStatTile::NativeConstruct()
{
	Super::NativeConstruct();

	Apply();
}

void UDinoStatTile::Apply()
{
	if (StatLabel != nullptr)
	{
		StatLabel->SetText(Stat.Label);
	}

	if (StatValue != nullptr)
	{
		StatValue->SetText(Stat.Value);
	}

	if (StatSub != nullptr)
	{
		// 아랫줄이 비는 타일도 있다. 빈 칸이 자리를 먹으면 타일 높이가 서로 안 맞는다.
		StatSub->SetText(Stat.Sub);
		StatSub->SetVisibility(Stat.Sub.IsEmpty()
			? ESlateVisibility::Collapsed
			: ESlateVisibility::HitTestInvisible);
	}
}
