// Copyright Perforce Software, Inc. All Rights Reserved.

#include "SP4SearchAnimatedButton.h"

void SP4SearchAnimatedButton::Construct(const FArguments& InArgs)
{
	SButton::Construct(InArgs);

	PressCurve = PressAnim.AddCurve(0.0f, 0.12f, ECurveEaseFunction::CubicOut);

	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	SetRenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateLambda(
		[this]() -> TOptional<FSlateRenderTransform>
	{
		// PressCurve runs 0 -> 1 while held (scaling down to 94%) and reverses back to 0 on
		// release (scaling back up to 100%).
		const float Scale = 1.0f - (PressCurve.GetLerp() * 0.06f);
		return FSlateRenderTransform(Scale);
	}));
}

void SP4SearchAnimatedButton::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SButton::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const bool bPressed = IsPressed();
	if (bPressed != bWasPressed)
	{
		bWasPressed = bPressed;
		if (bPressed)
		{
			PressAnim.Play(AsShared());
		}
		else
		{
			PressAnim.PlayReverse(AsShared());
		}
	}
}
