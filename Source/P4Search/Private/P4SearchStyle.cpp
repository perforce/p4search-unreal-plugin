// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4SearchStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/CoreStyle.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FP4SearchStyle::StyleInstance = nullptr;

void FP4SearchStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FP4SearchStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FP4SearchStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("P4SearchStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FP4SearchStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("P4SearchStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("P4Search")->GetBaseDir() / TEXT("Resources"));

	Style->Set("P4Search.OpenPluginWindow", new IMAGE_BRUSH(TEXT("p4search-favicon"), Icon20x20));
	Style->Set("P4Search.TabIcon", new IMAGE_BRUSH(TEXT("p4search-favicon"), Icon16x16));

	// Wordmark logo shown top-left of the tab's own toolbar — native aspect ratio is
	// 185.54:50.81 (~3.65:1), so this is sized to a 28px-tall display height rather than
	// forced into a square like the favicon it replaces there.
	Style->Set("P4Search.ProductLogo", new IMAGE_BRUSH_SVG(TEXT("p4search-product-logo"), FVector2D(102.2f, 28.0f)));

	// Custom sidebar file-type category icons — hand-drawn rather than borrowed from the
	// Editor's own icon set, since none of those glyphs were a good thematic fit.
	Style->Set("P4Search.Icon.AllAssets", new IMAGE_BRUSH_SVG(TEXT("icon-all-assets"), Icon16x16));
	Style->Set("P4Search.Icon.3D",        new IMAGE_BRUSH_SVG(TEXT("icon-3d"),         Icon16x16));
	Style->Set("P4Search.Icon.2D",        new IMAGE_BRUSH_SVG(TEXT("icon-2d"),         Icon16x16));
	Style->Set("P4Search.Icon.Unreal",    new IMAGE_BRUSH_SVG(TEXT("icon-unreal"),     Icon16x16));

	return Style;
}

void FP4SearchStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FP4SearchStyle::Get()
{
	return *StyleInstance;
}

FSlateFontInfo FP4SearchStyle::GetTitleFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 20);
}

FSlateFontInfo FP4SearchStyle::GetHeaderFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 16);
}

FSlateFontInfo FP4SearchStyle::GetSubHeaderFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 13);
}

FSlateFontInfo FP4SearchStyle::GetBodyFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 10);
}

FSlateFontInfo FP4SearchStyle::GetBodyBoldFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 10);
}

FSlateFontInfo FP4SearchStyle::GetCaptionFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 9);
}

FSlateFontInfo FP4SearchStyle::GetCaptionBoldFont()
{
	return FCoreStyle::GetDefaultFontStyle("Bold", 9);
}
