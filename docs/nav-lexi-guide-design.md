# 내비게이션 안내 로그 → 렉시 말풍선 + 도착 후 자동 종료 — 설계 (Opus, 2026-09-14)

구현: Sonnet. 검증·패키징·실기기: Opus. 브랜치 `feature/clock-time-reveal`.

## 0. 현재 상태(조사 결과)

- 상단 안내 문구 = `UNavGuideLogWidget`(C++ 로 트리 구성, `AddToViewport(200)`, 미니맵이 소유). 문구는 `ENavGuidePhase`
  (NavOn / Localized / FullMapOpen / Guiding / Arrived / Recognized) + `FNavDestinations::GuidingText/ArrivalText/RecognizedText`.
  표시/숨김은 0.12s 월드 타이머 `SyncWithMinimap`(미니맵이 실제로 그려질 때만). 확대 지도가 열리면 전체지도가 자기 렉시 말풍선을 가짐.
- 도착 판정 = `UNavRouteProgress::UpdatePose().bArrived`(80cm + 해제 히스테리시스 40cm). 미니맵 `SetCurrentPose` 가 매 pose 마다
  `OnGuidanceUpdated(FNavGuidance)` 를 방송(bArrived, RemainingCm, Instruction, Turn, NextTurn, StepRemainingCm).
- 도착 후 자동으로 끝나는 경로는 **없다**. 경로 정리는 `UNavMinimapWidget::ClearRoute()` + `SetDestinationNode("")`.
- 탭 전환은 `UDocentChatWidget::SetHudTab(EDocentHudTab)`(방금 추가, 단일 소유자). NavPanel 안에 `WBP_NavMinimap`(UNavMinimapWidget 자식)이 있다.
- 렉시 스킨 텍스처(이미 패키징됨): `/Game/UI/Nav/Skin/lexi_pointing`, `lexi_happy`, `lexi_idle`, `lexi_question`, `lexi_wink`, `bubble_speech`.
  스캔 HUD 의 렉시 말풍선(참고 스타일): 유리 패널 `FColor(18,22,30,215)` 둥근 모서리, 외곽선 강조색 1.5px, 왼쪽 로봇 아바타, 흰 본문.

## 1. 요구

1. 내비 상단 문구 전부를 **렉시의 말**로(말풍선 UI + 렉시 아바타 + "렉시" 이름표), 목적지 이름을 넣어 상황별로.
2. 목적지에 **도착하면 자동으로 내비게이션 종료**: 도착 문구 → 잠시 뒤 마무리 문구 → 경로/목적지 정리 → 스캔 탭으로 복귀.

## 2. 문구 계약 (`FNavDestinations`, 순수 함수 — 테스트 대상)

기존 `GuidingText/ArrivalText/RecognizedText` 는 **교체**한다(호출처는 GuideLog 와 테스트뿐). 새 API:

```cpp
/** 받침 유무로 "으로/로" (ㄹ 받침·받침 없음 → "로"). 한글 아니면 "(으)로". */
static FString ToParticle(const FString& Word);          // "트리케라톱스"→"로", "화장실"→"로"(ㄹ), "입구"→"로", "티라노사우르스 렉스"→"로", "브라키오사우루스"→"로", "안킬로사우루스"→"로", "화장실" ㄹ→"로", "정문"→"으로"
/** 받침 유무로 "이/가". */
static FString SubjectParticle(const FString& Word);     // (TimeRevealComponent.cpp 의 익명 함수를 여기로 옮기고 그쪽은 이걸 호출)

static FString LexiNavOnText();          // "바닥의 마커를 비춰주세요!\n제가 지금 위치를 찾아볼게요."
static FString LexiLocalizedText();      // "위치를 찾았어요!\n오른쪽 미니맵을 눌러 목적지를 골라주세요."
static FString LexiGuidingText(NodeType, Label, const FNavGuidance& G);
   // 1줄: RemainingCm ≤ 500 → "조금만 더 가면 도착해요!"  그 외 → "{Label}{으로/로} 안내할게요!"
   // 2줄: G.bValid && !G.Instruction.IsEmpty() → G.Instruction(서버 원문, 예 "앞으로 6m 직진하세요")
   //      아니면 exhibit → "바닥의 발자국을 따라오세요." / facility·entrance → "바닥의 화살표를 따라오세요."
static FString LexiOffRouteText();       // "경로에서 조금 벗어났어요.\n발자국이 보이는 곳으로 돌아와 주세요."
static FString LexiArrivedText(NodeType, Label);
   // exhibit → "{Label} 앞에 도착했어요!\n지금 이 공룡의 마커를 스캔하면 AR로 더 자세히 볼 수 있어요."
   // facility → "{Label}에 도착했어요!\n안내를 마칠게요." / entrance → "{Label}에 도착했어요!\n즐거운 관람 되셨나요?"
static FString LexiEndedText(NodeType);  // exhibit → "안내를 마칠게요.\nAR 스캔 탭에서 마커를 비춰보세요!"  그 외 → "안내를 마칠게요.\n또 필요하면 미니맵을 눌러주세요."
static FString LexiRecognizedText();     // "인식 완료!\n이제 공룡을 자세히 살펴보세요."
```
Label 이 비면 exhibit 은 "전시물", 그 외 "목적지".

## 3. 안내 로그 위젯 (`UNavGuideLogWidget`) — 렉시 말풍선으로 재구성

RebuildWidget(순수 C++ 경로)에서 트리를 이렇게 세운다(WBP 상속 시 같은 이름 BindWidgetOptional 우선):
```
CanvasPanel
 └ HorizontalBox (앵커 0.03,0 ~ 0.72,0 / 상단 Y 24 / 높이 자동, 최대 3줄)
    ├ SizeBox 96×96 → Image LexiAvatar (텍스처: 단계별, 아래 표)
    └ Border BubblePanel (유리 FColor(18,22,30,215), FSlateRoundedBoxBrush 반지름 22, 외곽선 1.5px 색=단계별 강조색, 패딩 16/10)
         └ VerticalBox
             ├ TextBlock NameText "렉시" (14pt, 색 #9CD8FF)
             └ TextBlock MessageText (20pt, 흰색, AutoWrap, 왼쪽 정렬)
```
- 아바타 텍스처(한 번만 로드, 4장): NavOn/측위 상실/OffRoute → `lexi_question`, Guiding → `lexi_pointing`, Localized/Arrived/Recognized → `lexi_happy`, Ended → `lexi_idle`. 로드 실패 시 아바타 칸을 접는다(문구는 계속).
- 강조색: 기본 파랑 `#4BA3FF`, Arrived/Recognized 초록(`FNavDestinations::AccentColor(Entrance)`), OffRoute 주황 `#EF7D1E`.
- `ENavGuidePhase` 에 `OffRoute`, `Ended` 추가. 전이:
  - `HandleGuidanceUpdated`: bArrived → Arrived(Recognized 가 아닐 때); 그 외 bOffRoute → OffRoute; 그 외 → Guiding. **Guiding 은 매 틱 문구가 바뀔 수 있으므로 마지막 FNavGuidance 를 저장하고 ApplyPhase 에서 LexiGuidingText 를 만들되 텍스트가 같으면 SetText 하지 않는다.**
  - 새 델리게이트 `UNavMinimapWidget::OnNavigationEnded` → Ended. `OnNavigationClosed` → Localized(경로 없음).
- 기존 안전장치(목적지 없는데 Guiding/Arrived 면 Localized 로) 유지. FullMapOpen 은 지금처럼 숨김.

## 4. 도착 후 자동 종료 (`UNavMinimapWidget`, Follow 모드만)

순수 상태 헬퍼 `FNavArrivalAutoEnd`(새 파일 `NavArrivalAutoEnd.h/.cpp`, 테스트 대상):
```cpp
struct FNavArrivalAutoEnd {
  float DwellSec = 4.0f;      // 도착 문구를 보여 주는 시간
  float EndMessageSec = 2.5f; // 마무리 문구 시간
  enum class EEvent { None, Ended, Closed };
  /** 매 틱. bArrived=false 로 돌아오면(히스테리시스 밖) Dwell 을 리셋(Ended 전까지만). 한 번 Closed 면 Reset 전까지 None. */
  EEvent Update(bool bArrived, float DeltaSec);
  void Reset();
};
```
- `UNavMinimapWidget`: 멤버 `FNavArrivalAutoEnd AutoEnd;` Config `ArriveAutoEndDwellSec=4`, `ArriveEndMessageSec=2.5`(ini 노출). `SetCurrentPose`(Follow, HasRoute)에서 `AutoEnd.Update(P.bArrived, RealDeltaTime)`:
  - `Ended` → `OnNavigationEnded.Broadcast()` (안내 로그: 마무리 문구. 발자국/링은 그대로).
  - `Closed` → `ClearRoute(); SetDestinationNode(TEXT(""));` (플로어 가이드·마름모 정리는 기존 ClearRoute 경로) → `OnNavigationClosed.Broadcast()`.
  - `SetRouteXY`/`ClearRoute` 에서 `AutoEnd.Reset()`.
- `UDocentChatWidget`: NativeConstruct 에서 `GetWidgetFromName("WBP_NavMinimap")` 를 `UNavMinimapWidget` 으로 캐스팅해 `OnNavigationClosed` 에 바인딩 → `SetHudTab(EDocentHudTab::Scan)`. (NativeDestruct 에서 해제.) 스캔 탭으로 돌아온 뒤 문구는 기존 스캔 힌트("AR 스캔을 눌러 시작해주세요…").
- 사용자가 도착 전에 X 로 내비를 닫는 경우는 지금처럼(변경 없음).

## 5. 파일 소유
- Sonnet: `NavDestinations.h/.cpp`, `NavDestinationsTest.cpp`, `NavGuideLogWidget.h/.cpp`, `NavMinimapWidget.h/.cpp`(AutoEnd·델리게이트 2개·Config 2개만), 새 `NavArrivalAutoEnd.h/.cpp` + `NavArrivalAutoEndTest.cpp`, `DocentChatWidget.h/.cpp`(OnNavigationClosed 바인딩 몇 줄만), `TimeRevealComponent.cpp`(SubjectParticle 을 FNavDestinations 로 교체).
- 건드리지 않음: WBP, NavFullMapWidget, NavStatusWidget, NavFloorGuideActor, NavRouteProgress.

## 6. 검증
- 헤드리스: `TimeMachineAR.Nav.Destinations`(새 문구·조사 규칙), `TimeMachineAR.Nav.ArrivalAutoEnd`(Dwell 리셋, Ended→Closed 순서·시간, Reset 후 재사용), 기존 `Nav.*` 회귀.
- 렌더: `TimeMachineAR.Nav.SkinPreview` 회귀 + 새 `TimeMachineAR.Nav.GuideLogPreview`: GuideLog 를 단독 생성해 Guiding(티라노, Instruction "앞으로 6m 직진하세요", Remaining 2800) / 근접(Remaining 300) / Arrived / Ended 4장을 `Saved/NavGuideLogPreview_*.png` 로 렌더(DocentSkinTest 의 FWidgetRenderer 패턴).
- 실기기(Opus): 마커 인식 → 목적지 선택 → 안내 문구 → 도착 → 4초 뒤 마무리 → 2.5초 뒤 스캔 탭 복귀.
