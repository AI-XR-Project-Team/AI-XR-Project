#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"   // FSlateBrush(목적지 아이콘 브러시 캐시)
#include "NavTypes.h"
#include "NavMinimapWidget.generated.h"

class UWidget;
class UNavRouteProgress;
class UNavFullMapWidget;
class UNavGuideLogWidget;
class UNavDestMarkerWidget;   // 9단계 §C-2 목적지 마름모 HUD
class ANavFloorGuideActor;    // 9단계 §C-1 바닥 발자국 액터
class UTexture2D;

/** 전체 지도에서 노드를 골랐을 때. BP 가 NavClient.RequestRoute 로 잇는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavDestinationChosen, const FString&, NodeId);

/** 전체 지도가 열리거나 닫혔을 때(8단계 §D 안내 로그가 "확대 지도" 상태를 안다). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavFullMapOpenChanged, bool, bOpen);

/** 매 틱 갱신되는 턴바이턴 안내(5-D). BP 가 WBP_NavStatus 배너로 잇는다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavGuidanceUpdated, const FNavGuidance&, Guidance);

/** 미니맵 표시 모드. */
UENUM(BlueprintType)
enum class ENavMinimapMode : uint8
{
	/** 작은 미니맵. 고정 배율·내 위치 화면 중앙 고정·맵이 움직인다(north-up). */
	Follow,
	/** 전체 지도. 벽·구조물·전체 노드·전체 엣지를 맞춰 그린다(현재까지의 기본 동작). */
	Full,
};

/**
 * 우측 상단 2D 미니맵. 경로를 위에서 내려다본 그림으로 그린다.
 *
 * nav-test-app 의 MinimapView(Android Canvas) 를 UE 로 옮긴 것이다. 다만 그림이
 * 다르다 — 노드는 채운 점이 아니라 빨간 링이고, 노드 사이는 선 하나가 아니라
 * 폭이 있는 띠이며, 띠 안에 진행 방향 셰브론이 2m 간격으로 반복된다.
 *
 * ## 왜 C++ 로 직접 그리는가
 *
 * UMG 위젯을 쌓아서는 못 그린다. 경로는 임의의 각도로 꺾이는데 Image 나 Border 로
 * 기울어진 띠를 만들려면 회전 변환을 위젯마다 걸어야 하고, 셰브론은 경로 길이에
 * 따라 개수가 매번 달라져서 미리 배치해 둘 수가 없다. Slate 의 NativePaint 는
 * Android 의 onDraw(Canvas) 와 같은 층위라 원본 로직을 거의 그대로 옮길 수 있다.
 *
 * 그래서 이 클래스는 **경로만** 그린다. 배경·테두리·빈 상태 문구는 WBP 가 맡는다
 * (팀 원칙: 로직은 C++, WBP 는 배치·스타일). 배경까지 C++ 에서 그리면 브러시
 * 에셋을 코드가 들고 있어야 해서 디자이너가 색 하나 못 바꾼다.
 *
 * ## WBP 로 상속할 때
 *
 * 필수 자식 위젯은 없다. 선택 자식:
 *   - EmptyHint (아무 위젯) : 경로가 없을 때만 보인다. "목적지를 선택하세요" 같은 안내.
 *
 * 미니맵 크기는 WBP 에서 이 위젯을 감싼 슬롯이 정한다. 이 클래스는 주어진
 * 영역에 경로를 꽉 채워 넣는다(fit-to-bounds).
 *
 * ## 좌표
 *
 * 입력은 전부 **서버 맵 좌표(cm)** 다. +X 오른쪽, +Y 안쪽(위). 화면에 그릴 때
 * y 를 뒤집어 +Y 가 미니맵 위쪽으로 가게 한다. Z(높이)는 무시한다 — 층 개념이
 * 아직 없고, 위에서 내려다본 그림에 높이는 안 쓰인다.
 *
 * 지도는 **북쪽 고정**이다(맵 +Y 가 항상 위). 진행 방향 고정(heading-up)은
 * 실시간 측위가 붙은 뒤에 선택지가 된다.
 *
 * ## 측위와의 관계
 *
 * SetCurrentPose 는 API 만 뚫어 두었고 지금은 아무도 부르지 않는다. 서버는
 * 맵 좌표만 다루고(측위 비종속), 앱에는 아직 "지금 내가 맵 어디인가"를 매
 * 프레임 알려주는 계층이 없다. 그 계층이 생기면 매 프레임 SetCurrentPose 만
 * 부르면 되고 이 파일은 손대지 않는다.
 *
 * 계약 원문: docs/nav-server-integration-guide.md
 */
UCLASS(Abstract, Config = Game)
class TIMEMACHINEAR_API UNavMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ 데이터

	/**
	 * 표시 모드. Follow(작은 미니맵, 고정 배율·내 위치 중앙) / Full(전체 지도, 맞춤).
	 * WBP 에서 WBP_NavMinimap 은 Follow, WBP_NavMinimapFull 의 MapView 는 Full 로 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap")
	ENavMinimapMode Mode = ENavMinimapMode::Full;

	/**
	 * Follow 모드에서 위젯 폭에 들어오는 실제 거리(cm). 기본 800=8m.
	 * 4m 면 긴 구간에 직선 하나만 남아, 대부분의 순간 다음 노드가 하나는 들어오도록 8m.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap",
		meta = (ClampMin = "100.0"))
	float FollowWindowCm = 800.f;

	/** 경로 응답 전체를 넣는다. waypoints 만 쓰고 steps 는 무시한다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void SetRoute(const FNavRoute& InRoute);

	/** 전체 지도 데이터(노드·엣지·벽·구조물). Full 모드가 그리고, 노드 터치 판정에도 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void SetGraph(const FNavGraph& InGraph);

	UFUNCTION(BlueprintPure, Category = "Nav|Minimap")
	bool HasGraph() const { return Graph.Nodes.Num() > 0; }

	/** 이 노드를 목적지로 강조(전체 지도에서 채운 링으로 구분). 빈 문자열이면 해제. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void SetDestinationNode(const FString& NodeId);

	/**
	 * 화면 로컬 좌표(px)에서 반경 안의 가장 가까운 그래프 노드를 찾는다(Full 모드).
	 * 마지막으로 그린 변환을 그대로 되짚어 판정한다 — 그리기 전이면 실패한다.
	 * @return 찾으면 true 와 OutNodeId.
	 */
	bool FindNodeAtLocal(const FVector2D& LocalPos, float RadiusPx, FString& OutNodeId) const;

	/**
	 * FindNodeAtLocal 과 같지만 **목적지 노드(아이콘이 있는 것)만** 판정한다(8단계 §B-1·D-5).
	 * 노드 터치 목적지 선택은 폐지됐고 아이콘을 눌렀을 때만 경로가 잡힌다 — 그 판정을 여기서.
	 */
	bool FindDestinationNodeAtLocal(const FVector2D& LocalPos, float RadiusPx, FString& OutNodeId) const;

	/** 노드 id 로 node_type / label 을 찾는다(없으면 빈 문자열). 안내 로그·아이콘 분기용. */
	UFUNCTION(BlueprintPure, Category = "Nav|Minimap")
	FString GetNodeType(const FString& NodeId) const;
	UFUNCTION(BlueprintPure, Category = "Nav|Minimap")
	FString GetNodeLabel(const FString& NodeId) const;

	/** 현재 목적지의 node_type / label(SetDestinationNode 로 지정된 노드). 안내 로그가 문구를 가른다. */
	FString GetDestinationNodeType() const { return GetNodeType(DestinationNodeId); }
	FString GetDestinationLabel() const { return GetNodeLabel(DestinationNodeId); }

	/** 폴리라인(맵 cm)을 직접 넣는다(전체 지도로 상태를 넘길 때 등). */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void SetRouteXY(const TArray<FVector2D>& InRouteXY);

	/** 진행률 계산기(측위 계층/배너가 공유). 없으면 만들어 반환한다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	UNavRouteProgress* GetRouteProgress();

	// ------------------------------------------------------------------ 전체 지도 열기(Follow)

	/**
	 * 전체 지도(WBP_NavMinimapFull)를 띄운다. 작은 미니맵(Follow)의 클릭 이벤트가 부른다.
	 * C++ 가 CreateWidget + AddToViewport 로 직접 다뤄 .uasset 수정을 피한다(spec §2.1).
	 * 지금 들고 있는 그래프·경로·현재 pose 를 그대로 넘긴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void OpenFullMap();

	/** 전체 지도에서 목적지를 골랐을 때 재방송. BP 가 NavClient.RequestRoute 로 잇는다. */
	UPROPERTY(BlueprintAssignable, Category = "Nav|Minimap")
	FOnNavDestinationChosen OnDestinationChosen;

	/** 전체 지도가 열리고/닫힐 때(8단계 §D 안내 로그가 구독). Follow 인스턴스가 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "Nav|Minimap")
	FOnNavFullMapOpenChanged OnFullMapOpenChanged;

	/**
	 * 턴바이턴 안내가 갱신될 때(측위 중, Follow 모드만). BP 가 WBP_NavStatus 배너로 잇는다.
	 * bArrived 로 도착 화면도 여기서 가른다(5-D).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Nav|Minimap")
	FOnNavGuidanceUpdated OnGuidanceUpdated;

	/** 전체 지도 위젯 클래스(WBP_NavMinimapFull). Follow 인스턴스의 WBP 기본값으로 지정. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap")
	TSubclassOf<UNavFullMapWidget> FullMapWidgetClass;

	/** 웨이포인트만 따로 넣고 싶을 때. SetRoute 가 내부적으로 이걸 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void SetWaypoints(const TArray<FNavWaypoint>& InWaypoints);

	/** 경로를 지운다. EmptyHint 가 있으면 다시 보인다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void ClearRoute();

	UFUNCTION(BlueprintPure, Category = "Nav|Minimap")
	bool HasRoute() const { return RouteXY.Num() > 0; }

	/**
	 * 현재 위치를 갱신한다. 측위 계층이 매 프레임 부를 자리다.
	 *
	 * @param PosXCm/PosYCm 맵 좌표(cm)
	 * @param HeadingDeg    바라보는 방향. +X 축 기준 CCW(도)
	 * @param bHasHeading   false 면 방향 삼각형 없이 점만 찍는다
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void SetCurrentPose(float PosXCm, float PosYCm, float HeadingDeg, bool bHasHeading = true);

	/** 현재 위치 표시를 끈다. 측위를 잃었을 때. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Minimap")
	void ClearCurrentPose();

	// ------------------------------------------------------------------ 스타일
	//
	// 전부 WBP 클래스 기본값에서 바꿀 수 있다. 기본값은 기획 스케치의 색을 그대로 뒀다.

	/** 경로 띠의 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style")
	FLinearColor PathColor = FLinearColor(0.12f, 0.12f, 0.92f, 1.f);

	/** 경로 띠의 폭(px). 통로의 실제 폭이 아니라 보기용 굵기다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "1.0"))
	float PathBandWidthPx = 12.f;

	/** 띠를 이루는 두 줄의 굵기(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "0.5"))
	float PathLineThicknessPx = 1.5f;

	/**
	 * 셰브론 간격(맵 cm). 기본 200 = 2m.
	 *
	 * 화면이 아니라 실제 거리 기준이다. 그래야 "화살표 세 개 = 6m" 처럼 읽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "10.0"))
	float ArrowSpacingCm = 200.f;

	/**
	 * 셰브론이 화면에서 이 간격보다 촘촘해지면 2m 의 정수배로 벌린다(0 이면 끔).
	 *
	 * 미니맵은 경로 전체를 상자에 맞춰 줄이므로 경로가 길수록 축척이 작아진다.
	 * 50m 경로를 그대로 2m 마다 찍으면 화살표 25개가 뭉개져 띠가 시커메진다.
	 * 정수배로만 벌리는 이유는 "한 칸 = 2m" 라는 의미를 유지하기 위해서다
	 * (4m, 6m 은 되지만 3.7m 은 안 된다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "0.0"))
	float MinArrowSpacingPx = 14.f;

	/** 셰브론의 앞뒤 길이(px). 커질수록 뾰족해진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "1.0"))
	float ArrowLengthPx = 7.f;

	/** 셰브론 굵기(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "0.5"))
	float ArrowThicknessPx = 1.5f;

	/** 노드 링의 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style")
	FLinearColor NodeRingColor = FLinearColor(0.87f, 0.16f, 0.13f, 1.f);

	/** 노드 링의 반지름(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "2.0"))
	float NodeRingRadiusPx = 9.f;

	/** 노드 링의 선 굵기(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "0.5"))
	float NodeRingThicknessPx = 3.f;

	/**
	 * true 면 출발/도착 노드를 아래 색으로 따로 칠한다. 기본은 꺼져 있어
	 * 스케치대로 전부 같은 빨강이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style")
	bool bDistinguishEndpoints = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (EditCondition = "bDistinguishEndpoints"))
	FLinearColor StartNodeColor = FLinearColor(0.30f, 0.69f, 0.31f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (EditCondition = "bDistinguishEndpoints"))
	FLinearColor DestNodeColor = FLinearColor(0.96f, 0.26f, 0.21f, 1.f);

	/** 현재 위치 점의 색. 측위가 붙기 전에는 쓰이지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style")
	FLinearColor CurrentPoseColor = FLinearColor(0.13f, 0.59f, 0.95f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Style",
		meta = (ClampMin = "2.0"))
	float CurrentPoseRadiusPx = 7.f;

	// ------------------------------------------------------------------ 전체 지도(Full) 스타일
	//
	// Full 모드에서만 쓰인다. 벽·구조물·전체 노드·전체 엣지의 색/굵기.

	/** 벽(외곽선) 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full")
	FLinearColor WallColor = FLinearColor(0.20f, 0.20f, 0.22f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full",
		meta = (ClampMin = "0.5"))
	float WallThicknessPx = 2.5f;

	/** 내부 구조물(채운 사각형) 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full")
	FLinearColor ObstacleColor = FLinearColor(0.20f, 0.20f, 0.22f, 0.35f);

	/** 전체 엣지(옅은 회색) 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full")
	FLinearColor GraphEdgeColor = FLinearColor(0.62f, 0.62f, 0.66f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full",
		meta = (ClampMin = "0.5"))
	float GraphEdgeThicknessPx = 1.5f;

	/** 전체 노드(작은 링) 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full")
	FLinearColor GraphNodeColor = FLinearColor(0.45f, 0.45f, 0.5f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full",
		meta = (ClampMin = "2.0"))
	float GraphNodeRadiusPx = 6.f;

	/** 목적지로 지정된 노드를 채워 구분하는 색. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full")
	FLinearColor DestinationNodeColor = FLinearColor(0.96f, 0.26f, 0.21f, 1.f);

	/** Full 모드에서 노드 터치 판정 반경(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full",
		meta = (ClampMin = "6.0"))
	float NodeHitRadiusPx = 28.f;

	/**
	 * true 면 노드 점·엣지 선을 다시 그린다(디버그). 기본 false — 사용자 화면에는 도면·구조물·
	 * 목적지 아이콘·경로선만 남긴다(final §D-7). 경로가 이상할 때 ini 로 켜서 원인을 본다.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Full")
	bool bDrawGraphDebug = false;

	// ------------------------------------------------------------------ 목적지 아이콘(§B-1 · D-8)
	//
	// 전시물·화장실·입구를 마름모 + 아이콘으로 지도에 얹는다. 색은 node_type 으로 고른다
	// (FNavDestinations). Full·Follow 공통이고 **화면 px 고정 크기**다(Follow 는 배율이 커서
	// 월드 크기로 그리면 거대해진다, final §D-8).

	/** 아이콘 그림의 한 변(px). E-2 에서 현우가 최종 조정. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Dest",
		meta = (ClampMin = "8.0"))
	float DestIconSizePx = 30.f;

	/** 마름모의 중심→꼭짓점 거리(px). 아이콘을 감싸도록 아이콘 반보다 크게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Dest",
		meta = (ClampMin = "6.0"))
	float DestDiamondHalfPx = 24.f;

	/** 마름모 테두리 굵기(px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Dest",
		meta = (ClampMin = "0.5"))
	float DestDiamondThicknessPx = 2.5f;

	/** 현재 안내 중인 목적지의 마름모 테두리 굵기(px). 다른 목적지보다 두껍게 강조. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Dest",
		meta = (ClampMin = "0.5"))
	float DestActiveDiamondThicknessPx = 4.5f;

	// ------------------------------------------------------------------ 안내 로그(§D)

	/**
	 * 안내 로그 오버레이 클래스. 비우면 순수 C++ 위젯(UNavGuideLogWidget)을 그대로 만들어
	 * 스스로 화면 상단에 띄운다(에디터 작업 0, final §0-E). WBP 로 꾸미고 싶을 때만 지정.
	 * Follow(HUD) 인스턴스만 만든다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap")
	TSubclassOf<UNavGuideLogWidget> GuideLogWidgetClass;

	// ------------------------------------------------------------------ AR 화면(§C, 9단계)
	//
	// Follow(HUD) 인스턴스가 C++ 로 소유·구동한다(GuideLog 와 같은 방식, 에디터 배선 0).
	// 바닥 발자국은 액터, 목적지 마름모는 HUD 오버레이. 둘 다 클래스를 비우면 순수 C++ 로 만든다.

	/** 바닥 발자국 액터 클래스(§C-1). 비우면 ANavFloorGuideActor 기본 클래스를 스폰. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap")
	TSubclassOf<ANavFloorGuideActor> FloorGuideActorClass;

	/** 목적지 마름모 HUD 클래스(§C-2). 비우면 순수 C++ UNavDestMarkerWidget 을 만든다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap")
	TSubclassOf<UNavDestMarkerWidget> DestMarkerWidgetClass;

	// ------------------------------------------------------------------ 여백

	/** 미니맵 테두리 안쪽 여백(px). 노드 링이 잘리지 않을 만큼은 줘야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Layout",
		meta = (ClampMin = "0.0"))
	float PaddingPx = 14.f;

	/**
	 * 경로 경계 바깥으로 더 잡는 여유(맵 cm).
	 *
	 * 없으면 직선 경로일 때 rangeY 가 0 이 되어 축척이 폭주한다. 경계에 딱 붙는
	 * 그림을 막는 역할도 겸한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Layout",
		meta = (ClampMin = "1.0"))
	float WorldPaddingCm = 60.f;

	/**
	 * true 면 UMG 디자이너에서 ㄱ자 샘플 경로를 그려 준다.
	 *
	 * 실기기에 올리지 않고도 색·굵기·간격을 맞출 수 있어야 한다. 실행 중에는
	 * 무시된다(IsDesignTime 으로 가른다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav|Minimap|Layout")
	bool bPreviewInDesigner = true;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** 경로가 없을 때만 보이는 안내. 없어도 된다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Nav|Minimap")
	TObjectPtr<UWidget> EmptyHint;

private:
	/** 서버가 준 정적 경로(맵 cm). Z 는 버린다. */
	TArray<FVector2D> RouteXY;

	/** 전체 지도 데이터(Full 모드 그리기 + 노드 터치 판정). */
	FNavGraph Graph;

	/** 목적지로 강조할 노드 id(빈 문자열이면 없음). */
	FString DestinationNodeId;

	/** 동적 경로선·진행률 계산기. NativeConstruct 에서 생성. */
	UPROPERTY(Transient)
	TObjectPtr<UNavRouteProgress> RouteProgress;

	FVector2D CurrentXY = FVector2D::ZeroVector;
	float CurrentHeadingDeg = 0.f;
	bool bHasCurrent = false;
	bool bCurrentHasHeading = false;

	/** 직전 프레임 이탈 상태(전환 시에만 로그). */
	bool bWasOffRoute = false;

	// --------------------------------------------------------------- 자동 reroute(5-B3, Follow 전용)
	//
	// 3중 게이트: ①이탈이 RerouteOffRouteHoldSeconds 이상 지속 ②직전 요청 후
	// RerouteCooldownSeconds 지남 ③측위 품질이 저하 상태가 아님(가짜 이탈 방어).
	// 임계값은 GetRouteProgress() 의 Config 에서 읽는다(spec §3.3).

	/** 이탈이 시작된 월드 시각(초). 아직 이탈 아님이면 음수. */
	float OffRouteSinceSeconds = -1.f;
	/** 마지막으로 reroute 를 요청한 월드 시각(초). 아직 없으면 큰 음수. */
	float LastRerouteSeconds = -1000.f;

	/** 3중 게이트를 평가하고, 다 통과하면 NavClient.Reroute 를 부른다(Follow 모드만). */
	void EvaluateAutoReroute(const FNavProgress& P);

	// 마지막으로 그린 맵→로컬 변환(노드 터치 판정용). NativePaint(const)가 채운다.
	//   Local.X = ScreenOrigin.X + (W.X - WorldOrigin.X) * Scale
	//   Local.Y = ScreenOrigin.Y - (W.Y - WorldOrigin.Y) * Scale   (y 뒤집음)
	mutable FVector2D CachedWorldOrigin = FVector2D::ZeroVector;
	mutable FVector2D CachedScreenOrigin = FVector2D::ZeroVector;
	mutable float CachedScale = 1.f;
	mutable bool bHasCachedTransform = false;

	/** 이번 프레임 위젯 로컬 크기(px). Follow 뷰 컬링·셰브론 창 판정에 쓴다. */
	mutable FVector2D CachedLocalSize = FVector2D::ZeroVector;

	/**
	 * 로컬 점이 위젯 영역(여백 Margin 포함) 안에 있나. Follow 창 밖 요소를 솎아낸다.
	 * Full 모드는 fit-to-bounds 라 전부 안에 들어오므로 사실상 통과한다.
	 */
	bool IsLocalInView(const FVector2D& P, float Margin) const;
	/** 두 로컬 점을 잇는 선분이 위젯 영역과 겹치나(둘 중 하나라도 보이면 그린다). */
	bool IsSegmentInView(const FVector2D& A, const FVector2D& B, float Margin) const;

	/** 열려 있는 전체 지도의 MapView(Full). 없으면 nullptr. Follow 가 상태를 흘려보낼 대상. */
	UNavMinimapWidget* GetOpenFullMapView() const;

	/** 전체 지도가 목적지를 방송하면 받아 재방송(+목적지 노드 강조). 바인드 대상. */
	UFUNCTION()
	void HandleDestinationChosen(const FString& NodeId);

	/** 전체 지도가 닫힐 때. OnFullMapOpenChanged(false) 를 방송한다. */
	UFUNCTION()
	void HandleFullMapClosed();

	/** 현재 떠 있는 전체 지도(중복 오픈 방지). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UNavFullMapWidget> FullMapInstance;

	/** 안내 로그 오버레이(Follow 인스턴스가 만들어 화면에 붙인다). */
	UPROPERTY(Transient)
	TObjectPtr<UNavGuideLogWidget> GuideLog;

	/** 아직 없으면 안내 로그를 만들어 화면 상단에 붙인다(Follow·비디자인만). */
	void EnsureGuideLog();

	// --------------------------------------------------------------- AR 화면(§C, 9단계)

	/** 바닥 발자국 액터(Follow 인스턴스가 스폰·소유). */
	UPROPERTY(Transient)
	TObjectPtr<ANavFloorGuideActor> FloorGuide;

	/** 목적지 마름모 HUD(Follow 인스턴스가 만들어 화면에 붙인다). */
	UPROPERTY(Transient)
	TObjectPtr<UNavDestMarkerWidget> DestMarker;

	/** 아직 없으면 발자국 액터를 스폰한다(Follow·비디자인만). */
	void EnsureFloorGuide();

	/** 아직 없으면 목적지 마름모 HUD 를 만들어 붙인다(Follow·비디자인만). */
	void EnsureDestMarker();

	/** 목적지 마름모에 현재 목적지(맵 좌표·종류·이름)를 실어 준다. 목적지 변경 시 부른다. */
	void PushDestinationToMarker();

	/** 매 프레임 바닥 발자국·목적지 남은거리를 갱신한다(Follow, 측위·경로 있을 때). */
	void RefreshArGuides(const FNavProgress& P);

	/** 노드 id 의 맵 좌표(cm)를 찾는다. 없으면 false. */
	bool GetNodePos(const FString& NodeId, FVector2D& OutXY) const;

	/** 목적지 아이콘 텍스처 브러시 캐시(오브젝트 경로 → 브러시). NativePaint 가 채운다. */
	mutable TMap<FString, FSlateBrush> IconBrushCache;

	/** 오브젝트 경로로 아이콘 브러시를 얻는다(없으면 로드해 캐시, 실패면 nullptr). */
	const FSlateBrush* ResolveIconBrush(const FString& ObjectPath) const;

	/** 목적지 노드마다 마름모 + 아이콘을 그린다(Full·Follow 공통, px 고정 크기). */
	void PaintDestinationIcons(FSlateWindowElementList& Out, int32& Layer,
		const FGeometry& AllottedGeometry) const;

	/** EmptyHint 를 경로 유무에 맞춘다. */
	void RefreshEmptyHint();

	/** 디자이너 미리보기용 ㄱ자 경로(맵 cm). */
	static void BuildPreviewRoute(TArray<FVector2D>& Out);

	/** 그릴 경로 폴리라인(맵 cm)을 고른다: 측위 중이면 동적 선, 아니면 정적/미리보기. */
	const TArray<FVector2D>& ResolveRoutePts(TArray<FVector2D>& DynamicScratch,
		TArray<FVector2D>& PreviewScratch) const;

	/** 캐시된 변환으로 맵 cm → 로컬 px. */
	FVector2D WorldToLocal(const FVector2D& W) const;

	/** 전체 지도(벽·구조물·전체 엣지·전체 노드)를 그린다. Full 모드에서만. */
	void PaintFullMapBase(FSlateWindowElementList& Out, int32& Layer,
		const FPaintGeometry& Geom) const;

	/** 채운 사각형(구조물)을 수평선 채움으로 그린다. */
	void PaintFilledRect(FSlateWindowElementList& Out, int32 Layer,
		const FPaintGeometry& Geom, const FVector2D& LocalA, const FVector2D& LocalB,
		const FLinearColor& Color) const;

	// --------------------------------------------------------------- 그리기 조각
	//
	// 전부 NativePaint 에서만 부른다. 좌표 인자는 이미 로컬(px) 로 투영된 값이다.

	/** 선분 하나를 폭 있는 띠(평행선 두 줄)로 그린다. */
	void PaintBand(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geom,
		const FVector2D& A, const FVector2D& B) const;

	/** 폴리라인을 따라 실제 거리 기준 간격으로 셰브론을 찍는다. */
	void PaintChevrons(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geom,
		const TArray<FVector2D>& LocalPts, const TArray<FVector2D>& WorldPts, float Scale) const;

	/** 원을 다각형 폴리라인으로 그린다. Slate 에 원 프리미티브가 없다. */
	void PaintRing(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geom,
		const FVector2D& Center, float Radius, float Thickness, const FLinearColor& Color) const;
};
