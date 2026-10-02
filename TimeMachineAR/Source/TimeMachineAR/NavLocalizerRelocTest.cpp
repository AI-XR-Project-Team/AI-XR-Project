// UNavLocalizer 재측위 감지·복구 판정 + 오버레이 순수 헬퍼 단위 테스트 (13-4 §C).
//
// 순수 static 로직이라 PIE·AR 세션 없이 검증한다. 에디터에서
//   Session Frontend > Automation > "TimeMachineAR.Nav.LocalizerReloc" 실행,
// 또는 커맨드라인(헤드리스):
//   UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests TimeMachineAR.Nav.LocalizerReloc; Quit" -unattended -nop4 -nosplash -NullRHI

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavLocalizer.h"
#include "NavRelocalizeOverlay.h"

namespace
{
	const EARTrackingQuality QGood = EARTrackingQuality::OrientationAndPosition;
	const EARTrackingQuality QLost = EARTrackingQuality::NotTracking;
	const EARTrackingQualityReason RNone = EARTrackingQualityReason::None;

	FNavRelocSample MakeRelocSample(EARTrackingQuality Q, EARTrackingQualityReason R, float Dt,
		const FVector& Cam = FVector::ZeroVector, float YawDeg = 0.f, float PitchDeg = 0.f, bool bSolved = false)
	{
		FNavRelocSample S;
		S.Quality = Q;
		S.Reason = R;
		S.DeltaTime = Dt;
		S.CameraWorld = Cam;
		S.CameraYawDeg = YawDeg;
		S.CameraPitchDeg = PitchDeg;
		S.bSolvedThisTick = bSolved;
		return S;
	}

	/** 카메라를 세워 둔 채 같은 품질을 Seconds 동안 Dt 간격으로 넣는다. 진입하면 그 판정을 돌려준다. */
	FNavRelocVerdict FeedQuality(FNavRelocAccum& A, const FNavRelocParams& P,
		EARTrackingQuality Q, EARTrackingQualityReason R, float Seconds, float Dt = 0.02f)
	{
		FNavRelocVerdict Last;
		const int32 Ticks = FMath::RoundToInt(Seconds / Dt);
		for (int32 i = 0; i < Ticks; ++i)
		{
			Last = UNavLocalizer::JudgeRelocEntry(MakeRelocSample(Q, R, Dt), P, A);
			if (!Last.Reason.IsEmpty())
			{
				return Last;
			}
		}
		return Last;
	}

	/** +X 로 한 틱에 StepCm 씩 Ticks 번 움직인다(품질 양호). 진입하면 그 판정을 돌려준다. */
	FNavRelocVerdict FeedWalk(FNavRelocAccum& A, const FNavRelocParams& P, float StepCm, int32 Ticks, float Dt = 0.02f)
	{
		FNavRelocVerdict Last;
		FVector Cam = A.bHasPrev ? A.PrevCameraWorld : FVector::ZeroVector;
		for (int32 i = 0; i < Ticks; ++i)
		{
			Cam.X += StepCm;
			Last = UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, Dt, Cam), P, A);
			if (!Last.Reason.IsEmpty())
			{
				return Last;
			}
		}
		return Last;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavLocalizerRelocTest,
	"TimeMachineAR.Nav.LocalizerReloc",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavLocalizerRelocTest::RunTest(const FString& Parameters)
{
	const FNavRelocParams P;   // 명세 §3.6 기본값(ini 와 같다)

	// ─── (1) A NotTracking 문턱 1.0초 ±10% ───
	{
		FNavRelocAccum A;
		TestTrue(TEXT("A NotTracking 0.9초 → 진입 없음"), FeedQuality(A, P, QLost, RNone, 0.9f).Reason.IsEmpty());
	}
	{
		FNavRelocAccum A;
		TestEqual(TEXT("A NotTracking 1.1초 → occluded"), FeedQuality(A, P, QLost, RNone, 1.1f).Reason, FString(TEXT("occluded")));
	}

	// ─── (2) A Reason 지속 3.0초 ±10% (품질 플래그는 양호 — ARCore 흔들림은 Reason 에만 실린다) ───
	{
		FNavRelocAccum A;
		TestTrue(TEXT("A ExcessiveMotion 2.7초 → 진입 없음"),
			FeedQuality(A, P, QGood, EARTrackingQualityReason::ExcessiveMotion, 2.7f).Reason.IsEmpty());
	}
	{
		FNavRelocAccum A;
		TestEqual(TEXT("A ExcessiveMotion 3.3초 → shake"),
			FeedQuality(A, P, QGood, EARTrackingQualityReason::ExcessiveMotion, 3.3f).Reason, FString(TEXT("shake")));
	}
	{
		FNavRelocAccum A;
		TestEqual(TEXT("A NotTracking+InsufficientLight 1.1초 → dark(구체 이유가 이긴다)"),
			FeedQuality(A, P, QLost, EARTrackingQualityReason::InsufficientLight, 1.1f).Reason, FString(TEXT("dark")));
	}
	{
		FNavRelocAccum A;
		TestEqual(TEXT("A InsufficientFeatures 3.3초 → featureless"),
			FeedQuality(A, P, QGood, EARTrackingQualityReason::InsufficientFeatures, 3.3f).Reason, FString(TEXT("featureless")));
	}

	// ─── (3) 손으로 가림: NotTracking ↔ InsufficientLight 번갈아 — 끊기지 않고 2초 안팎에 진입(§3.7 함정 2) ───
	{
		FNavRelocAccum A;
		FNavRelocVerdict V;
		float EnteredAt = -1.f;
		for (int32 i = 0; i < 70 && V.Reason.IsEmpty(); ++i)
		{
			const bool bLostFrame = (i % 2) == 0;
			V = UNavLocalizer::JudgeRelocEntry(MakeRelocSample(bLostFrame ? QLost : QGood,
				bLostFrame ? RNone : EARTrackingQualityReason::InsufficientLight, 0.05f), P, A);
			EnteredAt = (i + 1) * 0.05f;
		}
		TestTrue(TEXT("가림(번갈아) → 진입"), V.Reason == TEXT("occluded") || V.Reason == TEXT("dark"));
		TestTrue(TEXT("가림(번갈아) → 2.1초 안에"), EnteredAt <= 2.1f);
	}

	// ─── (4) 좋은 프레임 0.5초 연속이어야 누적을 지운다 ───
	{
		FNavRelocAccum A;
		FeedQuality(A, P, QLost, RNone, 0.6f);
		FeedQuality(A, P, QGood, RNone, 0.4f);
		TestEqual(TEXT("나쁨 0.6 + 양호 0.4 + 나쁨 0.5 → 누적 유지로 진입"),
			FeedQuality(A, P, QLost, RNone, 0.5f).Reason, FString(TEXT("occluded")));
	}
	{
		FNavRelocAccum A;
		FeedQuality(A, P, QLost, RNone, 0.6f);
		FeedQuality(A, P, QGood, RNone, 0.6f);
		TestTrue(TEXT("나쁨 0.6 + 양호 0.6 + 나쁨 0.5 → 리셋돼 진입 없음"), FeedQuality(A, P, QLost, RNone, 0.5f).Reason.IsEmpty());
	}

	// ─── (5) 한 틱 끊김(GC·앱 복귀)이 지속 문턱을 한 번에 넘기지 않는다 ───
	{
		FNavRelocAccum A;
		const FNavRelocVerdict V = UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QLost, RNone, 2.0f), P, A);
		TestTrue(TEXT("NotTracking 한 틱 2.0초 → 진입 없음(0.1초로 자름)"), V.Reason.IsEmpty());
	}

	// ─── (6) B 한 틱 이동 99cm / 101cm ───
	{
		FNavRelocAccum A;
		FeedWalk(A, P, 0.f, 1);
		const FNavRelocVerdict V = FeedWalk(A, P, 99.f, 1);
		TestTrue(TEXT("B 99cm → 진입 없음"), V.Reason.IsEmpty());
		TestTrue(TEXT("B 99cm → REJECT(튜닝 로그) 대상"), V.bNearMiss);
	}
	{
		FNavRelocAccum A;
		FeedWalk(A, P, 0.f, 1);
		const FNavRelocVerdict V = FeedWalk(A, P, 101.f, 1);
		TestEqual(TEXT("B 101cm → jump"), V.Reason, FString(TEXT("jump")));
		TestTrue(TEXT("B 101cm → dx 기록"), FMath::IsNearlyEqual(V.JumpCm, 101.f, 0.5f));
	}
	{
		FNavRelocAccum A;
		FeedWalk(A, P, 0.f, 1);
		TestFalse(TEXT("B 20cm → REJECT 아님(30cm 미만)"), FeedWalk(A, P, 20.f, 1).bNearMiss);
	}

	// ─── (7) B 한 틱 yaw 34° / 36° · 짐벌(위를 볼 때 yaw 뒤집힘)은 무시 ───
	{
		FNavRelocAccum A;
		UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector, 0.f), P, A);
		TestTrue(TEXT("B yaw 34° → 진입 없음"),
			UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector, 34.f), P, A).Reason.IsEmpty());
	}
	{
		FNavRelocAccum A;
		UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector, 170.f), P, A);
		TestEqual(TEXT("B yaw 170°→-154°(36°, 경계 넘김) → jump"),
			UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector, -154.f), P, A).Reason,
			FString(TEXT("jump")));
	}
	{
		FNavRelocAccum A;
		UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector, 10.f, 80.f), P, A);
		TestTrue(TEXT("B pitch 80° 에서 yaw 180° 뒤집힘 → 무시"),
			UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector, -170.f, 80.f), P, A).Reason.IsEmpty());
	}

	// ─── (8) B 0.5초 창 속도 3.0 m/s (한 틱 문턱엔 안 걸리는 여러 틱에 걸친 이동) ───
	{
		FNavRelocAccum A;
		FeedWalk(A, P, 0.f, 1);
		TestTrue(TEXT("B 창 2.5 m/s 1초 → 진입 없음"), FeedWalk(A, P, 5.f, 50).Reason.IsEmpty());
	}
	{
		FNavRelocAccum A;
		FeedWalk(A, P, 0.f, 1);
		TestEqual(TEXT("B 창 3.5 m/s 1초 → jump"), FeedWalk(A, P, 7.f, 50).Reason, FString(TEXT("jump")));
	}
	{
		FNavRelocAccum A;
		FeedWalk(A, P, 0.f, 1, 0.016f);
		TestTrue(TEXT("B 창이 덜 찬 시작 직후(6cm/16ms) → 진입 없음"), FeedWalk(A, P, 6.f, 2, 0.016f).Reason.IsEmpty());
	}

	// ─── (9) 재래치 틱은 비교에서 뺀다(§3.7 함정 1) ───
	{
		FNavRelocAccum A;
		UNavLocalizer::JudgeRelocEntry(MakeRelocSample(QGood, RNone, 0.02f, FVector::ZeroVector), P, A);
		const FNavRelocVerdict Solved = UNavLocalizer::JudgeRelocEntry(
			MakeRelocSample(QGood, RNone, 0.02f, FVector(150.f, 0.f, 0.f), 0.f, 0.f, /*bSolved=*/true), P, A);
		TestTrue(TEXT("재래치 틱 150cm → 진입 없음"), Solved.Reason.IsEmpty());
		TestFalse(TEXT("재래치 틱 → REJECT 도 아님"), Solved.bNearMiss);
		const FNavRelocVerdict Next = UNavLocalizer::JudgeRelocEntry(
			MakeRelocSample(QGood, RNone, 0.02f, FVector(152.f, 0.f, 0.f)), P, A);
		TestTrue(TEXT("재래치 다음 틱(새 자리에서 2cm) → 진입 없음"), Next.Reason.IsEmpty());
	}

	// ─── (10) 쿨다운: A(Reason)·B 무시, A(NotTracking) 는 예외 ───
	{
		FNavRelocAccum A;
		A.CooldownSeconds = 5.f;
		TestTrue(TEXT("쿨다운 중 ExcessiveMotion 3.3초 → 진입 없음"),
			FeedQuality(A, P, QGood, EARTrackingQualityReason::ExcessiveMotion, 3.3f).Reason.IsEmpty());
	}
	{
		FNavRelocAccum A;
		A.CooldownSeconds = 5.f;
		FeedWalk(A, P, 0.f, 1);
		const FNavRelocVerdict V = FeedWalk(A, P, 150.f, 1);
		TestTrue(TEXT("쿨다운 중 150cm 순간이동 → 진입 없음"), V.Reason.IsEmpty());
		TestTrue(TEXT("쿨다운 중 순간이동 → REJECT(cooldown) 로 남긴다"), V.bCooldownBlocked && V.bNearMiss);
	}
	{
		FNavRelocAccum A;
		A.CooldownSeconds = 5.f;
		TestEqual(TEXT("쿨다운 중 NotTracking 1.1초 → occluded(예외)"),
			FeedQuality(A, P, QLost, RNone, 1.1f).Reason, FString(TEXT("occluded")));
	}

	// ─── (11) RelocJumpCm=0 이면 B 전체를 끈다(런북 §2) ───
	{
		FNavRelocParams Off = P;
		Off.JumpCm = 0.f;
		FNavRelocAccum A;
		FeedWalk(A, Off, 0.f, 1);
		const FNavRelocVerdict V = FeedWalk(A, Off, 500.f, 1);
		TestTrue(TEXT("B 끔 → 500cm 도 진입 없음"), V.Reason.IsEmpty());
		TestFalse(TEXT("B 끔 → REJECT 도 없음"), V.bNearMiss);
	}

	// ─── (12) 복구: 최소 체류 2.0초 · 품질 양호 연속 0.5초 · 변환을 받았어야 한다 ───
	TestFalse(TEXT("복구 — 체류 1.9초 → 아직"), UNavLocalizer::JudgeRelocExit(1.9f, 1.0f, true, 2.0f, 0.5f));
	TestFalse(TEXT("복구 — 양호 0.4초 → 아직"), UNavLocalizer::JudgeRelocExit(2.1f, 0.4f, true, 2.0f, 0.5f));
	TestFalse(TEXT("복구 — 변환 없음 → 아직"), UNavLocalizer::JudgeRelocExit(9.0f, 9.0f, false, 2.0f, 0.5f));
	TestTrue(TEXT("복구 — 체류 2.1 · 양호 0.6 · 변환 있음 → 복구"), UNavLocalizer::JudgeRelocExit(2.1f, 0.6f, true, 2.0f, 0.5f));

	// ─── (13) 복구 후보: 안정(0.5초+) 중 **최근접** ───
	{
		TArray<FNavRelocCandidate> C;
		C.Add({ 350.f, 2.0f });
		C.Add({ 120.f, 0.3f });   // 더 가깝지만 아직 불안정
		C.Add({ 240.f, 0.7f });
		TestEqual(TEXT("후보 — 불안정한 최근접은 건너뛰고 안정된 최근접"), UNavLocalizer::PickRelocCandidate(C, 0.5f), 2);
		C[1].StableSeconds = 0.5f;
		TestEqual(TEXT("후보 — 안정해지면 최근접"), UNavLocalizer::PickRelocCandidate(C, 0.5f), 1);
		TArray<FNavRelocCandidate> Unstable;
		Unstable.Add({ 100.f, 0.1f });
		TestEqual(TEXT("후보 — 안정된 핀 없음 → INDEX_NONE"), UNavLocalizer::PickRelocCandidate(Unstable, 0.5f), static_cast<int32>(INDEX_NONE));
	}

	// ─── (14) 오버레이 문구(명세 §3.4) ───
	TestEqual(TEXT("문구 shake"), UNavRelocalizeOverlay::SubTextForReason(TEXT("shake")), FString(TEXT("너무 빨리 움직였어요")));
	TestEqual(TEXT("문구 dark"), UNavRelocalizeOverlay::SubTextForReason(TEXT("dark")), FString(TEXT("카메라가 가려졌거나 어두워요")));
	TestEqual(TEXT("문구 featureless"), UNavRelocalizeOverlay::SubTextForReason(TEXT("featureless")), FString(TEXT("무늬가 있는 곳을 비춰 주세요")));
	TestEqual(TEXT("문구 resume"), UNavRelocalizeOverlay::SubTextForReason(TEXT("resume")), FString(TEXT("위치를 다시 확인하고 있어요")));
	TestEqual(TEXT("문구 30초 단계"), UNavRelocalizeOverlay::StageSubText(1, TEXT("shake")),
		FString(TEXT("전시 안내판이나 벽면이 보이도록 천천히 돌려 주세요")));
	TestEqual(TEXT("문구 90초 단계"), UNavRelocalizeOverlay::StageSubText(2, TEXT("shake")),
		FString(TEXT("밝은 곳으로 몇 걸음 옮긴 뒤 다시 천천히 둘러봐 주세요")));

	// ─── (15) 스캔 사이클 구간표(런북 §B-3) ───
	{
		auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.6f); };
		TestTrue(TEXT("사이클 0.2초 — 왼쪽 끝 정지"), Near(UNavRelocalizeOverlay::EvalScanPose(0.2f).X, -37.f));
		TestTrue(TEXT("사이클 1.45초 — 느린 스캔 가운데"), Near(UNavRelocalizeOverlay::EvalScanPose(1.45f).X, 0.f));
		TestTrue(TEXT("사이클 2.7초 — 오른쪽 끝 정지"), Near(UNavRelocalizeOverlay::EvalScanPose(2.7f).X, 37.f));
		TestTrue(TEXT("사이클 3.1초 — 빠른 복귀 가운데"), Near(UNavRelocalizeOverlay::EvalScanPose(3.1f).X, 0.f));
		TestTrue(TEXT("사이클 3.6초 — 왼쪽 끝 정지"), Near(UNavRelocalizeOverlay::EvalScanPose(3.6f).X, -37.f));
		TestTrue(TEXT("스캔 중 6°"), Near(UNavRelocalizeOverlay::EvalScanPose(1.0f).AngleDeg, 6.f));
		TestTrue(TEXT("복귀 중 −14° · α0.55"), Near(UNavRelocalizeOverlay::EvalScanPose(3.2f).AngleDeg, -14.f)
			&& FMath::IsNearlyEqual(UNavRelocalizeOverlay::EvalScanPose(3.2f).Opacity, 0.55f, 0.01f));
		TestTrue(TEXT("다음 사이클(3.9+1.45초)도 같다"), Near(UNavRelocalizeOverlay::EvalScanPose(5.35f).X, 0.f));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
