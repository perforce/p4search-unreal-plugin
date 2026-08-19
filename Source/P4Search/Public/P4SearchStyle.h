// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Fonts/SlateFontInfo.h"

/**  */
class FP4SearchStyle
{
public:

	static void Initialize();

	static void Shutdown();

	/** reloads textures used by slate renderer */
	static void ReloadTextures();

	/** @return The Slate style set for the Shooter game */
	static const ISlateStyle& Get();

	static FName GetStyleSetName();

	// Shared corner radius for cards, panels, buttons, and inputs — one value everywhere
	// instead of each widget picking its own.
	static constexpr float CornerRadius = 10.0f;

	// Spacing scale. Pick the nearest value instead of a bespoke literal so padding reads as
	// an intentional rhythm rather than ad hoc per-widget numbers.
	static constexpr float SpacingXS = 4.0f;
	static constexpr float SpacingS = 8.0f;
	static constexpr float SpacingM = 12.0f;
	static constexpr float SpacingL = 16.0f;
	static constexpr float SpacingXL = 24.0f;

	// Type scale — named tiers instead of ad hoc FCoreStyle::GetDefaultFontStyle calls.
	// Title is reserved for the single largest heading in the UI (the asset name in the
	// "More Info" panel); Header is for section/panel titles; SubHeader for card titles and
	// sub-section labels; Body for ordinary read text; Caption for secondary/muted text.
	static FSlateFontInfo GetTitleFont();
	static FSlateFontInfo GetHeaderFont();
	static FSlateFontInfo GetSubHeaderFont();
	static FSlateFontInfo GetBodyFont();
	static FSlateFontInfo GetBodyBoldFont();
	static FSlateFontInfo GetCaptionFont();
	static FSlateFontInfo GetCaptionBoldFont();

private:

	static TSharedRef< class FSlateStyleSet > Create();

private:

	static TSharedPtr< class FSlateStyleSet > StyleInstance;
};