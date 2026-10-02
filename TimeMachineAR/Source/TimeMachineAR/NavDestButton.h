#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "NavDestButton.generated.h"

/** 목적지 버튼 클릭. 어느 노드인지(node_id)를 실어 준다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavDestButtonClicked, const FString&, NodeId);

/**
 * 하단 목적지 리스트 버튼 하나(8단계 §B-2). UButton 의 OnClicked 는 누가 눌렀는지 안 알려줘서,
 * 버튼마다 자기 node_id 를 들고 클릭 시 그것을 실어 재방송한다. WBP 없이 C++ 로 동적 생성한다
 * (WidgetTree->ConstructWidget). 스타일(아이콘·문구·테두리색)은 NavFullMapWidget 이 채운다.
 */
UCLASS()
class TIMEMACHINEAR_API UNavDestButton : public UButton
{
	GENERATED_BODY()

public:
	/** 이 버튼이 가리키는 목적지 노드 id. 클릭하면 이 값으로 경로를 잡는다. */
	FString NodeId;

	/** 클릭 시 node_id 를 실어 방송한다. */
	UPROPERTY()
	FOnNavDestButtonClicked OnDestClicked;

	/** OnClicked 에 내부 핸들러를 물려 재방송을 잇는다. 생성 직후 한 번 부른다. */
	void WireClick();

private:
	UFUNCTION()
	void HandleClicked();
};
