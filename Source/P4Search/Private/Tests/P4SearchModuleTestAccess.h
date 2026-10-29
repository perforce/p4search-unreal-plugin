// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#pragma once

#if WITH_AUTOMATION_TESTS

#include "P4Search.h"
#include "P4SearchHttp.h"

// Thin friend-based accessor exposing FP4SearchModule's private, HTTP-driving methods and
// state to automation tests, without loosening its production API (everything it forwards to
// stays private). Gated by WITH_AUTOMATION_TESTS so it never compiles into a shipping build.
class FP4SearchModuleTestAccess
{
public:
	static void SetHttpRequester(FP4SearchModule& Module, TSharedRef<IP4SearchHttpRequester> Requester)
	{
		Module.HttpRequester = Requester;
	}

	static void SetServerURL(FP4SearchModule& Module, const FString& InServerURL) { Module.ServerURL = InServerURL; }

	static void PerformLogin(FP4SearchModule& Module, const FString& Username, const FString& Password)
	{
		Module.PerformLogin(Username, Password);
	}

	static bool HandleAuthFailure(FP4SearchModule& Module, int32 ResponseCode)
	{
		return Module.HandleAuthFailure(ResponseCode);
	}

	static void SetLoggedIn(FP4SearchModule& Module, bool bValue) { Module.bIsLoggedIn = bValue; }
	static void SetCachedJWT(FP4SearchModule& Module, const FString& InJWT) { Module.CachedJWT = InJWT; }
	static void SetCachedNonce(FP4SearchModule& Module, const FString& InNonce) { Module.CachedNonce = InNonce; }

	static bool IsLoggedIn(const FP4SearchModule& Module) { return Module.bIsLoggedIn; }
	static const FString& GetCachedJWT(const FP4SearchModule& Module) { return Module.CachedJWT; }
	static const FString& GetCachedNonce(const FP4SearchModule& Module) { return Module.CachedNonce; }
	static const FString& GetLoginUsername(const FP4SearchModule& Module) { return Module.LoginUsername; }

	// Search-request accessors. Note ExecuteSearch early-returns unless bIsLoggedIn, so tests
	// must call SetLoggedIn(Module, true) first. A stack-local FP4SearchModule has never run
	// StartupModule, so SortOptions is empty and SelectedSortOption is null -- set the sort
	// via SetSelectedSortOption rather than expecting a default.
	static void ExecuteSearch(FP4SearchModule& Module) { Module.ExecuteSearch(); }
	static void TriggerImmediateSearch(FP4SearchModule& Module) { Module.TriggerImmediateSearch(); }

	static void SetSearchSize(FP4SearchModule& Module, int32 InSize) { Module.SearchSize = InSize; }
	static void SetCurrentPage(FP4SearchModule& Module, int32 InPage) { Module.CurrentPage = InPage; }
	static int32 GetCurrentPage(const FP4SearchModule& Module) { return Module.CurrentPage; }

	static void SetSelectedSortOption(FP4SearchModule& Module, TSharedPtr<FString> InOption)
	{
		Module.SelectedSortOption = InOption;
	}
};

#endif // WITH_AUTOMATION_TESTS
