// Copyright Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "P4SearchStyle.h"

class FP4SearchCommands : public TCommands<FP4SearchCommands>
{
public:

	FP4SearchCommands()
		: TCommands<FP4SearchCommands>(TEXT("P4Search"), NSLOCTEXT("Contexts", "P4Search", "P4Search Plugin"), NAME_None, FP4SearchStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};