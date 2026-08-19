// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "Input/DragAndDrop.h"

class SWidget;
struct FSlateBrush;

/**
 * Started when a result card is dragged. Neither the Content Browser nor the level
 * viewport have an extensibility point for accepting an arbitrary custom drag
 * payload for an asset that doesn't exist locally yet (both only recognize their
 * own built-in FContentBrowserDataDragDropOp / FAssetDragDropOp, which require a
 * real local UObject/FAssetData) — so this operation doesn't try to integrate with
 * a specific drop target. It gives the drag gesture a native-feeling cursor
 * decorator (thumbnail + filename), and on drop, triggers the same download flow
 * as clicking the card's "Add to Project" button.
 */
class FP4SearchDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FP4SearchDragDropOp, FDragDropOperation)

	static TSharedRef<FP4SearchDragDropOp> New(const FString& InFileName, TSharedPtr<FSlateBrush> InThumbnailBrush,
		FSimpleDelegate InOnDropped);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

private:
	FString FileName;

	// Shared (not raw) ownership — keeps the brush alive for the whole drag even if the
	// result card (and FP4SearchModule::ThumbnailBrushes, its real owner) goes away first,
	// e.g. a new search's ClearResults() firing while this drag is still in flight.
	TSharedPtr<FSlateBrush> ThumbnailBrush;
	FSimpleDelegate OnDropped;
};
