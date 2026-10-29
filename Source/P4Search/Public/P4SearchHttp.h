// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

// Seam between FP4SearchModule's HTTP call sites and the engine's HTTP module — every
// FHttpModule::Get().CreateRequest() call in P4Search.cpp goes through an
// IP4SearchHttpRequester instead, so automation tests can substitute a fake requester (see
// Tests/P4SearchFakeHttp.h) and drive the module's network-calling functions without a real
// network round trip.
class IP4SearchHttpRequester
{
public:
	virtual ~IP4SearchHttpRequester() = default;
	virtual FHttpRequestRef CreateRequest() = 0;
};

// Default, production requester — thin passthrough to FHttpModule.
class FP4SearchRealHttpRequester : public IP4SearchHttpRequester
{
public:
	virtual FHttpRequestRef CreateRequest() override;
};
