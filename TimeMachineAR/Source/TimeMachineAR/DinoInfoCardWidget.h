#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoInfoCardWidget.generated.h"

class UButton;
class UDinoInfoData;
class UDinoStatTile;
class UPanelWidget;
class UTextBlock;

/** 카드의 "AI 도슨트에게 질문하기" 를 눌렀을 때. 대상 전시물 UUID 를 넘긴다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAskDocentClicked, const FString&, ExhibitId);

/**
 * 공룡을 탭했을 때 뜨는 정보 카드.
 *
 * 내용 채우기·열고 닫기를 C++ 이 맡고 WBP 는 배치와 스타일만 잡는다.
 * 도슨트 챗과 같은 규칙이다(DocentChatWidget 참고).
 *
 * 1단계 범위: 번호·이름·학명·태그·소개·요약 타일·닫기.
 * 탭 메뉴(소개/특징/서식지/발견)와 3D 모델 뷰는 다음 단계다.
 *
 * WBP 로 상속할 때 필요한 자식 위젯 (이름이 다르면 WBP 컴파일이 알려준다):
 *   - NameKoText   (Text Block)        : 필수
 *   - NumberText   (Text Block)        : 선택. 왼쪽 위 번호
 *   - NameSciText  (Text Block)        : 선택. 학명
 *   - DietTagText  (Text Block)        : 선택. 육식동물 등
 *   - PeriodTagText(Text Block)        : 선택. 백악기 후기
 *   - PeriodSubText(Text Block)        : 선택. 약 6,800만 년 전
 *   - IntroLabel   (Text Block)        : 선택. 소개 본문
 *   - StatBox      (Wrap/Horizontal Box) : 선택. 요약 타일이 붙는 자리
 *   - CloseButton  (Button)            : 선택. 카드 닫기
 *   - AskDocentButton (Button)         : 선택. 도슨트 CTA
 *   - CardPanel    (아무 위젯)          : 선택. 열고 닫을 대상
 *
 * 그리고 클래스 기본값에서 StatTileClass 를 지정해야 요약 타일이 나온다.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UDinoInfoCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 카드를 이 공룡 내용으로 채우고 연다.
	 *
	 * 다른 공룡을 탭하면 그대로 다시 부르면 된다. 내용을 갈아 끼운다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dino|Card")
	void ShowFor(UDinoInfoData* InInfo);

	/** 카드를 닫는다. 내용은 남겨 두므로 같은 공룡이면 다시 채울 필요가 없다. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Card")
	void HideCard();

	UFUNCTION(BlueprintPure, Category = "Dino|Card")
	bool IsCardOpen() const { return bIsOpen; }

	/** 지금 카드가 보여 주고 있는 공룡. 없으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "Dino|Card")
	UDinoInfoData* GetCurrentInfo() const { return CurrentInfo; }

	/**
	 * 도슨트 버튼이 눌렸을 때. 도슨트 챗 위젯이 여기에 붙는다.
	 *
	 * 카드가 직접 챗 위젯을 들고 있지 않은 이유는, 챗이 화면 어디에 어떻게
	 * 붙어 있는지를 카드가 알 필요가 없기 때문이다. 연결은 BP 에서 한다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Card")
	FOnAskDocentClicked OnAskDocentClicked;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ------------------------------------------------------------ 바인딩 위젯

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Dino|Card")
	TObjectPtr<UTextBlock> NameKoText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> NumberText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> NameSciText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> DietTagText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> PeriodTagText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> PeriodSubText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> IntroLabel;

	/** 요약 타일이 붙는 자리. 카드가 채울 때마다 비우고 새로 만든다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UPanelWidget> StatBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UButton> AskDocentButton;

	/**
	 * 열고 닫을 대상. 보통 카드 전체를 감싼 패널이다.
	 *
	 * 이 위젯 자체를 숨기지 않는 이유는 도슨트 챗과 같다 — 루트를 숨기면
	 * 카드 밖에 둔 다른 UI 까지 같이 사라진다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UWidget> CardPanel;

	/** 요약 타일로 쓸 위젯 클래스. WBP_DinoStatTile 을 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Card")
	TSubclassOf<UDinoStatTile> StatTileClass;

	/** 시작할 때 카드를 닫아 둘지. AR 화면을 가리지 않도록 기본은 닫힘. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dino|Card")
	bool bStartHidden = true;

private:
	/** UButton::OnClicked 는 다이나믹이라 UFUNCTION 이어야 한다. */
	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleAskDocentClicked();

	/** CurrentInfo 를 각 위젯에 뿌린다. */
	void ApplyInfo();

	/** 요약 타일을 지우고 Stats 개수만큼 새로 만든다. */
	void RebuildStats();

	/** 값이 비면 칸을 접는다. 빈 줄이 남으면 카드 여백이 들쭉날쭉해진다. */
	static void SetTextOrCollapse(UTextBlock* Block, const FText& Value);

	UPROPERTY()
	TObjectPtr<UDinoInfoData> CurrentInfo;

	bool bIsOpen = false;
};
