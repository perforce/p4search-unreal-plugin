// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SButton.h"
#include "Animation/CurveSequence.h"

// Drop-in replacement for SButton that adds a subtle press animation -- a slight scale-down
// while held, springing back on release -- so clicking anything in the plugin reads as a
// physical press rather than a flat style-swap. Every existing SNew(SButton)/
// SAssignNew(Var, SButton) call site can switch to this by name alone: it reuses
// SButton::FArguments verbatim (no new SLATE_BEGIN_ARGS of its own), so no call site needs to
// change its own argument list, and SButton::Construct still does all the real setup.
class SP4SearchAnimatedButton : public SButton
{
public:
	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FCurveSequence PressAnim;
	FCurveHandle   PressCurve;
	bool bWasPressed = false;
};
