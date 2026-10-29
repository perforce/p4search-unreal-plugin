<a href="mailto:support@perforce.com" target="_blank" rel="noopener noreferrer"><img src="https://img.shields.io/badge/Support-Official-green.svg" alt="Support"></a>

# P4 Search for Unreal Engine

P4 Search for Unreal Engine is officially supported by Perforce. Pull requests will be managed by Perforce's engineering teams. We will do our best to acknowledge these in a timely manner based on available capacity. Issues will not be managed on GitHub. All issues should be recorded by <a href="https://portal.perforce.com/s/contactsupport" target="_blank" rel="noopener noreferrer">logging a case</a> via Perforce's standard support process.

## Overview
P4 Search for Unreal Engine is an Unreal Editor plugin that brings <a href="https://help.perforce.com/helix-core/integrations-plugins/p4search/current/Content/P4Search/Home-p4search.html" target="_blank" rel="noopener noreferrer">P4 Search</a>'s full-text search and asset discovery directly into the Unreal Editor. Search, preview, and pull assets from a P4 depot indexed by P4 Search: without leaving the Editor, and without a local `p4` CLI or Perforce credentials beyond an in-Editor login.

* **Search**: keyword/tag search with filters for file extension, depot path, date (relative presets or a calendar picker), file size range, and "head revision only".
* **Result cards**: include full thumbnails, with a hover-preview filmstrip for animated/sequential assets.
* **"More Info" panel**: full asset metadata, Perforce attributes (scene references, scene stats), and an embedded interactive 3D viewport for supported mesh formats.
* **Add to Project**: download a depot asset straight into `Content/P4Search/`, with automatic Content Browser registration.
* **Generate Thumbnail**: render and save a thumbnail back to Perforce for uasset files that don't have one yet (Blueprint graphs and static meshes are both supported).
* **Multi-select & bulk actions**: select multiple results and add them to the project in one action.
* **Drag-and-drop**: drag a result card out of the search tab to trigger the same download flow as "Add to Project".
* **Login-based auth**: in-Editor login panel.

## Requirements
* Unreal Engine 5.5-5.8.
* Windows or macOS (Editor-only plugin — never loaded in packaged games).
* A running <a href="https://help.perforce.com/helix-core/integrations-plugins/p4search/current/Content/P4Search/Home-p4search.html" target="_blank" rel="noopener noreferrer">P4 Search</a> server (`search-service`) to connect to.

## Documentation
### Project structure

    P4Search/
    ├── P4Search.uplugin              — plugin descriptor
    ├── Source/P4Search/
    │   ├── Public/                   — module + widget headers
    │   └── Private/
    │       ├── P4Search.cpp/.h       — module entry point; builds the entire search tab UI,
    │       │                          search execution, thumbnail/model fetches, login, bulk actions
    │       ├── SP4SearchResultCard.* — one result card (thumbnail, hover ribbon, multi-select,
    │       │                          drag source)
    │       ├── SP4SearchModelViewport.* /
    │       │   SP4SearchModelViewportClient.* — embedded 3D preview viewport
    │       ├── P4SearchDragDropOp.*  — drag-and-drop operation for dragging cards out of the tab
    │       ├── P4SearchSettings.*    — Editor Preferences page (Plugins > P4Search)
    │       └── P4SearchStyle.* / P4SearchCommands.* — Slate style set / UI command bindings
    ├── Content/, Resources/          — plugin icons and bundled content
    └── build.sh / build-ci.bat       — local dev / CI build scripts

### Backend integration
The plugin talks to a P4 Search server exclusively over HTTPS, there is no direct Perforce connection and no bundled `p4` CLI dependency. Login exchanges credentials for a per-session JWT (kept in memory only, never persisted), which is then sent as a bearer token on every subsequent request for search, thumbnails, previews, attributes, and asset downloads.

## Support
This project is maintained by Perforce Engineering and fully supported. Pull requests will be managed by Perforce's engineering teams. We will do our best to acknowledge these in a timely manner based on available capacity. Issues will not be managed on GitHub. All issues should be recorded by <a href="https://portal.perforce.com/s/contactsupport" target="_blank" rel="noopener noreferrer">logging a case</a> via Perforce's standard support process.

## Usage
### Installing
1. Find the "P4Search" listing on <a href="https://www.fab.com" target="_blank" rel="noopener noreferrer">Fab</a> and click **Add to My Library** (or, from within the Unreal Editor, open the **Fab** tab and search for "P4Search").
2. From your Fab library, click **Add to Project** and select the target Unreal project (5.5-5.8).
3. Launch the project's Editor. If the plugin isn't already enabled, go to **Edit > Plugins**, search for "P4Search", and enable it — the Editor will prompt to restart.
4. Open the **P4Search** tab (Window > P4Search) and log in with your Perforce credentials against a running <a href="https://help.perforce.com/helix-core/integrations-plugins/p4search/current/Content/P4Search/Home-p4search.html" target="_blank" rel="noopener noreferrer">P4 Search</a> server.

### Building from source
Local development builds (macOS):

    cd P4Search
    ./build.sh [unreal-engine-version]

`unreal-engine-version` defaults to `5.5` if omitted (e.g. `./build.sh 5.6`). This requires Unreal Engine installed at `/Users/Shared/Epic Games/UE_{VERSION}`. It runs UAT's `BuildPlugin` and installs the result into your local Editor's `Engine/Plugins/P4Search` for quick iteration.

Local development builds (Windows):

    cd P4Search
    build.bat [unreal-engine-version]

`unreal-engine-version` defaults to `5.5` if omitted, same as `build.sh`. This requires Unreal Engine installed at `C:\Program Files\Epic Games\UE_{VERSION}` and runs the same PREP/BUILD/PACKAGE/CLEANUP/STAMP VERSION/DEPLOY steps as `build.sh`, installing the result into your local Editor's `Engine\Plugins\P4Search`.
