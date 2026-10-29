// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace GLTF
{
	struct FNode;
}

// Pure, HTTP/Slate-free logic shared out of P4Search.cpp so it can be exercised by the
// automation tests in Private/Tests without standing up the Editor-only module's UI.
// These tests run in-Editor via Session Frontend > Automation; there is no standalone test
// runner or headless target for this plugin.
namespace P4SearchCore
{
	// Formats a byte count the way result cards/the "More Info" panel display file sizes.
	FString FormatFileSize(int64 Bytes);

	// Builds the DSL query string the /search/dsl endpoint expects from the tab's current
	// filter state. Ext should already be resolved to a real extension or "*".
	FString BuildSearchDslQuery(const FString& SearchDepotPath, const FString& Ext, const FString& SearchDetected,
		const FString& SearchDate, int32 SizeFromMB, int32 SizeToMB, bool bHeadOnly);

	// Sort combo labels, in display order. The first entry is the default and must remain
	// "Relevance" -- BuildSortParam maps it to an empty string, so the out-of-the-box search
	// keeps the server's relevance ordering.
	const TArray<FString>& GetSortOptionLabels();

	// Maps a sort combo label to the /search/dsl 'sort' query parameter value, e.g.
	// "Largest" -> "fileSize:desc". Returns empty for "Relevance" and for any unrecognized
	// label, so a stale persisted label falls back to relevance rather than producing a
	// malformed parameter the server would reject.
	FString BuildSortParam(const FString& Label);

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

	// Parses the /status endpoint's JSON body ({"data":[{"name":"license","status":"SUCCESS"},
	// ...]}) and returns whether the "license" entry reports "SUCCESS" (the server has a DAM
	// or Search capability — LicenseStatus on the backend treats those as equivalent). Fails
	// open (returns true) if the body can't be parsed or has no "license" entry, so a
	// malformed/unexpected status response never blocks thumbnails on its own.
	bool ExtractLicenseStatusOk(const FString& ResponseBody);

	// Host name portion of a URL, e.g. "https://host.example.com:1601/api" -> "host.example.com".
	// IPv6 literals keep their brackets ("[::1]"). Returns empty if there's no host.
	FString ExtractUrlHost(const FString& Url);

	// Builds the notification text for a failed /login attempt, so the user can tell a
	// connection/certificate problem apart from bad credentials or a server error.
	// bGotResponse is false when the request never completed (DNS, refused, TLS failure...);
	// ResponseCode is only meaningful when it's true.
	FText BuildLoginFailureMessage(const FString& ServerURL, bool bGotResponse, int32 ResponseCode, bool bTimedOut);

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
