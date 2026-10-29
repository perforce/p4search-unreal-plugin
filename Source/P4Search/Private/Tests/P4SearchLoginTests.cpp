// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "P4SearchModuleTestAccess.h"
#include "P4SearchFakeHttp.h"

// These exercise FP4SearchModule::PerformLogin/HandleAuthFailure on a bare, stack-local module
// instance (never StartupModule()'d, no spawned tab) via FP4SearchModuleTestAccess, with a
// FP4SearchFakeHttpRequester standing in for the real network. Safe because both functions only
// touch plain data members plus a handful of TSharedPtr widgets that are always null-checked
// before use (ResultsContainer, NoResultsPanel, InitialStatePanel) — see ExecuteSearch's and
// ClearResults' own IsValid() guards, which is what lets PerformLogin's success path safely
// chain into ExecuteSearch() here without a real widget tree.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchPerformLoginRejectsInsecureSchemeTest, "P4Search.PerformLogin.RejectsInsecureScheme",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchPerformLoginRejectsInsecureSchemeTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
	FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("http://not-localhost.example.com"));

	FP4SearchModuleTestAccess::PerformLogin(Module, TEXT("someuser"), TEXT("somepass"));

	TestEqual(TEXT("No request is sent for a non-https, non-localhost server URL"), FakeRequester->CreatedRequests.Num(), 0);
	TestFalse(TEXT("Not logged in after a rejected login attempt"), FP4SearchModuleTestAccess::IsLoggedIn(Module));
	TestTrue(TEXT("CachedJWT stays empty after a rejected login attempt"), FP4SearchModuleTestAccess::GetCachedJWT(Module).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchPerformLoginAllowsLoopbackHttpTest, "P4Search.PerformLogin.AllowsLoopbackHttp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchPerformLoginAllowsLoopbackHttpTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
	FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("http://localhost:1601"));
	FakeRequester->QueueResponse(200, TEXT("{\"data\":{\"accessToken\":\"tok\"}}"));

	FP4SearchModuleTestAccess::PerformLogin(Module, TEXT("someuser"), TEXT("somepass"));

	TestEqual(TEXT("Plain http is allowed for localhost"), FakeRequester->CreatedRequests.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchPerformLoginSuccessTest, "P4Search.PerformLogin.Success",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchPerformLoginSuccessTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
	FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("https://p4search.example.com"));
	FakeRequester->QueueResponse(200, TEXT("{\"data\":{\"accessToken\":\"the-jwt\"}}"), {{TEXT("X-Nonce-Token"), TEXT("the-nonce")}});

	FP4SearchModuleTestAccess::PerformLogin(Module, TEXT("alice"), TEXT("secret"));

	TestTrue(TEXT("A request was sent to /api/v1.4/login"),
		FakeRequester->CreatedRequests.Num() == 1 && FakeRequester->CreatedRequests[0]->CapturedURL.Contains(TEXT("/api/v1.4/login")));
	TestEqual(TEXT("Login is a POST"), FakeRequester->CreatedRequests[0]->CapturedVerb, FString(TEXT("POST")));
	TestTrue(TEXT("Logged in after a successful response"), FP4SearchModuleTestAccess::IsLoggedIn(Module));
	TestEqual(TEXT("CachedJWT is the access token from the response body"), FP4SearchModuleTestAccess::GetCachedJWT(Module), FString(TEXT("the-jwt")));
	TestEqual(TEXT("CachedNonce is the X-Nonce-Token response header"), FP4SearchModuleTestAccess::GetCachedNonce(Module), FString(TEXT("the-nonce")));
	TestEqual(TEXT("LoginUsername is recorded"), FP4SearchModuleTestAccess::GetLoginUsername(Module), FString(TEXT("alice")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchPerformLoginFailureTest, "P4Search.PerformLogin.Failure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchPerformLoginFailureTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
	FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("https://p4search.example.com"));
	FakeRequester->QueueResponse(401, TEXT("{\"status\":{\"message\":\"bad credentials\"}}"));

	FP4SearchModuleTestAccess::PerformLogin(Module, TEXT("alice"), TEXT("wrong"));

	TestFalse(TEXT("Not logged in after a 401 response"), FP4SearchModuleTestAccess::IsLoggedIn(Module));
	TestTrue(TEXT("CachedJWT stays empty after a failed login"), FP4SearchModuleTestAccess::GetCachedJWT(Module).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchHandleAuthFailureTest, "P4Search.HandleAuthFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchHandleAuthFailureTest::RunTest(const FString& Parameters)
{
	{
		FP4SearchModule Module;
		TestFalse(TEXT("Non-401 codes are not treated as an auth failure"), FP4SearchModuleTestAccess::HandleAuthFailure(Module, 500));
	}
	{
		FP4SearchModule Module;
		FP4SearchModuleTestAccess::SetLoggedIn(Module, true);
		FP4SearchModuleTestAccess::SetCachedJWT(Module, TEXT("some-jwt"));
		FP4SearchModuleTestAccess::SetCachedNonce(Module, TEXT("some-nonce"));

		const bool bHandled = FP4SearchModuleTestAccess::HandleAuthFailure(Module, 401);

		TestTrue(TEXT("401 is treated as an auth failure"), bHandled);
		TestFalse(TEXT("A 401 while logged in logs the module out"), FP4SearchModuleTestAccess::IsLoggedIn(Module));
		TestTrue(TEXT("CachedJWT is cleared on logout"), FP4SearchModuleTestAccess::GetCachedJWT(Module).IsEmpty());
		TestTrue(TEXT("CachedNonce is cleared on logout"), FP4SearchModuleTestAccess::GetCachedNonce(Module).IsEmpty());

		// A second, already-logged-out 401 (e.g. from another in-flight request that also
		// 401'd) must still report "handled" without crashing on the repeat LogOut().
		TestTrue(TEXT("A repeat 401 after logout is still reported as handled"),
			FP4SearchModuleTestAccess::HandleAuthFailure(Module, 401));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
