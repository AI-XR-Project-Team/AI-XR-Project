#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoInfoData.h"		// FDinoTab 을 값으로 들고 있어 전방 선언으로는 안 된다
#include "DinoInfoCardWidget.generated.h"

class UButton;
class UDinoInfoData;
class UDinoStatTile;
class UDinoTabButton;
class UImage;
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
 * 3D 모델 뷰는 아직 없다.
 *
 * WBP 로 상속할 때 필요한 자식 위젯 (이름이 다르면 WBP 컴파일이 알려준다):
 *   - NameKoText   (Text Block)        : 필수
 *   - NumberText   (Text Block)        : 선택. 왼쪽 위 번호
 *   - NameSciText  (Text Block)        : 선택. 학명
 *   - DietTagText  (Text Block)        : 선택. 육식동물 등
 *   - PeriodTagText(Text Block)        : 선택. 백악기 후기
 *   - PeriodSubText(Text Block)        : 선택. 약 6,800만 년 전
 *   - TabBar       (Horizontal Box)    : 선택. 탭 버튼이 붙는 자리
 *   - TabHeading   (Text Block)        : 선택. 본문 위 굵은 제목
 *   - TabImage     (Image)             : 선택. 본문 위 사진
 *   - IntroLabel   (Text Block)        : 선택. 선택된 탭의 본문
 *   - StatBox      (Wrap/Horizontal Box) : 선택. 요약 타일이 붙는 자리
 *   - CloseButton  (Button)            : 선택. 카드 닫기
 *   - AskDocentButton (Button)         : 선택. 도슨트 CTA
 *   - CardPanel    (아무 위젯)          : 선택. 열고 닫을 대상
 *
 * 그리고 클래스 기본값에서 StatTileClass 와 TabButtonClass 를 지정해야
 * 요약 타일과 탭 버튼이 나온다.
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
	 * 탭을 고른다. 범위를 벗어나면 아무 일도 하지 않는다.
	 *
	 * 탭 버튼이 부르지만, 카드를 특정 탭으로 열고 싶을 때 BP 에서 직접 불러도 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dino|Card")
	void SelectTab(int32 TabIndex);

	UFUNCTION(BlueprintPure, Category = "Dino|Card")
	int32 GetSelectedTab() const { return SelectedTabIndex; }

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

	/** 탭 버튼이 붙는 자리. 카드를 채울 때마다 비우고 새로 만든다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UPanelWidget> TabBar;

	/** 선택된 탭의 사진. 탭에 사진이 없으면 접힌다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UImage> TabImage;

	/** 선택된 탭의 굵은 제목. 비면 접힌다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> TabHeading;

	/** 선택된 탭의 본문. 탭이 없던 시절 이름이라 Intro 지만 모든 탭이 여기 쓴다. */
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

	/** 탭 버튼으로 쓸 위젯 클래스. WBP_DinoTabButton 을 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Card")
	TSubclassOf<UDinoTabButton> TabButtonClass;

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

	/** 탭 버튼을 지우고 ResolvedTabs 개수만큼 새로 만든다. */
	void RebuildTabs();

	/** 선택된 탭의 제목·사진·본문을 칸에 뿌린다. */
	void ApplyTabContent();

	/**
	 * 표시할 탭 목록을 정한다.
	 *
	 * Tabs 가 비어 있으면 IntroText 로 탭 하나를 만들어 낸다. 탭이 생기기 전에
	 * 만든 DA 도 카드가 빈 채로 열리지 않게 하기 위함이다.
	 */
	void ResolveTabs();

	/** 탭 버튼의 네이티브 델리게이트 수신부. */
	void HandleTabClicked(int32 TabIndex);

	/** 값이 비면 칸을 접는다. 빈 줄이 남으면 카드 여백이 들쭉날쭉해진다. */
	static void SetTextOrCollapse(UTextBlock* Block, const FText& Value);

	UPROPERTY()
	TObjectPtr<UDinoInfoData> CurrentInfo;

	/** 이번 카드가 실제로 보여 줄 탭. DA 의 Tabs 이거나, 비면 IntroText 로 만든 하나. */
	TArray<FDinoTab> ResolvedTabs;

	/** 만들어 둔 탭 버튼. 선택이 바뀔 때 이전 것을 꺼야 해서 들고 있는다. */
	UPROPERTY()
	TArray<TObjectPtr<UDinoTabButton>> TabButtons;

	int32 SelectedTabIndex = 0;

	bool bIsOpen = false;
};
