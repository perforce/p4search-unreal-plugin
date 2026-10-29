// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#include "SP4SearchModelViewport.h"
#include "SP4SearchModelViewportClient.h"

void SP4SearchModelViewport::Construct(const FArguments& InArgs)
{
	TypedViewportClient = MakeShared<FP4SearchModelViewportClient>();

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

void SP4SearchModelViewport::SetPreviewMesh(UStaticMesh* Mesh)
{
	if (TypedViewportClient.IsValid())
	{
		TypedViewportClient->SetPreviewMesh(Mesh);
	}
}

#if !(ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))
TSharedPtr<SWidget> SP4SearchModelViewport::MakeViewportToolbar()
{
	// No toolbar — this is a plain preview, not a full asset editor.
	return nullptr;
}
#endif

TSharedRef<FEditorViewportClient> SP4SearchModelViewport::MakeEditorViewportClient()
{
	return TypedViewportClient.ToSharedRef();
}

void SP4SearchModelViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}

void SP4SearchModelViewport::OnFocusViewportToSelection()
{
	// No selection support in this minimal preview — nothing to do.
}
