                            Release Notes for
                       P4Search for Unreal Engine

                          Version 2026.4

Introduction

	P4Search for Unreal Engine is an Unreal Editor plugin that brings P4Search's
	full-text search and asset discovery directly into the Unreal Editor. It lets
	artists and designers search, preview, and pull assets from a Helix Core
	(Perforce) depot indexed by P4Search — without leaving the Editor, and without
	a local 'p4' CLI or Perforce credentials beyond an in-Editor login.

	Please send all feedback to support@perforce.com

--------------------------------------------------------------------------

Requirements

	* Unreal Engine 5.5-5.7.

	* Windows or macOS (this is an Editor-only plugin — it is never loaded in
	  packaged games).

	* A running P4Search server (search-service) to connect to.

Documentation

	See README.md (in this directory) for an overview of the plugin's features,
	project structure, and backend integration. Additional P4Search product
	documentation is available at https://www.perforce.com/products/p4search.

Installation

	See the "Installation" section of README.md.

Known Limitations

	* Ribbon hover-preview: the hover-preview filmstrip depends on a
	  search-service endpoint that not every P4Search server deployment has yet.
	  If your server predates this feature, hovering a result card simply shows
	  no filmstrip rather than failing.

--------------------------------------------------------------------------
Changes in 1.0 (unreleased)

	Initial feature set:

	* Keyword/tag search with filters for file extension, depot path, date
	  (relative presets or a calendar picker), file size range, and "head
	  revision only".
	* Result cards with full thumbnails and a hover-preview filmstrip for
	  animated/sequential assets.
	* "More Info" panel with full asset metadata, Perforce attributes, and an
	  embedded interactive 3D viewport for supported mesh formats.
	* Add to Project — download a depot asset straight into Content/P4Search/,
	  with automatic Content Browser registration.
	* Generate Thumbnail — render and save a thumbnail back to Perforce for
	  uasset files that don't have one yet.
	* Multi-select and bulk actions.
	* Drag-and-drop from a result card, triggering the same download flow as
	  "Add to Project".
	* In-Editor login panel, authenticating against a per-session JWT — no
	  local 'p4' CLI or stored Perforce credentials required.
