// Copyright 2026 Perforce Software, Inc. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "P4SearchModuleTestAccess.h"
#include "P4SearchFakeHttp.h"

// Exercises FP4SearchModule::SavePreviewToPerforce (the "Generate Thumbnail" save step) on a
// bare stack-local module instance via FP4SearchModuleTestAccess, with a
// FP4SearchFakeHttpRequester standing in for the real network -- same approach as
// P4SearchLoginTests.cpp.

namespace
{
	const TArray<uint8> FakeJpeg = { 0xFF, 0xD8, 0xFF, 0xD9 };

	bool BodyContains(const TArray<uint8>& Body, const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		const int32 Len = Utf8.Length();
		for (int32 Start = 0; Start + Len <= Body.Num(); ++Start)
		{
			if (FMemory::Memcmp(Body.GetData() + Start, Utf8.Get(), Len) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchSavePreviewRequestTest, "P4Search.SavePreviewToPerforce.Request",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchSavePreviewRequestTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
	FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("https://p4search.example.com"));
	FP4SearchModuleTestAccess::SetLoggedIn(Module, true);
	FP4SearchModuleTestAccess::SetCachedJWT(Module, TEXT("the-jwt"));
	FP4SearchModuleTestAccess::SetCachedNonce(Module, TEXT("the-nonce"));
	FakeRequester->QueueResponse(200, TEXT("{\"set\":[\"preview\"]}"));

	FP4SearchModuleTestAccess::SavePreviewToPerforce(Module, TEXT("//depot/art/Hero.uasset"), 7, FakeJpeg);

	if (!TestEqual(TEXT("One request is sent"), FakeRequester->CreatedRequests.Num(), 1))
	{
		return false;
	}
	const auto& Request = FakeRequester->CreatedRequests[0];
	TestEqual(TEXT("Targets /update/preview (content/attribute was never implemented server-side)"),
		Request->CapturedURL, FString(TEXT("https://p4search.example.com/api/v1.4/update/preview")));
	TestEqual(TEXT("Is a POST"), Request->CapturedVerb, FString(TEXT("POST")));
	TestEqual(TEXT("Sends the JWT"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer the-jwt")));
	TestEqual(TEXT("Sends the nonce"), Request->GetHeader(TEXT("X-Nonce-Token")), FString(TEXT("the-nonce")));

	const FString ContentType = Request->GetHeader(TEXT("Content-Type"));
	FString Boundary;
	TestTrue(TEXT("Is multipart/form-data with a boundary"),
		ContentType.Split(TEXT("multipart/form-data; boundary="), nullptr, &Boundary) && !Boundary.IsEmpty());

	const TArray<uint8>& Body = Request->CapturedBodyBytes;
	TestTrue(TEXT("Carries depotFile"), BodyContains(Body, TEXT("name=\"depotFile\"\r\n\r\n//depot/art/Hero.uasset\r\n")));
	TestTrue(TEXT("Carries rev"), BodyContains(Body, TEXT("name=\"rev\"\r\n\r\n7\r\n")));
	TestTrue(TEXT("Carries the image as the 'preview' file part"),
		BodyContains(Body, TEXT("name=\"preview\"; filename=\"Hero.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n")));
	TestTrue(TEXT("Uses the boundary from the Content-Type header"), BodyContains(Body, TEXT("--") + Boundary + TEXT("--\r\n")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchSavePreviewForbiddenTest, "P4Search.SavePreviewToPerforce.ForbiddenKeepsSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FP4SearchSavePreviewForbiddenTest::RunTest(const FString& Parameters)
{
	FP4SearchModule Module;
	TSharedRef<FP4SearchFakeHttpRequester> FakeRequester = MakeShared<FP4SearchFakeHttpRequester>();
	FP4SearchModuleTestAccess::SetHttpRequester(Module, FakeRequester);
	FP4SearchModuleTestAccess::SetServerURL(Module, TEXT("https://p4search.example.com"));
	FP4SearchModuleTestAccess::SetLoggedIn(Module, true);
	FP4SearchModuleTestAccess::SetCachedJWT(Module, TEXT("the-jwt"));
	FakeRequester->QueueResponse(403, TEXT("{\"status\":{\"code\":403,\"message\":\"No write access\"}}"));

	FP4SearchModuleTestAccess::SavePreviewToPerforce(Module, TEXT("//depot/art/Hero.uasset"), 7, FakeJpeg);

	// 403 means "no write access to this file", not an expired session -- only 401 logs out.
	TestTrue(TEXT("Still logged in after a 403"), FP4SearchModuleTestAccess::IsLoggedIn(Module));
	TestEqual(TEXT("JWT kept after a 403"), FP4SearchModuleTestAccess::GetCachedJWT(Module), FString(TEXT("the-jwt")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
