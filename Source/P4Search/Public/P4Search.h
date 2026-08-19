// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "Animation/CurveSequence.h"
#include "Containers/Ticker.h"
#include "P4SearchHttp.h"

class FToolBarBuilder;
class FMenuBuilder;
class SWrapBox;
class SBox;
class SP4SearchResultCard;
class UTexture2D;
class SP4SearchModelViewport;
class SComboButton;
class SDockTab;
struct FKeyEvent;

class FP4SearchModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

	// Test-only backdoor — lets automation tests substitute a fake IP4SearchHttpRequester and
	// drive the module's otherwise-private HTTP-calling methods. Gated by WITH_AUTOMATION_TESTS
	// so it never compiles into a shipping build; see Tests/P4SearchModuleTestAccess.h.
#if WITH_AUTOMATION_TESTS
	friend class FP4SearchModuleTestAccess;
#endif

private:

	void RegisterMenus();

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	void ExecuteSearch();
	void ResetToDefaultSearch();

	// Auto-search on filter change. TriggerDebouncedSearch reschedules a single shared
	// one-shot ticker (typing) so only the last change in a burst fires a search;
	// TriggerImmediateSearch cancels any pending debounce and searches right away
	// (discrete selections: checkboxes, combo boxes, calendar/preset picks, Enter).
	// Both reset CurrentPage to 0, matching every other filter-changing action.
	void TriggerDebouncedSearch();
	void TriggerImmediateSearch();
	void FetchThumbnail(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard);
	void FetchRibbon(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard);

	// Decodes arbitrary compressed image bytes (auto-detected format) into a new transient,
	// GC-rooted UTexture2D — the common first half of FetchThumbnail/FetchRibbon/
	// FetchInfoPanelPreview, which otherwise differ in what they do with the result (brush
	// size, corner-rounding, which member/array owns it). DebugContext (e.g. "FetchThumbnail")
	// is only used to prefix log messages, so a failure is still traceable to its caller.
	// Returns nullptr, having already logged the specific failure, if decoding fails.
	UTexture2D* DecodeImageToTexture(const TArray<uint8>& ImageData, const FString& DebugContext, const FString& DepotFile);

	// Fetches the whitelisted "dam_scene_refs" P4 attribute (JSON array of scene
	// references — not part of the search hit's own document body) and updates
	// SceneRefsDisplayText, which the "More Info" panel's Scene References row
	// reads reactively.
	void FetchSceneRefs(const FString& DepotFile, int32 Rev);

	// Same pattern as FetchSceneRefs, for the whitelisted "dam_scene_stats" P4
	// attribute — a JSON object matching search-service's SceneStatsModel
	// (edges/faces/triangles/vertices/animations/armatures/materials/meshes/
	// textures/uv_maps). Updates SceneStatsDisplayText.
	void FetchSceneStats(const FString& DepotFile, int32 Rev);
	void ClearResults();
	void AddToProject(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard);
	void GenerateThumbnail(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard);
	void ShowAssetInfoPanel(TWeakPtr<SP4SearchResultCard> WeakCard, const FString& FileName, const FString& DepotFile,
		const FString& DetectedTags, int32 Rev, int32 Change, const FString& FileSize, bool bIsHead);
	void HideAssetInfoPanel();
	void FetchModelPreview(const FString& DepotFile, int32 Rev);

	// 2D fallback preview for the "More Info" panel when the asset has no "model" attribute
	// — see InfoPanelPreviewBrush's comment.
	void FetchInfoPanelPreview(const FString& DepotFile, TWeakPtr<SP4SearchResultCard> WeakCard);

	void ToggleSettingsPanel();
	void HideSettingsPanel();

	// Escape closes whichever of the More Info / Settings panels is open, but only while
	// this plugin's tab is the foreground tab — registered application-wide since plain
	// Slate key events only route through whatever widget currently holds keyboard focus,
	// which isn't reliably anything inside this tab (search boxes/buttons don't hold focus
	// after use).
	void OnGlobalKeyDown(const FKeyEvent& KeyEvent);

	// Tears down CurrentModelViewport (an SEditorViewport) while it's still safe to — bound to
	// FCoreDelegates::OnEnginePreExit, which fires right at the start of engine exit, before
	// FSlateApplication::Shutdown() and long before FModuleManager::UnloadModulesAtShutdown()
	// (where ShutdownModule() runs). Leaving the viewport to destruct implicitly, as part of
	// FP4SearchModule's own destructor at that much later point, crashes inside
	// FEditorModeTools::ExitAllModesPendingDeactivate() (confirmed via a real crash log) —
	// by then GEditor's own teardown has already invalidated whatever it depends on.
	void OnEnginePreExit();

	// Date filter dropdown (hardcoded relative-date presets + calendar day-picker).
	TSharedRef<class SWidget> BuildDateDropdownContent();
	TSharedRef<class SWidget> BuildCalendarGrid();
	void ShiftCalendarMonth(int32 Delta);

	// Recent-searches dropdown next to the main search box.
	TSharedRef<class SWidget> BuildSearchHistoryDropdownContent();

	void PerformLogin(const FString& Username, const FString& Password);
	void LogOut();

	// Call at every "did the request succeed" check across the module. Returns true (and
	// logs the user out, showing the login panel + a notification) if ResponseCode is 401 —
	// callers should treat true as "handled, stop here" and skip their own generic failure
	// handling. Returns false for any other code, leaving the caller's existing logic as-is.
	bool HandleAuthFailure(int32 ResponseCode);

	void BulkAddSelectedToProject();
	void ClearMultiSelection();
	int32 CountMultiSelected() const;

private:
	// Set in OnSpawnPluginTab, read by OnGlobalKeyDown to scope Escape-to-close to when this
	// tab is actually the one on screen.
	TWeakPtr<SDockTab> MyTab;

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<SWrapBox> ResultsContainer;
	TSharedPtr<SBox>     NoResultsPanel;
	TSharedPtr<SBox>     InitialStatePanel;
	TArray<TSharedPtr<FSlateDynamicImageBrush>> ThumbnailBrushes;
	TArray<UTexture2D*> ThumbnailTextures;

	TSharedPtr<FSlateDynamicImageBrush> BackgroundBrush;
	UTexture2D* BackgroundTexture = nullptr;

	int32 SearchSize = 20;
	int32 CurrentPage = 0;
	int32 TotalResults = 0;

	// Incremented each time ExecuteSearch actually fires a request; its completion
	// handler captures the value at fire time and drops the response if a newer
	// search has since started, so two searches in flight can't interleave results.
	int32 SearchGeneration = 0;

	TArray<TSharedPtr<FString>> ExtensionOptions;
	TSharedPtr<FString> SelectedExtension;

	TArray<TSharedPtr<FString>> PageSizeOptions;
	TSharedPtr<FString> SelectedPageSize;

	FString SearchDepotPath;

	// Date filter — free-text entry (e.g. "pastWeek") plus an auxiliary dropdown
	// offering hardcoded relative-date presets and a calendar day-picker.
	// CalendarView{Year,Month} track the month currently displayed in the calendar
	// grid, independently of whatever date (if any) is currently selected.
	FString SearchDate;
	TSharedPtr<SComboButton> DateComboButton;
	TSharedPtr<SBox> CalendarGridBox;
	int32 CalendarViewYear = 0;
	int32 CalendarViewMonth = 0; // 1-12, 0 = not yet initialized

	// Size filter — split from a single free-text field into From/To integer
	// bounds, unconditionally treated as megabytes (query-building appends "MB").
	TOptional<int32> SearchFileSizeFromMB;
	TOptional<int32> SearchFileSizeToMB;

	bool bHeadOnly = true;
	FString SearchDetected;
	FTSTicker::FDelegateHandle SearchDebounceTickerHandle;

	// Most-recent-first, deduplicated, capped at 10 — persisted across Editor sessions
	// the same way ServerURL/SearchDepotPath/SearchSize are.
	TArray<FString> SearchHistory;
	TSharedPtr<SComboButton> SearchHistoryComboButton;

	// One-shot post-download tickers scheduled by AddToProject/GenerateThumbnail. Tracked so
	// ShutdownModule can cancel any still pending — their lambdas capture `this`, which would
	// otherwise be a dangling pointer if the module is unloaded before they fire.
	TArray<FTSTicker::FDelegateHandle> PendingAssetTickers;

	// All cards from the current search — used for bulk multi-select actions.
	TArray<TWeakPtr<SP4SearchResultCard>> AllCards;

	struct FMultiSelectedAsset
	{
		FString DepotFile;
		int32 Rev = 0;
	};

	// Keyed by "DepotFile#Rev", maintained by each card's OnMultiSelectChanged callback.
	// Deliberately NOT cleared by ClearResults()/a new search — cards are destroyed and
	// recreated every page, so tracking selection by key (rather than by card pointer) is
	// what lets a multi-select survive paging Next/Prev. Only ClearMultiSelection() (the
	// bulk bar's "Clear" button) empties it.
	TMap<FString, FMultiSelectedAsset> MultiSelectedAssets;

	FEditableTextBoxStyle SearchBoxStyle;
	FEditableTextBoxStyle FilterBoxStyle;
	FButtonStyle          SearchButtonStyle;
	FButtonStyle          IconOnlyButtonStyle;  // transparent, for the top-bar refresh/settings icons
	FButtonStyle          GearButtonStyle;      // transparent normal, visible hover/press — the floating settings gear needs to read as clickable on its own, away from any toolbar context
	FComboBoxStyle        FilterComboStyle;

	// "Latest" toggle switch — a plain SCheckBox with every native brush hidden
	// (FSlateNoResource), whose .Content() is a hand-drawn pill track + sliding
	// thumb instead, since Slate has no built-in toggle-switch widget.
	FCheckBoxStyle ToggleCheckBoxStyle;
	TUniquePtr<FSlateRoundedBoxBrush> ToggleTrackBrush;
	TUniquePtr<FSlateRoundedBoxBrush> ToggleThumbBrush;

	// Flat-fill + thin outline brush for the "More Info"/"Settings" slide-in panels — gives
	// them a visible edge against the results grid instead of relying on background color
	// contrast alone to read as a separate, elevated surface.
	TUniquePtr<FSlateRoundedBoxBrush> PanelElevationBrush;

	// Sidebar file-type row's selected-state pill — baked-in tint is white so
	// BorderBackgroundColor (Select when chosen, Transparent otherwise) is the only thing
	// that actually drives visibility, matching the SP4SearchResultCard outer-ring pattern.
	TUniquePtr<FSlateRoundedBoxBrush> FileTypeRowBrush;

	// Every HTTP call in this module goes through this seam rather than calling
	// FHttpModule::Get().CreateRequest() directly — see P4SearchHttp.h. Defaults to the real,
	// network-backed requester; automation tests swap it via FP4SearchModuleTestAccess.
	TSharedRef<IP4SearchHttpRequester> HttpRequester = MakeShared<FP4SearchRealHttpRequester>();

	FString ServerURL;
	static const FString DefaultServerURL;

	FString CachedJWT;      // in-memory only, never persisted — re-login is required each Editor session
	FString CachedNonce;    // CSRF nonce issued by v1.4 login, required by v1.4 endpoints (e.g. view/asset)
	FString LoginUsername;
	FString LoginPassword;  // in-memory only, cleared immediately after each login attempt
	bool bIsLoggedIn = false;

	bool bIsSearching = false;
	bool bSearchFailed = false;

	// Set alongside bSearchFailed — distinguishes "never got a response" (connection/DNS/
	// timeout) from "got a response, but it was an error" (bad query, server error, etc.), so
	// the failure panel doesn't tell the user to "check connection" when the server is
	// perfectly reachable and just rejected the request. A successful 200 with zero hits is
	// NOT a failure at all — that's NoResultsPanel, a separate path that never touches this.
	FString SearchFailureMessage;

	// "More Info" panel — docked to the right edge of the tab, slides open/closed
	// (width animated via InfoPanelCurve) rather than a separate floating window.
	TSharedPtr<SBox> InfoPanelContainer;
	TSharedPtr<SBox> InfoPanelContent;
	FCurveSequence   InfoPanelAnim;
	FCurveHandle     InfoPanelCurve;
	static constexpr float InfoPanelWidth = 640.0f;
	TWeakPtr<SP4SearchResultCard> SelectedResultCard;  // highlighted while its info is shown

	// "Settings" panel — same docked-right slide-in pattern as the "More Info" panel
	// above, triggered by the gear icon floating in the tab's top-right corner.
	TSharedPtr<SBox> SettingsPanelContainer;
	FCurveSequence   SettingsPanelAnim;
	FCurveHandle     SettingsPanelCurve;
	static constexpr float SettingsPanelWidth = 320.0f;

	// Embedded 3D preview shown at the top of the "More Info" panel.
	TSharedPtr<SP4SearchModelViewport> CurrentModelViewport;

	enum class EModelPreviewStatus : uint8
	{
		Loading,  // fetch in flight — show a spinner/progress bar
		Found,    // mesh loaded — show CurrentModelViewport
		NotFound, // confirmed no "model" attribute (or the fetch failed) — show a message
	};
	EModelPreviewStatus ModelPreviewStatus = EModelPreviewStatus::NotFound;

	// User's manual override for assets that DO have a 3D model — lets them switch back to
	// the 2D preview image instead of the viewport. Reset to false (3D wins by default) each
	// time a different asset's "More Info" panel opens; see ShowAssetInfoPanel.
	bool bShowPreviewInsteadOfModel = false;

	// Download progress for the current model fetch, updated from OnRequestProgress64.
	// ModelPreviewContentLength is 0 until the server reports Content-Length (chunked/unknown
	// responses never populate it, in which case the progress bar shows an indeterminate marquee).
	uint64 ModelPreviewBytesReceived = 0;
	uint64 ModelPreviewContentLength = 0;

	// Read reactively by the "More Info" panel's Scene References row (see MakeInfoRow's
	// TAttribute usage) — set to "Loading..." when the panel opens, then overwritten once
	// FetchSceneRefs's async request resolves.
	FString SceneRefsDisplayText;

	// Same pattern, for the Scene Stats row / FetchSceneStats.
	FString SceneStatsDisplayText;

	// 2D fallback shown in the "More Info" panel's viewport area for assets with no "model"
	// attribute (EModelPreviewStatus::NotFound) — a dedicated fetch/texture independent of the
	// result cards' own thumbnails, since ClearResults() (a new search) tears those down but
	// the panel can still be open showing an asset from a previous search.
	UTexture2D* InfoPanelPreviewTexture = nullptr;
	TSharedPtr<FSlateDynamicImageBrush> InfoPanelPreviewBrush;
};
