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
	// SEditorViewport::MakeViewportToolbar() became `final` in UE 5.7, returning nullptr by
	// default -- the same no-toolbar behavior this override existed to provide, so on 5.7+ the
	// base's own default already does the job and this override would fail to compile.
#if !(ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
#endif
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void BindCommands() override;
	virtual void OnFocusViewportToSelection() override;
	// End of SEditorViewport interface

private:
	TSharedPtr<FP4SearchModelViewportClient> TypedViewportClient;
};
