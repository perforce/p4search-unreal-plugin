// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "P4SearchSettings.generated.h"

/** Editor Preferences > Plugins > P4Search */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "P4Search"))
class UP4SearchSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UP4SearchSettings();

	/** When enabled, any .uasset search result missing a thumbnail is silently generated
	 *  and saved back to Perforce (same as clicking "Generate Thumbnail" manually). */
	UPROPERTY(config, EditAnywhere, Category = "Thumbnails")
	bool bAutoGenerateThumbnails = true;
};
