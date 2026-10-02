#include "NavDestButton.h"

void UNavDestButton::WireClick()
{
	// 중복 배선 방지 후 연결. ConstructWidget 직후 한 번 부른다.
	OnClicked.RemoveDynamic(this, &UNavDestButton::HandleClicked);
	OnClicked.AddDynamic(this, &UNavDestButton::HandleClicked);
}

void UNavDestButton::HandleClicked()
{
	OnDestClicked.Broadcast(NodeId);
}
