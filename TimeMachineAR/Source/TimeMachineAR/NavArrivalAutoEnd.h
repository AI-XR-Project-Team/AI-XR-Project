#pragma once

#include "CoreMinimal.h"

/**
 * 도착 후 자동 종료 타이머(순수 상태 헬퍼, UObject·월드 없음). nav-lexi-guide-design.md §4.
 *
 * 도착(bArrived)이 몇 초 유지되면 "도착" 이벤트를, 다시 몇 초 뒤 "종료" 이벤트를 한 번씩만
 * 낸다. UNavMinimapWidget::SetCurrentPose(Follow 모드) 가 매 pose 마다 실시간 델타로 Update
 * 를 부르고, Ended → 안내 로그 마무리 문구, Closed → ClearRoute()+SetDestinationNode("") 로 잇는다.
 *
 * 헤드리스 테스트 대상(`TimeMachineAR.Nav.ArrivalAutoEnd`) — UObject 를 안 쓰므로 -nullrhi 로도
 * 시간 흐름만 흉내 내 검증할 수 있다.
 */
struct TIMEMACHINEAR_API FNavArrivalAutoEnd
{
	/** 도착 문구를 보여 주는 시간(초). 이만큼 bArrived 가 유지되면 Ended 를 낸다. */
	float DwellSec = 4.0f;

	/** Ended 뒤 마무리 문구를 보여 주는 시간(초). 이만큼 지나면 Closed 를 낸다. */
	float EndMessageSec = 2.5f;

	enum class EEvent : uint8
	{
		/** 아직 아무 일도 없다. */
		None,
		/** Dwell 이 다 찼다 — 안내 로그를 마무리 문구로 바꾼다. 한 사이클에 한 번만 난다. */
		Ended,
		/** EndMessage 도 다 찼다 — 경로/목적지를 정리하고 스캔 탭으로 돌아간다. 한 사이클에 한 번만 난다. */
		Closed,
	};

	/**
	 * 매 틱(또는 매 pose) 부른다.
	 *
	 * Ended 전까지는 bArrived 가 false 로 돌아오면(도착 판정의 해제 히스테리시스 밖으로
	 * 나가면) Dwell 을 리셋하고 처음부터 다시 잰다 — 도착선 근처를 서성이다 살짝 벗어난
	 * 것까지 "도착"으로 치지 않기 위해서다. Ended 가 한 번 나가면 그 뒤로는 bArrived 값과
	 * 무관하게 EndMessage 카운트다운을 계속한다(이미 도착 문구를 봤으므로 되돌릴 이유가
	 * 없다). Closed 가 한 번 나가면 Reset() 전까지 항상 None 만 돌려준다(재방송 방지).
	 */
	EEvent Update(bool bArrived, float DeltaSec);

	/** 새 경로가 잡히거나 경로가 지워질 때 부른다. 처음 상태로 되돌려 다음 도착을 다시 잴 수 있게 한다. */
	void Reset();

private:
	enum class EInternalStage : uint8
	{
		/** 아직 도착 전(또는 Dwell 리셋 직후). */
		Idle,
		/** 도착 유지 시간을 재는 중(Ended 전). */
		Dwelling,
		/** Ended 를 낸 뒤 마무리 문구 시간을 재는 중(Closed 전). */
		AfterEnded,
		/** Closed 까지 다 냈다. Reset 전까지 조용하다. */
		Done,
	};

	EInternalStage Stage = EInternalStage::Idle;
	float ElapsedSec = 0.f;
};
