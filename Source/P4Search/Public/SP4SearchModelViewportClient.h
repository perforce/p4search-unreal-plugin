// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "PreviewScene.h"
#include "EditorViewportClient.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Minimal viewport client for the "More Info" panel's 3D preview — a single
 * static mesh in an owned preview scene, with the base class's default
 * orbit/pan/zoom camera controls. Modelled on Engine/Plugins/Experimental/
 * ActorPalette's FActorPaletteViewportClient (the simplest real embedded-
 * viewport-client example in the engine).
 */
class FP4SearchModelViewportClient : public FEditorViewportClient
{
public:
	FP4SearchModelViewportClient();

	virtual FLinearColor GetBackgroundColor() const override;

	// Always orbit around the (fixed) look-at point rather than the default free-look
	// camera rotation — appropriate for a small preview of a single centered asset.
	virtual bool ShouldOrbitCamera() const override;

	// Slowly spins the previewed mesh in place (around its own local origin) —
	// simpler and more robust than orbiting the camera, which drifts the subject
	// out of frame since orbit-mode camera position isn't just a function of
	// SetViewRotation. Runs independently of manual camera dragging, so the two
	// never fight each other.
	virtual void Tick(float DeltaSeconds) override;

	/** Replaces the previewed mesh (nullptr clears it) and frames the camera on its bounds. */
	void SetPreviewMesh(UStaticMesh* Mesh);

private:
	FPreviewScene OwnedPreviewScene;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;

	float AutoRotateYawDegrees = 0.0f;
	static constexpr float AutoRotateDegreesPerSecond = 15.0f;
};
