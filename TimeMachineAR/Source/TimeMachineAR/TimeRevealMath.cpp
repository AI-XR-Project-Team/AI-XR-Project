#include "TimeRevealMath.h"
#include "Curves/CurveFloat.h"

bool FTimeRevealMath::ClassifySwipe(const FVector2D& Press, const FVector2D& Release, float Seconds,
	const FVector2D& Viewport, float MinNormalized, float MaxSeconds, float MaxSideRatio)
{
	if (Viewport.Y <= 0.f || Seconds < 0.f || Seconds > MaxSeconds) return false;
	const float Up = Press.Y - Release.Y;
	const float Side = FMath::Abs(Release.X - Press.X);
	return Up / Viewport.Y >= MinNormalized && Side <= Up * MaxSideRatio;
}

float FTimeRevealMath::PhaseAlpha(float Time, float Start, float End, const UCurveFloat* Curve)
{
	const float A = End > Start ? FMath::Clamp((Time - Start) / (End - Start), 0.f, 1.f) : (Time >= End ? 1.f : 0.f);
	return Curve ? FMath::Clamp(Curve->GetFloatValue(A), 0.f, 1.f) : FMath::SmoothStep(0.f, 1.f, A);
}

FVector FTimeRevealMath::ThrowPoint(const FVector& A, const FVector& B, const FVector& Up, float ArcHeight,
	float Alpha, const UCurveFloat* Ease)
{
	const float S = FMath::Clamp(Alpha, 0.f, 1.f);
	const float E = Ease ? FMath::Clamp(Ease->GetFloatValue(S), 0.f, 1.f) : 1.f - FMath::Square(1.f - S);
	return FMath::Lerp(A, B, E) + Up.GetSafeNormal() * ArcHeight * 4.f * S * (1.f - S);
}
