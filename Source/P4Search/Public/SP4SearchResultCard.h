// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "Animation/CurveSequence.h"
#include "P4SearchStyle.h"

class SImage;
class SBox;
class SButton;
class SCheckBox;
class STextBlock;

class SP4SearchResultCard : public SCompoundWidget
{
public:

	// Corner radius shared by the card's own border brushes and the thumbnail image's
	// rounded top corners (see Construct() and FP4SearchModule's brush-construction sites
	// in P4Search.cpp) so the thumbnail's rounding always matches the card frame's — tied to
	// FP4SearchStyle::CornerRadius so cards match every other rounded surface in the plugin.
	static constexpr float CardCornerRadius = FP4SearchStyle::CornerRadius;

	// Outer-ring border width — same for both states now, so selection reads purely through
	// the border's color change (see CardBorder/FStyleColors::Select in Construct/SetSelected)
	// rather than a jump in thickness.
	static constexpr float BorderWidthUnselected = 2.0f;
	static constexpr float BorderWidthSelected   = 2.0f;

	// Depot path / tags / changelist / file size are shown in the "More Info"
	// side panel instead, built from the same search-result data independently
	// in FP4SearchModule. Only the revision is shown on the card face itself.
	SLATE_BEGIN_ARGS(SP4SearchResultCard)
		: _FileName(TEXT(""))
		, _DepotFile(TEXT(""))
		, _Rev(0)
		, _bIsHead(true)
		, _bSupportsAddToProject(true)
		, _AppearIndex(0)
	{}
		SLATE_ARGUMENT(FString,      FileName)
		SLATE_ARGUMENT(FString,      DepotFile)
		SLATE_ARGUMENT(int32,        Rev)
		SLATE_ARGUMENT(bool,         bIsHead)
		SLATE_ARGUMENT(FButtonStyle, ActionButtonStyle)

		// Hides the "Add to Project" button for extensions no registered UFactory can import
		// in this project (see FP4SearchModule::IsExtensionSupportedForImport) -- offering a
		// button that would just silently do nothing is worse than not offering it at all.
		SLATE_ARGUMENT(bool,         bSupportsAddToProject)

		// Position of this card within the current page's results (0-based) -- staggers the
		// appear-animation so cards cascade in one after another instead of all moving at once.
		SLATE_ARGUMENT(int32,        AppearIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SP4SearchResultCard() override;

	// SWidget interface — lets the card itself be dragged out (see P4SearchDragDropOp.h
	// for why the drop side just re-triggers the same download as "Add to Project"), and
	// opens "More Info" on a plain click (mouse up without enough movement to have
	// triggered OnDragDetected) anywhere the click isn't first intercepted by one of the
	// card's own buttons/checkbox.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	// Scrubs the hover-preview filmstrip: while hovering, the frame shown tracks the mouse's
	// horizontal position across the card (left edge = first frame, right edge = last frame)
	// rather than auto-playing on a timer.
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	// Drives the appear-animation's fade-in. RenderOpacity (unlike RenderTransform) is a plain
	// float with no TAttribute/builder binding, so it has to be pushed via SetRenderOpacity()
	// each frame instead of read declaratively -- only does anything while AppearAnim is
	// actually playing.
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	// Slate keeps ticking every card regardless of scroll position (SScrollBox only clips
	// painting, it doesn't stop off-screen children from ticking), so playing the appear
	// animation unconditionally at construction time means cards below the fold finish
	// animating in the background before the user ever scrolls down to see them -- by the
	// time they're actually visible, nothing appears to happen. OnPaint() gets MyCullingRect
	// (this card's real, currently-visible clip region), which Tick() doesn't -- used here to
	// defer the very first Play() until this card is genuinely on-screen for the first time.
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	/** Called once the async thumbnail response arrives */
	void SetThumbnailBrush(TSharedPtr<FSlateBrush> Brush);

	/** Called once the async ribbon (sprite filmstrip) response arrives. Not shown until hovered.
	 * FrameUVs gives each frame's exact texture-space rect (from the ribbon/meta endpoint),
	 * in the same left-to-right order they should play. */
	void SetSpriteBrush(FSlateBrush* Brush, TArray<FBox2f> FrameUVs);

	/** Wire up the Add to Project callback post-construction */
	void SetOnAddToProject(FSimpleDelegate D);

	/** Wire up the Generate Thumbnail callback post-construction */
	void SetOnGenerateThumbnail(FSimpleDelegate D);

	/** Wire up the More Info callback post-construction */
	void SetOnMoreInfo(FSimpleDelegate D);

	/** Mark Add to Project button as done */
	void SetAdded();

	/** Mark Generate Thumbnail button as done */
	void SetGenerated();

	/** Reveal the Generate Thumbnail button (hidden until asset class is known) */
	void ShowGenerateThumbnailButton();

	/** Shows/hides a banner across the top of the thumbnail explaining that this asset's
	 *  thumbnail/preview needs a DAM (or Search) license — called from FetchThumbnail once
	 *  a thumbnail fetch comes back empty on an unlicensed server. */
	void SetLicenseBannerVisible(bool bVisible);

	/** Called while an Add to Project / Generate Thumbnail download is in flight — shows a
	 *  progress bar under the action icons. An unset Percent shows an indeterminate marquee
	 *  (content length not yet known), matching the "More Info" panel's model-download bar. */
	void SetDownloadProgress(TOptional<float> Percent);

	/** Called once the download finishes (success or failure) to hide the progress bar again. */
	void SetDownloadIdle();

	/** Highlights the card's border while the "More Info" panel is showing this asset */
	void SetSelected(bool bSelected);

	/** Multi-select checkbox, for bulk actions (distinct from SetSelected's "More Info" highlight) */
	void SetMultiSelected(bool bSelected);
	bool IsMultiSelected() const { return bIsMultiSelected; }

	DECLARE_DELEGATE_OneParam(FOnMultiSelectChanged, bool /*bSelected*/);

	/** Wire up a callback fired whenever multi-select state actually changes (checkbox click
	 *  or SetMultiSelected) — lets FP4SearchModule maintain a running selected-count instead
	 *  of rescanning every card each time the bulk-select bar repaints. */
	void SetOnMultiSelectChanged(FOnMultiSelectChanged D);

	const FString& GetDepotFile() const { return CardDepotFile; }
	const FString& GetCardFileName() const { return CardFileName; }
	int32 GetRev() const { return CardRev; }

	// The "download" glyph used for every Add to Project button (this card's own, and the
	// one in FP4SearchModule's "More Info" panel) — Resources/download-icon.png, matching
	// the Force Design Spec's Material Symbols Rounded "download" icon (search-app's design
	// system; there's no shared asset pipeline between the two products, so this is a
	// one-off PNG rather than a live pull from that spec). Loaded once and shared across
	// every card instance — decoded lazily on first call, cached for the process lifetime.
	static const FSlateBrush* GetDownloadIconBrush();

private:

	void StartSpriteAnimation();
	void StopSpriteAnimation();

	TSharedPtr<SImage>  ThumbnailImage;
	TSharedPtr<SImage>  SpriteImage;
	TSharedPtr<SButton> HoverDetector;  // transparent, sits over the thumbnail purely to catch hover
	TSharedPtr<SButton> AddButton;
	TSharedPtr<SImage>  AddButtonIcon;
	TSharedPtr<SButton> GenThumbButton;
	TSharedPtr<SImage>  GenThumbButtonIcon;
	TSharedPtr<SBox>    GenThumbContainer;
	TSharedPtr<SCheckBox> MultiSelectCheckBox;
	TSharedPtr<SBox>    LicenseBannerContainer;

	TUniquePtr<FSlateRoundedBoxBrush> CardOuterBrush;
	TUniquePtr<FSlateRoundedBoxBrush> CardInnerBrush;

	// Shown before any real/blur thumbnail has loaded — rounded on top only (like the
	// thumbnail brushes FP4SearchModule builds later) so the card's rounded top corners
	// are never hidden behind a flush rectangular placeholder.
	TUniquePtr<FSlateRoundedBoxBrush> InitialThumbnailBrush;

	// Background of the SBorder wrapping the thumbnail overlay — must be rounded on top
	// with the exact same radius as InitialThumbnailBrush/the loaded-thumbnail brushes,
	// otherwise its own opaque rectangular fill shows through underneath whichever
	// thumbnail brush is on top once that brush's corners go transparent.
	TUniquePtr<FSlateRoundedBoxBrush> ThumbnailWrapperBrush;

	// Background of the "DAM license required" banner — rounded on top only, same radius as
	// ThumbnailWrapperBrush, since the banner sits flush against the card's own rounded top
	// edge (see Construct()'s license-banner overlay slot).
	TUniquePtr<FSlateRoundedBoxBrush> LicenseBannerBrush;

	FButtonStyle    ActionButtonStyle;
	FButtonStyle    IconButtonStyle;      // must outlive Construct (SButton stores a raw pointer)
	FButtonStyle    DownloadButtonStyle;  // like IconButtonStyle, but with a visible hover/press background so "Add to Project" reads as clickable
	FSimpleDelegate StoredOnAddToProject;
	FSimpleDelegate StoredOnGenerateThumbnail;
	FSimpleDelegate StoredOnMoreInfo;
	FOnMultiSelectChanged StoredOnMultiSelectChanged;

	// Hover-preview filmstrip — owned (texture/brush) by FP4SearchModule, this card just
	// holds a raw pointer and mutates its UVRegion while animating on hover.
	FSlateBrush* SpriteBrush = nullptr;
	TArray<FBox2f> SpriteFrameUVs;
	int32 SpriteCurrentFrame = 0;
	bool bIsHoveringSprite = false;

	bool bIsCardSelected = false;
	bool bIsMultiSelected = false;
	bool bIsDownloading = false;
	TOptional<float> DownloadProgressPercent;

	// Played once, forward only, the first time this card is actually painted on-screen (see
	// OnPaint()) -- every card slides up from just below its final position while fading in,
	// giving new search results a bit of arrival motion instead of just popping into the
	// grid. Mutable: triggered from OnPaint(), which the base class declares const.
	mutable FCurveSequence AppearAnim;
	FCurveHandle   AppearCurve;

	// Guards AppearAnim.Play() to only ever fire once per card, the first time OnPaint()
	// finds it actually visible (not fully clipped by a scrolled-past viewport).
	mutable bool bHasTriggeredAppearAnim = false;

	FString CardFileName;
	FString CardDepotFile;
	int32   CardRev = 0;
	bool    CardIsHead = true;
	bool    bCardSupportsAddToProject = true;

	// Shared (not raw) ownership — FP4SearchDragDropOp keeps its own TSharedPtr to whichever
	// brush was current when a drag started, so a new search's ClearResults() (which empties
	// FP4SearchModule::ThumbnailBrushes, the brush's real owner) can't free it out from under
	// an in-flight drag's decorator.
	TSharedPtr<FSlateBrush> CurrentThumbnailBrush;
};
