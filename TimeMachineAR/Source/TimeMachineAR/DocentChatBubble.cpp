#include "DocentChatBubble.h"

#include "Components/TextBlock.h"

void UDocentChatBubble::Setup(bool bInIsUser, const FString& InText)
{
	bIsUser = bInIsUser;
	Body = InText;

	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Body));
	}

	OnRoleApplied(bIsUser);
}

void UDocentChatBubble::AppendText(const FString& InText)
{
	if (InText.IsEmpty())
	{
		return;
	}

	// FText 를 매번 문자열로 되돌리지 않고 Body 를 기준으로 삼는다. 델타가
	// 수십 번 오는 경로라 왕복 변환 비용이 쌓인다.
	Body += InText;

	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Body));
	}
}

void UDocentChatBubble::SetText(const FString& InText)
{
	Body = InText;

	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Body));
	}
}
