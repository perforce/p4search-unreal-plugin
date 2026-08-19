// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4SearchCommands.h"

#define LOCTEXT_NAMESPACE "FP4SearchModule"

void FP4SearchCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "P4Search", "Bring up P4Search window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
