// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#include "SP4SearchResultCard.h"
#include "P4SearchDragDropOp.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Interfaces/IPluginManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Animation/CurveSequence.h"
#include "SP4SearchAnimatedButton.h"

const FSlateBrush* SP4SearchResultCard::GetDownloadIconBrush()
{
	static TSharedPtr<FSlateDynamicImageBrush> CachedBrush;
	if (CachedBrush.IsValid())
	{
		return CachedBrush.Get();
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("P4Search"));
	if (!Plugin.IsValid())
	{
		return nullptr;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *(Plugin->GetBaseDir() / TEXT("Resources/download-icon.png"))))
	{
		return nullptr;
	}

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	const TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::PNG);
	if (!IW.IsValid() || !IW->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return nullptr;
	}

	TArray<uint8> BGRA;
	if (!IW->GetRaw(ERGBFormat::BGRA, 8, BGRA))
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(IW->GetWidth(), IW->GetHeight(), PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	uint8* MipData = static_cast<uint8*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	FMemory::Memcpy(MipData, BGRA.GetData(), BGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	Texture->AddToRoot(); // this brush is cached for the process lifetime — never garbage collect it

	CachedBrush = MakeShareable(new FSlateDynamicImageBrush(
		Texture, FVector2D((float)IW->GetWidth(), (float)IW->GetHeight()), FName(TEXT("P4Search.DownloadIcon"))));
	return CachedBrush.Get();
}

void SP4SearchResultCard::Construct(const FArguments& InArgs)
{
	// ── Colour palette — same FStyleColors Unreal Editor itself uses, so the
	// card tracks whatever theme (dark/light/custom) the user has active.
	const FSlateColor CardBorder = FStyleColors::InputOutline;
	const FSlateColor CardBg     = FStyleColors::Panel;
	const FSlateColor ThumbBg    = FStyleColors::Recessed;
	const FSlateColor ColTitle   = FStyleColors::Foreground;
	const FSlateColor ColMeta    = FStyleColors::Foreground;
	const FSlateColor ColAccent  = FStyleColors::Primary;
	const FSlateColor ColDone    = FStyleColors::Success;

	const FSlateFontInfo FontName = FP4SearchStyle::GetBodyBoldFont();
	const FSlateFontInfo FontMeta = FP4SearchStyle::GetBodyFont();

	// ── Arguments ─────────────────────────────────────────────────────────
	// Depot path / tags / changelist / file size are shown in the "More Info"
	// side panel instead — only the revision appears on the card face.
	const FString FileName = InArgs._FileName;
	const int32   Rev      = InArgs._Rev;
	CardFileName = FileName;
	CardDepotFile = InArgs._DepotFile;
	CardRev = Rev;
	CardIsHead = InArgs._bIsHead;
	bCardSupportsAddToProject = InArgs._bSupportsAddToProject;

	// Staggered by 0.02s per card position so a page of results cascades in rather than every
	// card moving in lockstep -- AppearCurve stays at 0 (SetRenderOpacity(0.0f) below, no visible
	// slide via the RenderTransform lambda either) until this card's turn in the sequence.
	const float AppearDelay = InArgs._AppearIndex * 0.02f;
	AppearCurve = AppearAnim.AddCurve(AppearDelay, 0.3f, ECurveEaseFunction::CubicOut);

	const float CardRadius = CardCornerRadius;
	// Baked-in tint is neutral white — the outer SBorder's BorderBackgroundColor (below)
	// multiplies against it, so that's the only place the actual ring color is driven from,
	// letting SetSelected() swap it reactively without rebuilding the brush.
	CardOuterBrush   = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, CardRadius);
	// Inner radius is compensated for the current border width (see SetSelected) so the
	// inner rounded rect stays concentric with the outer ring instead of looking "rounder"
	// once the selected state's thicker border shrinks its bounding box.
	CardInnerBrush   = MakeUnique<FSlateRoundedBoxBrush>(CardBg, CardRadius - BorderWidthUnselected);

	// Rounded on top only (bottom corners are square — they're interior to the card, not
	// touching its own rounded frame). Both the thumbnail wrapper's own background AND the
	// placeholder image drawn on top of it need this same rounding — the wrapper's opaque
	// rectangular fill would otherwise show through once the image's corners go transparent.
	const FVector4 TopRoundingOnly(CardRadius, CardRadius, 0.0f, 0.0f);
	ThumbnailWrapperBrush = MakeUnique<FSlateRoundedBoxBrush>(CardBg,  TopRoundingOnly);
	InitialThumbnailBrush = MakeUnique<FSlateRoundedBoxBrush>(ThumbBg, TopRoundingOnly);
	ActionButtonStyle = InArgs._ActionButtonStyle;

	// ── Small transparent icon-button style (Download / Info / Generate) ──
	// NOTE: stored as a member — SButton keeps a raw pointer to this style,
	// so it must NOT be a local variable (it would become a dangling pointer
	// the moment Construct() returns, causing a crash on first paint).
	IconButtonStyle = FButtonStyle()
		.SetNormal  (FSlateNoResource())
		.SetHovered (FSlateNoResource())
		.SetPressed (FSlateNoResource())
		.SetDisabled(FSlateNoResource())
		.SetNormalPadding (FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	// ── "Add to Project" download button — transparent at rest, but a visible rounded
	// highlight on hover/press so it reads as clickable rather than a static icon.
	DownloadButtonStyle = FButtonStyle()
		.SetNormal  (FSlateNoResource())
		.SetHovered (FSlateRoundedBoxBrush(FStyleColors::Hover,  CardRadius))
		.SetPressed (FSlateRoundedBoxBrush(FStyleColors::Hover2, CardRadius))
		.SetDisabled(FSlateNoResource())
		.SetNormalPadding (FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	// ── Widget tree ───────────────────────────────────────────────────────
	// Compact, always-visible tile — small thumbnail, name below it, then a
	// single row with the revision on the left and small action icons
	// (download / info / generate) on the right. No hover-reveal.
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(192.0f) // 20% narrower than the original 240.0f
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform>
		{
			// Slides down from 32px above its final position as AppearCurve runs 0 -> 1 --
			// i.e. the card arrives travelling upward into place, alongside the fade driven
			// by Tick() below (RenderOpacity is a plain float with no TAttribute/builder
			// binding, unlike RenderTransform, so it can't be set declaratively here).
			const float SlideOffset = (1.0f - AppearCurve.GetLerp()) * 32.0f;
			return FSlateRenderTransform(FVector2D(0.0f, SlideOffset));
		})
		[
			// Border ring (rounded) — tints to FStyleColors::Select and thickens while
			// this card's "More Info" panel is open, back to a thin CardBorder ring otherwise.
			SNew(SBorder)
			.BorderImage(CardOuterBrush.Get())
			.BorderBackgroundColor_Lambda([this, CardBorder]() -> FSlateColor
			{
				return bIsCardSelected ? FStyleColors::Select : CardBorder;
			})
			.Padding_Lambda([this]() -> FMargin
			{
				return FMargin(bIsCardSelected ? BorderWidthSelected : BorderWidthUnselected);
			})
			[
				SNew(SBorder)
				.BorderImage(CardInnerBrush.Get())
				.Padding(0.0f)
				[
					SNew(SVerticalBox)

					// ── Thumbnail (+ hover-preview sprite overlay) ──────────
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(ThumbnailWrapperBrush.Get())
						.Padding(0.0f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SAssignNew(ThumbnailImage, SImage)
								.DesiredSizeOverride(FVector2D(192.0f, 108.0f))
								.Image(InitialThumbnailBrush.Get())
								.ColorAndOpacity(FLinearColor::White)
							]
							+ SOverlay::Slot()
							[
								SAssignNew(SpriteImage, SImage)
								.Visibility(EVisibility::Collapsed)
								.DesiredSizeOverride(FVector2D(192.0f, 108.0f))
							]
							+ SOverlay::Slot()
							[
								SAssignNew(HoverDetector, SP4SearchAnimatedButton)
								.ButtonStyle(&IconButtonStyle)
								.ContentPadding(FMargin(0.0f))
								.Cursor(EMouseCursor::Default)
								.OnHovered_Lambda([this]() { StartSpriteAnimation(); })
								.OnUnhovered_Lambda([this]() { StopSpriteAnimation(); })
								.OnClicked_Lambda([this]() -> FReply
								{
									StoredOnMoreInfo.ExecuteIfBound();
									return FReply::Handled();
								})
							]
							// Bulk-select checkbox — top-left corner, independent of the
							// "More Info" selection highlight on the card's border.
							+ SOverlay::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Top)
							.Padding(8.0f)
							[
								SAssignNew(MultiSelectCheckBox, SCheckBox)
								.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
								{
									const bool bNewSelected = (NewState == ECheckBoxState::Checked);
									if (bNewSelected != bIsMultiSelected)
									{
										bIsMultiSelected = bNewSelected;
										StoredOnMultiSelectChanged.ExecuteIfBound(bNewSelected);
									}
								})
							]
						]
					]

					// ── Filename ───────────────────────────────────────────
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.0f, 12.0f, 16.0f, 3.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FileName))
						.Font(FontName)
						.ColorAndOpacity(ColTitle)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]

					// ── Revision + action icons ────────────────────────────
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.0f, 0.0f, 12.0f, 3.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::Format(NSLOCTEXT("P4Search", "RevLabel", "#{0}"), FText::AsNumber(Rev)))
							.Font(FontMeta)
							.ColorAndOpacity(ColMeta)
						]

						// Not-head-revision warning — reuses the engine's own pre-colored
						// alert-triangle icon (Force UI's equivalent: Material Symbols Rounded
						// "warning", warning-orange on light surfaces — same triangle+exclamation
						// semantics, sourced from Slate directly instead of hand-drawn).
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SImage)
							.Visibility(CardIsHead ? EVisibility::Collapsed : EVisibility::Visible)
							.Image(FCoreStyle::Get().GetBrush("Icons.WarningWithColor"))
							.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
							.ToolTipText(NSLOCTEXT("P4Search", "NotHeadRevisionTip", "Not the head revision"))
						]

						// Add to Project (download icon → check once added)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SAssignNew(AddButton, SP4SearchAnimatedButton)
							.Visibility(bCardSupportsAddToProject ? EVisibility::Visible : EVisibility::Collapsed)
							.ButtonStyle(&DownloadButtonStyle)
							.ContentPadding(FMargin(4.0f))
							.ToolTipText(FText::FromString(TEXT("Add to Project")))
							.OnClicked_Lambda([this]() -> FReply
							{
								StoredOnAddToProject.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								SAssignNew(AddButtonIcon, SImage)
								.Image(GetDownloadIconBrush())
								.DesiredSizeOverride(FVector2D(22.0f, 22.0f))
								.ColorAndOpacity(ColAccent)
							]
						]

						// Generate Thumbnail (uasset/Blueprint only — hidden until known)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SAssignNew(GenThumbContainer, SBox)
							.Visibility(EVisibility::Collapsed)
							[
								SAssignNew(GenThumbButton, SP4SearchAnimatedButton)
								.ButtonStyle(&IconButtonStyle)
								.ContentPadding(FMargin(4.0f))
								.ToolTipText(FText::FromString(TEXT("Generate EventGraph Thumbnail")))
								.OnClicked_Lambda([this]() -> FReply
								{
									StoredOnGenerateThumbnail.ExecuteIfBound();
									return FReply::Handled();
								})
								[
									SAssignNew(GenThumbButtonIcon, SImage)
									.Image(FCoreStyle::Get().GetBrush("Icons.MakeStaticMesh"))
									.DesiredSizeOverride(FVector2D(22.0f, 22.0f))
									.ColorAndOpacity(ColMeta)
								]
							]
						]
					]

					// Shown while an Add to Project / Generate Thumbnail download is in flight.
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.0f, 0.0f, 16.0f, 8.0f)
					[
						SNew(SProgressBar)
						.Visibility_Lambda([this]() { return bIsDownloading ? EVisibility::Visible : EVisibility::Collapsed; })
						.Percent_Lambda([this]() -> TOptional<float> { return DownloadProgressPercent; })
					]
				]
			]
		]
	];

	SetRenderOpacity(0.0f); // Tick() below fades this back up to 1.0 once AppearAnim starts (OnPaint())
}

void SP4SearchResultCard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (AppearAnim.IsPlaying())
	{
		SetRenderOpacity(AppearCurve.GetLerp());
	}
}

int32 SP4SearchResultCard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!bHasTriggeredAppearAnim)
	{
		bool bOverlapping = false;
		MyCullingRect.IntersectionWith(AllottedGeometry.GetLayoutBoundingRect(), bOverlapping);
		if (bOverlapping)
		{
			bHasTriggeredAppearAnim = true;
			AppearAnim.Play(ConstCastSharedRef<SWidget>(AsShared()));
		}
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

SP4SearchResultCard::~SP4SearchResultCard()
{
}

void SP4SearchResultCard::SetThumbnailBrush(TSharedPtr<FSlateBrush> Brush)
{
	CurrentThumbnailBrush = Brush;

	if (ThumbnailImage.IsValid())
	{
		// Feature 1: reset color tint to white when real image arrives
		ThumbnailImage->SetImage(Brush.Get());
		ThumbnailImage->SetColorAndOpacity(FLinearColor::White);
	}
}

void SP4SearchResultCard::SetSpriteBrush(FSlateBrush* Brush, TArray<FBox2f> FrameUVs)
{
	SpriteBrush = Brush;
	SpriteFrameUVs = MoveTemp(FrameUVs);
	SpriteCurrentFrame = 0;
}

void SP4SearchResultCard::StartSpriteAnimation()
{
	if (!SpriteBrush || SpriteFrameUVs.Num() == 0 || !SpriteImage.IsValid())
	{
		return;
	}

	bIsHoveringSprite = true;
	SpriteCurrentFrame = 0;
	SpriteBrush->SetUVRegion(SpriteFrameUVs[0]);
	SpriteImage->SetImage(SpriteBrush);
	SpriteImage->SetVisibility(EVisibility::HitTestInvisible);
}

void SP4SearchResultCard::StopSpriteAnimation()
{
	bIsHoveringSprite = false;

	if (SpriteImage.IsValid())
	{
		SpriteImage->SetVisibility(EVisibility::Collapsed);
	}
}

void SP4SearchResultCard::SetOnAddToProject(FSimpleDelegate D)
{
	StoredOnAddToProject = D;
}

void SP4SearchResultCard::SetOnGenerateThumbnail(FSimpleDelegate D)
{
	StoredOnGenerateThumbnail = D;
}

void SP4SearchResultCard::SetOnMoreInfo(FSimpleDelegate D)
{
	StoredOnMoreInfo = D;
}

void SP4SearchResultCard::SetOnMultiSelectChanged(FOnMultiSelectChanged D)
{
	StoredOnMultiSelectChanged = D;
}

void SP4SearchResultCard::SetAdded()
{
	if (AddButtonIcon.IsValid())
	{
		AddButtonIcon->SetImage(FCoreStyle::Get().GetBrush("Icons.Check"));
		AddButtonIcon->SetColorAndOpacity(FStyleColors::Success);
	}
	if (AddButton.IsValid())
	{
		AddButton->SetEnabled(false);
	}
}

void SP4SearchResultCard::SetGenerated()
{
	if (GenThumbButtonIcon.IsValid())
	{
		GenThumbButtonIcon->SetImage(FCoreStyle::Get().GetBrush("Icons.Check"));
		GenThumbButtonIcon->SetColorAndOpacity(FStyleColors::Success);
	}
	if (GenThumbButton.IsValid())
	{
		GenThumbButton->SetEnabled(false);
	}
}

void SP4SearchResultCard::ShowGenerateThumbnailButton()
{
	if (GenThumbContainer.IsValid())
	{
		GenThumbContainer->SetVisibility(EVisibility::Visible);
	}
}

void SP4SearchResultCard::SetDownloadProgress(TOptional<float> Percent)
{
	bIsDownloading = true;
	DownloadProgressPercent = Percent;
}

void SP4SearchResultCard::SetDownloadIdle()
{
	bIsDownloading = false;
	DownloadProgressPercent.Reset();
}

void SP4SearchResultCard::SetSelected(bool bSelected)
{
	bIsCardSelected = bSelected;

	// Keep the inner rounded rect concentric with the outer ring — its radius has to shrink
	// by the same amount the border width grows, otherwise the same nominal 12px radius
	// looks proportionally "rounder" once the selected state's thicker border eats into its
	// bounding box (see the matching comment in Construct()).
	if (CardInnerBrush.IsValid())
	{
		const float BorderWidth = bSelected ? BorderWidthSelected : BorderWidthUnselected;
		CardInnerBrush->OutlineSettings = FSlateBrushOutlineSettings(CardCornerRadius - BorderWidth);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SP4SearchResultCard::SetMultiSelected(bool bSelected)
{
	if (bSelected == bIsMultiSelected)
	{
		return;
	}
	bIsMultiSelected = bSelected;
	if (MultiSelectCheckBox.IsValid())
	{
		MultiSelectCheckBox->SetIsChecked(bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	}
	StoredOnMultiSelectChanged.ExecuteIfBound(bSelected);
}

FReply SP4SearchResultCard::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

FReply SP4SearchResultCard::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Reaching here means the matching mouse-down was captured by the card itself (i.e. it
	// wasn't intercepted by Add to Project / Generate Thumbnail / the multi-select checkbox)
	// and didn't cross the drag threshold into OnDragDetected — a plain click on the card
	// body, so open "More Info" (there's no dedicated button for this anymore — the whole
	// card is the click target).
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		StoredOnMoreInfo.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SP4SearchResultCard::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().BeginDragDrop(FP4SearchDragDropOp::New(CardFileName, CurrentThumbnailBrush, StoredOnAddToProject));
}

FReply SP4SearchResultCard::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsHoveringSprite && SpriteBrush && SpriteFrameUVs.Num() > 0 && SpriteImage.IsValid())
	{
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
		const float Width  = FMath::Max(MyGeometry.GetLocalSize().X, 1.0f);
		const int32 Frame  = FMath::Clamp(FMath::FloorToInt(LocalX / Width * SpriteFrameUVs.Num()), 0, SpriteFrameUVs.Num() - 1);

		if (Frame != SpriteCurrentFrame)
		{
			SpriteCurrentFrame = Frame;
			SpriteBrush->SetUVRegion(SpriteFrameUVs[SpriteCurrentFrame]);
			SpriteImage->InvalidateImage();
		}
	}
	return FReply::Unhandled();
}
