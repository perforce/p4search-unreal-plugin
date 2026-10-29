// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "P4SearchCore.h"
#include "P4SearchModuleTestAccess.h"
#include "P4SearchFakeHttp.h"

// Exercises the sort combo's effect on the /search/dsl request FP4SearchModule::ExecuteSearch
// builds, on a bare stack-local module instance via FP4SearchModuleTestAccess, with a
// FP4SearchFakeHttpRequester standing in for the real network -- same approach and the same
// safety argument as P4SearchLoginTests.cpp (ExecuteSearch null-checks every widget it
// touches, so no spawned tab is needed).
//
// Two things a stack-local module needs that StartupModule would otherwise have done:
// ExecuteSearch early-returns unless bIsLoggedIn, and SortOptions/SelectedSortOption are
// empty, so the sort has to be set explicitly.

namespace
{
	// Stands up a logged-in module pointed at a fake requester, with the given sort label
	// selected. Returns the label holder so it stays alive for the duration of the test --
	// SelectedSortOption is a TSharedPtr the module does not own a second reference to.
	TSharedPtr<FString> SetUpModule(FP4SearchModule& Module, TSharedRef<FP4SearchFakeHttpRequester> FakeRequester,
		const FString& SortLabel)
	{
		FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
		FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("https://p4search.example.com"));
		FP4SearchModuleTestAccess::SetLoggedIn(Module, true);
		FP4SearchModuleTestAccess::SetSearchSize(Module, 20);

		TSharedPtr<FString> Option = MakeShareable(new FString(SortLabel));
		FP4SearchModuleTestAccess::SetSelectedSortOption(Module, Option);
		return Option;
	}

	// Guards against these tests passing with labels the combo no longer offers: the label
	// literals below must still be real entries in GetSortOptionLabels(), or renaming one
	// would leave the tests green while the UI stopped producing that sort.
	bool IsRealSortLabel(const FString& Label)
	{
		return P4SearchCore::GetSortOptionLabels().Contains(Label);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExecuteSearchIncludesSortParamTest,
	"P4Search.ExecuteSearch.IncludesSortParam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchExecuteSearchIncludesSortParamTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	TestTrue(TEXT("'Largest' is still a real combo label"), IsRealSortLabel(TEXT("Largest")));
	TSharedPtr<FString> Option = SetUpModule(Module, FakeRequester, TEXT("Largest"));

	FP4SearchModuleTestAccess::ExecuteSearch(Module);

	TestEqual(TEXT("One search request was sent"), FakeRequester->CreatedRequests.Num(), 1);
	const FString& URL = FakeRequester->CreatedRequests[0]->CapturedURL;
	TestTrue(TEXT("Request targets /search/dsl"), URL.Contains(TEXT("/api/v1.3/search/dsl")));
	// The ':' is URL-encoded, matching how the DSL query itself is encoded two lines above it
	TestTrue(FString::Printf(TEXT("URL carries the encoded sort parameter: %s"), *URL),
		URL.Contains(TEXT("&sort=fileSize%3Adesc")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExecuteSearchOmitsSortParamForRelevanceTest,
	"P4Search.ExecuteSearch.OmitsSortParamForRelevance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchExecuteSearchOmitsSortParamForRelevanceTest::RunTest(const FString& Parameters)
{
	{
		FP4SearchModule Module;
		TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
		TestTrue(TEXT("'Relevance' is still a real combo label"), IsRealSortLabel(TEXT("Relevance")));
		TSharedPtr<FString> Option = SetUpModule(Module, FakeRequester, TEXT("Relevance"));

		FP4SearchModuleTestAccess::ExecuteSearch(Module);

		TestEqual(TEXT("One search request was sent"), FakeRequester->CreatedRequests.Num(), 1);
		// Not merely absent-and-empty: the parameter must not appear at all, so the default
		// request stays byte-identical to a client from before this feature existed.
		TestFalse(TEXT("Relevance sends no sort parameter"),
			FakeRequester->CreatedRequests[0]->CapturedURL.Contains(TEXT("sort=")));
	}
	{
		// A null SelectedSortOption (module never initialised) must behave as relevance rather
		// than crashing, mirroring how SelectedExtension is guarded in the same function.
		FP4SearchModule Module;
		TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
		FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
		FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("https://p4search.example.com"));
		FP4SearchModuleTestAccess::SetLoggedIn(Module, true);
		FP4SearchModuleTestAccess::SetSearchSize(Module, 20);

		FP4SearchModuleTestAccess::ExecuteSearch(Module);

		TestEqual(TEXT("A request is still sent with no sort selected"), FakeRequester->CreatedRequests.Num(), 1);
		TestFalse(TEXT("No sort parameter when SelectedSortOption is null"),
			FakeRequester->CreatedRequests[0]->CapturedURL.Contains(TEXT("sort=")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchSortChangeResetsToFirstPageTest,
	"P4Search.ExecuteSearch.SortChangeResetsPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchSortChangeResetsToFirstPageTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	TestTrue(TEXT("'Newest Change' is still a real combo label"), IsRealSortLabel(TEXT("Newest Change")));
	TSharedPtr<FString> Option = SetUpModule(Module, FakeRequester, TEXT("Newest Change"));

	// Simulate the user having paged forward before changing the sort
	FP4SearchModuleTestAccess::SetCurrentPage(Module, 3);

	// What the combo's OnSelectionChanged handler calls. TriggerImmediateSearch (not
	// TriggerDebouncedSearch) is deliberate: it searches synchronously, so no ticker pump is
	// needed here, and it is the correct choice for a discrete selection in production too.
	FP4SearchModuleTestAccess::TriggerImmediateSearch(Module);

	TestEqual(TEXT("Changing the sort resets to the first page"),
		FP4SearchModuleTestAccess::GetCurrentPage(Module), 0);
	TestEqual(TEXT("One search request was sent"), FakeRequester->CreatedRequests.Num(), 1);

	const FString& URL = FakeRequester->CreatedRequests[0]->CapturedURL;
	// Stale results would otherwise be requested from an offset that no longer makes sense
	TestTrue(FString::Printf(TEXT("Request starts from offset 0: %s"), *URL), URL.Contains(TEXT("&from=0")));
	TestTrue(TEXT("Request carries the new sort"), URL.Contains(TEXT("&sort=change%3Adesc")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
