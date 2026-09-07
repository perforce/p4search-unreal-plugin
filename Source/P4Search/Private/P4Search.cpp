// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4Search.h"
#include "P4SearchCore.h"
#include "P4SearchStyle.h"
#include "P4SearchCommands.h"
#include "P4SearchSettings.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "SP4SearchResultCard.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Misc/DateTime.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectBase.h"
#include "Misc/AutomationTest.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformFileManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "ToolMenus.h"
#include "Interfaces/IHttpRequest.h"
#include "SP4SearchAnimatedButton.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "SP4SearchModelViewport.h"
#include "GLTFReader.h"
#include "GLTFAsset.h"
#include "GLTFMeshFactory.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "StaticMeshOperations.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Styling/StyleColors.h"
#include "ObjectTools.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Base64.h"
#include "SGraphPanel.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/MeshComponent.h"

static const FName P4SearchTabName("P4Search");
const FString FP4SearchModule::DefaultServerURL = TEXT("https://test-p4search.das.perforce.com:1601");

#define LOCTEXT_NAMESPACE "FP4SearchModule"

namespace
{
	// Minimal BlurHash decoder (https://blurha.sh) — the search index stores each asset's
	// "blur" hash inline on the document, so decoding it client-side gives an instant
	// placeholder thumbnail with no network round trip, before the real thumbnail arrives.
	const FString Base83Alphabet = TEXT("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#$%*+,-.:;=?@[]^_{|}~");

	int32 Base83Decode(const FString& Str)
	{
		int32 Value = 0;
		for (int32 Idx = 0; Idx < Str.Len(); ++Idx)
		{
			const int32 CharIndex = Base83Alphabet.Find(Str.Mid(Idx, 1), ESearchCase::CaseSensitive);
			if (CharIndex == INDEX_NONE)
			{
				return -1;
			}
			Value = Value * 83 + CharIndex;
		}
		return Value;
	}

	float SRGBToLinear(float Value255)
	{
		const float Value = Value255 / 255.0f;
		return (Value <= 0.04045f) ? (Value / 12.92f) : FMath::Pow((Value + 0.055f) / 1.055f, 2.4f);
	}

	uint8 LinearToSRGB(float Value)
	{
		Value = FMath::Clamp(Value, 0.0f, 1.0f);
		const float S = (Value <= 0.0031308f) ? (Value * 12.92f) : (1.055f * FMath::Pow(Value, 1.0f / 2.4f) - 0.055f);
		return (uint8)FMath::Clamp(FMath::RoundToInt(S * 255.0f), 0, 255);
	}

	float SignedPow(float Value, float Exp)
	{
		return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), Exp);
	}

	FVector DecodeDC(int32 Value)
	{
		return FVector(
			SRGBToLinear((float)((Value >> 16) & 255)),
			SRGBToLinear((float)((Value >> 8) & 255)),
			SRGBToLinear((float)(Value & 255)));
	}

	FVector DecodeAC(int32 Value, float MaximumValue)
	{
		const int32 QuantR = Value / (19 * 19);
		const int32 QuantG = (Value / 19) % 19;
		const int32 QuantB = Value % 19;
		return FVector(
			SignedPow((QuantR - 9) / 9.0f, 2.0f) * MaximumValue,
			SignedPow((QuantG - 9) / 9.0f, 2.0f) * MaximumValue,
			SignedPow((QuantB - 9) / 9.0f, 2.0f) * MaximumValue);
	}

	// Decodes into BGRA8 pixels (matching PF_B8G8R8A8, ready for UTexture2D::CreateTransient).
	bool DecodeBlurHash(const FString& Hash, int32 Width, int32 Height, TArray<uint8>& OutBGRA)
	{
		if (Hash.Len() < 6 || Width <= 0 || Height <= 0)
		{
			return false;
		}

		const int32 SizeFlag = Base83Decode(Hash.Mid(0, 1));
		const int32 QuantMax = Base83Decode(Hash.Mid(1, 1));
		if (SizeFlag < 0 || QuantMax < 0)
		{
			return false;
		}

		const int32 NumX = (SizeFlag % 9) + 1;
		const int32 NumY = (SizeFlag / 9) + 1;
		if (Hash.Len() != 4 + 2 * NumX * NumY)
		{
			return false;
		}

		const float MaximumValue = (QuantMax + 1) / 166.0f;

		TArray<FVector> Colors;
		Colors.SetNum(NumX * NumY);

		const int32 DCValue = Base83Decode(Hash.Mid(2, 4));
		if (DCValue < 0)
		{
			return false;
		}
		Colors[0] = DecodeDC(DCValue);

		for (int32 i = 1; i < NumX * NumY; ++i)
		{
			const int32 ACValue = Base83Decode(Hash.Mid(4 + i * 2, 2));
			if (ACValue < 0)
			{
				return false;
			}
			Colors[i] = DecodeAC(ACValue, MaximumValue);
		}

		TArray<float> CosX;
		CosX.SetNumUninitialized(Width * NumX);
		for (int32 x = 0; x < Width; ++x)
		{
			for (int32 i = 0; i < NumX; ++i)
			{
				CosX[x * NumX + i] = FMath::Cos(PI * x * i / (float)Width);
			}
		}

		TArray<float> CosY;
		CosY.SetNumUninitialized(Height * NumY);
		for (int32 y = 0; y < Height; ++y)
		{
			for (int32 j = 0; j < NumY; ++j)
			{
				CosY[y * NumY + j] = FMath::Cos(PI * y * j / (float)Height);
			}
		}

		OutBGRA.SetNumUninitialized(Width * Height * 4);
		for (int32 y = 0; y < Height; ++y)
		{
			for (int32 x = 0; x < Width; ++x)
			{
				FVector Pixel(0.0f, 0.0f, 0.0f);
				for (int32 j = 0; j < NumY; ++j)
				{
					for (int32 i = 0; i < NumX; ++i)
					{
						Pixel += Colors[i + j * NumX] * (CosX[x * NumX + i] * CosY[y * NumY + j]);
					}
				}

				const int32 PixelIndex = (y * Width + x) * 4;
				OutBGRA[PixelIndex + 0] = LinearToSRGB((float)Pixel.Z); // B
				OutBGRA[PixelIndex + 1] = LinearToSRGB((float)Pixel.Y); // G
				OutBGRA[PixelIndex + 2] = LinearToSRGB((float)Pixel.X); // R
				OutBGRA[PixelIndex + 3] = 255;
			}
		}

		return true;
	}

	// Rounds a freshly-constructed, per-card thumbnail brush's top corners to match the
	// card's own rounded frame (SP4SearchResultCard::CardCornerRadius) — otherwise the
	// brush's sharp rectangular corners sit flush against the card's rounded top edge and
	// visually cover its curve, while the bottom (nothing sits flush there) stays rounded.
	// Must NOT be called on shared/global style brushes (e.g. FCoreStyle "GenericWhiteBox")
	// since it mutates the brush in place — only ever call this on a brush this module just
	// constructed for one specific card.
	void RoundThumbnailBrushTopCorners(FSlateBrush* Brush)
	{
		if (!Brush)
		{
			return;
		}
		Brush->DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush->OutlineSettings = FSlateBrushOutlineSettings(
			FVector4(SP4SearchResultCard::CardCornerRadius, SP4SearchResultCard::CardCornerRadius, 0.0f, 0.0f));
	}


	// Decodes a glTF image's bundled bytes into a transient UTexture2D. Only handles images
	// embedded directly in the GLB (Image.Data/DataByteLength populated) — a glTF that
	// references an external file/URI instead has no Data here, and this deliberately
	// returns nullptr for that case rather than trying to resolve/fetch it.
	UTexture2D* DecodeGltfImageToTexture(const GLTF::FImage& Image)
	{
		if (!Image.Data || Image.DataByteLength == 0)
		{
			return nullptr;
		}

		const EImageFormat Format = (Image.Format == GLTF::FImage::EFormat::JPEG)
			? EImageFormat::JPEG : EImageFormat::PNG;

		IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(Format);
		if (!IW.IsValid() || !IW->SetCompressed(Image.Data, Image.DataByteLength))
		{
			return nullptr;
		}

		TArray<uint8> BGRA;
		if (!IW->GetRaw(ERGBFormat::BGRA, 8, BGRA))
		{
			return nullptr;
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(IW->GetWidth(), IW->GetHeight(), PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		uint8* MipData = static_cast<uint8*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		FMemory::Memcpy(MipData, BGRA.GetData(), BGRA.Num());
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}


	// Builds a minimal, transient material that just samples the given texture into BaseColor —
	// no attempt at full PBR fidelity (metallic/roughness/normal maps), just enough to show the
	// asset's actual base color texture instead of the flat engine default grey.
	UMaterialInterface* BuildBaseColorTextureMaterial(UTexture2D* Texture)
	{
		if (!Texture)
		{
			return nullptr;
		}

		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->MaterialDomain = MD_Surface;

		UMaterialExpressionTextureSample* TexExpr = Cast<UMaterialExpressionTextureSample>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSample::StaticClass()));
		if (!TexExpr)
		{
			return nullptr;
		}
		TexExpr->Texture = Texture;
		TexExpr->SamplerType = SAMPLERTYPE_Color;

		UMaterialEditingLibrary::ConnectMaterialProperty(TexExpr, TEXT(""), EMaterialProperty::MP_BaseColor);

		// Deliberately NOT UMaterialEditingLibrary::RecompileMaterial() — its last step,
		// FMaterialEditorUtilities::BuildTextureStreamingData(), assumes a real persistent
		// material asset (it computes per-instance streaming distances across the level) and
		// crashes (SIGSEGV) on a transient, GetTransientPackage()-parented material like this
		// one. PreEditChange/PostEditChange alone is what actually triggers recompilation —
		// that's all RecompileMaterial does before the streaming-data step that crashes.
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		return Material;
	}
}

void FP4SearchModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Added once here, not in OnSpawnPluginTab — that can run again each time the tab is reopened.
	InfoPanelCurve = InfoPanelAnim.AddCurve(0.0f, 0.25f, ECurveEaseFunction::CubicOut);
	SettingsPanelCurve = SettingsPanelAnim.AddCurve(0.0f, 0.25f, ECurveEaseFunction::CubicOut);

	FSlateApplication::Get().OnApplicationPreInputKeyDownListener().AddRaw(this, &FP4SearchModule::OnGlobalKeyDown);
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FP4SearchModule::OnEnginePreExit);

	for (const TCHAR* Ext : { TEXT("*"), TEXT("FBX"), TEXT("OBJ"), TEXT("GLB"), TEXT("USD"), TEXT("ABC"), TEXT("UASSET"), TEXT("JPEG"), TEXT("PNG"), TEXT("BMP") })
	{
		ExtensionOptions.Add(MakeShareable(new FString(Ext)));
	}
	SelectedExtension = ExtensionOptions[1]; // "FBX" -- default filter, not "*" (All Assets)

	// Sentinel values recognized by P4SearchCore::BuildSearchDslQuery, which expands each to
	// every extension in that category -- see its own comment for the sync-with-this-file note.
	AllThreeDOption = MakeShareable(new FString(TEXT("ALL3D")));
	AllTwoDOption   = MakeShareable(new FString(TEXT("ALL2D")));

	for (const TCHAR* Size : { TEXT("10"), TEXT("20"), TEXT("50"), TEXT("100") })
	{
		PageSizeOptions.Add(MakeShareable(new FString(Size)));
	}
	SelectedPageSize = PageSizeOptions[1]; // "20", matches the default SearchSize

	// Rounded search-box style
	const FSlateColor SearchBg       = FStyleColors::Input;
	const FSlateColor SearchBgActive = FStyleColors::Hover;
	SearchBoxStyle = FEditableTextBoxStyle()
		.SetBackgroundImageNormal  (FSlateRoundedBoxBrush(SearchBg,       FP4SearchStyle::CornerRadius))
		.SetBackgroundImageHovered (FSlateRoundedBoxBrush(SearchBgActive, FP4SearchStyle::CornerRadius))
		.SetBackgroundImageFocused (FSlateRoundedBoxBrush(SearchBgActive, FP4SearchStyle::CornerRadius))
		.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(SearchBg,       FP4SearchStyle::CornerRadius))
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 13))
		.SetPadding(FMargin(16.0f, 12.0f))
		.SetForegroundColor(FStyleColors::Foreground);

	// ── Filter controls style (same rounding, slightly smaller) ───────────
	const FSlateColor FilterBg       = FStyleColors::Input;
	const FSlateColor FilterBgActive = FStyleColors::Hover;
	FilterBoxStyle = FEditableTextBoxStyle()
		.SetBackgroundImageNormal  (FSlateRoundedBoxBrush(FilterBg,       FP4SearchStyle::CornerRadius))
		.SetBackgroundImageHovered (FSlateRoundedBoxBrush(FilterBgActive, FP4SearchStyle::CornerRadius))
		.SetBackgroundImageFocused (FSlateRoundedBoxBrush(FilterBgActive, FP4SearchStyle::CornerRadius))
		.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FilterBg,       FP4SearchStyle::CornerRadius))
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.SetPadding(FMargin(12.0f, 8.0f))
		.SetForegroundColor(FStyleColors::Foreground);

	// ── Search button style (Editor's own accent colour) ────────────────────
	const FSlateColor BtnNormal  = FStyleColors::Primary;
	const FSlateColor BtnHovered = FStyleColors::PrimaryHover;
	const FSlateColor BtnPressed = FStyleColors::PrimaryPress;
	SearchButtonStyle = FButtonStyle()
		.SetNormal  (FSlateRoundedBoxBrush(BtnNormal,                              FP4SearchStyle::CornerRadius))
		.SetHovered (FSlateRoundedBoxBrush(BtnHovered,                             FP4SearchStyle::CornerRadius))
		.SetPressed (FSlateRoundedBoxBrush(BtnPressed,                             FP4SearchStyle::CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(FStyleColors::Recessed, FP4SearchStyle::CornerRadius))
		.SetNormalPadding (FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	// ── Transparent icon-only button style (top-bar refresh/settings) ─────
	IconOnlyButtonStyle = FButtonStyle()
		.SetNormal  (FSlateNoResource())
		.SetHovered (FSlateNoResource())
		.SetPressed (FSlateNoResource())
		.SetDisabled(FSlateNoResource())
		.SetNormalPadding (FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	// ── Floating settings gear — transparent at rest, but a visible rounded highlight on
	// hover/press so it reads as a clickable button rather than a static icon sitting in
	// the corner of the tab.
	// A permanent (not just hover) subtle backing — the gear glyph's ring + 50%-opacity inner
	// circle blend ambiguously into the toolbar's busy hex-pattern background without one,
	// reading as a smudge/crescent rather than a clean gear even before any hover feedback.
	GearButtonStyle = FButtonStyle()
		.SetNormal  (FSlateRoundedBoxBrush(FStyleColors::Recessed, FP4SearchStyle::CornerRadius))
		.SetHovered (FSlateRoundedBoxBrush(FStyleColors::Hover,    FP4SearchStyle::CornerRadius))
		.SetPressed (FSlateRoundedBoxBrush(FStyleColors::Hover2,   FP4SearchStyle::CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(FStyleColors::Recessed, FP4SearchStyle::CornerRadius))
		.SetNormalPadding (FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	// ── Ext combo box style (rounded, inherits engine defaults for arrow etc.) ─
	FilterComboStyle = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox"));
	FilterComboStyle.ComboButtonStyle.ButtonStyle
		.SetNormal  (FSlateRoundedBoxBrush(FilterBg,                               FP4SearchStyle::CornerRadius))
		.SetHovered (FSlateRoundedBoxBrush(FilterBgActive,                         FP4SearchStyle::CornerRadius))
		.SetPressed (FSlateRoundedBoxBrush(FStyleColors::Hover2,   FP4SearchStyle::CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(FStyleColors::Recessed, FP4SearchStyle::CornerRadius))
		.SetNormalPadding (FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));

	// ── "Latest" toggle switch — pill track + circular thumb, both drawn white so
	// SBorder::BorderBackgroundColor can freely retint them per state. Deliberately NOT
	// FP4SearchStyle::CornerRadius: a toggle's track/thumb radius must stay exactly half their
	// own height/diameter to read as a pill and a circle — unifying them with the flatter
	// 10px card/panel radius would visibly break this shape, not modernize it.
	ToggleTrackBrush = MakeUnique<FSlateRoundedBoxBrush>(FStyleColors::White, 9.0f);
	ToggleThumbBrush = MakeUnique<FSlateRoundedBoxBrush>(FStyleColors::White, 7.0f);

	// ── Sidebar file-type row selected-state pill ──
	FileTypeRowBrush = MakeUnique<FSlateRoundedBoxBrush>(FStyleColors::White, FP4SearchStyle::CornerRadius);
	ToggleCheckBoxStyle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::CheckBox)
		.SetUncheckedImage       (FSlateNoResource())
		.SetUncheckedHoveredImage(FSlateNoResource())
		.SetUncheckedPressedImage(FSlateNoResource())
		.SetCheckedImage         (FSlateNoResource())
		.SetCheckedHoveredImage  (FSlateNoResource())
		.SetCheckedPressedImage  (FSlateNoResource())
		.SetPadding(FMargin(0.0f));

	FP4SearchStyle::Initialize();
	FP4SearchStyle::ReloadTextures();

	// Helper: load a PNG from the plugin's Resources folder into a texture + brush.
	// Re-uses the same manual-decode path as thumbnail loading.
	auto LoadPluginPNG = [](const FString& RelPath, const FName& BrushName,
	                        ESlateBrushTileType::Type Tiling,
	                        UTexture2D*& OutTex) -> TSharedPtr<FSlateDynamicImageBrush>
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("P4Search"));
		if (!Plugin.IsValid()) return nullptr;

		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *(Plugin->GetBaseDir() / RelPath)))
			return nullptr;

		IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::PNG);
		if (!IW.IsValid() || !IW->SetCompressed(FileData.GetData(), FileData.Num()))
			return nullptr;

		TArray<uint8> BGRA;
		if (!IW->GetRaw(ERGBFormat::BGRA, 8, BGRA)) return nullptr;

		const int32 W = IW->GetWidth(), H = IW->GetHeight();
		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex) return nullptr;

		uint8* Mip = static_cast<uint8*>(Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		FMemory::Memcpy(Mip, BGRA.GetData(), BGRA.Num());
		Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
		Tex->UpdateResource();
		Tex->AddToRoot();
		OutTex = Tex;
		return MakeShareable(new FSlateDynamicImageBrush(
			Tex, FVector2D((float)W, (float)H), BrushName,
			FLinearColor::White, Tiling));
	};

	BackgroundBrush = LoadPluginPNG(TEXT("Resources/login-helix-bg.png"),
	                                FName(TEXT("P4Search.Background")),
	                                ESlateBrushTileType::Both, BackgroundTexture);

	FP4SearchCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FP4SearchCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FP4SearchModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FP4SearchModule::RegisterMenus));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(P4SearchTabName, FOnSpawnTab::CreateRaw(this, &FP4SearchModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FP4SearchTabTitle", "P4Search"))
		.SetIcon(FSlateIcon(FP4SearchStyle::GetStyleSetName(), "P4Search.TabIcon"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// Load persisted filter + settings state
	GConfig->GetString(TEXT("P4Search"), TEXT("SearchDepotPath"), SearchDepotPath, GEditorPerProjectIni);
	GConfig->GetInt   (TEXT("P4Search"), TEXT("SearchSize"),      SearchSize,      GEditorPerProjectIni);
	GConfig->GetArray (TEXT("P4Search"), TEXT("SearchHistory"),   SearchHistory,   GEditorPerProjectIni);
	SelectedPageSize = nullptr;
	for (auto& Opt : PageSizeOptions)
		if (FCString::Atoi(**Opt) == SearchSize) { SelectedPageSize = Opt; break; }
	if (!SelectedPageSize.IsValid())
	{
		// Previously-saved size doesn't match one of the presets (e.g. from manual entry) — snap to the default.
		SelectedPageSize = PageSizeOptions[1];
		SearchSize = FCString::Atoi(**SelectedPageSize);
	}
	// Deliberately not restoring a saved file-type filter here — the default search on
	// every fresh Editor session should always be "All Assets", not whatever narrower
	// extension filter happened to be selected last time.
	if (!GConfig->GetString(TEXT("P4Search"), TEXT("ServerURL"), ServerURL, GEditorPerProjectIni) || ServerURL.IsEmpty())
	{
		ServerURL = DefaultServerURL;
	}
	if (SearchDetected.IsEmpty())
	{
		SearchDetected = TEXT("*");
	}

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Editor", "Plugins", "P4Search",
			LOCTEXT("P4SearchSettingsName", "P4Search"),
			LOCTEXT("P4SearchSettingsDescription", "Configure P4Search plugin behavior."),
			GetMutableDefault<UP4SearchSettings>());
	}
}

void FP4SearchModule::ShutdownModule()
{
	// Only call this here for the non-exit case — the module being unloaded on its own (e.g.
	// disabling the plugin from the Plugins browser) without the engine exiting, where Slate is
	// still fully alive. During an actual engine exit, OnEnginePreExit() already did this much
	// earlier; calling it again this late crashes — FSlateApplication::Shutdown() has already
	// run by the time ShutdownModule() executes, and HideAssetInfoPanel()'s
	// FCurveSequence::PlayReverse() call asserts on a widget it can no longer safely touch
	// (confirmed via a real crash log: SIGSEGV in FCurveSequence::PlayReverse(), called from
	// HideAssetInfoPanel(), called from this function).
	if (!IsEngineExitRequested())
	{
		HideAssetInfoPanel();
	}

	FCoreDelegates::OnEnginePreExit.RemoveAll(this);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationPreInputKeyDownListener().RemoveAll(this);
	}

	if (SearchDebounceTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SearchDebounceTickerHandle);
	}

	if (NarrowLayoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NarrowLayoutTickerHandle);
	}

	for (const FTSTicker::FDelegateHandle& Handle : PendingAssetTickers)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}
	PendingAssetTickers.Empty();

	// Save filter + settings state before shutdown — deliberately excludes the file-type
	// filter (see StartupModule) so every fresh session starts back at "All Assets".
	GConfig->SetString(TEXT("P4Search"), TEXT("SearchDepotPath"), *SearchDepotPath, GEditorPerProjectIni);
	GConfig->SetInt   (TEXT("P4Search"), TEXT("SearchSize"),      SearchSize,       GEditorPerProjectIni);
	GConfig->SetString(TEXT("P4Search"), TEXT("ServerURL"),  *ServerURL,  GEditorPerProjectIni);
	GConfig->SetArray (TEXT("P4Search"), TEXT("SearchHistory"), SearchHistory, GEditorPerProjectIni);

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "Plugins", "P4Search");
	}

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	ClearResults();

	BackgroundBrush.Reset();
	// See ClearResults()'s comment — RemoveFromRoot() is unsafe once the UObject system has
	// already been shut down, which is the case by the time ShutdownModule() runs at engine exit.
	if (UObjectInitialized())
	{
		auto ReleaseTexture = [](UTexture2D*& Tex) { if (Tex) { Tex->RemoveFromRoot(); Tex = nullptr; } };
		ReleaseTexture(BackgroundTexture);
		ReleaseTexture(InfoPanelPreviewTexture);
	}
	InfoPanelPreviewBrush.Reset();

	FP4SearchStyle::Shutdown();

	FP4SearchCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(P4SearchTabName);
}

TSharedRef<SDockTab> FP4SearchModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	// Shared colours — same palette Unreal Editor itself uses (FStyleColors),
	// so the plugin tracks whatever theme (dark/light/custom) the user has
	// active instead of a hand-picked, fixed set of colors.
	const FSlateColor BgToolbar    = FStyleColors::Header;
	const FSlateColor BgToolbarSep = FStyleColors::Recessed;
	const FSlateColor ColText      = FStyleColors::Foreground;
	const FSlateColor ColLabel     = FStyleColors::Foreground;
	const FSlateColor ColAccent    = FStyleColors::Primary;
	const FSlateFontInfo FontLabel = FP4SearchStyle::GetCaptionFont();

	// Fallback solid brush used when the background image hasn't loaded
	static const FSlateBrush* FallbackBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FSlateBrush* BgBrush = BackgroundBrush.IsValid() ? BackgroundBrush.Get() : FallbackBrush;

	// ── Sidebar: file-type quick filters ────────────────────────────────────
	// Built via a loop (one row per ExtensionOptions entry) rather than inline
	// in the widget-tree below, since that only supports a fixed slot count.
	// Every extension other than "*" (All Assets) is sorted into one of three
	// expandable groups — 3D model formats, 2D images, or UASSET (Unreal's own
	// generic asset container, not a 2D/3D format at all) — rather than one flat list.
	static const TSet<FString> ThreeDExtensions = { TEXT("FBX"), TEXT("OBJ"), TEXT("GLB"), TEXT("USD"), TEXT("ABC") };
	static const TSet<FString> UnrealExtensions = { TEXT("UASSET") };

	// Icon + label content for the "All Assets" row and each expandable group's header — a
	// small distinguishing glyph per top-level category, matching how most marketplace-style
	// category filters pair each top-level entry with its own icon (leaf extensions stay
	// text-only, same as the reference this is modelled on).
	auto MakeIconLabel = [](const FSlateBrush* Icon, FText Label, const FSlateFontInfo& Font, TAttribute<FSlateColor> Color) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(Icon)
				.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
				.ColorAndOpacity(Color)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font)
				.ColorAndOpacity(Color)
			];
	};

	// Selected row gets a solid rounded pill background (matching how most marketplace-style
	// category filters highlight the active choice) rather than just a text-color change.
	// AllAssetsIcon is only non-null for the "*" row — leaf extensions stay text-only.
	auto MakeExtensionRow = [this, MakeIconLabel](const TSharedPtr<FString>& Opt, const FSlateBrush* AllAssetsIcon = nullptr) -> TSharedRef<SWidget>
	{
		const FString DisplayName = (*Opt == TEXT("*"))      ? TEXT("ALL ASSETS")
			: (*Opt == TEXT("ALL3D")) ? TEXT("All 3D Assets")
			: (*Opt == TEXT("ALL2D")) ? TEXT("All 2D Assets")
			: *Opt;
		return SNew(SBorder)
			.BorderImage(FileTypeRowBrush.Get())
			.BorderBackgroundColor_Lambda([this, Opt]() -> FSlateColor
			{
				return (SelectedExtension == Opt) ? FSlateColor(FStyleColors::Select) : FSlateColor(FLinearColor::Transparent);
			})
			.Padding(0.0f)
			[
				SNew(SP4SearchAnimatedButton)
				.ButtonStyle(&IconOnlyButtonStyle)
				.ContentPadding(FMargin(8.0f, 6.0f))
				.HAlign(HAlign_Left)
				.OnClicked_Lambda([this, Opt]() -> FReply
				{
					SelectedExtension = Opt;
					CurrentPage = 0;
					ExecuteSearch();
					return FReply::Handled();
				})
				[
					AllAssetsIcon
					? MakeIconLabel(AllAssetsIcon, FText::FromString(DisplayName), FP4SearchStyle::GetBodyFont(),
						TAttribute<FSlateColor>::CreateLambda([this, Opt]() -> FSlateColor
						{
							return (SelectedExtension == Opt) ? FSlateColor(FLinearColor::White) : FSlateColor(FStyleColors::Foreground);
						}))
					: StaticCastSharedRef<SWidget>(SNew(STextBlock)
						.Text(FText::FromString(DisplayName))
						.Font(FP4SearchStyle::GetBodyFont())
						.ColorAndOpacity_Lambda([this, Opt]() -> FSlateColor
						{
							return (SelectedExtension == Opt)
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FStyleColors::Foreground);
						}))
				]
			];
	};

	TSharedRef<SVerticalBox> FileTypeList = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> ThreeDGroupList = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> TwoDGroupList = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> UnrealGroupList = SNew(SVerticalBox);

	// Icons for the four top-level categories only — leaf extensions (FBX, PNG, etc.) stay
	// text-only, matching the reference. Custom-drawn glyphs (see P4SearchStyle.cpp) rather
	// than borrowed Editor icons, so each one actually fits its category.
	const FSlateBrush* AllAssetsIcon = FP4SearchStyle::Get().GetBrush("P4Search.Icon.AllAssets");
	const FSlateBrush* ThreeDIcon    = FP4SearchStyle::Get().GetBrush("P4Search.Icon.3D");
	const FSlateBrush* TwoDIcon      = FP4SearchStyle::Get().GetBrush("P4Search.Icon.2D");
	const FSlateBrush* UnrealIcon    = FP4SearchStyle::Get().GetBrush("P4Search.Icon.Unreal");

	// "All 3D/2D Assets" pseudo-options sit above that group's individual extensions, matching
	// how "*" (All Assets) sits above every group at the top level.
	ThreeDGroupList->AddSlot().AutoHeight().Padding(0.0f, 1.0f) [ MakeExtensionRow(AllThreeDOption) ];
	TwoDGroupList->AddSlot().AutoHeight().Padding(0.0f, 1.0f) [ MakeExtensionRow(AllTwoDOption) ];

	for (const TSharedPtr<FString>& Opt : ExtensionOptions)
	{
		if (*Opt == TEXT("*"))
		{
			FileTypeList->AddSlot().AutoHeight().Padding(0.0f, 1.0f) [ MakeExtensionRow(Opt, AllAssetsIcon) ];
			continue;
		}

		TSharedRef<SVerticalBox>& TargetGroup = ThreeDExtensions.Contains(*Opt) ? ThreeDGroupList
			: UnrealExtensions.Contains(*Opt) ? UnrealGroupList
			: TwoDGroupList;
		TargetGroup->AddSlot().AutoHeight().Padding(0.0f, 1.0f) [ MakeExtensionRow(Opt) ];
	}

	auto MakeExtensionGroup = [FontLabel, MakeIconLabel](const FSlateBrush* Icon, FText Title, TSharedRef<SVerticalBox> GroupList) -> TSharedRef<SWidget>
	{
		return SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
			.HeaderPadding(FMargin(0.0f, 4.0f))
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.HeaderContent()
			[
				MakeIconLabel(Icon, Title, FontLabel, FSlateColor(FStyleColors::Foreground))
			]
			.BodyContent()
			[
				GroupList
			];
	};

	FileTypeList->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		MakeExtensionGroup(ThreeDIcon, LOCTEXT("FileTypeGroup3D", "3D"), ThreeDGroupList)
	];
	FileTypeList->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		MakeExtensionGroup(TwoDIcon, LOCTEXT("FileTypeGroup2D", "2D"), TwoDGroupList)
	];
	FileTypeList->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		MakeExtensionGroup(UnrealIcon, LOCTEXT("FileTypeGroupUnreal", "Unreal"), UnrealGroupList)
	];

	TSharedRef<SWidget> Sidebar =
		SNew(SBox)
		.WidthOverride(150.0f)
		[
			SNew(SBorder)
			.BorderBackgroundColor(FStyleColors::Recessed)
			.Padding(12.0f, 16.0f, 12.0f, 12.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 8.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SidebarFileType", "FILE TYPE"))
					.Font(FP4SearchStyle::GetCaptionFont())
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					FileTypeList
				]
			]
		];

	auto Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.OnTabClosed_Lambda([this](TSharedRef<SDockTab> /*ClosedTab*/)
		{
			HideAssetInfoPanel();
			if (NarrowLayoutTickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(NarrowLayoutTickerHandle);
				NarrowLayoutTickerHandle.Reset();
			}
		})
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				// HelixSearch hex-pattern background fills the whole tab
				SNew(SBorder)
				.BorderImage(BgBrush)
				.Padding(0.0f)
				[
					// Main content (unindented below) on the left, "More Info" panel
				// slides in from the right in the second slot further down.
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]()
				{
					// Collapsed instead of squeezed alongside a Settings panel that's taken
					// over the whole content width on a narrow dock -- see
					// IsSettingsPanelTakingOverNarrowLayout().
					return (IsSettingsPanelTakingOverNarrowLayout() || IsInfoPanelTakingOverNarrowLayout())
						? EVisibility::Collapsed : EVisibility::Visible;
				})

				// ── Top bar: logo + title + search + refresh/settings ─────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(BgToolbar)
					.Padding(16.0f, 12.0f)
					[
						// Outer SWrapBox (not SHorizontalBox) so the logo can drop to its own row
						// once the rest of the bar (search field + buttons) no longer fits beside
						// it -- see the nested SHorizontalBox below, which wraps as a single unit.
						SNew(SWrapBox)
						.UseAllottedSize(true)
						.InnerSlotPadding(FVector2D(8.0f, 8.0f))

						// Product wordmark logo — style-registered vector asset (P4SearchStyle.cpp),
						// so unlike the old runtime-loaded favicon PNG it doesn't need a fallback
						// path. Clickable — resets the current search back to "All Assets".
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&IconOnlyButtonStyle)
							.ContentPadding(FMargin(0.0f))
							.ToolTipText(LOCTEXT("FaviconResetTip", "Reset to default search"))
							.OnClicked_Lambda([this]() -> FReply
							{
								ResetToDefaultSearch();
								return FReply::Handled();
							})
							[
								SNew(SBox)
								.WidthOverride(102.2f)
								.HeightOverride(28.0f)
								[
									SNew(SImage)
									.Image(FP4SearchStyle::Get().GetBrush("P4Search.ProductLogo"))
								]
							]
						]

						// The rest of the bar (search field + buttons) as ONE nested SHorizontalBox,
						// so it wraps to a new line as a single atomic unit -- rather than each
						// button wrapping independently -- once it no longer fits beside the logo.
						// FillEmptySpace(true) makes SWrapBox give this slot the rest of the line's
						// width (whether sharing the line with the logo or alone on its own line)
						// instead of just its own minimal desired size -- without this, the nested
						// box's Fill/MinWidth/MaxWidth search field below would never receive more
						// room than its 240px floor, on any panel width.
						+ SWrapBox::Slot()
						.FillEmptySpace(true)
						[
							SNew(SHorizontalBox)

						// FillWidth (rather than the old fixed AutoWidth+SBox.WidthOverride(840)) so this
						// grows/shrinks with actual available row width instead of overflowing when
						// narrow. MinWidth/MaxWidth are SLOT-level constraints (SHorizontalBox::FSlot),
						// clamping the Fill-computed size directly -- NOT SBox's MinDesiredWidth/
						// MaxDesiredWidth, which only affects what a widget reports to its OWN parent
						// and has no effect on how a Fill slot's arranged size is computed.
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.MinWidth(240.0f)
						.MaxWidth(840.0f)
						.Padding(0.0f, 0.0f, 12.0f, 0.0f)
						[
							SNew(SEditableTextBox)
							.Style(&SearchBoxStyle)
							.HintText(LOCTEXT("DetectedHint", "Search by tags... (* for all)"))
							.Text_Lambda([this]() { return FText::FromString(SearchDetected); })
							.OnTextChanged_Lambda([this](const FText& NewText)
							{
								SearchDetected = NewText.ToString();

								// Debounced search-as-you-type: each keystroke reschedules a fresh
								// one-shot ticker, so only the last keystroke in a burst actually fires.
								if (SearchDebounceTickerHandle.IsValid())
								{
									FTSTicker::GetCoreTicker().RemoveTicker(SearchDebounceTickerHandle);
								}
								SearchDebounceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
									[this](float) -> bool
								{
									CurrentPage = 0;
									ExecuteSearch();
									return false; // one-shot
								}), 0.5f);
							})
							.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
							{
								SearchDetected = NewText.ToString();
								if (CommitType == ETextCommit::OnEnter)
								{
									if (SearchDebounceTickerHandle.IsValid())
									{
										FTSTicker::GetCoreTicker().RemoveTicker(SearchDebounceTickerHandle);
										SearchDebounceTickerHandle.Reset();
									}
									CurrentPage = 0;
									ExecuteSearch();
								}
							})
						]
						// Recent-searches dropdown — only shown once there's history to show.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SAssignNew(SearchHistoryComboButton, SComboButton)
							.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButtonWithIcon")
							.HasDownArrow(true)
							.ContentPadding(FMargin(2.0f, 0.0f))
							.ToolTipText(LOCTEXT("SearchHistoryTip", "Recent searches"))
							.Visibility_Lambda([this]() -> EVisibility
							{
								return SearchHistory.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
							})
							.OnGetMenuContent_Raw(this, &FP4SearchModule::BuildSearchHistoryDropdownContent)
							.ButtonContent()
							[
								SNew(SBox)
							]
						]
						// Feature 9: Clear button (✕) between text box and Search button
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
							.ContentPadding(FMargin(8.0f, 4.0f))
							.Visibility_Lambda([this]() -> EVisibility
							{
								return (SearchDetected.IsEmpty() || SearchDetected == TEXT("*")) ? EVisibility::Collapsed : EVisibility::Visible;
							})
							.OnClicked_Lambda([this]() -> FReply
							{
								SearchDetected = TEXT("*");
								TriggerImmediateSearch();
								return FReply::Handled();
							})
							[
								SNew(SImage)
								.Image(FCoreStyle::Get().GetBrush("Icons.X"))
								.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
								.ColorAndOpacity(ColLabel)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&SearchButtonStyle)
							.ContentPadding(FMargin(24.0f, 8.0f))
							.ForegroundColor(FLinearColor::White)
							.OnClicked(FOnClicked::CreateLambda([this]() -> FReply
							{
								CurrentPage = 0;
								ExecuteSearch();
								return FReply::Handled();
							}))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SearchButton", "Search"))
								.Font(FP4SearchStyle::GetSubHeaderFont())
								.ColorAndOpacity(FLinearColor::White)
							]
						]

						// Flexible spacer — absorbs any leftover width beyond the search field's
						// 840px cap (it has no Max, unlike the search field), pushing the
						// gear-reserved gutter to the right instead of leaving the search field to
						// stretch past its intended max width.
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNullWidget::NullWidget
						]

						// Reserved gutter matching the floating Settings gear's footprint (16.0f
						// baseline right-margin + ~34px button, see the gear's own Padding lambda
						// below) so the search box's Fill share never grows into the space the gear
						// occupies -- keep these two numbers in sync if the gear's size/margin changes.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(56.0f)
						]
						]
					]
				]

				// Thin separator under toolbar
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(BgToolbarSep)
					.Padding(0.0f, 1.0f)
					[SNew(SSeparator).Thickness(1.0f).ColorAndOpacity(FStyleColors::Recessed)]
				]

				// ── Options band: Ext, Depot Path, Size ───────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(BgToolbar)
					.Padding(12.0f, 8.0f)
					[
						SNew(SWrapBox)
						.UseAllottedSize(true)
						.InnerSlotPadding(FVector2D(4.0f, 4.0f))
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DepotPathLabel", "Depot Path:"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(220.0f)
							[
								SNew(SEditableTextBox)
								.Style(&FilterBoxStyle)
								.HintText(LOCTEXT("DepotPathHint", "e.g. //depot/..."))
								.Text_Lambda([this]() { return FText::FromString(SearchDepotPath); })
								.OnTextChanged_Lambda([this](const FText& NewText)
								{
									SearchDepotPath = NewText.ToString();
									TriggerDebouncedSearch();
								})
								.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
								{
									SearchDepotPath = NewText.ToString();
									if (CommitType == ETextCommit::OnEnter)
									{
										TriggerImmediateSearch();
									}
								})
							]
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DateLabel", "Date:"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								[
									SNew(SEditableTextBox)
									.Style(&FilterBoxStyle)
									.HintText(LOCTEXT("DateHint", "e.g. pastWeek"))
									.Text_Lambda([this]() { return FText::FromString(SearchDate); })
									.OnTextChanged_Lambda([this](const FText& NewText)
									{
										SearchDate = NewText.ToString();
										TriggerDebouncedSearch();
									})
									.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
									{
										SearchDate = NewText.ToString();
										if (CommitType == ETextCommit::OnEnter)
										{
											TriggerImmediateSearch();
										}
									})
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SAssignNew(DateComboButton, SComboButton)
									.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButtonWithIcon")
									.HasDownArrow(true)
									.ContentPadding(FMargin(2.0f, 0.0f))
									.OnGetMenuContent_Raw(this, &FP4SearchModule::BuildDateDropdownContent)
									.ButtonContent()
									[
										SNew(SBox)
									]
								]
							]
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FileSizeLabel", "Size:"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FileSizeFromLabel", "From"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(56.0f)
							[
								SNew(SSpinBox<int32>)
								.MinValue(0)
								.MinSliderValue(0)
								.MaxSliderValue(1024)
								.Value_Lambda([this]() { return SearchFileSizeFromMB.Get(0); })
								.OnValueChanged_Lambda([this](int32 NewValue)
								{
									SearchFileSizeFromMB = NewValue;
									TriggerDebouncedSearch();
								})
								.OnValueCommitted_Lambda([this](int32 NewValue, ETextCommit::Type)
								{
									SearchFileSizeFromMB = NewValue;
									TriggerImmediateSearch();
								})
							]
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FileSizeFromUnit", "MB"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FileSizeToLabel", "To"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(56.0f)
							[
								SNew(SSpinBox<int32>)
								.MinValue(0)
								.MinSliderValue(0)
								.MaxSliderValue(1024)
								.Value_Lambda([this]() { return SearchFileSizeToMB.Get(0); })
								.OnValueChanged_Lambda([this](int32 NewValue)
								{
									SearchFileSizeToMB = NewValue;
									TriggerDebouncedSearch();
								})
								.OnValueCommitted_Lambda([this](int32 NewValue, ETextCommit::Type)
								{
									SearchFileSizeToMB = NewValue;
									TriggerImmediateSearch();
								})
							]
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FileSizeToUnit", "MB"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SCheckBox)
							.Style(&ToggleCheckBoxStyle)
							.IsChecked_Lambda([this]() -> ECheckBoxState
							{
								return bHeadOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
							{
								bHeadOnly = (NewState == ECheckBoxState::Checked);
								TriggerImmediateSearch();
							})
							[
								SNew(SBox)
								.WidthOverride(32.0f)
								.HeightOverride(18.0f)
								[
									SNew(SOverlay)
									+ SOverlay::Slot()
									[
										SNew(SBorder)
										.BorderImage(ToggleTrackBrush.Get())
										.Padding(0.0f)
										.BorderBackgroundColor_Lambda([this]() -> FSlateColor
										{
											return bHeadOnly ? FStyleColors::Primary : FStyleColors::InputOutline;
										})
									]
									+ SOverlay::Slot()
									.Padding(TAttribute<FMargin>::CreateLambda([this]() -> FMargin
									{
										return bHeadOnly ? FMargin(16.0f, 2.0f, 2.0f, 2.0f) : FMargin(2.0f, 2.0f, 16.0f, 2.0f);
									}))
									[
										SNew(SBox)
										.WidthOverride(14.0f)
										.HeightOverride(14.0f)
										[
											SNew(SBorder)
											.BorderImage(ToggleThumbBrush.Get())
											.Padding(0.0f)
											.BorderBackgroundColor(FStyleColors::White)
										]
									]
								]
							]
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HeadOnlyLabel", "Latest"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PageSizeLabel", "Per Page:"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SWrapBox::Slot()
						[
							SNew(SBox)
							.WidthOverride(70.0f)
							[
							SNew(SComboBox<TSharedPtr<FString>>)
							.ComboBoxStyle(&FilterComboStyle)
							.OptionsSource(&PageSizeOptions)
							.InitiallySelectedItem(SelectedPageSize)
							.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
							{
								SelectedPageSize = NewValue;
								if (SelectedPageSize.IsValid())
								{
									SearchSize = FCString::Atoi(**SelectedPageSize);
									TriggerImmediateSearch();
								}
							})
							.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
							{
								return SNew(STextBlock)
									.Text(FText::FromString(*Item))
									.Font(FP4SearchStyle::GetBodyFont())
									.Justification(ETextJustify::Center)
									.Margin(FMargin(6.0f, 3.0f));
							})
							[
								SNew(STextBlock)
								.Text_Lambda([this]()
								{
									return FText::FromString(SelectedPageSize.IsValid() ? *SelectedPageSize : TEXT(""));
								})
								.Font(FP4SearchStyle::GetBodyFont())
								.Justification(ETextJustify::Center)
								.ColorAndOpacity(FStyleColors::Foreground)
							]
							]
						]
					]
					]

				// ── Bulk action bar — only shown once 1+ cards are selected (via a card's own
				// checkbox); see the Visibility_Lambda below for why.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(BgToolbarSep)
					.Padding(12.0f, 8.0f)
					.Visibility_Lambda([this]()
					{
						// Gated on selection count, not AllCards.Num() — the latter flickers to 0
						// and back on every search/page reload, which was making this whole bar
						// (and everything below it, including the sidebar) jump vertically.
						return CountMultiSelected() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SCheckBox)
							.ToolTipText(LOCTEXT("SelectAllTip", "Select all on this page"))
							.IsChecked_Lambda([this]() -> ECheckBoxState
							{
								bool bAnySelected = false;
								bool bAllSelected = AllCards.Num() > 0;
								for (const TWeakPtr<SP4SearchResultCard>& WeakCard : AllCards)
								{
									if (TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin())
									{
										if (Card->IsMultiSelected())
										{
											bAnySelected = true;
										}
										else
										{
											bAllSelected = false;
										}
									}
								}
								if (bAllSelected) return ECheckBoxState::Checked;
								return bAnySelected ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
							{
								const bool bSelectAll = (NewState == ECheckBoxState::Checked);
								for (const TWeakPtr<SP4SearchResultCard>& WeakCard : AllCards)
								{
									if (TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin())
									{
										Card->SetMultiSelected(bSelectAll);
									}
								}
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
							.ContentPadding(FMargin(12.0f, 4.0f))
							.OnClicked_Lambda([this]() -> FReply
							{
								for (const TWeakPtr<SP4SearchResultCard>& WeakCard : AllCards)
								{
									if (TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin())
									{
										Card->SetMultiSelected(true);
									}
								}
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("BulkSelectAll", "Select All"))
								.Font(FontLabel)
								.ColorAndOpacity(ColLabel)
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								return FText::Format(LOCTEXT("BulkSelectedCount", "{0} selected"), FText::AsNumber(CountMultiSelected()));
							})
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
							.ContentPadding(FMargin(12.0f, 4.0f))
							.OnClicked_Lambda([this]() -> FReply
							{
								BulkAddSelectedToProject();
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Text_Lambda([this]()
								{
									return FText::Format(LOCTEXT("BulkAddToProject", "Add {0} to Project"), FText::AsNumber(CountMultiSelected()));
								})
								.Font(FontLabel)
								.ColorAndOpacity(ColLabel)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
							.ContentPadding(FMargin(12.0f, 4.0f))
							.OnClicked_Lambda([this]() -> FReply
							{
								ClearMultiSelection();
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("BulkClearSelection", "Clear"))
								.Font(FontLabel)
								.ColorAndOpacity(ColLabel)
							]
						]
					]
				]

				// ── Results grid (Feature 2: wrapped in SOverlay for "No results" panel) ──
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						Sidebar
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						.Padding(FMargin(16.0f, 8.0f, 16.0f, 8.0f))
						[
							SAssignNew(ResultsContainer, SWrapBox)
							.UseAllottedSize(true)
							.InnerSlotPadding(FVector2D(4.0f, 4.0f))
						]
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SAssignNew(NoResultsPanel, SBox)
						.Visibility(EVisibility::Collapsed)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NoResults", "No results found"))
							.Font(FP4SearchStyle::GetHeaderFont())
							.ColorAndOpacity(ColLabel)
						]
					]

					// Feature 11: Initial/empty state — shown until the first search completes
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SAssignNew(InitialStatePanel, SBox)
						.Visibility(EVisibility::Visible)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SubmitSearchPrompt", "Submit a search to get results"))
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
					]

					// Loading indicator
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() -> EVisibility
						{
							return bIsSearching ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(SThrobber)
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Searching", "Searching..."))
								.Font(FontLabel)
								.ColorAndOpacity(ColLabel)
							]
						]
					]

					// Search failure — bigger and centered (rather than a row squeezed into
					// ResultsContainer) so it reads as a real error state, not a stray result.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() -> EVisibility
						{
							return bSearchFailed ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock)
								.Text_Lambda([this]() { return FText::FromString(SearchFailureMessage); })
								.Font(FP4SearchStyle::GetHeaderFont())
								.ColorAndOpacity(ColLabel)
								.WrapTextAt(360.0f)
								.Justification(ETextJustify::Center)
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 16.0f, 0.0f, 0.0f)
							[
								SNew(SP4SearchAnimatedButton)
								.ButtonStyle(&SearchButtonStyle)
								.ContentPadding(FMargin(24.0f, 8.0f))
								.ForegroundColor(FLinearColor::White)
								.OnClicked_Lambda([this]() -> FReply
								{
									ExecuteSearch();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RetrySearch", "Retry"))
									.Font(FP4SearchStyle::GetSubHeaderFont())
									.ColorAndOpacity(FLinearColor::White)
								]
							]
						]
					]

					// ── Inline login — takes over the results area entirely while
					// logged out; once PerformLogin succeeds it collapses and a real
					// search runs in its place. ──────────────────────────────────
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderBackgroundColor(FStyleColors::Panel)
						.Visibility_Lambda([this]()
						{
							return bIsLoggedIn ? EVisibility::Collapsed : EVisibility::Visible;
						})
						[
							SNew(SBox)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								// Visible card frame — a bright accent border (rather than the
								// subdued Header color previously used) plus a heading make this
								// panel unmissable, instead of reading as just another filter box.
								SNew(SBorder)
								.BorderBackgroundColor(ColAccent)
								.Padding(4.0f)
								[
									SNew(SBorder)
									.BorderBackgroundColor(FStyleColors::Recessed)
									.Padding(36.0f, 30.0f)
									[
										SNew(SBox)
										.WidthOverride(320.0f)
										[
											SNew(SVerticalBox)

								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 4.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("LoginHeading", "Log in to P4Search"))
									.Font(FP4SearchStyle::GetHeaderFont())
									.ColorAndOpacity(ColAccent)
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 24.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("LoginSubheading", "Sign in to search and browse depot assets"))
									.Font(FontLabel)
									.ColorAndOpacity(ColLabel)
								]

								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("ServerUrlLabel2", "Server URL"))
									.Font(FontLabel)
									.ColorAndOpacity(ColLabel)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
								[
									SNew(SEditableTextBox)
									.Style(&FilterBoxStyle)
									.HintText(FText::FromString(DefaultServerURL))
									.Text_Lambda([this]() { return FText::FromString(ServerURL); })
									.OnTextChanged_Lambda([this](const FText& NewText)
									{
										ServerURL = NewText.ToString();
										if (ServerURL.IsEmpty()) ServerURL = DefaultServerURL;
									})
								]

								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("UsernameLabel2", "Username"))
									.Font(FontLabel)
									.ColorAndOpacity(ColLabel)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
								[
									SNew(SEditableTextBox)
									.Style(&FilterBoxStyle)
									.Text_Lambda([this]() { return FText::FromString(LoginUsername); })
									.OnTextChanged_Lambda([this](const FText& NewText) { LoginUsername = NewText.ToString(); })
								]

								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PasswordLabel2", "Password"))
									.Font(FontLabel)
									.ColorAndOpacity(ColLabel)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
								[
									SNew(SEditableTextBox)
									.Style(&FilterBoxStyle)
									.IsPassword(true)
									.Text_Lambda([this]() { return FText::FromString(LoginPassword); })
									.OnTextChanged_Lambda([this](const FText& NewText) { LoginPassword = NewText.ToString(); })
									.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type CommitType)
									{
										if (CommitType == ETextCommit::OnEnter)
										{
											PerformLogin(LoginUsername, LoginPassword);
											LoginPassword.Empty();
										}
									})
								]

								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SP4SearchAnimatedButton)
									.ButtonStyle(&SearchButtonStyle)
									.HAlign(HAlign_Center)
									.ContentPadding(FMargin(24.0f, 12.0f))
									.ForegroundColor(FLinearColor::White)
									.OnClicked_Lambda([this]() -> FReply
									{
										PerformLogin(LoginUsername, LoginPassword);
										LoginPassword.Empty();
										return FReply::Handled();
									})
									[
										SNew(STextBlock)
										.Text(LOCTEXT("LoginSubmitButton", "Log In"))
										.Font(FP4SearchStyle::GetSubHeaderFont())
										.ColorAndOpacity(FLinearColor::White)
									]
									]
								]
							]
						]
					]
					// closes the FillWidth(1.0f) slot wrapping the SOverlay above
					]
				]
				]
			]

				// ── Pagination strip -- hidden while logged out, since Prev/Next would just
				// sit there disabled with nothing to page through, and (once this row's own
				// height is freed up) leaves more room for the inline login form above it in
				// short/docked layouts. ─────────────────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(BgToolbar)
					.Padding(12.0f, 8.0f)
					.Visibility_Lambda([this]()
					{
						return bIsLoggedIn ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SHorizontalBox)

						// Push content to centre
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&SearchButtonStyle)
							.ContentPadding(FMargin(16.0f, 8.0f))
							.ForegroundColor(FLinearColor::White)
							.IsEnabled_Lambda([this]() { return CurrentPage > 0; })
							.OnClicked(FOnClicked::CreateLambda([this]() -> FReply
							{
								CurrentPage = FMath::Max(0, CurrentPage - 1);
								ExecuteSearch();
								return FReply::Handled();
							}))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PrevButton", "< Prev"))
								.Font(FP4SearchStyle::GetSubHeaderFont())
								.ColorAndOpacity(FLinearColor::White)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(16.0f, 0.0f)
						[
							// Feature 4: show result count in page label
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								if (TotalResults == 0)
								{
									return FText::Format(LOCTEXT("PageNumber", "Page {0}"), FText::AsNumber(CurrentPage + 1));
								}
								const int32 From = CurrentPage * SearchSize + 1;
								const int32 To   = FMath::Min(From + SearchSize - 1, TotalResults);
								return FText::Format(
									LOCTEXT("PageNumberWithCount", "Page {0}  ·  {1}–{2} of {3}"),
									FText::AsNumber(CurrentPage + 1),
									FText::AsNumber(From),
									FText::AsNumber(To),
									FText::AsNumber(TotalResults));
							})
							.Font(FontLabel)
							.ColorAndOpacity(ColLabel)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SP4SearchAnimatedButton)
							.ButtonStyle(&SearchButtonStyle)
							.ContentPadding(FMargin(16.0f, 8.0f))
							.ForegroundColor(FLinearColor::White)
							.IsEnabled_Lambda([this]() { return (CurrentPage + 1) * SearchSize < TotalResults; })
							.OnClicked(FOnClicked::CreateLambda([this]() -> FReply
							{
								CurrentPage++;
								ExecuteSearch();
								return FReply::Handled();
							}))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("NextButton", "Next >"))
								.Font(FP4SearchStyle::GetSubHeaderFont())
								.ColorAndOpacity(FLinearColor::White)
							]
						]

						// Mirror filler to keep group centred
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
					]
				]

				// ── Status bar: current depot path + result count ────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderBackgroundColor(BgToolbarSep)
					.Padding(16.0f, 8.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								return FText::FromString(SearchDepotPath.IsEmpty() ? TEXT("All depot paths") : SearchDepotPath);
							})
							.Font(FP4SearchStyle::GetCaptionFont())
							.ColorAndOpacity(ColLabel)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								return FText::Format(LOCTEXT("AssetCount", "{0} assets"), FText::AsNumber(TotalResults));
							})
							.Font(FP4SearchStyle::GetCaptionFont())
							.ColorAndOpacity(ColLabel)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() -> FText
							{
								if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("P4Search")))
								{
									return FText::Format(LOCTEXT("PluginVersion", "v{0}"),
										FText::FromString(Plugin->GetDescriptor().VersionName));
								}
								return FText::GetEmpty();
							})
							.Font(FP4SearchStyle::GetCaptionFont())
							.ColorAndOpacity(ColLabel)
						]
					]
				]
				// closes the FillWidth(1.0f) slot opened before SNew(SVerticalBox) above
				]

				// ── "More Info" panel — slides in from the right ──────────
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(InfoPanelContainer, SBox)
					.WidthOverride_Lambda([this]() -> FOptionalSize
					{
						return FOptionalSize(InfoPanelCurve.GetLerp() * GetInfoPanelTargetWidth());
					})
					.Visibility_Lambda([this]()
					{
						return (InfoPanelAnim.IsPlaying() || InfoPanelCurve.GetLerp() > 0.0f)
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Clipping(EWidgetClipping::ClipToBoundsAlways)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
						.Padding(0.0f)
						[
							SAssignNew(InfoPanelContent, SBox)
							.WidthOverride_Lambda([this]() -> FOptionalSize
							{
								return FOptionalSize(GetInfoPanelTargetWidth());
							})
						]
					]
				]

				// -- "Settings" panel -- slides in from the right, same as "More Info" --
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(SettingsPanelContainer, SBox)
					.WidthOverride_Lambda([this]() -> FOptionalSize
					{
						return FOptionalSize(SettingsPanelCurve.GetLerp() * GetSettingsPanelTargetWidth());
					})
					.Visibility_Lambda([this]()
					{
						return (SettingsPanelAnim.IsPlaying() || SettingsPanelCurve.GetLerp() > 0.0f)
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Clipping(EWidgetClipping::ClipToBoundsAlways)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
						.Padding(0.0f)
						[
							SNew(SBox)
							.WidthOverride_Lambda([this]() -> FOptionalSize
							{
								return FOptionalSize(GetSettingsPanelTargetWidth());
							})
							[
								SNew(SVerticalBox)

								// -- Header: title + close button --
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(24.0f, 24.0f, 24.0f, 8.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("SettingsPanelTitle", "Settings"))
										.Font(FP4SearchStyle::GetHeaderFont())
										.ColorAndOpacity(FStyleColors::Foreground)
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Top)
									[
										SNew(SP4SearchAnimatedButton)
										.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
										.ContentPadding(FMargin(8.0f, 4.0f))
										.ToolTipText(FText::FromString(TEXT("Close")))
										.OnClicked_Lambda([this]() -> FReply
										{
											HideSettingsPanel();
											return FReply::Handled();
										})
										[
											SNew(SImage)
											.Image(FCoreStyle::Get().GetBrush("Icons.X"))
											.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
											.ColorAndOpacity(FStyleColors::Foreground)
										]
									]
								]

								// -- Server URL row --
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(24.0f, 8.0f, 24.0f, 4.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("ServerURLLabel", "Server URL"))
									.Font(FontLabel)
									.ColorAndOpacity(ColLabel)
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(24.0f, 0.0f, 24.0f, 16.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.Padding(0.0f, 0.0f, 8.0f, 0.0f)
									[
										SNew(SEditableTextBox)
										.Style(&FilterBoxStyle)
										.HintText(FText::FromString(DefaultServerURL))
										.Text_Lambda([this]() { return FText::FromString(ServerURL); })
										.OnTextChanged_Lambda([this](const FText& NewText)
										{
											ServerURL = NewText.ToString();
											if (ServerURL.IsEmpty()) ServerURL = DefaultServerURL;
										})
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(SP4SearchAnimatedButton)
										.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
										.ContentPadding(FMargin(8.0f, 4.0f))
										.ToolTipText(LOCTEXT("ResetURLTip", "Reset to default server URL"))
										.OnClicked_Lambda([this]() -> FReply
										{
											ServerURL = DefaultServerURL;
											LogOut();
											return FReply::Handled();
										})
										[
											SNew(SImage)
											.Image(FCoreStyle::Get().GetBrush("Icons.Refresh"))
											.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
											.ColorAndOpacity(ColLabel)
										]
									]
								]

								// -- Login row --
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(24.0f, 0.0f, 24.0f, 24.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									.Padding(0.0f, 0.0f, 4.0f, 0.0f)
									[
										SNew(STextBlock)
										.Text_Lambda([this]()
										{
											return bIsLoggedIn
												? FText::Format(LOCTEXT("LoggedInAs", "Logged in as {0}"), FText::FromString(LoginUsername))
												: LOCTEXT("NotLoggedIn", "Not logged in");
										})
										.Font(FontLabel)
										.ColorAndOpacity(ColLabel)
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(SP4SearchAnimatedButton)
										.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
										.ContentPadding(FMargin(12.0f, 4.0f))
										.Visibility_Lambda([this]()
										{
											return bIsLoggedIn ? EVisibility::Visible : EVisibility::Collapsed;
										})
										.OnClicked_Lambda([this]() -> FReply
										{
											LogOut();
											return FReply::Handled();
										})
										[
											SNew(STextBlock)
											.Text(LOCTEXT("LogOutButton", "Log Out"))
											.Font(FontLabel)
											.ColorAndOpacity(ColLabel)
										]
									]
								]

								// -- Thumbnails section --
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(24.0f, 0.0f, 24.0f, 4.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("ThumbnailsSectionLabel", "Thumbnails"))
									.Font(FontLabel)
									.ColorAndOpacity(ColLabel)
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(24.0f, 0.0f, 24.0f, 24.0f)
								[
									SNew(SCheckBox)
									.ToolTipText(LOCTEXT("AutoGenThumbTip",
										"When enabled, any .uasset search result missing a thumbnail is silently "
										"generated and saved back to Perforce (same as clicking \"Generate Thumbnail\" manually)."))
									.IsChecked_Lambda([]() -> ECheckBoxState
									{
										return GetDefault<UP4SearchSettings>()->bAutoGenerateThumbnails
											? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
									})
									.OnCheckStateChanged_Lambda([](ECheckBoxState NewState)
									{
										UP4SearchSettings* Settings = GetMutableDefault<UP4SearchSettings>();
										Settings->bAutoGenerateThumbnails = (NewState == ECheckBoxState::Checked);
										Settings->SaveConfig();
									})
									[
										SNew(STextBlock)
										.Text(LOCTEXT("AutoGenThumbLabel", "Auto-generate missing thumbnails"))
										.Font(FontLabel)
										.ColorAndOpacity(ColLabel)
									]
								]
							]
						]
					]
				]
				]
			]

			// Settings gear — floats in the tab's top-right corner, independent of the
			// toolbar row, and toggles the "Settings" slide-in panel. Shifts left by
			// whatever "More Info"/"Settings" panel width is currently open, so it
			// doesn't sit on top of (and hide) that panel's own close button. The top
			// search bar's trailing reserved-gutter spacer is
			// sized to match this button's footprint -- keep both in sync.
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(TAttribute<FMargin>::CreateLambda([this]() -> FMargin
			{
				const float ReservedRight = 16.0f
					+ (InfoPanelCurve.GetLerp() * GetInfoPanelTargetWidth())
					+ (SettingsPanelCurve.GetLerp() * GetSettingsPanelTargetWidth());
				return FMargin(16.0f, 12.0f, ReservedRight, 12.0f);
			}))
			[
				SNew(SP4SearchAnimatedButton)
				.ButtonStyle(&GearButtonStyle)
				.ContentPadding(FMargin(8.0f))
				.IsFocusable(false) // otherwise the button keeps a visible focus ring after being
				                    // clicked once, showing a rounded highlight even while not hovered
				.ToolTipText(LOCTEXT("FloatingSettingsTip", "Settings"))
				.OnClicked_Lambda([this]() -> FReply
				{
					ToggleSettingsPanel();
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Toolbar.Settings"))
					.DesiredSizeOverride(FVector2D(18.0f, 18.0f))
					.ColorAndOpacity(ColLabel)
				]
			]
		];

	MyTab = Tab;

	if (NarrowLayoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NarrowLayoutTickerHandle);
	}
	NarrowLayoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this](float) -> bool
	{
		TSharedPtr<SDockTab> PinnedTab = MyTab.Pin();
		if (PinnedTab.IsValid())
		{
			MeasuredContentWidth = PinnedTab->GetContent()->GetCachedGeometry().GetLocalSize().X;
			bSettingsPanelNarrow = MeasuredContentWidth > 0.0f && MeasuredContentWidth < SettingsNarrowThreshold;
			bInfoPanelNarrow = MeasuredContentWidth > 0.0f && MeasuredContentWidth < InfoPanelNarrowThreshold;
		}
		return true;
	}), 0.1f);

	// Feature 3: Execute search immediately on tab open
	ExecuteSearch();

	return Tab;
}

void FP4SearchModule::ClearResults()
{
	// ClearResults() also runs from ShutdownModule() at engine exit, by which point
	// UnloadModulesAtShutdown() runs AFTER the UObject system has already been torn down
	// ("Object subsystem successfully closed") — touching any UObject that late (even a
	// simple RemoveFromRoot()) dereferences an already-invalidated FUObjectItem slot and
	// crashes (SIGSEGV / "Assertion failed: Index >= 0" in UObjectArray.h). At that point
	// every UObject is about to be destroyed regardless, so rooting no longer matters and
	// this loop is skipped entirely.
	if (UObjectInitialized())
	{
		for (UTexture2D* Tex : ThumbnailTextures)
		{
			if (Tex)
			{
				Tex->RemoveFromRoot();
			}
		}
	}
	ThumbnailTextures.Empty();
	ThumbnailBrushes.Empty();

	if (ResultsContainer.IsValid())
	{
		ResultsContainer->ClearChildren();
	}

	AllCards.Empty();

	// Otherwise "No results found" from the previous search stays visible underneath the
	// "Searching..." spinner until this new search's own results come back.
	if (NoResultsPanel.IsValid())
	{
		NoResultsPanel->SetVisibility(EVisibility::Collapsed);
	}
}

void FP4SearchModule::ResetToDefaultSearch()
{
	SearchDepotPath.Empty();
	SearchDate.Empty();
	SearchFileSizeFromMB.Reset();
	SearchFileSizeToMB.Reset();
	bHeadOnly = true;
	SearchDetected = TEXT("*");
	if (ExtensionOptions.Num() > 1)
	{
		SelectedExtension = ExtensionOptions[1]; // "FBX" -- matches StartupModule's default
	}
	CurrentPage = 0;
	ExecuteSearch();
}

void FP4SearchModule::ShiftCalendarMonth(int32 Delta)
{
	CalendarViewMonth += Delta;
	P4SearchCore::NormalizeYearMonth(CalendarViewYear, CalendarViewMonth);
	if (CalendarGridBox.IsValid())
	{
		CalendarGridBox->SetContent(BuildCalendarGrid());
	}
}

TSharedRef<SWidget> FP4SearchModule::BuildCalendarGrid()
{
	if (CalendarViewYear == 0)
	{
		const FDateTime Today = FDateTime::Now();
		CalendarViewYear = Today.GetYear();
		CalendarViewMonth = Today.GetMonth();
	}

	const FSlateFontInfo FontLabel = FP4SearchStyle::GetCaptionFont();
	const FSlateFontInfo FontBold  = FP4SearchStyle::GetCaptionBoldFont();
	const FSlateColor ColLabel = FStyleColors::Foreground;

	static const TCHAR* MonthNames[] = {
		TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"), TEXT("June"),
		TEXT("July"), TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")
	};
	static const TCHAR* DayHeaders[] = { TEXT("Mo"), TEXT("Tu"), TEXT("We"), TEXT("Th"), TEXT("Fr"), TEXT("Sa"), TEXT("Su") };

	TSharedRef<SUniformGridPanel> DayGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(1.0f));

	for (int32 Col = 0; Col < 7; ++Col)
	{
		DayGrid->AddSlot(Col, 0)
		[
			SNew(SBox)
			.WidthOverride(22.0f)
			.HeightOverride(18.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(DayHeaders[Col]))
				.Font(FontLabel)
				.ColorAndOpacity(ColLabel)
			]
		];
	}

	for (const P4SearchCore::FCalendarDayCell& Cell : P4SearchCore::ComputeCalendarDayCells(CalendarViewYear, CalendarViewMonth))
	{
		const int32 Day = Cell.Day;
		const int32 Row = Cell.Row;
		const int32 Col = Cell.Col;
		const int32 Year = CalendarViewYear;
		const int32 Month = CalendarViewMonth;

		DayGrid->AddSlot(Col, Row)
		[
			SNew(SBox)
			.WidthOverride(22.0f)
			.HeightOverride(20.0f)
			[
				SNew(SP4SearchAnimatedButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([this, Year, Month, Day]() -> FReply
				{
					SearchDate = FString::Printf(TEXT("%04d/%02d/%02d"), Year, Month, Day);
					TriggerImmediateSearch();
					if (DateComboButton.IsValid())
					{
						DateComboButton->SetIsOpen(false);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(Day))
					.Font(FontLabel)
					.ColorAndOpacity(ColLabel)
				]
			]
		];
	}

	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SP4SearchAnimatedButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this]() -> FReply { ShiftCalendarMonth(-1); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("<"))).Font(FontBold).ColorAndOpacity(ColLabel)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%s %d"), MonthNames[CalendarViewMonth - 1], CalendarViewYear)))
			.Font(FontBold)
			.ColorAndOpacity(ColLabel)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SP4SearchAnimatedButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this]() -> FReply { ShiftCalendarMonth(1); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(FontBold).ColorAndOpacity(ColLabel)
			]
		]
	]
	+ SVerticalBox::Slot().AutoHeight()
	[
		DayGrid
	];
}

TSharedRef<SWidget> FP4SearchModule::BuildDateDropdownContent()
{
	const FSlateFontInfo FontLabel = FP4SearchStyle::GetCaptionFont();
	const FSlateFontInfo FontBold  = FP4SearchStyle::GetCaptionBoldFont();
	const FSlateColor ColLabel = FStyleColors::Foreground;

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	static const TCHAR* DatePresets[] = { TEXT("pastWeek"), TEXT("pastMonth"), TEXT("pastYear") };

	Content->AddSlot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("DatePresetsLabel", "Presets"))
		.Font(FontBold)
		.ColorAndOpacity(ColLabel)
	];

	for (const TCHAR* Preset : DatePresets)
	{
		const FString Value = Preset;
		Content->AddSlot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 0.0f)
		[
			SNew(SP4SearchAnimatedButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Value]() -> FReply
			{
				SearchDate = Value;
				TriggerImmediateSearch();
				if (DateComboButton.IsValid())
				{
					DateComboButton->SetIsOpen(false);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Value))
				.Font(FontLabel)
				.ColorAndOpacity(ColLabel)
			]
		];
	}

	Content->AddSlot().AutoHeight().Padding(4.0f, 4.0f, 4.0f, 4.0f)
	[
		SNew(SSeparator)
	];

	Content->AddSlot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SAssignNew(CalendarGridBox, SBox)
		[
			BuildCalendarGrid()
		]
	];

	return Content;
}

TSharedRef<SWidget> FP4SearchModule::BuildSearchHistoryDropdownContent()
{
	const FSlateFontInfo FontLabel = FP4SearchStyle::GetCaptionFont();
	const FSlateFontInfo FontBold  = FP4SearchStyle::GetCaptionBoldFont();
	const FSlateColor ColLabel = FStyleColors::Foreground;

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SearchHistoryLabel", "Recent Searches"))
		.Font(FontBold)
		.ColorAndOpacity(ColLabel)
	];

	for (const FString& Query : SearchHistory)
	{
		Content->AddSlot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 0.0f)
		[
			SNew(SP4SearchAnimatedButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Query]() -> FReply
			{
				SearchDetected = Query;
				TriggerImmediateSearch();
				if (SearchHistoryComboButton.IsValid())
				{
					SearchHistoryComboButton->SetIsOpen(false);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Query))
				.Font(FontLabel)
				.ColorAndOpacity(ColLabel)
			]
		];
	}

	return Content;
}

void FP4SearchModule::TriggerDebouncedSearch()
{
	// Each call reschedules a fresh one-shot ticker, so only the last change in a
	// burst (e.g. fast typing) actually fires a search.
	if (SearchDebounceTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SearchDebounceTickerHandle);
	}
	SearchDebounceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this](float) -> bool
	{
		CurrentPage = 0;
		ExecuteSearch();
		return false; // one-shot
	}), 0.5f);
}

void FP4SearchModule::TriggerImmediateSearch()
{
	if (SearchDebounceTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SearchDebounceTickerHandle);
		SearchDebounceTickerHandle.Reset();
	}
	CurrentPage = 0;
	ExecuteSearch();
}

void FP4SearchModule::ExecuteSearch()
{
	// "Submit a search to get results" is only relevant pre-login too; once the tab has
	// been opened at all, the login panel (or real results) should be all that's shown.
	if (InitialStatePanel.IsValid())
	{
		InitialStatePanel->SetVisibility(EVisibility::Collapsed);
	}

	if (!bIsLoggedIn)
	{
		return;
	}

	if (!SearchDetected.IsEmpty() && SearchDetected != TEXT("*"))
	{
		// Most-recent-first, deduplicated — re-running an existing query (e.g. paging
		// Next/Prev, or re-searching the same term) just moves it back to the front
		// instead of creating a repeat entry.
		SearchHistory.Remove(SearchDetected);
		SearchHistory.Insert(SearchDetected, 0);
		constexpr int32 MaxSearchHistory = 10;
		if (SearchHistory.Num() > MaxSearchHistory)
		{
			SearchHistory.SetNum(MaxSearchHistory);
		}
	}

	ClearResults();
	bIsSearching = true;
	bSearchFailed = false;
	const int32 Gen = ++SearchGeneration;

	auto Request = HttpRequester->CreateRequest();
	const FString Ext = (SelectedExtension.IsValid() ? *SelectedExtension : TEXT("*"));
	const int32 SizeFromMB = SearchFileSizeFromMB.Get(0);
	const int32 SizeToMB = SearchFileSizeToMB.Get(0);
	const FString SearchQuery = P4SearchCore::BuildSearchDslQuery(SearchDepotPath, Ext, SearchDetected, SearchDate, SizeFromMB, SizeToMB, bHeadOnly);

	const FString EncodedQuery = FGenericPlatformHttp::UrlEncode(SearchQuery);
	const int32 From = CurrentPage * SearchSize;
	const FString SearchURL = FString::Printf(
		TEXT("%s/api/v1.3/search/dsl?search=%s&from=%d&size=%d"),
		*ServerURL, *EncodedQuery, From, SearchSize);
	UE_LOG(LogTemp, Log, TEXT("P4Search: ExecuteSearch query='%s' url=%s"), *SearchQuery, *SearchURL);
	Request->SetURL(SearchURL);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->OnProcessRequestComplete().BindLambda([this, Gen](FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bSuccess)
	{
		// A newer search has already started — this response is stale, and the
		// newer request's own completion handler owns bIsSearching/results from here.
		if (Gen != SearchGeneration)
		{
			return;
		}

		bIsSearching = false;

		if (InitialStatePanel.IsValid())
		{
			InitialStatePanel->SetVisibility(EVisibility::Collapsed);
		}

		if (!ResultsContainer.IsValid())
		{
			return;
		}

		const int32 SearchResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (HandleAuthFailure(SearchResponseCode))
		{
			return;
		}

		if (!bSuccess || !Response.IsValid())
		{
			// The request itself never completed — DNS/timeout/unreachable server, not
			// something the server can tell us about. Distinct from a completed request
			// that came back an error status, which isn't a connection problem at all.
			// Kept generic on screen (no ServerURL, no raw error text) — full detail goes to
			// the log only, since a search-tab user shouldn't have to parse a server error.
			UE_LOG(LogTemp, Warning, TEXT("P4Search: ExecuteSearch could not reach %s"), *ServerURL);
			SearchFailureMessage = TEXT("Search failed. Please check your connection and try again.");
			bSearchFailed = true;
			if (NoResultsPanel.IsValid())
			{
				NoResultsPanel->SetVisibility(EVisibility::Collapsed);
			}
			return;
		}

		if (SearchResponseCode != 200)
		{
			// The server responded, so this is a server/query problem, not a connection one.
			// Its own error message (same {"status":{"message":...}} envelope used across the
			// rest of this API) can be a raw parser/grammar dump for a bad DSL query — logged
			// for diagnosis, but not shown on screen.
			const FString ServerMessage = P4SearchCore::ExtractJsonStatusMessage(Response->GetContentAsString());
			UE_LOG(LogTemp, Warning, TEXT("P4Search: ExecuteSearch failed — HTTP %d: %s"),
				SearchResponseCode, ServerMessage.IsEmpty() ? *Response->GetContentAsString().Left(500) : *ServerMessage);
			SearchFailureMessage = TEXT("Search failed. Please try again, or contact your administrator if the problem continues.");
			bSearchFailed = true;
			if (NoResultsPanel.IsValid())
			{
				NoResultsPanel->SetVisibility(EVisibility::Collapsed);
			}
			return;
		}

		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		FJsonSerializer::Deserialize(JsonReader, JsonObject);

		TotalResults = P4SearchCore::ExtractTotalResults(JsonObject.ToSharedRef());
		TArray<TSharedPtr<FJsonValue>> SearchResults = JsonObject->GetObjectField(TEXT("hits"))->GetArrayField(TEXT("hits"));

		UE_LOG(LogTemp, Log, TEXT("P4Search: ExecuteSearch response HTTP %d, TotalResults=%d, hits.hits.Num=%d, body: %s"),
			Response->GetResponseCode(), TotalResults, SearchResults.Num(), *Response->GetContentAsString().Left(500));

		// Blurs are decoded synchronously (cheap, local) for every card in this loop so they
		// all appear instantly; the (slower, async) full-resolution thumbnail fetches are
		// queued here and only fired afterwards, once every card already has its blur shown.
		struct FPendingThumbnail
		{
			FString DepotFile;
			int32 Rev;
			TSharedPtr<SP4SearchResultCard> Card;
		};
		TArray<FPendingThumbnail> PendingThumbnails;

		int32 ResultIndex = 0;
		for (TSharedPtr<FJsonValue> JsonValue : SearchResults)
		{
			TSharedPtr<FJsonObject> Source = JsonValue->AsObject()->GetObjectField(TEXT("_source"));

			const P4SearchCore::FSearchHit Hit = P4SearchCore::ParseSearchHit(Source.ToSharedRef());
			const FString& DepotFile = Hit.DepotFile;
			const FString& FileName = Hit.FileName;
			const int32 Rev = Hit.Rev;
			const int32 Change = Hit.Change;
			const bool bIsHead = Hit.bIsHead;
			const FString& FileSizeStr = Hit.FileSizeStr;
			const FString& DetectedTagsStr = Hit.DetectedTagsStr;

			TSharedPtr<SP4SearchResultCard> Card;

			ResultsContainer->AddSlot()
			.Padding(4.0f)
			[
				SAssignNew(Card, SP4SearchResultCard)
				.FileName(FileName)
				.DepotFile(DepotFile)
				.Rev(Rev)
				.bIsHead(bIsHead)
				.ActionButtonStyle(SearchButtonStyle)
				.AppearIndex(ResultIndex)
				.bSupportsAddToProject(IsExtensionSupportedForImport(FPaths::GetExtension(FileName)))
			];
			++ResultIndex;

			TWeakPtr<SP4SearchResultCard> WeakCard = Card;
			AllCards.Add(WeakCard);
			Card->SetOnAddToProject(FSimpleDelegate::CreateLambda([this, DepotFile, Rev, WeakCard]()
			{
				AddToProject(DepotFile, Rev, WeakCard);
			}));

			Card->SetOnGenerateThumbnail(FSimpleDelegate::CreateLambda([this, DepotFile, Rev, WeakCard]()
			{
				GenerateThumbnail(DepotFile, Rev, WeakCard);
			}));

			Card->SetOnMoreInfo(FSimpleDelegate::CreateLambda([this, WeakCard, FileName, DepotFile, DetectedTagsStr, Rev, Change, FileSizeStr, bIsHead]()
			{
				ShowAssetInfoPanel(WeakCard, FileName, DepotFile, DetectedTagsStr, Rev, Change, FileSizeStr, bIsHead);
			}));

			const FString MultiSelectKey = FString::Printf(TEXT("%s#%d"), *DepotFile, Rev);
			Card->SetOnMultiSelectChanged(SP4SearchResultCard::FOnMultiSelectChanged::CreateLambda([this, MultiSelectKey, DepotFile, Rev](bool bSelected)
			{
				if (bSelected)
				{
					MultiSelectedAssets.Add(MultiSelectKey, { DepotFile, Rev });
				}
				else
				{
					MultiSelectedAssets.Remove(MultiSelectKey);
				}
			}));

			// Restore checkbox state for an asset selected on a previous page view — see
			// MultiSelectedAssets' comment in P4Search.h for why selection outlives the card.
			if (MultiSelectedAssets.Contains(MultiSelectKey))
			{
				Card->SetMultiSelected(true);
			}

			// Instant low-res placeholder, decoded locally from the "blur"/"blurSize" fields
			// already present on the search hit — no network round trip needed.
			FString Blur, BlurSize;
			int32 BlurWidth = 32, BlurHeight = 18;
			if (Source->TryGetStringField(TEXT("blur"), Blur) && !Blur.IsEmpty()
				&& Source->TryGetStringField(TEXT("blurSize"), BlurSize))
			{
				FString WStr, HStr;
				if (BlurSize.Split(TEXT("x"), &WStr, &HStr))
				{
					BlurWidth  = FMath::Max(1, FCString::Atoi(*WStr));
					BlurHeight = FMath::Max(1, FCString::Atoi(*HStr));
				}

				TArray<uint8> BlurBGRA;
				if (DecodeBlurHash(Blur, BlurWidth, BlurHeight, BlurBGRA))
				{
					UTexture2D* BlurTexture = UTexture2D::CreateTransient(BlurWidth, BlurHeight, PF_B8G8R8A8);
					if (BlurTexture)
					{
						uint8* MipData = static_cast<uint8*>(BlurTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
						FMemory::Memcpy(MipData, BlurBGRA.GetData(), BlurBGRA.Num());
						BlurTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
						BlurTexture->UpdateResource();
						BlurTexture->AddToRoot();
						ThumbnailTextures.Add(BlurTexture);

						TSharedPtr<FSlateDynamicImageBrush> BlurBrush = MakeShareable(
							new FSlateDynamicImageBrush(BlurTexture, FVector2D(192.0f, 108.0f), FName(*(DepotFile + TEXT("_blur")))));
						RoundThumbnailBrushTopCorners(BlurBrush.Get());
						ThumbnailBrushes.Add(BlurBrush);

						Card->SetThumbnailBrush(BlurBrush);
					}
				}
			}

			PendingThumbnails.Add({DepotFile, Rev, Card});
		}

		// Feature 2: Show/hide "No results" panel
		if (NoResultsPanel.IsValid())
		{
			NoResultsPanel->SetVisibility(SearchResults.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed);
		}

		// Only now — after every card already has its blur placeholder showing — start
		// fetching the slower full-resolution thumbnails.
		for (const FPendingThumbnail& Pending : PendingThumbnails)
		{
			FetchThumbnail(Pending.DepotFile, Pending.Rev, Pending.Card);
		}
	});
	Request->ProcessRequest();
}

UTexture2D* FP4SearchModule::DecodeImageToTexture(const TArray<uint8>& ImageData, const FString& DebugContext, const FString& DepotFile)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	const EImageFormat DetectedFormat = ImageWrapperModule.DetectImageFormat(ImageData.GetData(), ImageData.Num());
	if (DetectedFormat == EImageFormat::Invalid)
	{
		UE_LOG(LogTemp, Warning, TEXT("P4Search: %s could not detect image format for '%s'"), *DebugContext, *DepotFile);
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(DetectedFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("P4Search: %s SetCompressed failed for '%s'"), *DebugContext, *DepotFile);
		return nullptr;
	}

	TArray<uint8> RawBGRA;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawBGRA))
	{
		UE_LOG(LogTemp, Warning, TEXT("P4Search: %s GetRaw failed for '%s'"), *DebugContext, *DepotFile);
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	if (Width <= 0 || Height <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("P4Search: %s image for '%s' has invalid dimensions %dx%d"), *DebugContext, *DepotFile, Width, Height);
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	uint8* MipData = static_cast<uint8*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	FMemory::Memcpy(MipData, RawBGRA.GetData(), RawBGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	Texture->AddToRoot();
	return Texture;
}

void FP4SearchModule::FetchThumbnail(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard)
{
	auto Request = HttpRequester->CreateRequest();
	FString EncodedFile = FGenericPlatformHttp::UrlEncode(DepotFile);
	Request->SetURL(FString::Printf(TEXT("%s/api/v1.3/view/thumb?file=%s"), *ServerURL, *EncodedFile));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->SetHeader(TEXT("Content-Type"), TEXT("image/png"));
	Request->OnProcessRequestComplete().BindLambda([this, WeakCard, DepotFile, Rev](FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bSuccess)
	{
		TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin();
		if (!bSuccess || !Card.IsValid() || !Response.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchThumbnail failed for '%s' (bSuccess=%d, Card=%d, Response=%d)"),
				*DepotFile, bSuccess, Card.IsValid(), Response.IsValid());
			return;
		}

		const int32 HttpCode = Response->GetResponseCode();
		if (HandleAuthFailure(HttpCode))
		{
			return;
		}

		const TArray<uint8>& ImageData = Response->GetContent();
		UE_LOG(LogTemp, Log, TEXT("P4Search: FetchThumbnail '%s' — HTTP %d, %d bytes"),
			*DepotFile, HttpCode, ImageData.Num());

		if (HttpCode != 200 || ImageData.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchThumbnail bad response for '%s': HTTP %d, body: %s"),
				*DepotFile, HttpCode, *Response->GetContentAsString().Left(200));

			// No thumbnail exists yet — silently generate and save one, same as clicking
			// "Generate Thumbnail" manually, if the user has opted into this behavior.
			if (GetDefault<UP4SearchSettings>()->bAutoGenerateThumbnails && DepotFile.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
			{
				GenerateThumbnail(DepotFile, Rev, WeakCard);
			}
			return;
		}

		UTexture2D* Texture = DecodeImageToTexture(ImageData, TEXT("FetchThumbnail"), DepotFile);
		if (!Texture)
		{
			return;
		}
		ThumbnailTextures.Add(Texture);

		TSharedPtr<FSlateDynamicImageBrush> Brush = MakeShareable(
			new FSlateDynamicImageBrush(Texture, FVector2D(160.0f, 160.0f), FName(*DepotFile))
		);
		RoundThumbnailBrushTopCorners(Brush.Get());
		ThumbnailBrushes.Add(Brush);

		Card->SetThumbnailBrush(Brush);

		// Only once the real thumbnail is showing do we lazily fetch the (optional,
		// video/3D-only) hover-preview filmstrip.
		FetchRibbon(DepotFile, Rev, WeakCard);
	});
	Request->ProcessRequest();
}

void FP4SearchModule::FetchRibbon(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard)
{
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject());
	Body->SetStringField(TEXT("depotFile"), FString::Printf(TEXT("%s#%d"), *DepotFile, Rev));
	Body->SetStringField(TEXT("attribute"), TEXT("dam_ribbon_json"));
	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// Frame layout (spriteWidth/spriteHeight, and each frame's exact x/y) comes from the
	// dam_ribbon_json attribute via the generic /view/attribute endpoint (whitelisted
	// attribute names only) — fetched first so we can skip the (larger) filmstrip image
	// entirely for assets with no ribbon at all.
	auto MetaRequest = HttpRequester->CreateRequest();
	MetaRequest->SetVerb(TEXT("POST"));
	MetaRequest->SetURL(ServerURL + TEXT("/api/v1.4/view/attribute"));
	MetaRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	MetaRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	MetaRequest->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	MetaRequest->SetContentAsString(BodyStr);
	UE_LOG(LogTemp, Log, TEXT("P4Search: FetchRibbon meta request '%s' body=%s"), *MetaRequest->GetURL(), *BodyStr);
	MetaRequest->OnProcessRequestComplete().BindLambda(
		[this, WeakCard, DepotFile, Rev, BodyStr](FHttpRequestPtr, FHttpResponsePtr MetaResponse, bool bMetaSuccess)
	{
		// No ribbon for this asset (not video/3D, or none generated yet) — not an error, just skip.
		if (!bMetaSuccess || !MetaResponse.IsValid() || MetaResponse->GetResponseCode() != 200)
		{
			const int32 MetaCode = MetaResponse.IsValid() ? MetaResponse->GetResponseCode() : -1;
			if (HandleAuthFailure(MetaCode))
			{
				return;
			}
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchRibbon meta failed for '%s' — bSuccess=%d HTTP %d, body: %s"),
				*DepotFile, bMetaSuccess, MetaCode, MetaResponse.IsValid() ? *MetaResponse->GetContentAsString().Left(300) : TEXT("<no response>"));
			return;
		}

		TSharedPtr<FJsonObject> MetaJson;
		TSharedRef<TJsonReader<TCHAR>> MetaReader = TJsonReaderFactory<TCHAR>::Create(MetaResponse->GetContentAsString());
		if (!FJsonSerializer::Deserialize(MetaReader, MetaJson) || !MetaJson.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchRibbon meta JSON parse failed for '%s', body: %s"),
				*DepotFile, *MetaResponse->GetContentAsString().Left(300));
			return;
		}

		const TOptional<P4SearchCore::FRibbonMeta> ParsedMeta = P4SearchCore::ParseRibbonMeta(MetaJson.ToSharedRef());
		if (!ParsedMeta.IsSet())
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchRibbon meta for '%s' invalid/empty, body: %s"),
				*DepotFile, *MetaResponse->GetContentAsString().Left(300));
			return;
		}
		const int32 SpriteWidth = ParsedMeta->SpriteWidth;
		const int32 SpriteHeight = ParsedMeta->SpriteHeight;
		const TArray<P4SearchCore::FRibbonFrameRect>& Frames = ParsedMeta->Frames;

		UE_LOG(LogTemp, Log, TEXT("P4Search: FetchRibbon meta OK for '%s' — spriteWidth=%d spriteHeight=%d frames=%d"),
			*DepotFile, SpriteWidth, SpriteHeight, Frames.Num());

		// Now fetch the actual filmstrip image itself.
		auto ImageRequest = HttpRequester->CreateRequest();
		ImageRequest->SetVerb(TEXT("POST"));
		ImageRequest->SetURL(ServerURL + TEXT("/api/v1.4/view/ribbon"));
		ImageRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		ImageRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
		ImageRequest->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
		ImageRequest->SetContentAsString(BodyStr);
		ImageRequest->OnProcessRequestComplete().BindLambda(
			[this, WeakCard, DepotFile, SpriteWidth, SpriteHeight, Frames](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
		{
			TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin();
			if (!bSuccess || !Card.IsValid() || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				const int32 ImgCode = Response.IsValid() ? Response->GetResponseCode() : -1;
				if (HandleAuthFailure(ImgCode))
				{
					return;
				}
				UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchRibbon image failed for '%s' — bSuccess=%d Card=%d HTTP %d"),
					*DepotFile, bSuccess, Card.IsValid(), ImgCode);
				return;
			}

			const TArray<uint8>& ImageData = Response->GetContent();
			if (ImageData.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchRibbon image response for '%s' had 0 bytes"), *DepotFile);
				return;
			}

			UTexture2D* Texture = DecodeImageToTexture(ImageData, TEXT("FetchRibbon"), DepotFile);
			if (!Texture)
			{
				return;
			}
			const int32 Width = Texture->GetSizeX();
			const int32 Height = Texture->GetSizeY();
			ThumbnailTextures.Add(Texture);

			const TArray<FBox2f> FrameUVs = P4SearchCore::ComputeRibbonFrameUVs(Frames, SpriteWidth, SpriteHeight, Width, Height);

			TSharedPtr<FSlateDynamicImageBrush> Brush = MakeShareable(
				new FSlateDynamicImageBrush(Texture, FVector2D((float)Width, (float)Height), FName(*(DepotFile + TEXT("_ribbon"))))
			);
			RoundThumbnailBrushTopCorners(Brush.Get());
			ThumbnailBrushes.Add(Brush);

			Card->SetSpriteBrush(Brush.Get(), FrameUVs);
			UE_LOG(LogTemp, Log, TEXT("P4Search: FetchRibbon SetSpriteBrush applied for '%s' — %d frames"), *DepotFile, FrameUVs.Num());
		});
		ImageRequest->ProcessRequest();
	});
	MetaRequest->ProcessRequest();
}

void FP4SearchModule::FetchSceneRefs(const FString& DepotFile, int32 Rev)
{
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject());
	Body->SetStringField(TEXT("depotFile"), FString::Printf(TEXT("%s#%d"), *DepotFile, Rev));
	Body->SetStringField(TEXT("attribute"), TEXT("dam_scene_refs"));
	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// dam_scene_refs is a whitelisted P4 attribute (not part of the search hit's own
	// document body — same generic /view/attribute endpoint FetchRibbon uses for
	// dam_ribbon_json), whose value is a JSON array of scene references. Its exact
	// per-element shape isn't documented beyond "JSON array" (see docs/API.md), so each
	// entry is described as leniently as possible: a plain string is used directly, an
	// object tries a few likely key names, and anything else falls back to AsString().
	auto Request = HttpRequester->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(ServerURL + TEXT("/api/v1.4/view/attribute"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->SetContentAsString(BodyStr);
	Request->OnProcessRequestComplete().BindLambda(
		[this, DepotFile](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (HandleAuthFailure(ResponseCode))
		{
			return;
		}

		// No dam_scene_refs set for this asset (or the fetch failed) — not an error.
		if (!bSuccess || !Response.IsValid() || ResponseCode != 200)
		{
			SceneRefsDisplayText = TEXT("None");
			return;
		}

		TArray<TSharedPtr<FJsonValue>> RefsArray;
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, RefsArray) || RefsArray.Num() == 0)
		{
			SceneRefsDisplayText = TEXT("None");
			return;
		}

		SceneRefsDisplayText = P4SearchCore::FormatSceneRefs(RefsArray);
		UE_LOG(LogTemp, Log, TEXT("P4Search: FetchSceneRefs for '%s' — %s"), *DepotFile, *SceneRefsDisplayText);
	});
	Request->ProcessRequest();
}

void FP4SearchModule::FetchSceneStats(const FString& DepotFile, int32 Rev)
{
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject());
	Body->SetStringField(TEXT("depotFile"), FString::Printf(TEXT("%s#%d"), *DepotFile, Rev));
	Body->SetStringField(TEXT("attribute"), TEXT("dam_scene_stats"));
	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// dam_scene_stats is a JSON object matching search-service's SceneStatsModel — same
	// generic /view/attribute endpoint as FetchRibbon/FetchSceneRefs, response body parsed
	// directly as an object (not wrapped in a string) per docs/API.md.
	auto Request = HttpRequester->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(ServerURL + TEXT("/api/v1.4/view/attribute"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->SetContentAsString(BodyStr);
	Request->OnProcessRequestComplete().BindLambda(
		[this, DepotFile](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (HandleAuthFailure(ResponseCode))
		{
			return;
		}

		// No dam_scene_stats set for this asset (or the fetch failed) — not an error.
		if (!bSuccess || !Response.IsValid() || ResponseCode != 200)
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchSceneStats failed for '%s' — bSuccess=%d HTTP %d, body: %s"),
				*DepotFile, bSuccess, ResponseCode, Response.IsValid() ? *Response->GetContentAsString().Left(300) : TEXT("<no response>"));
			SceneStatsDisplayText = TEXT("None");
			return;
		}

		TSharedPtr<FJsonObject> StatsJson;
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, StatsJson) || !StatsJson.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: FetchSceneStats JSON parse failed for '%s', body: %s"),
				*DepotFile, *Response->GetContentAsString().Left(300));
			SceneStatsDisplayText = TEXT("None");
			return;
		}

		SceneStatsDisplayText = P4SearchCore::FormatSceneStats(StatsJson.ToSharedRef());
		UE_LOG(LogTemp, Log, TEXT("P4Search: FetchSceneStats for '%s' — %s, raw body: %s"),
			*DepotFile, *SceneStatsDisplayText, *Response->GetContentAsString().Left(300));
	});
	Request->ProcessRequest();
}

bool FP4SearchModule::IsExtensionSupportedForImport(const FString& Extension)
{
	static TSet<FString> SupportedExtensions;
	static bool bScanned = false;
	if (!bScanned)
	{
		bScanned = true;
		TArray<UClass*> FactoryClasses;
		GetDerivedClasses(UFactory::StaticClass(), FactoryClasses);
		for (UClass* FactoryClass : FactoryClasses)
		{
			if (FactoryClass->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			const UFactory* Factory = FactoryClass->GetDefaultObject<UFactory>();
			if (!Factory || !Factory->bEditorImport)
			{
				continue;
			}
			TArray<FString> FactoryExtensions;
			Factory->GetSupportedFileExtensions(FactoryExtensions);
			for (const FString& FactoryExtension : FactoryExtensions)
			{
				SupportedExtensions.Add(FactoryExtension.ToLower());
			}
		}
	}

	const FString LowerExtension = Extension.ToLower();
	return LowerExtension == TEXT("uasset") || SupportedExtensions.Contains(LowerExtension);
}

void FP4SearchModule::AddToProject(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard)
{
	UE_LOG(LogTemp, Log, TEXT("P4Search: AddToProject called — DepotFile='%s' Rev=%d"), *DepotFile, Rev);

	if (!bIsLoggedIn)
	{
		return;
	}

	const FString FileName  = FPaths::GetCleanFilename(DepotFile);
	const FString AssetName = FPaths::GetBaseFilename(DepotFile);
	const FString OutputDir = FPaths::ProjectContentDir() / TEXT("P4Search") / AssetName;
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDir);
	const FString OutputPath = OutputDir / FileName;

	// "content/download" was only ever a proposed endpoint — it doesn't exist server-side.
	// The existing "view/asset/{file}" endpoint already streams raw depot file bytes.
	const FString EncodedFile = FGenericPlatformHttp::UrlEncode(DepotFile);
	const FString DownloadURL = FString::Printf(
		TEXT("%s/api/v1.4/view/asset/%s?rev=%d"), *ServerURL, *EncodedFile, Rev);
	UE_LOG(LogTemp, Log, TEXT("P4Search: Downloading '%s' from %s"), *DepotFile, *DownloadURL);

	if (auto C = WeakCard.Pin()) { C->SetDownloadProgress(TOptional<float>()); }

	auto Request = HttpRequester->CreateRequest();
	Request->SetURL(DownloadURL);
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->OnRequestProgress64().BindLambda(
		[WeakCard](FHttpRequestPtr Req, uint64 /*BytesSent*/, uint64 BytesReceived)
	{
		TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin();
		if (!Card.IsValid())
		{
			return;
		}
		const FHttpResponsePtr InProgressResponse = Req.IsValid() ? Req->GetResponse() : nullptr;
		const uint64 ContentLength = InProgressResponse.IsValid() ? InProgressResponse->GetContentLength() : 0;
		Card->SetDownloadProgress(ContentLength > 0
			? TOptional<float>((float)BytesReceived / (float)ContentLength)
			: TOptional<float>());
	});
	Request->OnProcessRequestComplete().BindLambda(
		[this, FileName, OutputPath, AssetName, WeakCard](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (auto C = WeakCard.Pin()) { C->SetDownloadIdle(); }
		if (HandleAuthFailure(ResponseCode))
		{
			return;
		}

		const bool  bOk = bSuccess && ResponseCode == 200
			&& FFileHelper::SaveArrayToFile(Response->GetContent(), *OutputPath);

		if (!bOk)
		{
			UE_LOG(LogTemp, Warning, TEXT("P4Search: AddToProject failed — bSuccess=%d HTTP %d body: %s"),
				bSuccess, ResponseCode, Response.IsValid() ? *Response->GetContentAsString().Left(500) : TEXT("<no response>"));
		}

		FNotificationInfo Info(bOk
			? FText::Format(LOCTEXT("AddSuccess", "Downloaded '{0}' into Content/P4Search/"),
			                FText::FromString(FileName))
			: FText::Format(LOCTEXT("AddFailed",  "Download failed for '{0}' (HTTP {1})"),
			                FText::FromString(FileName), FText::AsNumber(ResponseCode)));
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);

		if (!bOk)
		{
			return;
		}

		// Feature 7: Mark card as "Added ✓"
		if (auto C = WeakCard.Pin()) { C->SetAdded(); }

		// Feature 10: Register/import the downloaded asset.
		// .uasset files are already in Unreal's native format — just
		// scan the asset registry so the editor picks them up.
		// All other formats go through the interactive import dialog.
		// Deferred to the next tick to avoid re-entering the task graph.
		TArray<FString> Files      = { OutputPath };
		FString         ImportDest = TEXT("/Game/P4Search/") + AssetName;
		const bool      bIsUAsset  = OutputPath.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase);
		UE_LOG(LogTemp, Log, TEXT("P4Search: Scheduling ticker — bIsUAsset=%d OutputPath='%s' ImportDest='%s'"), bIsUAsset, *OutputPath, *ImportDest);
		TSharedRef<FTSTicker::FDelegateHandle> TickerHandleBox = MakeShared<FTSTicker::FDelegateHandle>();
		*TickerHandleBox = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([this, TickerHandleBox, Files, ImportDest, AssetName, bIsUAsset, OutputPath, WeakCard](float) -> bool
			{
				PendingAssetTickers.Remove(*TickerHandleBox);
				if (!IsEngineExitRequested())
				{
					if (bIsUAsset)
					{
						const int64 FileSize = IFileManager::Get().FileSize(*OutputPath);
						UE_LOG(LogTemp, Log, TEXT("P4Search: Ticker fired — file size on disk: %lld bytes, scanning '%s'"), FileSize, *ImportDest);
						IAssetRegistry& AssetReg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
						AssetReg.ScanPathsSynchronous({ ImportDest }, true);
						TArray<FAssetData> Found;
						AssetReg.GetAssetsByPath(FName(*ImportDest), Found, true);
						UE_LOG(LogTemp, Log, TEXT("P4Search: Ticker — GetAssetsByPath found %d asset(s)"), Found.Num());
						if (Found.Num() > 0)
						{
							// Detect filename/asset-name mismatch (file renamed in depot).
							// Unreal requires the filename to match the internal object name.
							// If they differ, move the file so the names align, then rescan.
							const FString CorrectName = Found[0].AssetName.ToString();
							if (!AssetName.Equals(CorrectName, ESearchCase::IgnoreCase))
							{
								UE_LOG(LogTemp, Log, TEXT("P4Search: Name mismatch — depot filename '%s' vs internal asset name '%s'. Relocating."), *AssetName, *CorrectName);
								const FString ContentDir  = FPaths::ProjectContentDir();
								const FString CorrectDir  = ContentDir / TEXT("P4Search") / CorrectName;
								const FString CorrectPath = CorrectDir / CorrectName + TEXT(".uasset");
								FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*CorrectDir);
								if (IFileManager::Get().Move(*CorrectPath, *OutputPath, /*bReplace=*/true))
								{
									UE_LOG(LogTemp, Log, TEXT("P4Search: Moved to '%s'"), *CorrectPath);
									IFileManager::Get().DeleteDirectory(*FPaths::GetPath(OutputPath), false, false);
									const FString CorrectImportDest = TEXT("/Game/P4Search/") + CorrectName;
									AssetReg.ScanPathsSynchronous({ CorrectImportDest }, true);
									Found.Empty();
									AssetReg.GetAssetsByPath(FName(*CorrectImportDest), Found, true);
									UE_LOG(LogTemp, Log, TEXT("P4Search: Rescan after relocation found %d asset(s)"), Found.Num());
								}
								else
								{
									UE_LOG(LogTemp, Warning, TEXT("P4Search: Failed to move file — Content Browser may not show the asset"));
								}
							}
							if (Found.Num() > 0)
							{
								for (const FAssetData& AD : Found)
								{
									UE_LOG(LogTemp, Log, TEXT("P4Search: Final asset: ObjectPath='%s' AssetName='%s' Class='%s'"),
										*AD.GetSoftObjectPath().ToString(), *AD.AssetName.ToString(), *AD.AssetClassPath.ToString());
								}
								FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								CB.Get().SyncBrowserToAssets(Found, /*bAllowLockedBrowsers=*/true, /*bFocusContentBrowser=*/true);
								UE_LOG(LogTemp, Log, TEXT("P4Search: SyncBrowserToAssets called"));

								// Show "Generate Thumbnail" only for Blueprints without mesh components
								const FAssetData& FirstAsset = Found[0];
								const bool bIsBlueprint = FirstAsset.AssetClassPath == FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"));
								if (bIsBlueprint)
								{
									bool bHasMesh = false;
									UBlueprint* BP = Cast<UBlueprint>(FirstAsset.GetAsset());
									if (BP && BP->SimpleConstructionScript)
									{
										for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
										{
											if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(UMeshComponent::StaticClass()))
											{
												bHasMesh = true;
												break;
											}
										}
									}
									if (!bHasMesh)
									{
										if (TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin())
										{
											Card->ShowGenerateThumbnailButton();
										}
									}
								}
							}
						}
						else
						{
							// Fallback: navigate Content Browser to the parent folder
							UE_LOG(LogTemp, Warning, TEXT("P4Search: No assets found — navigating browser to '/Game/P4Search/'"));
							FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
							TArray<FString> Paths = { TEXT("/Game/P4Search") };
							CB.Get().SyncBrowserToFolders(Paths, /*bAllowLockedBrowsers=*/true, /*bFocusContentBrowser=*/true);
						}
					}
					else
					{
						FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools")
							.Get().ImportAssets(Files, ImportDest);
					}
				}
				return false; // one-shot: do not repeat
			})
		);
		PendingAssetTickers.Add(*TickerHandleBox);
	});
	Request->ProcessRequest();
}

void FP4SearchModule::GenerateThumbnail(const FString& DepotFile, int32 Rev, TWeakPtr<SP4SearchResultCard> WeakCard)
{
	UE_LOG(LogTemp, Log, TEXT("P4Search: GenerateThumbnail called — DepotFile='%s' Rev=%d"), *DepotFile, Rev);

	if (!bIsLoggedIn)
	{
		return;
	}

	const FString FileName  = FPaths::GetCleanFilename(DepotFile);
	const FString AssetName = FPaths::GetBaseFilename(DepotFile);
	const FString OutputDir = FPaths::ProjectContentDir() / TEXT("P4Search") / AssetName;
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDir);
	const FString OutputPath = OutputDir / FileName;

	// "content/download" was only ever a proposed endpoint — it doesn't exist server-side.
	// The existing "view/asset/{file}" endpoint already streams raw depot file bytes.
	const FString EncodedFile = FGenericPlatformHttp::UrlEncode(DepotFile);
	const FString DownloadURL = FString::Printf(
		TEXT("%s/api/v1.4/view/asset/%s?rev=%d"), *ServerURL, *EncodedFile, Rev);

	if (auto C = WeakCard.Pin()) { C->SetDownloadProgress(TOptional<float>()); }

	auto Request = HttpRequester->CreateRequest();
	Request->SetURL(DownloadURL);
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->OnRequestProgress64().BindLambda(
		[WeakCard](FHttpRequestPtr Req, uint64 /*BytesSent*/, uint64 BytesReceived)
	{
		TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin();
		if (!Card.IsValid())
		{
			return;
		}
		const FHttpResponsePtr InProgressResponse = Req.IsValid() ? Req->GetResponse() : nullptr;
		const uint64 ContentLength = InProgressResponse.IsValid() ? InProgressResponse->GetContentLength() : 0;
		Card->SetDownloadProgress(ContentLength > 0
			? TOptional<float>((float)BytesReceived / (float)ContentLength)
			: TOptional<float>());
	});
	Request->OnProcessRequestComplete().BindLambda(
		[this, AssetName, FileName, DepotFile, OutputPath, WeakCard](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (auto C = WeakCard.Pin()) { C->SetDownloadIdle(); }
		if (HandleAuthFailure(ResponseCode))
		{
			return;
		}

		const bool  bOk = bSuccess && ResponseCode == 200
			&& FFileHelper::SaveArrayToFile(Response->GetContent(), *OutputPath);
		if (!bOk)
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("GenThumbDownloadFailed", "Download failed for '{0}' (HTTP {1})"),
				FText::FromString(FileName), FText::AsNumber(ResponseCode)));
			Info.ExpireDuration = 4.0f;
			Info.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(Info);
			return;
		}

		TSharedRef<FTSTicker::FDelegateHandle> TickerHandleBox = MakeShared<FTSTicker::FDelegateHandle>();
		*TickerHandleBox = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([this, TickerHandleBox, AssetName, FileName, DepotFile, WeakCard](float) -> bool
			{
				PendingAssetTickers.Remove(*TickerHandleBox);
				if (IsEngineExitRequested()) return false;

				// ── Load .uasset directly ─────────────────────────
				// .uasset is already Unreal's native format — ImportAssets()
				// only handles external formats (FBX, PNG, …). We just need
				// to tell the asset registry about the new file and load the
				// package directly.
				const FString PackageName = TEXT("/Game/P4Search/") + AssetName + TEXT("/") + AssetName;

				IAssetRegistry& AssetReg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
				AssetReg.ScanPathsSynchronous({ TEXT("/Game/P4Search/") + AssetName }, true);

				UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *PackageName);
				if (!Asset)
				{
					FNotificationInfo Info(FText::Format(
						LOCTEXT("GenThumbLoadFailed", "Could not load '{0}' for thumbnail generation"),
						FText::FromString(FileName)));
					Info.ExpireDuration = 4.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
					return false;
				}
				TArray<uint8> PNGData;
				UTexture2D* ThumbTex = nullptr;

				// For Blueprint assets, render the event graph directly.
				// For everything else use the standard 3D thumbnail path.
				UBlueprint* BP = Cast<UBlueprint>(Asset);
				UEdGraph* GraphToRender = nullptr;
				if (BP)
				{
					for (UEdGraph* G : BP->UbergraphPages)
						if (G && G->Nodes.Num() > 0) { GraphToRender = G; break; }
					if (!GraphToRender)
						for (UEdGraph* G : BP->FunctionGraphs)
							if (G && G->Nodes.Num() > 0) { GraphToRender = G; break; }
				}

				if (GraphToRender)
				{
					const FVector2D RenderSize(1440.0f, 810.0f);

					// Use SGraphPanel directly (same as SGraphPreviewer) so we can call Update()
					// explicitly. SGraphEditor wraps SGraphPanel but gives no access to it,
					// and node widgets are NOT built until Update() is called — the constructor
					// only registers an OnGraphChanged delegate, which never fires for pre-existing nodes.
					TSharedPtr<SGraphPanel> GraphPanel;
					SAssignNew(GraphPanel, SGraphPanel)
						.GraphObj(GraphToRender)
						.IsEditable(false);
					GraphPanel->Update(); // Builds SGraphNode widgets for all existing nodes

					// Compute view transform from node positions.
					// SetViewLocation takes effect immediately, unlike the deferred ZoomToFit.
					float MinX = TNumericLimits<float>::Max();
					float MinY = TNumericLimits<float>::Max();
					float MaxX = TNumericLimits<float>::Lowest();
					float MaxY = TNumericLimits<float>::Lowest();
					for (UEdGraphNode* Node : GraphToRender->Nodes)
					{
						if (Node)
						{
							MinX = FMath::Min(MinX, (float)Node->NodePosX);
							MinY = FMath::Min(MinY, (float)Node->NodePosY);
							MaxX = FMath::Max(MaxX, (float)Node->NodePosX);
							MaxY = FMath::Max(MaxY, (float)Node->NodePosY);
						}
					}
					MaxX += 300.0f; MaxY += 150.0f;
					MinX -= 80.0f;  MinY -= 80.0f;
					const float GraphW = MaxX - MinX;
					const float GraphH = MaxY - MinY;
					const float Zoom = FMath::Clamp(
						FMath::Min(RenderSize.X / GraphW, RenderSize.Y / GraphH) * 0.9f, 0.05f, 1.5f);
					const FVector2D ViewOffset(
						MinX - (RenderSize.X / Zoom - GraphW) / 2.0f,
						MinY - (RenderSize.Y / Zoom - GraphH) / 2.0f);
					UE_LOG(LogTemp, Log, TEXT("P4Search: Graph view — nodes=%d zoom=%.3f offset=(%.0f,%.0f)"),
						GraphToRender->Nodes.Num(), Zoom, ViewOffset.X, ViewOffset.Y);
					GraphPanel->RestoreViewSettings(ViewOffset, Zoom);

					FWidgetRenderer* WR = new FWidgetRenderer(true, false);
					UTextureRenderTarget2D* RT = WR->DrawWidget(GraphPanel.ToSharedRef(), RenderSize);
					FlushRenderingCommands();

					if (RT)
					{
						RT->AddToRoot();
						TArray<FColor> Pixels;
						FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
						if (Res && Res->ReadPixels(Pixels) && Pixels.Num() > 0)
						{
							IImageWrapperModule& IWM =
								FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
							TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);
							IW->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
								(int32)RenderSize.X, (int32)RenderSize.Y, ERGBFormat::BGRA, 8);
							PNGData = IW->GetCompressed(75);

							ThumbTex = UTexture2D::CreateTransient(
								(int32)RenderSize.X, (int32)RenderSize.Y, PF_B8G8R8A8);
							if (ThumbTex)
							{
								uint8* Mip = static_cast<uint8*>(
									ThumbTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
								FMemory::Memcpy(Mip, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
								ThumbTex->GetPlatformData()->Mips[0].BulkData.Unlock();
								ThumbTex->UpdateResource();
							}
						}
						RT->RemoveFromRoot();
					}
					delete WR;
				}
				else
				{
					// Standard 3D thumbnail path (meshes, non-Blueprint assets)
					FObjectThumbnail ObjThumb;
					ThumbnailTools::RenderThumbnail(
						Asset, 1440, 810,
						ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
						nullptr, &ObjThumb);

					const TArray<uint8>& BGRA = ObjThumb.GetUncompressedImageData();
					if (BGRA.Num() == 1440 * 810 * 4)
					{
						IImageWrapperModule& IWM =
							FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
						TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);
						IW->SetRaw(BGRA.GetData(), BGRA.Num(), 1440, 810, ERGBFormat::BGRA, 8);
						PNGData = IW->GetCompressed(75);

						ThumbTex = UTexture2D::CreateTransient(1440, 810, PF_B8G8R8A8);
						if (ThumbTex)
						{
							uint8* Mip = static_cast<uint8*>(
								ThumbTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
							FMemory::Memcpy(Mip, BGRA.GetData(), BGRA.Num());
							ThumbTex->GetPlatformData()->Mips[0].BulkData.Unlock();
							ThumbTex->UpdateResource();
						}
					}
				}

				if (PNGData.Num() == 0)
				{
					FNotificationInfo Info(FText::Format(
						LOCTEXT("GenThumbRenderFailed", "Thumbnail render produced no data for '{0}'"),
						FText::FromString(FileName)));
					Info.ExpireDuration = 4.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
					return false;
				}

				// ── Update card image ─────────────────────────────
				if (ThumbTex)
				{
					ThumbTex->AddToRoot();
					ThumbnailTextures.Add(ThumbTex);
					TSharedPtr<FSlateDynamicImageBrush> Brush = MakeShareable(
						new FSlateDynamicImageBrush(ThumbTex,
							FVector2D(128.f, 128.f), FName(*DepotFile)));
					RoundThumbnailBrushTopCorners(Brush.Get());
					ThumbnailBrushes.Add(Brush);
					if (auto C = WeakCard.Pin()) { C->SetThumbnailBrush(Brush); }
				}

				// ── Mark button done ──────────────────────────────
				if (auto C = WeakCard.Pin()) { C->SetGenerated(); }

				// ── Save thumbnail via p4search ────────────────────
				// Sent directly from memory — no local p4 binary or temp file needed.
				const FString EncodedAttrFile = FGenericPlatformHttp::UrlEncode(DepotFile);
				const FString AttrURL = FString::Printf(
					TEXT("%s/api/v1.3/content/attribute?file=%s"), *ServerURL, *EncodedAttrFile);

				auto AttrRequest = HttpRequester->CreateRequest();
				AttrRequest->SetVerb(TEXT("POST"));
				AttrRequest->SetURL(AttrURL);
				AttrRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
				AttrRequest->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
				AttrRequest->SetHeader(TEXT("Content-Type"), TEXT("image/jpeg"));
				AttrRequest->SetContent(PNGData);
				AttrRequest->OnProcessRequestComplete().BindLambda(
					[this, FileName](FHttpRequestPtr, FHttpResponsePtr AttrResponse, bool bAttrSuccess)
				{
					const int32 AttrCode = AttrResponse.IsValid() ? AttrResponse->GetResponseCode() : 0;
					if (HandleAuthFailure(AttrCode))
					{
						return;
					}

					const bool  bAttrOk  = bAttrSuccess && AttrCode == 200;

					FNotificationInfo Info(bAttrOk
						? FText::Format(LOCTEXT("GenThumbOK",   "Thumbnail saved to Perforce for '{0}'"),
						                FText::FromString(FileName))
						: FText::Format(LOCTEXT("GenThumbFail", "Failed to save thumbnail for '{0}' (HTTP {1})"),
						                FText::FromString(FileName), FText::AsNumber(AttrCode)));
					Info.ExpireDuration = 4.0f;
					Info.bUseSuccessFailIcons = true;
					FSlateNotificationManager::Get().AddNotification(Info);
				});
				AttrRequest->ProcessRequest();

				return false; // one-shot ticker
			})
		);
		PendingAssetTickers.Add(*TickerHandleBox);
	});
	Request->ProcessRequest();
}

void FP4SearchModule::FetchInfoPanelPreview(const FString& DepotFile, TWeakPtr<SP4SearchResultCard> WeakCard)
{
	auto Request = HttpRequester->CreateRequest();
	const FString EncodedFile = FGenericPlatformHttp::UrlEncode(DepotFile);
	// /view/preview (~1440px), not /view/thumb (~240px, used for the small card thumbnails) —
	// this image is shown full-size in the "More Info" panel, so it needs the higher-res asset.
	Request->SetURL(FString::Printf(TEXT("%s/api/v1.3/view/preview?file=%s"), *ServerURL, *EncodedFile));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->OnProcessRequestComplete().BindLambda(
		[this, WeakCard, DepotFile](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		// Stale response for a panel that's since been closed or replaced by another asset.
		if (WeakCard != SelectedResultCard)
		{
			return;
		}

		const int32 HttpCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (HandleAuthFailure(HttpCode))
		{
			return;
		}

		const TArray<uint8>& ImageData = Response.IsValid() ? Response->GetContent() : TArray<uint8>();
		if (!bSuccess || HttpCode != 200 || ImageData.Num() == 0)
		{
			return;
		}

		UTexture2D* Texture = DecodeImageToTexture(ImageData, TEXT("FetchInfoPanelPreview"), DepotFile);
		if (!Texture)
		{
			return;
		}

		if (InfoPanelPreviewTexture)
		{
			InfoPanelPreviewTexture->RemoveFromRoot();
		}
		InfoPanelPreviewTexture = Texture;
		InfoPanelPreviewBrush = MakeShareable(new FSlateDynamicImageBrush(Texture, FVector2D(Texture->GetSizeX(), Texture->GetSizeY()), FName(*DepotFile)));
	});
	Request->ProcessRequest();
}

void FP4SearchModule::ShowAssetInfoPanel(TWeakPtr<SP4SearchResultCard> WeakCard, const FString& FileName,
	const FString& DepotFile, const FString& DetectedTags, int32 Rev, int32 Change, const FString& FileSize,
	bool bIsHead)
{
	if (!InfoPanelContent.IsValid() || !InfoPanelContainer.IsValid())
	{
		return;
	}

	if (TSharedPtr<SP4SearchResultCard> PreviouslySelected = SelectedResultCard.Pin())
	{
		PreviouslySelected->SetSelected(false);
	}
	SelectedResultCard = WeakCard;
	if (TSharedPtr<SP4SearchResultCard> NewlySelected = WeakCard.Pin())
	{
		NewlySelected->SetSelected(true);
	}

	// FetchModelPreview (below) will flip this to Found or NotFound once the fetch resolves.
	ModelPreviewStatus = EModelPreviewStatus::Loading;
	ModelPreviewBytesReceived = 0;
	ModelPreviewContentLength = 0;
	bShowPreviewInsteadOfModel = false;

	// Drop the previous asset's 2D fallback preview so it doesn't flash while the new one
	// (FetchInfoPanelPreview, below) loads — it only ever shows once ModelPreviewStatus
	// resolves to NotFound anyway.
	if (InfoPanelPreviewTexture)
	{
		InfoPanelPreviewTexture->RemoveFromRoot();
		InfoPanelPreviewTexture = nullptr;
	}
	InfoPanelPreviewBrush.Reset();

	// FetchSceneRefs/FetchSceneStats (below) overwrite these once each fetch resolves; the
	// info rows read them reactively via MakeInfoRow's TAttribute, so no panel rebuild is
	// needed when they change.
	SceneRefsDisplayText = TEXT("Loading...");
	SceneStatsDisplayText = TEXT("Loading...");

	const float InnerWidth = InfoPanelWidth - 40.0f;

	auto MakeInfoRow = [InnerWidth](const FString& Label, TAttribute<FString> Value, bool bCopyOnClick = false)
	{
		TSharedRef<SWidget> ValueWidget = SNew(STextBlock)
			.Text_Lambda([Value]() { return FText::FromString(Value.Get()); })
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
			.WrapTextAt(InnerWidth)
			.ColorAndOpacity(FStyleColors::Foreground);

		if (bCopyOnClick)
		{
			ValueWidget = SNew(SP4SearchAnimatedButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(0.0f))
				.HAlign(HAlign_Left)
				.Cursor(EMouseCursor::Hand)
				.ToolTipText(LOCTEXT("ClickToCopyTip", "Click to copy"))
				.OnClicked_Lambda([Value]() -> FReply
				{
					FPlatformApplicationMisc::ClipboardCopy(*Value.Get());

					FNotificationInfo Info(LOCTEXT("CopiedToClipboard", "Copied to clipboard"));
					Info.ExpireDuration = 2.0f;
					Info.bUseSuccessFailIcons = false;
					FSlateNotificationManager::Get().AddNotification(Info);

					return FReply::Handled();
				})
				[
					ValueWidget
				];
		}

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 16.0f, 0.0f, 3.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label.ToUpper()))
				.Font(FP4SearchStyle::GetCaptionBoldFont())
				.ColorAndOpacity(FStyleColors::AccentGray)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ValueWidget
			];
	};

	InfoPanelContent->SetContent(
		SNew(SBorder)
		.BorderBackgroundColor(FStyleColors::Transparent)
		.Padding(0.0f)
		[
			// Scrollable -- when the tab is docked short (e.g. in a bottom-bar layout), this
			// panel's content (title, 3D viewer, every info row, scene refs/stats) can be taller
			// than the available height, so it needs to scroll rather than just clip.
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(24.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FileName))
					.Font(FP4SearchStyle::GetTitleFont())
					.ColorAndOpacity(FStyleColors::Foreground)
					.WrapTextAt(InnerWidth - 30.0f)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				[
					SNew(SP4SearchAnimatedButton)
					.ButtonStyle(&FilterComboStyle.ComboButtonStyle.ButtonStyle)
					.ContentPadding(FMargin(8.0f, 4.0f))
					.ToolTipText(FText::FromString(TEXT("Close")))
					.OnClicked_Lambda([this]() -> FReply
					{
						HideAssetInfoPanel();
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("Icons.X"))
						.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
						.ColorAndOpacity(FStyleColors::Foreground)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SP4SearchAnimatedButton)
				.Visibility(IsExtensionSupportedForImport(FPaths::GetExtension(FileName))
					? EVisibility::Visible : EVisibility::Collapsed)
				.ButtonStyle(&SearchButtonStyle)
				.HAlign(HAlign_Center)
				.ContentPadding(FMargin(16.0f, 8.0f))
				.ForegroundColor(FLinearColor::White)
				.OnClicked_Lambda([this, DepotFile, Rev, WeakCard]() -> FReply
				{
					AddToProject(DepotFile, Rev, WeakCard);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SImage)
						.Image(SP4SearchResultCard::GetDownloadIconBrush())
						.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
						.ColorAndOpacity(FLinearColor::White)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("InfoPanelAddToProject", "Add to Project"))
						.Font(FP4SearchStyle::GetSubHeaderFont())
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.HeightOverride(440.0f)
				[
					SNew(SOverlay)

					// 2D fallback preview (FetchInfoPanelPreview) — background layer, shown
					// scaled to fill whenever the 3D viewport isn't: Loading (so there's
					// something to look at while the model downloads), NotFound, or Found but
					// the user has toggled back to the preview via bShowPreviewInsteadOfModel.
					+ SOverlay::Slot()
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFill)
						.Visibility_Lambda([this]() -> EVisibility
						{
							const bool bShowingModel = ModelPreviewStatus == EModelPreviewStatus::Found
								&& !bShowPreviewInsteadOfModel;
							return !bShowingModel && InfoPanelPreviewBrush.IsValid()
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SImage)
							.Image_Lambda([this]() -> const FSlateBrush*
							{
								return InfoPanelPreviewBrush.IsValid()
									? InfoPanelPreviewBrush.Get()
									: FCoreStyle::Get().GetDefaultBrush();
							})
						]
					]

					// 3D viewport — visible once a mesh has actually loaded, unless the user
					// has switched back to the 2D preview.
					+ SOverlay::Slot()
					[
						SNew(SBox)
						.Visibility_Lambda([this]() -> EVisibility
						{
							return ModelPreviewStatus == EModelPreviewStatus::Found && !bShowPreviewInsteadOfModel
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SAssignNew(CurrentModelViewport, SP4SearchModelViewport)
						]
					]

					// Preview/3D toggle — only offered when there's actually a choice: a model
					// loaded AND a 2D preview available to switch to.
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Top)
					.Padding(8.0f)
					[
						SNew(SP4SearchAnimatedButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.Visibility_Lambda([this]() -> EVisibility
						{
							return ModelPreviewStatus == EModelPreviewStatus::Found && InfoPanelPreviewBrush.IsValid()
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						.ToolTipText_Lambda([this]() -> FText
						{
							return bShowPreviewInsteadOfModel
								? LOCTEXT("SwitchToModelTooltip", "Switch to 3D model")
								: LOCTEXT("SwitchToPreviewTooltip", "Switch to preview image");
						})
						.OnClicked_Lambda([this]() -> FReply
						{
							bShowPreviewInsteadOfModel = !bShowPreviewInsteadOfModel;
							return FReply::Handled();
						})
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icons.Visibility"))
								.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text_Lambda([this]() -> FText
								{
									return bShowPreviewInsteadOfModel
										? LOCTEXT("ShowModelLabel", "View 3D Model")
										: LOCTEXT("ShowPreviewLabel", "View Preview");
								})
								.Font(FP4SearchStyle::GetCaptionFont())
							]
						]
					]

					// Loading state — spinner plus a progress bar, drawn over the 2D preview
					// above (if one's arrived yet) rather than a blank area. The bar is
					// determinate once the server's Content-Length is known (large models),
					// otherwise it falls back to an indeterminate marquee (Percent unset).
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
						.Padding(16.0f, 12.0f)
						.Visibility_Lambda([this]() -> EVisibility
						{
							return ModelPreviewStatus == EModelPreviewStatus::Loading
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(SThrobber)
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ModelLoading", "Loading 3D preview..."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
								.ColorAndOpacity(FLinearColor::White)
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(200.0f)
								[
									SNew(SProgressBar)
									.Percent_Lambda([this]() -> TOptional<float>
									{
										if (ModelPreviewContentLength == 0)
										{
											return TOptional<float>(); // unknown length — indeterminate marquee
										}
										return (float)ModelPreviewBytesReceived / (float)ModelPreviewContentLength;
									})
								]
							]
						]
					]

					// Confirmed no "model" attribute for this asset (or the fetch failed) —
					// the 2D fallback preview above is already showing (if it loaded); this is
					// just the explanatory caption over it, or a plain message if even that
					// fetch failed too.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Bottom)
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
						.Padding(12.0f, 4.0f)
						.Visibility_Lambda([this]() -> EVisibility
						{
							return ModelPreviewStatus == EModelPreviewStatus::NotFound && InfoPanelPreviewBrush.IsValid()
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ModelNotFoundShowingPreview", "No 3D model available"))
							.Font(FP4SearchStyle::GetBodyFont())
							.ColorAndOpacity(FLinearColor::White)
						]
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Visibility_Lambda([this]() -> EVisibility
						{
							return ModelPreviewStatus == EModelPreviewStatus::NotFound && !InfoPanelPreviewBrush.IsValid()
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						.Text(LOCTEXT("ModelNotFound", "No 3D model available for this asset"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
						.ColorAndOpacity(FStyleColors::Foreground)
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight() [ MakeInfoRow(TEXT("Depot Path"), DepotFile, /*bCopyOnClick=*/true) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeInfoRow(TEXT("Revision"), FString::Printf(TEXT("#%d"), Rev))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				.Padding(8.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SImage)
					.Visibility(bIsHead ? EVisibility::Collapsed : EVisibility::Visible)
					.Image(FCoreStyle::Get().GetBrush("Icons.WarningWithColor"))
					.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
					.ToolTipText(LOCTEXT("NotHeadRevisionInfoTip", "Not the head revision"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight() [ MakeInfoRow(TEXT("Changelist"), FString::FromInt(Change)) ]
			+ SVerticalBox::Slot().AutoHeight() [ MakeInfoRow(TEXT("File Size"), FileSize) ]
			+ SVerticalBox::Slot().AutoHeight() [ MakeInfoRow(TEXT("Detected Tags"), DetectedTags.IsEmpty() ? TEXT("No tags found") : DetectedTags) ]
			+ SVerticalBox::Slot().AutoHeight() [ MakeInfoRow(TEXT("Scene References"), TAttribute<FString>::CreateLambda([this]() { return SceneRefsDisplayText; })) ]
				+ SVerticalBox::Slot().AutoHeight() [ MakeInfoRow(TEXT("Scene Stats"), TAttribute<FString>::CreateLambda([this]() { return SceneStatsDisplayText; })) ]
			]
		]
	);

	FetchModelPreview(DepotFile, Rev);
	FetchInfoPanelPreview(DepotFile, WeakCard);
	FetchSceneRefs(DepotFile, Rev);
	FetchSceneStats(DepotFile, Rev);

	// Only play the slide-in animation if the panel isn't already open — switching the
	// selected asset while it's open should just update its content in place, not restart
	// the curve from 0 (which would visibly collapse the panel/results grid, then reopen it).
	const bool bAlreadyOpen = InfoPanelAnim.IsPlaying() || InfoPanelCurve.GetLerp() > 0.0f;
	if (!bAlreadyOpen)
	{
		// Mutually exclusive with "Settings" -- both are docked-right slide-in panels sharing
		// the same content-area width, so having both open at once would either overlap or (in
		// narrow layouts) fight over the same full-width takeover.
		HideSettingsPanel(/*bAnimate=*/false);
		InfoPanelAnim.Play(InfoPanelContainer.ToSharedRef());
	}
}

void FP4SearchModule::HideAssetInfoPanel(bool bAnimate)
{
	if (InfoPanelContainer.IsValid())
	{
		if (bAnimate)
		{
			InfoPanelAnim.PlayReverse(InfoPanelContainer.ToSharedRef());
		}
		else
		{
			InfoPanelAnim.Pause();
			InfoPanelAnim.JumpToStart();
		}
	}

	if (TSharedPtr<SP4SearchResultCard> PreviouslySelected = SelectedResultCard.Pin())
	{
		PreviouslySelected->SetSelected(false);
	}
	SelectedResultCard.Reset();

	// Drop the last strong ref to the embedded viewport so its FEditorViewportClient stops
	// ticking. A Slate-Collapsed widget is never ticked, so IsVisible() falls back to its
	// "never ticked yet" branch and reports true forever — without this, a hidden viewport
	// keeps rendering/ticking in the background indefinitely.
	if (InfoPanelContent.IsValid())
	{
		InfoPanelContent->SetContent(SNullWidget::NullWidget);
	}
	CurrentModelViewport.Reset();
	ModelPreviewStatus = EModelPreviewStatus::NotFound;

	// See ClearResults()'s comment — also called from ShutdownModule(), by which point the
	// UObject system may already be torn down, making RemoveFromRoot() unsafe.
	if (InfoPanelPreviewTexture && UObjectInitialized())
	{
		InfoPanelPreviewTexture->RemoveFromRoot();
		InfoPanelPreviewTexture = nullptr;
	}
	InfoPanelPreviewBrush.Reset();
}

void FP4SearchModule::ToggleSettingsPanel()
{
	if (!SettingsPanelContainer.IsValid())
	{
		return;
	}

	const bool bCurrentlyOpen = SettingsPanelAnim.IsPlaying() || SettingsPanelCurve.GetLerp() > 0.0f;
	if (bCurrentlyOpen)
	{
		SettingsPanelAnim.PlayReverse(SettingsPanelContainer.ToSharedRef());
	}
	else
	{
		// Mutually exclusive with "More Info" -- see its own Play() call site for why.
		HideAssetInfoPanel(/*bAnimate=*/false);
		SettingsPanelAnim.Play(SettingsPanelContainer.ToSharedRef());
	}
}

void FP4SearchModule::HideSettingsPanel(bool bAnimate)
{
	if (SettingsPanelContainer.IsValid())
	{
		if (bAnimate)
		{
			SettingsPanelAnim.PlayReverse(SettingsPanelContainer.ToSharedRef());
		}
		else
		{
			SettingsPanelAnim.Pause();
			SettingsPanelAnim.JumpToStart();
		}
	}
}

void FP4SearchModule::OnGlobalKeyDown(const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() != EKeys::Escape)
	{
		return;
	}

	const TSharedPtr<SDockTab> Tab = MyTab.Pin();
	if (!Tab.IsValid() || !Tab->IsForeground())
	{
		return;
	}

	const bool bInfoPanelOpen = InfoPanelAnim.IsPlaying() || InfoPanelCurve.GetLerp() > 0.0f;
	const bool bSettingsPanelOpen = SettingsPanelAnim.IsPlaying() || SettingsPanelCurve.GetLerp() > 0.0f;
	if (bInfoPanelOpen)
	{
		HideAssetInfoPanel();
	}
	else if (bSettingsPanelOpen)
	{
		HideSettingsPanel();
	}
}

void FP4SearchModule::OnEnginePreExit()
{
	HideAssetInfoPanel();
}

void FP4SearchModule::FetchModelPreview(const FString& DepotFile, int32 Rev)
{
	if (!CurrentModelViewport.IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject());
	Body->SetStringField(TEXT("depotFile"), FString::Printf(TEXT("%s#%d"), *DepotFile, Rev));
	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	auto Request = HttpRequester->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(ServerURL + TEXT("/api/v1.4/view/model"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedJWT));
	Request->SetHeader(TEXT("X-Nonce-Token"), CachedNonce);
	Request->SetContentAsString(BodyStr);

	TWeakPtr<SP4SearchModelViewport> WeakViewport = CurrentModelViewport;

	Request->OnRequestProgress64().BindLambda(
		[this, WeakViewport](FHttpRequestPtr Req, uint64 /*BytesSent*/, uint64 BytesReceived)
	{
		TSharedPtr<SP4SearchModelViewport> Viewport = WeakViewport.Pin();
		if (!Viewport.IsValid() || Viewport != CurrentModelViewport)
		{
			// Stale request for a panel that's since been closed or replaced by another asset.
			return;
		}
		ModelPreviewBytesReceived = BytesReceived;
		const FHttpResponsePtr InProgressResponse = Req.IsValid() ? Req->GetResponse() : nullptr;
		ModelPreviewContentLength = InProgressResponse.IsValid() ? InProgressResponse->GetContentLength() : 0;
	});

	Request->OnProcessRequestComplete().BindLambda(
		[this, WeakViewport](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		TSharedPtr<SP4SearchModelViewport> Viewport = WeakViewport.Pin();
		if (!Viewport.IsValid() || Viewport != CurrentModelViewport)
		{
			// Stale response for a panel that's since been closed or replaced by another asset.
			return;
		}

		const int32 ModelResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (HandleAuthFailure(ModelResponseCode))
		{
			ModelPreviewStatus = EModelPreviewStatus::NotFound;
			Viewport->SetPreviewMesh(nullptr);
			return;
		}

		if (!bSuccess || !Response.IsValid() || ModelResponseCode != 200)
		{
			// No "model" attribute (or fetch failed) — show the "not found" message.
			ModelPreviewStatus = EModelPreviewStatus::NotFound;
			Viewport->SetPreviewMesh(nullptr);
			return;
		}

		// Unique per-fetch filename — a fixed shared path would let two overlapping fetches
		// (e.g. quickly switching between assets) clobber each other's file mid-read.
		const FString GlbPath = FPaths::ProjectIntermediateDir()
			/ FString::Printf(TEXT("P4SearchModelPreview_%s.glb"), *FGuid::NewGuid().ToString());
		if (!FFileHelper::SaveArrayToFile(Response->GetContent(), *GlbPath))
		{
			ModelPreviewStatus = EModelPreviewStatus::NotFound;
			Viewport->SetPreviewMesh(nullptr);
			return;
		}

		GLTF::FAsset Asset;
		GLTF::FFileReader Reader;
		Reader.ReadFile(GlbPath, true, false, Asset);
		IFileManager::Get().Delete(*GlbPath);

		if (Asset.Meshes.Num() == 0)
		{
			ModelPreviewStatus = EModelPreviewStatus::NotFound;
			Viewport->SetPreviewMesh(nullptr);
			return;
		}

		// glTF -> UE axis conversion (Y-up right-handed -> Z-up left-handed) is already baked
		// in by GLTF::ConvertVec3, applied unconditionally to every position/normal/tangent
		// accessor AND to every node's local Transform at parse time (see engine source:
		// GLTFAccessor.cpp, GLTFReader.cpp). What's NOT accounted for by using Asset.Meshes[0]
		// directly is the glTF *scene node* that actually places this mesh — many exporters
		// (Blender's included) put a corrective rotation on the node's own local transform
		// rather than baking it into vertex data, precisely to convert their own up-axis to
		// glTF's Y-up. Skipping that node transform (as this used to, via FTransform::Identity)
		// leaves the mesh in whatever orientation it happened to be authored in — hence models
		// rendering correctly in other glTF-aware tools (which walk the full scene graph) but
		// sideways here. Find the node that references this mesh and use its accumulated
		// (already-UE-space) global transform instead.
		const FTransform MeshGlobalTransform = P4SearchCore::ComputeMeshNodeGlobalTransform(Asset.Nodes, 0);

		FMeshDescription MeshDescription;
		GLTF::FMeshFactory MeshFactory;
		MeshFactory.SetUniformScale(100.0f); // glTF meters -> UE centimeters
		MeshFactory.FillMeshDescription(Asset.Meshes[0], MeshGlobalTransform, &MeshDescription);

		bool bHasInvalidNormals = false;
		bool bHasInvalidTangents = false;
		FStaticMeshOperations::HasInvalidVertexInstanceNormalsOrTangents(MeshDescription, bHasInvalidNormals, bHasInvalidTangents);
		if (bHasInvalidNormals)
		{
			FStaticMeshOperations::ComputeTriangleTangentsAndNormals(MeshDescription, THRESH_POINTS_ARE_SAME);
			FStaticMeshOperations::ComputeTangentsAndNormals(MeshDescription,
				EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents | EComputeNTBsFlags::UseMikkTSpace);
		}
		else if (bHasInvalidTangents)
		{
			FStaticMeshOperations::ComputeMikktTangents(MeshDescription, true);
		}

		UStaticMesh* PreviewMesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		PreviewMesh->SetNumSourceModels(1);

		FStaticMeshAttributes Attributes(MeshDescription);
		TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		TArray<FStaticMaterial>& StaticMaterials = PreviewMesh->GetStaticMaterials();
		for (FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
		{
			FStaticMaterial& StaticMaterial = StaticMaterials.AddDefaulted_GetRef();
			StaticMaterial.MaterialSlotName = MaterialSlotNames[PolygonGroupID];
			StaticMaterial.MaterialInterface = nullptr;
			StaticMaterial.UVChannelData = FMeshUVChannelInfo(1.f);

			// FMeshFactory names each polygon group's material slot after the glTF material
			// index it came from (see Engine's GLTFMeshFactory.cpp) — use that to find the
			// source material's base color texture. Only works for textures bundled directly
			// in the GLB (DecodeGltfImageToTexture returns nullptr for external file/URI
			// references) — external textures are left showing the flat default material.
			const int32 GltfMaterialIndex = FCString::Atoi(*StaticMaterial.MaterialSlotName.ToString());
			if (Asset.Materials.IsValidIndex(GltfMaterialIndex))
			{
				const GLTF::FMaterial& GltfMaterial = Asset.Materials[GltfMaterialIndex];
				const int32 TextureIndex = GltfMaterial.BaseColor.TextureIndex;
				if (Asset.Textures.IsValidIndex(TextureIndex))
				{
					if (UTexture2D* BaseColorTex = DecodeGltfImageToTexture(Asset.Textures[TextureIndex].Source))
					{
						StaticMaterial.MaterialInterface = BuildBaseColorTextureMaterial(BaseColorTex);
					}
				}
			}
		}

		UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
		BuildParams.bMarkPackageDirty = false;
		BuildParams.bBuildSimpleCollision = false;
		BuildParams.bCommitMeshDescription = false;
		BuildParams.bFastBuild = true;

		TArray<const FMeshDescription*> MeshDescriptionPtrs;
		MeshDescriptionPtrs.Add(&MeshDescription);
		PreviewMesh->BuildFromMeshDescriptions(MeshDescriptionPtrs, BuildParams);

		ModelPreviewStatus = EModelPreviewStatus::Found;
		Viewport->SetPreviewMesh(PreviewMesh);
	});
	Request->ProcessRequest();
}

void FP4SearchModule::PerformLogin(const FString& Username, const FString& Password)
{
	// Refuse to send credentials over plain http to anything but the local machine — a
	// misconfigured/typo'd Server URL would otherwise silently leak the password in cleartext.
	const bool bIsHttps = ServerURL.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	const bool bIsLoopbackHttp = ServerURL.StartsWith(TEXT("http://localhost"), ESearchCase::IgnoreCase)
		|| ServerURL.StartsWith(TEXT("http://127.0.0.1"), ESearchCase::IgnoreCase);
	if (!bIsHttps && !bIsLoopbackHttp)
	{
		FNotificationInfo Info(LOCTEXT("LoginRejectedScheme",
			"Login blocked: Server URL must use https:// (plain http is only allowed for localhost)"));
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// LoginRequestModel on the backend deserializes "user"/"passwd", not "username"/"password".
	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject());
	Body->SetStringField(TEXT("user"), Username);
	Body->SetStringField(TEXT("passwd"), Password);
	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	auto Request = HttpRequester->CreateRequest();
	Request->SetVerb(TEXT("POST"));
	// v1.4 (not v1.3) so login also issues the CSRF nonce that v1.4 endpoints (e.g. view/asset) require.
	Request->SetURL(ServerURL + TEXT("/api/v1.4/login"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(BodyStr);
	Request->OnProcessRequestComplete().BindLambda(
		[this, Username](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
		FString Token;
		FString Nonce;
		if (bSuccess && ResponseCode == 200 && Response.IsValid())
		{
			// v1.4 login returns {"data": {"accessToken": "...", "refreshToken": "..."}}.
			Token = P4SearchCore::ExtractLoginAccessToken(Response->GetContentAsString());
			// The nonce is issued as a response header, not part of the JSON body.
			Nonce = Response->GetHeader(TEXT("X-Nonce-Token"));
		}

		FNotificationInfo Info(Token.IsEmpty()
			? FText::Format(LOCTEXT("LoginFailed",  "Login failed (HTTP {0})"), FText::AsNumber(ResponseCode))
			: FText::Format(LOCTEXT("LoginSuccess", "Logged in as {0}"), FText::FromString(Username)));
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);

		if (!Token.IsEmpty())
		{
			CachedJWT     = Token;
			CachedNonce   = Nonce;
			LoginUsername = Username;
			bIsLoggedIn   = true;

			// The inline login panel (shown where results normally are) gives way to a
			// real search now that we're logged in.
			ExecuteSearch();
		}
	});
	Request->ProcessRequest();
}

bool FP4SearchModule::HandleAuthFailure(int32 ResponseCode)
{
	if (ResponseCode != 401)
	{
		return false;
	}

	// bIsLoggedIn guards against every in-flight request that 401s at once (e.g. a whole
	// page of thumbnails) each independently firing their own logout + notification.
	if (bIsLoggedIn)
	{
		LogOut();

		FNotificationInfo Info(LOCTEXT("SessionExpired", "Your session has expired. Please log in again."));
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return true;
}

void FP4SearchModule::LogOut()
{
	CachedJWT.Empty();
	CachedNonce.Empty();
	bIsLoggedIn = false;

	// The inline login panel takes over the results area next repaint — clear whatever
	// was showing so stale cards don't linger underneath it.
	ClearResults();
	if (NoResultsPanel.IsValid())
	{
		NoResultsPanel->SetVisibility(EVisibility::Collapsed);
	}
}

int32 FP4SearchModule::CountMultiSelected() const
{
	return MultiSelectedAssets.Num();
}

void FP4SearchModule::ClearMultiSelection()
{
	MultiSelectedAssets.Empty();
	for (const TWeakPtr<SP4SearchResultCard>& WeakCard : AllCards)
	{
		if (TSharedPtr<SP4SearchResultCard> Card = WeakCard.Pin())
		{
			Card->SetMultiSelected(false);
		}
	}
}

void FP4SearchModule::BulkAddSelectedToProject()
{
	// Selection can include assets from pages other than the one currently on screen (see
	// MultiSelectedAssets), so this iterates the selection itself rather than AllCards —
	// matching a WeakCard where one happens to still be on screen (for the "Added" mark),
	// and an empty one otherwise (AddToProject already null-guards WeakCard.Pin()).
	// Snapshot first — AddToProject's async completion mutates the same card, and
	// SetAdded()/notifications shouldn't race with us still iterating.
	TArray<FMultiSelectedAsset> SelectedAssets;
	MultiSelectedAssets.GenerateValueArray(SelectedAssets);

	for (const FMultiSelectedAsset& Asset : SelectedAssets)
	{
		TWeakPtr<SP4SearchResultCard> WeakCard;
		for (const TWeakPtr<SP4SearchResultCard>& Candidate : AllCards)
		{
			if (TSharedPtr<SP4SearchResultCard> Card = Candidate.Pin())
			{
				if (Card->GetDepotFile() == Asset.DepotFile && Card->GetRev() == Asset.Rev)
				{
					WeakCard = Candidate;
					break;
				}
			}
		}
		AddToProject(Asset.DepotFile, Asset.Rev, WeakCard);
	}
}

void FP4SearchModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(P4SearchTabName);
}

void FP4SearchModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FP4SearchCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FP4SearchCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FP4SearchModule, P4Search)

#if WITH_AUTOMATION_TESTS

// DecodeBlurHash (declared in the anonymous namespace above) has no network/UObject/Slate
// dependencies — pure string parsing and math — making it the one piece of this plugin
// cheap and meaningful to unit test without any editor/engine bootstrapping beyond what
// running an automation test already requires.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchDecodeBlurHashTest, "P4Search.DecodeBlurHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchDecodeBlurHashTest::RunTest(const FString& Parameters)
{
	// Hand-constructed minimal valid hash: SizeFlag='0' -> NumX=NumY=1 (DC component only, no
	// AC), QuantMax='0', DC="0000" -> DecodeDC(0) = (0,0,0). With NumX=NumY=1 the DC term's
	// cosine weight (cos(0)*cos(0)) is always 1 regardless of x/y, so every pixel should come
	// out solid black — an exact, analytically-derived expectation, not a "golden" fixture.
	TArray<uint8> OutBGRA;
	const bool bDecoded = DecodeBlurHash(TEXT("000000"), 4, 4, OutBGRA);
	TestTrue(TEXT("Valid minimal hash decodes successfully"), bDecoded);
	TestEqual(TEXT("Output buffer is Width*Height*4 bytes"), OutBGRA.Num(), 4 * 4 * 4);
	for (int32 PixelIndex = 0; PixelIndex < 16; ++PixelIndex)
	{
		const int32 Base = PixelIndex * 4;
		TestEqual(TEXT("B channel is 0"), OutBGRA[Base + 0], (uint8)0);
		TestEqual(TEXT("G channel is 0"), OutBGRA[Base + 1], (uint8)0);
		TestEqual(TEXT("R channel is 0"), OutBGRA[Base + 2], (uint8)0);
		TestEqual(TEXT("Alpha channel is fully opaque"), OutBGRA[Base + 3], (uint8)255);
	}

	TArray<uint8> ShortOut;
	TestFalse(TEXT("Hash shorter than 6 chars is rejected"), DecodeBlurHash(TEXT("00"), 4, 4, ShortOut));

	TArray<uint8> ZeroDimOut;
	TestFalse(TEXT("Zero width is rejected"), DecodeBlurHash(TEXT("000000"), 0, 4, ZeroDimOut));
	TestFalse(TEXT("Negative height is rejected"), DecodeBlurHash(TEXT("000000"), 4, -1, ZeroDimOut));

	TArray<uint8> InvalidCharOut;
	TestFalse(TEXT("Hash with a non-Base83 character is rejected"), DecodeBlurHash(TEXT("00000 "), 4, 4, InvalidCharOut));

	// SizeFlag='0' implies exactly 4 + 2*1*1 = 6 characters — two extra trailing characters
	// make this malformed even though every individual character is valid Base83.
	TArray<uint8> LengthMismatchOut;
	TestFalse(TEXT("Hash length inconsistent with its SizeFlag is rejected"),
		DecodeBlurHash(TEXT("00000000"), 4, 4, LengthMismatchOut));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
