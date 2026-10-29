// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#include "SP4SearchModelViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/StaticMesh.h"

FP4SearchModelViewportClient::FP4SearchModelViewportClient()
	: FEditorViewportClient(nullptr, nullptr, nullptr)
{
	PreviewScene = &OwnedPreviewScene;

	SetViewModes(VMI_Lit, VMI_Lit);
	SetRealtime(true);

	EngineShowFlags.Selection = false;
	EngineShowFlags.SelectionOutline = false;
	EngineShowFlags.Grid = false;

	// Disables eye adaptation/TemporalAA/Lumen GI/AO/etc — without this, rotating the camera
	// rapidly changes how much of the frame is the near-black background vs. the model, and
	// auto-exposure re-metering on every frame reads as a visible flash/flicker. This is the
	// same call the engine's own small live-preview viewports use for exactly this reason
	// (see e.g. Cascade's CascadePreviewViewportClient, Material Editor's preview viewport) —
	// its own doc comment says "for rendering thumbnails, cascade, material editor, static
	// mesh editor".
	EngineShowFlags.DisableAdvancedFeatures();

	SetViewLocation(FVector(-250.0f, -250.0f, 180.0f));
	SetViewRotation(FRotator(-20.0f, 45.0f, 0.0f));
	SetViewportType(LVT_Perspective);
	SetLookAtLocation(FVector::ZeroVector, false);

	// Fill light — dimmer, from roughly the opposite side of FPreviewScene's own default key
	// light, so the model's shadowed side isn't left completely flat/black. FPreviewScene's
	// own light defaults to UE_PI (~3.14) intensity; this is deliberately about half that.
	UDirectionalLightComponent* FillLight = NewObject<UDirectionalLightComponent>();
	FillLight->Intensity = 1.5f;
	FillLight->LightColor = FColor::White;
	FillLight->SetRelativeRotation(FRotator(-30.0f, 135.0f, 0.0f));
	OwnedPreviewScene.AddComponent(FillLight, FTransform::Identity);
}

FLinearColor FP4SearchModelViewportClient::GetBackgroundColor() const
{
	return FLinearColor(0.02f, 0.02f, 0.02f);
}

bool FP4SearchModelViewportClient::ShouldOrbitCamera() const
{
	return true;
}

void FP4SearchModelViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	if (PreviewMeshComponent)
	{
		AutoRotateYawDegrees = FMath::Fmod(AutoRotateYawDegrees + AutoRotateDegreesPerSecond * DeltaSeconds, 360.0f);
		PreviewMeshComponent->SetWorldRotation(FRotator(0.0f, AutoRotateYawDegrees, 0.0f));
		Invalidate();
	}
}

void FP4SearchModelViewportClient::SetPreviewMesh(UStaticMesh* Mesh)
{
	if (PreviewMeshComponent)
	{
		OwnedPreviewScene.RemoveComponent(PreviewMeshComponent);
		PreviewMeshComponent = nullptr;
	}

	if (!Mesh)
	{
		Invalidate();
		return;
	}

	AutoRotateYawDegrees = 0.0f;
	PreviewMeshComponent = NewObject<UStaticMeshComponent>();
	PreviewMeshComponent->SetStaticMesh(Mesh);
	OwnedPreviewScene.AddComponent(PreviewMeshComponent, FTransform::Identity);

	// FocusViewportOnBox re-centers the look-at on the mesh's bounding box center but also
	// turns orbit mode off as a side effect; re-pin the pivot to that same center (not the
	// world origin) so the next drag orbits around the middle of the object itself — e.g. a
	// character mesh with its feet at Z=0 orbits around its torso, not its feet.
	const FBox BoundingBox = Mesh->GetBoundingBox();
	FocusViewportOnBox(BoundingBox, true);
	SetLookAtLocation(BoundingBox.GetCenter(), false);
	Invalidate();
}
