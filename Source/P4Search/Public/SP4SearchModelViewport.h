// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "SEditorViewport.h"

class FP4SearchModelViewportClient;
class UStaticMesh;

/**
 * Small embedded 3D viewport for the "More Info" panel — no toolbar, no
 * asset-editor-tab wiring, just a plain Slate widget wrapping a single
 * FEditorViewportClient/FPreviewScene. Modelled on Engine/Plugins/
 * Experimental/ActorPalette's SActorPaletteViewport.
 */
class SP4SearchModelViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SP4SearchModelViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replaces the previewed mesh (nullptr clears it). */
	void SetPreviewMesh(UStaticMesh* Mesh);

	// SEditorViewport interface
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void BindCommands() override;
	virtual void OnFocusViewportToSelection() override;
	// End of SEditorViewport interface

private:
	TSharedPtr<FP4SearchModelViewportClient> TypedViewportClient;
};
