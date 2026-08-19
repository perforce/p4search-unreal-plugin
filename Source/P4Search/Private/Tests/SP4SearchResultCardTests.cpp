// Copyright Perforce Software, Inc. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SP4SearchResultCard.h"

// Editor-context Slate widget tests: construct SP4SearchResultCard directly (no tab, no
// results grid, no HTTP) and drive it through its public API, the same way any other
// self-contained Slate widget is unit-tested. A true level/Actor-based "Functional Test" doesn't
// apply here — this card has no gameplay/world footprint — so this is the idiomatic UE
// equivalent for Slate: EditorContext automation tests instantiating the widget in isolation.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchResultCardConstructionTest, "P4Search.SP4SearchResultCard.Construction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchResultCardConstructionTest::RunTest(const FString& Parameters)
{
	TSharedRef<SP4SearchResultCard> Card = SNew(SP4SearchResultCard)
		.FileName(TEXT("thing.uasset"))
		.DepotFile(TEXT("//depot/main/thing.uasset"))
		.Rev(7)
		.bIsHead(true);

	TestEqual(TEXT("GetCardFileName returns the constructed FileName"), Card->GetCardFileName(), FString(TEXT("thing.uasset")));
	TestEqual(TEXT("GetDepotFile returns the constructed DepotFile"), Card->GetDepotFile(), FString(TEXT("//depot/main/thing.uasset")));
	TestEqual(TEXT("GetRev returns the constructed Rev"), Card->GetRev(), 7);
	TestFalse(TEXT("A freshly constructed card is not multi-selected"), Card->IsMultiSelected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchResultCardMultiSelectTest, "P4Search.SP4SearchResultCard.MultiSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchResultCardMultiSelectTest::RunTest(const FString& Parameters)
{
	TSharedRef<SP4SearchResultCard> Card = SNew(SP4SearchResultCard)
		.FileName(TEXT("thing.png"))
		.DepotFile(TEXT("//depot/main/thing.png"))
		.Rev(1)
		.bIsHead(true);

	TArray<bool> ObservedStates;
	Card->SetOnMultiSelectChanged(SP4SearchResultCard::FOnMultiSelectChanged::CreateLambda(
		[&ObservedStates](bool bSelected) { ObservedStates.Add(bSelected); }));

	Card->SetMultiSelected(true);
	TestTrue(TEXT("IsMultiSelected reflects SetMultiSelected(true)"), Card->IsMultiSelected());

	Card->SetMultiSelected(false);
	TestFalse(TEXT("IsMultiSelected reflects SetMultiSelected(false)"), Card->IsMultiSelected());

	TestEqual(TEXT("OnMultiSelectChanged fires once per actual state change"), ObservedStates.Num(), 2);
	if (ObservedStates.Num() == 2)
	{
		TestTrue(TEXT("First callback reports the selected state"), ObservedStates[0]);
		TestFalse(TEXT("Second callback reports the deselected state"), ObservedStates[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchResultCardActionStateTest, "P4Search.SP4SearchResultCard.ActionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchResultCardActionStateTest::RunTest(const FString& Parameters)
{
	TSharedRef<SP4SearchResultCard> Card = SNew(SP4SearchResultCard)
		.FileName(TEXT("thing.fbx"))
		.DepotFile(TEXT("//depot/main/thing.fbx"))
		.Rev(2)
		.bIsHead(false);

	// None of these have observable getters on the card itself (they drive button
	// enabled-state/visibility internally) — the test's value is that none of them crash when
	// called in whatever order a real download can call them, including before/after each other
	// and on a card that was never shown a thumbnail.
	Card->SetSelected(true);
	Card->SetSelected(false);
	Card->ShowGenerateThumbnailButton();
	Card->SetDownloadProgress(0.5f);
	Card->SetDownloadProgress(TOptional<float>());
	Card->SetDownloadIdle();
	Card->SetAdded();
	Card->SetGenerated();

	TestTrue(TEXT("Card survives the full action-state call sequence without crashing"), true);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
