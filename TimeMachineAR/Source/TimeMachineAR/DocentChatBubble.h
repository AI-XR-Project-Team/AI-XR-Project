#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DocentChatBubble.generated.h"

class UTextBlock;

/**
 * 대화 말풍선 하나.
 *
 * WBP 로 상속해 모양만 잡는다. 필요한 자식 위젯:
 *   - MessageText (Text Block)  : 필수. 이름이 다르면 WBP 컴파일이 에러로 알려준다.
 *
 * 색·정렬 같은 역할별 스타일은 OnRoleApplied 에서 블루프린트로 처리한다.
 * C++ 이 스타일을 강제하지 않는 이유는, 디자인이 바뀔 때마다 재컴파일하지
 * 않게 하기 위함이다.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UDocentChatBubble : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 말풍선을 채운다. 도슨트 응답은 빈 문자열로 만들어 두고 델타로 채워 나간다. */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void Setup(bool bInIsUser, const FString& InText);

	/** 스트리밍 조각을 뒤에 이어 붙인다. */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void AppendText(const FString& InText);

	/** 지금까지의 전체 본문. */
	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	FString GetText() const { return Body; }

	/** 본문을 통째로 바꾼다. 응답을 못 받았을 때 오류 문구로 대체하는 용도. */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void SetText(const FString& InText);

	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	bool IsUserBubble() const { return bIsUser; }

protected:
	/**
	 * 역할이 정해진 직후 호출된다. 배경색·정렬·아바타 표시를 여기서 처리한다.
	 * Setup 에서 불리므로 위젯이 화면에 붙기 전에 실행된다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Docent|Chat")
	void OnRoleApplied(bool bInIsUser);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Docent|Chat")
	TObjectPtr<UTextBlock> MessageText;

private:
	/** 화면 갱신은 MessageText 를 거치지만, 이어 붙이기의 기준은 이 값이다. */
	FString Body;

	bool bIsUser = false;
};
