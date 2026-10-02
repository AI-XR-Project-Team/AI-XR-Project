#include "NavArrivalAutoEnd.h"

FNavArrivalAutoEnd::EEvent FNavArrivalAutoEnd::Update(bool bArrived, float DeltaSec)
{
	if (Stage == EInternalStage::Done)
	{
		return EEvent::None;
	}

	if (Stage == EInternalStage::Idle)
	{
		if (!bArrived)
		{
			return EEvent::None;
		}
		// 방금 도착했다 — 이번 틱의 DeltaSec 부터 곧장 Dwell 에 잡는다(if 를 안 끊고 아래
		// Dwelling 블록으로 흘려보낸다). 여기서 return 해버리면 도착을 감지한 첫 틱의
		// 시간이 그냥 버려져, 매 프레임 호출 때마다 한 틱만큼 도착 판정이 늦어진다.
		Stage = EInternalStage::Dwelling;
		ElapsedSec = 0.f;
	}

	if (Stage == EInternalStage::Dwelling)
	{
		if (!bArrived)
		{
			Stage = EInternalStage::Idle;
			ElapsedSec = 0.f;
			return EEvent::None;
		}
		ElapsedSec += DeltaSec;
		if (ElapsedSec >= DwellSec)
		{
			Stage = EInternalStage::AfterEnded;
			ElapsedSec = 0.f;
			return EEvent::Ended;
		}
		return EEvent::None;
	}

	// AfterEnded. bArrived 를 더 보지 않는다 — 이미 도착 문구를 띄웠고, 마무리로 가는
	// 길에 사용자가 살짝 물러났다고 되돌리면 오히려 어색하다.
	ElapsedSec += DeltaSec;
	if (ElapsedSec >= EndMessageSec)
	{
		Stage = EInternalStage::Done;
		return EEvent::Closed;
	}
	return EEvent::None;
}

void FNavArrivalAutoEnd::Reset()
{
	Stage = EInternalStage::Idle;
	ElapsedSec = 0.f;
}
