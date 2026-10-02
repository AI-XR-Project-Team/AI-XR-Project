#pragma once
#include "CoreMinimal.h"

struct TIMEMACHINEAR_API FTimeRevealMath
{
	static bool ClassifySwipe(const FVector2D& Press, const FVector2D& Release, float Seconds,
		const FVector2D& Viewport, float MinNormalized, float MaxSeconds, float MaxSideRatio);
	static FVector ThrowPoint(const FVector& A, const FVector& B, const FVector& Up, float ArcHeight,
		float Alpha, const class UCurveFloat* Ease = nullptr);
	static float PhaseAlpha(float Time, float Start, float End, const class UCurveFloat* Curve = nullptr);
};
