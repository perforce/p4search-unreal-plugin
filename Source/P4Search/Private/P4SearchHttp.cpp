// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4SearchHttp.h"
#include "HttpModule.h"

FHttpRequestRef FP4SearchRealHttpRequester::CreateRequest()
{
	return FHttpModule::Get().CreateRequest();
}
