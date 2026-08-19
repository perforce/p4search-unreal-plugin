// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace GLTF
{
	struct FNode;
}

// Pure, HTTP/Slate-free logic shared out of P4Search.cpp so it can be exercised by a standalone
// test executable (see the P4SearchCoreTests Program target) for real code coverage measurement,
// which isn't possible for logic living directly in an Editor-only module.
namespace P4SearchCore
{
	// Formats a byte count the way result cards/the "More Info" panel display file sizes.
	FString FormatFileSize(int64 Bytes);

	// Builds the DSL query string the /search/dsl endpoint expects from the tab's current
	// filter state. Ext should already be resolved to a real extension or "*".
	FString BuildSearchDslQuery(const FString& SearchDepotPath, const FString& Ext, const FString& SearchDetected,
		const FString& SearchDate, int32 SizeFromMB, int32 SizeToMB, bool bHeadOnly);

	// One parsed search-hit's display-ready fields, extracted from a /search/dsl hit's
	// "_source" object.
	struct FSearchHit
	{
		FString DepotFile;
		FString FileName;
		int32 Rev = 0;
		int32 Change = 0;
		int64 FileSizeBytes = 0;
		FString FileSizeStr;
		bool bIsHead = true;
		FString DetectedTagsStr;
	};

	FSearchHit ParseSearchHit(const TSharedRef<FJsonObject>& Source);

	// Total-hit count from a /search/dsl response's "hits.total.value". Returns 0 if any part
	// of that path is missing.
	int32 ExtractTotalResults(const TSharedRef<FJsonObject>& JsonObject);

	// Pulls the {"status":{"message":...}} envelope's message out of an error response body —
	// empty string if the body isn't JSON or doesn't have that shape.
	FString ExtractJsonStatusMessage(const FString& ResponseBody);

	// One hover-preview filmstrip frame's top-left corner within the sprite sheet, in texture
	// pixels — from the dam_ribbon_json attribute's "sprites" array.
	struct FRibbonFrameRect
	{
		int32 X = 0;
		int32 Y = 0;
	};

	struct FRibbonMeta
	{
		int32 SpriteWidth = 0;
		int32 SpriteHeight = 0;
		TArray<FRibbonFrameRect> Frames;
	};

	// Parses the dam_ribbon_json attribute's meta JSON (spriteWidth/spriteHeight/sprites[])
	// into frame rects. Returns an unset TOptional if spriteWidth/Height aren't positive,
	// "sprites" is missing/empty, or every sprite entry turned out invalid.
	TOptional<FRibbonMeta> ParseRibbonMeta(const TSharedRef<FJsonObject>& MetaJson);

	// Computes each frame's exact texture-space UV rect within the decoded filmstrip image.
	TArray<FBox2f> ComputeRibbonFrameUVs(const TArray<FRibbonFrameRect>& Frames, int32 SpriteWidth,
		int32 SpriteHeight, int32 TextureWidth, int32 TextureHeight);

	// Formats each dam_scene_refs entry into a display string — leniently, since the exact
	// per-element shape isn't documented beyond "JSON array" (see docs/API.md): a plain string
	// is used directly, an object tries a few likely key names, anything else falls back to
	// AsString(). Returns "None" if the array is empty or every entry was blank.
	FString FormatSceneRefs(const TArray<TSharedPtr<FJsonValue>>& RefsArray);

	// Formats the dam_scene_stats attribute's known numeric fields into a display string — only
	// non-zero fields are included, in a fixed display order. Returns "None" if every field was
	// zero/absent.
	FString FormatSceneStats(const TSharedRef<FJsonObject>& StatsJson);

	// Parses the v1.4 login endpoint's JSON body ({"data":{"accessToken":"...", ...}}). The
	// nonce isn't part of this since it arrives as a response header, not the body.
	FString ExtractLoginAccessToken(const FString& ResponseBody);

	// Carries month over/under into the year as needed after adding a delta.
	void NormalizeYearMonth(int32& Year, int32& Month);

	// One day-of-month's position in a Monday-first, 7-column calendar grid.
	struct FCalendarDayCell
	{
		int32 Day = 0;
		int32 Row = 0;
		int32 Col = 0;
	};

	// Computes every day-of-month's (row, col) grid position.
	TArray<FCalendarDayCell> ComputeCalendarDayCells(int32 Year, int32 Month);

	// Finds the node referencing TargetMeshIndex and returns its accumulated (parent-chain-
	// composed) global transform. Returns Identity if no node references that mesh index.
	FTransform ComputeMeshNodeGlobalTransform(const TArray<GLTF::FNode>& Nodes, int32 TargetMeshIndex);
}
