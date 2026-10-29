// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4SearchCore.h"
#include "Misc/AutomationTest.h"
#include "GLTFAsset.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchFormatFileSizeTest, "P4Search.FormatFileSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchFormatFileSizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 bytes"), P4SearchCore::FormatFileSize(0), TEXT("0 B"));
	TestEqual(TEXT("Just under 1 KB stays bytes"), P4SearchCore::FormatFileSize(1023), TEXT("1023 B"));
	TestEqual(TEXT("Exactly 1 KB"), P4SearchCore::FormatFileSize(1024), TEXT("1.0 KB"));
	TestEqual(TEXT("Exactly 1 MB"), P4SearchCore::FormatFileSize(1024 * 1024), TEXT("1.0 MB"));
	TestEqual(TEXT("Fractional MB"), P4SearchCore::FormatFileSize(2621440), TEXT("2.5 MB")); // 2.5 * 1024 * 1024
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchBuildSortParamTest, "P4Search.BuildSortParam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchBuildSortParamTest::RunTest(const FString& Parameters)
{
	// "Relevance" must stay the first label: it is the combo's default, and mapping it to an
	// empty parameter is what keeps the out-of-the-box request identical to a pre-sort client.
	const TArray<FString>& Labels = P4SearchCore::GetSortOptionLabels();
	TestEqual(TEXT("Relevance is the first (default) label"), Labels[0], FString(TEXT("Relevance")));
	TestEqual(TEXT("Relevance means no sort parameter"), P4SearchCore::BuildSortParam(TEXT("Relevance")), FString());

	TestEqual(TEXT("Newest Change"), P4SearchCore::BuildSortParam(TEXT("Newest Change")), FString(TEXT("change:desc")));
	TestEqual(TEXT("Oldest Change"), P4SearchCore::BuildSortParam(TEXT("Oldest Change")), FString(TEXT("change:asc")));
	TestEqual(TEXT("Largest"), P4SearchCore::BuildSortParam(TEXT("Largest")), FString(TEXT("fileSize:desc")));
	TestEqual(TEXT("Smallest"), P4SearchCore::BuildSortParam(TEXT("Smallest")), FString(TEXT("fileSize:asc")));
	TestEqual(TEXT("Name A-Z"), P4SearchCore::BuildSortParam(TEXT("Name A-Z")), FString(TEXT("fileName:asc")));
	TestEqual(TEXT("Name Z-A"), P4SearchCore::BuildSortParam(TEXT("Name Z-A")), FString(TEXT("fileName:desc")));

	// A stale label persisted by an older build must fall back to relevance rather than
	// producing a malformed parameter the endpoint would reject with a 400.
	TestEqual(TEXT("Unrecognized label falls back to no sort"), P4SearchCore::BuildSortParam(TEXT("Bogus")), FString());
	TestEqual(TEXT("Empty label falls back to no sort"), P4SearchCore::BuildSortParam(TEXT("")), FString());

	// Every label must map to something the endpoint accepts: the friendly alias, never a raw
	// Elasticsearch path (the service rejects e.g. "fileName.keyword" with a 400).
	for (const FString& Label : Labels)
	{
		const FString Param = P4SearchCore::BuildSortParam(Label);
		TestFalse(FString::Printf(TEXT("'%s' does not use a raw ES path"), *Label), Param.Contains(TEXT(".keyword")));
		if (!Param.IsEmpty())
		{
			TestTrue(FString::Printf(TEXT("'%s' specifies a direction"), *Label),
				Param.EndsWith(TEXT(":asc")) || Param.EndsWith(TEXT(":desc")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchBuildSearchDslQueryTest, "P4Search.BuildSearchDslQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchBuildSearchDslQueryTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No filters set at all"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT(""), TEXT(""), 0, 0, false),
		TEXT("+model: asset"));

	TestEqual(TEXT("Head-only appends +rev: latest"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT(""), TEXT(""), 0, 0, true),
		TEXT("+model: asset +rev: latest"));

	TestEqual(TEXT("Extension is lowercased"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("FBX"), TEXT(""), TEXT(""), 0, 0, false),
		TEXT("+model: asset +ext:fbx"));

	TestEqual(TEXT("\"ALL3D\" pseudo-option expands to every 3D extension, unprefixed so ES ORs them"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("ALL3D"), TEXT(""), TEXT(""), 0, 0, false),
		TEXT("+model: asset ext:fbx obj glb usd abc"));

	TestEqual(TEXT("\"ALL2D\" pseudo-option expands to every 2D extension, unprefixed so ES ORs them"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("ALL2D"), TEXT(""), TEXT(""), 0, 0, false),
		TEXT("+model: asset ext:jpeg png bmp"));

	TestEqual(TEXT("\"ALLAUDIO\" pseudo-option expands to every audio extension, unprefixed so ES ORs them"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("ALLAUDIO"), TEXT(""), TEXT(""), 0, 0, false),
		TEXT("+model: asset ext:wav mp3 ogg flac aif opus"));

	TestEqual(TEXT("Depot path prefixes the query"),
		P4SearchCore::BuildSearchDslQuery(TEXT("//depot/foo"), TEXT("*"), TEXT(""), TEXT(""), 0, 0, false),
		TEXT("+depotPath: //depot/foo +model: asset"));

	TestEqual(TEXT("Search bar term uses the \"text\" DSL field"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT("cat"), TEXT(""), 0, 0, false),
		TEXT("+model: asset +text: cat"));

	TestEqual(TEXT("\"*\" detected value is treated as no filter"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT("*"), TEXT(""), 0, 0, false),
		TEXT("+model: asset"));

	TestEqual(TEXT("Size upper bound only gets an explicit 0MB lower bound"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT(""), TEXT(""), 0, 50, false),
		TEXT("+model: asset +size: 0MB 50MB"));

	TestEqual(TEXT("Both size bounds set"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT(""), TEXT(""), 10, 50, false),
		TEXT("+model: asset +size: 10MB 50MB"));

	TestEqual(TEXT("Date filter"),
		P4SearchCore::BuildSearchDslQuery(TEXT(""), TEXT("*"), TEXT(""), TEXT("pastWeek"), 0, 0, false),
		TEXT("+model: asset +date: pastWeek"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchParseSearchHitTest, "P4Search.ParseSearchHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchParseSearchHitTest::RunTest(const FString& Parameters)
{
	{
		TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("depotFile"), TEXT("//depot/main/art/logo.png"));
		Source->SetStringField(TEXT("fileName"), TEXT("logo.png"));
		Source->SetNumberField(TEXT("rev"), 3);
		Source->SetNumberField(TEXT("change"), 12345);
		Source->SetNumberField(TEXT("fileSize"), 2048);
		Source->SetBoolField(TEXT("head"), false);

		TArray<TSharedPtr<FJsonValue>> Detected;
		TSharedRef<FJsonObject> Tag1 = MakeShared<FJsonObject>();
		Tag1->SetStringField(TEXT("tag"), TEXT("cat"));
		Detected.Add(MakeShared<FJsonValueObject>(Tag1));
		TSharedRef<FJsonObject> Tag2 = MakeShared<FJsonObject>();
		Tag2->SetStringField(TEXT("tag"), TEXT("animal"));
		Detected.Add(MakeShared<FJsonValueObject>(Tag2));
		Source->SetArrayField(TEXT("detected"), Detected);

		const P4SearchCore::FSearchHit Hit = P4SearchCore::ParseSearchHit(Source);
		TestEqual(TEXT("DepotFile"), Hit.DepotFile, TEXT("//depot/main/art/logo.png"));
		TestEqual(TEXT("FileName"), Hit.FileName, TEXT("logo.png"));
		TestEqual(TEXT("Rev"), Hit.Rev, 3);
		TestEqual(TEXT("Change"), Hit.Change, 12345);
		TestEqual(TEXT("FileSizeBytes"), Hit.FileSizeBytes, (int64)2048);
		TestEqual(TEXT("FileSizeStr"), Hit.FileSizeStr, TEXT("2.0 KB"));
		TestFalse(TEXT("bIsHead honors an explicit false"), Hit.bIsHead);
		TestEqual(TEXT("DetectedTagsStr joins multiple tags"), Hit.DetectedTagsStr, TEXT("cat, animal"));
	}

	{
		// Absent "head" and "detected" fields fall back to defaults rather than failing.
		TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("depotFile"), TEXT("//depot/main/art/other.png"));
		Source->SetStringField(TEXT("fileName"), TEXT("other.png"));
		Source->SetNumberField(TEXT("rev"), 1);
		Source->SetNumberField(TEXT("change"), 1);
		Source->SetNumberField(TEXT("fileSize"), 0);

		const P4SearchCore::FSearchHit Hit = P4SearchCore::ParseSearchHit(Source);
		TestTrue(TEXT("bIsHead defaults to true when absent"), Hit.bIsHead);
		TestEqual(TEXT("DetectedTagsStr is empty when absent"), Hit.DetectedTagsStr, TEXT(""));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExtractTotalResultsTest, "P4Search.ExtractTotalResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchExtractTotalResultsTest::RunTest(const FString& Parameters)
{
	{
		TSharedRef<FJsonObject> Total = MakeShared<FJsonObject>();
		Total->SetNumberField(TEXT("value"), 42);
		TSharedRef<FJsonObject> Hits = MakeShared<FJsonObject>();
		Hits->SetObjectField(TEXT("total"), Total);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetObjectField(TEXT("hits"), Hits);
		TestEqual(TEXT("Well-formed response"), P4SearchCore::ExtractTotalResults(Root), 42);
	}

	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TestEqual(TEXT("Missing \"hits\" entirely"), P4SearchCore::ExtractTotalResults(Root), 0);
	}

	{
		TSharedRef<FJsonObject> Hits = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetObjectField(TEXT("hits"), Hits);
		TestEqual(TEXT("\"hits\" present but missing \"total\""), P4SearchCore::ExtractTotalResults(Root), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExtractJsonStatusMessageTest, "P4Search.ExtractJsonStatusMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchExtractJsonStatusMessageTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Well-formed status envelope"),
		P4SearchCore::ExtractJsonStatusMessage(TEXT("{\"status\":{\"code\":400,\"message\":\"Bad request\"}}")),
		TEXT("Bad request"));

	TestEqual(TEXT("Not JSON at all"), P4SearchCore::ExtractJsonStatusMessage(TEXT("not json")), TEXT(""));

	TestEqual(TEXT("Valid JSON but no \"status\" key"),
		P4SearchCore::ExtractJsonStatusMessage(TEXT("{\"foo\":\"bar\"}")), TEXT(""));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchParseRibbonMetaTest, "P4Search.ParseRibbonMeta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchParseRibbonMetaTest::RunTest(const FString& Parameters)
{
	{
		TSharedRef<FJsonObject> Sprite0 = MakeShared<FJsonObject>();
		Sprite0->SetNumberField(TEXT("x"), 0);
		Sprite0->SetNumberField(TEXT("y"), 0);
		TSharedRef<FJsonObject> Sprite1 = MakeShared<FJsonObject>();
		Sprite1->SetNumberField(TEXT("x"), 64);
		Sprite1->SetNumberField(TEXT("y"), 0);

		TSharedRef<FJsonObject> MetaJson = MakeShared<FJsonObject>();
		MetaJson->SetNumberField(TEXT("spriteWidth"), 64);
		MetaJson->SetNumberField(TEXT("spriteHeight"), 36);
		TArray<TSharedPtr<FJsonValue>> Sprites;
		Sprites.Add(MakeShared<FJsonValueObject>(Sprite0));
		Sprites.Add(MakeShared<FJsonValueObject>(Sprite1));
		MetaJson->SetArrayField(TEXT("sprites"), Sprites);

		const TOptional<P4SearchCore::FRibbonMeta> Meta = P4SearchCore::ParseRibbonMeta(MetaJson);
		TestTrue(TEXT("Well-formed meta parses successfully"), Meta.IsSet());
		if (Meta.IsSet())
		{
			TestEqual(TEXT("SpriteWidth"), Meta->SpriteWidth, 64);
			TestEqual(TEXT("SpriteHeight"), Meta->SpriteHeight, 36);
			TestEqual(TEXT("Frame count"), Meta->Frames.Num(), 2);
			TestEqual(TEXT("Second frame's X"), Meta->Frames[1].X, 64);
		}
	}

	{
		// spriteWidth/Height missing (defaults to 0 via GetNumberField) is rejected.
		TSharedRef<FJsonObject> MetaJson = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Sprites;
		TSharedRef<FJsonObject> Sprite0 = MakeShared<FJsonObject>();
		Sprite0->SetNumberField(TEXT("x"), 0);
		Sprite0->SetNumberField(TEXT("y"), 0);
		Sprites.Add(MakeShared<FJsonValueObject>(Sprite0));
		MetaJson->SetArrayField(TEXT("sprites"), Sprites);
		TestFalse(TEXT("Zero spriteWidth/Height is rejected"), P4SearchCore::ParseRibbonMeta(MetaJson).IsSet());
	}

	{
		// "sprites" present but empty is rejected.
		TSharedRef<FJsonObject> MetaJson = MakeShared<FJsonObject>();
		MetaJson->SetNumberField(TEXT("spriteWidth"), 64);
		MetaJson->SetNumberField(TEXT("spriteHeight"), 36);
		MetaJson->SetArrayField(TEXT("sprites"), TArray<TSharedPtr<FJsonValue>>());
		TestFalse(TEXT("Empty sprites array is rejected"), P4SearchCore::ParseRibbonMeta(MetaJson).IsSet());
	}

	{
		// "sprites" missing entirely is rejected.
		TSharedRef<FJsonObject> MetaJson = MakeShared<FJsonObject>();
		MetaJson->SetNumberField(TEXT("spriteWidth"), 64);
		MetaJson->SetNumberField(TEXT("spriteHeight"), 36);
		TestFalse(TEXT("Missing sprites array is rejected"), P4SearchCore::ParseRibbonMeta(MetaJson).IsSet());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchComputeRibbonFrameUVsTest, "P4Search.ComputeRibbonFrameUVs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchComputeRibbonFrameUVsTest::RunTest(const FString& Parameters)
{
	TArray<P4SearchCore::FRibbonFrameRect> Frames;
	Frames.Add({0, 0});
	Frames.Add({64, 0});

	// A 128x36 filmstrip of two 64x36 frames side by side.
	const TArray<FBox2f> FrameUVs = P4SearchCore::ComputeRibbonFrameUVs(Frames, 64, 36, 128, 36);
	TestEqual(TEXT("Two frames produce two UV rects"), FrameUVs.Num(), 2);

	TestEqual(TEXT("Frame 0 Min.X"), FrameUVs[0].Min.X, 0.0f);
	TestEqual(TEXT("Frame 0 Min.Y"), FrameUVs[0].Min.Y, 0.0f);
	TestEqual(TEXT("Frame 0 Max.X"), FrameUVs[0].Max.X, 0.5f);
	TestEqual(TEXT("Frame 0 Max.Y"), FrameUVs[0].Max.Y, 1.0f);

	TestEqual(TEXT("Frame 1 Min.X"), FrameUVs[1].Min.X, 0.5f);
	TestEqual(TEXT("Frame 1 Max.X"), FrameUVs[1].Max.X, 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchFormatSceneRefsTest, "P4Search.FormatSceneRefs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchFormatSceneRefsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Empty array is \"None\""), P4SearchCore::FormatSceneRefs(TArray<TSharedPtr<FJsonValue>>()), TEXT("None"));

	{
		TArray<TSharedPtr<FJsonValue>> RefsArray;
		RefsArray.Add(MakeShared<FJsonValueString>(TEXT("//depot/main/scenes/hero.usd")));
		TestEqual(TEXT("A plain string entry is used directly"),
			P4SearchCore::FormatSceneRefs(RefsArray), TEXT("//depot/main/scenes/hero.usd"));
	}

	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), TEXT("//depot/main/scenes/level.usd"));
		TArray<TSharedPtr<FJsonValue>> RefsArray;
		RefsArray.Add(MakeShared<FJsonValueObject>(Obj));
		TestEqual(TEXT("An object entry tries known key names"),
			P4SearchCore::FormatSceneRefs(RefsArray), TEXT("//depot/main/scenes/level.usd"));
	}

	{
		TArray<TSharedPtr<FJsonValue>> RefsArray;
		RefsArray.Add(MakeShared<FJsonValueString>(TEXT("a")));
		RefsArray.Add(MakeShared<FJsonValueString>(TEXT("b")));
		TestEqual(TEXT("Multiple entries are comma-joined"), P4SearchCore::FormatSceneRefs(RefsArray), TEXT("a, b"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchFormatSceneStatsTest, "P4Search.FormatSceneStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchFormatSceneStatsTest::RunTest(const FString& Parameters)
{
	{
		TSharedRef<FJsonObject> StatsJson = MakeShared<FJsonObject>();
		TestEqual(TEXT("No fields set is \"None\""), P4SearchCore::FormatSceneStats(StatsJson), TEXT("None"));
	}

	{
		TSharedRef<FJsonObject> StatsJson = MakeShared<FJsonObject>();
		StatsJson->SetNumberField(TEXT("triangles"), 500);
		StatsJson->SetNumberField(TEXT("vertices"), 0); // zero fields are omitted
		StatsJson->SetNumberField(TEXT("materials"), 3);
		TestEqual(TEXT("Only non-zero fields, in fixed display order"),
			P4SearchCore::FormatSceneStats(StatsJson), TEXT("500 triangles, 3 materials"));
	}

	{
		// "objects" is deliberately not one of the recognized fields (deprecated).
		TSharedRef<FJsonObject> StatsJson = MakeShared<FJsonObject>();
		StatsJson->SetNumberField(TEXT("objects"), 42);
		TestEqual(TEXT("Deprecated \"objects\" field is ignored"), P4SearchCore::FormatSceneStats(StatsJson), TEXT("None"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExtractLoginAccessTokenTest, "P4Search.ExtractLoginAccessToken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchExtractLoginAccessTokenTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Well-formed login response"),
		P4SearchCore::ExtractLoginAccessToken(TEXT("{\"data\":{\"accessToken\":\"abc123\",\"refreshToken\":\"xyz\"}}")),
		TEXT("abc123"));

	TestEqual(TEXT("Not JSON at all"), P4SearchCore::ExtractLoginAccessToken(TEXT("not json")), TEXT(""));

	TestEqual(TEXT("Valid JSON but no \"data\" key"),
		P4SearchCore::ExtractLoginAccessToken(TEXT("{\"foo\":\"bar\"}")), TEXT(""));

	TestEqual(TEXT("\"data\" present but no \"accessToken\""),
		P4SearchCore::ExtractLoginAccessToken(TEXT("{\"data\":{\"refreshToken\":\"xyz\"}}")), TEXT(""));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExtractUrlHostTest, "P4Search.ExtractUrlHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchExtractUrlHostTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Scheme, port and path stripped"),
		P4SearchCore::ExtractUrlHost(TEXT("https://host.example.com:1601/api/v1.4/login")), TEXT("host.example.com"));
	TestEqual(TEXT("Short host name with port"), P4SearchCore::ExtractUrlHost(TEXT("https://myserver:1601")), TEXT("myserver"));
	TestEqual(TEXT("No port or path"), P4SearchCore::ExtractUrlHost(TEXT("http://localhost")), TEXT("localhost"));
	TestEqual(TEXT("No scheme"), P4SearchCore::ExtractUrlHost(TEXT("myserver:1601")), TEXT("myserver"));
	TestEqual(TEXT("User-info dropped"), P4SearchCore::ExtractUrlHost(TEXT("https://user@host.example.com:1601")), TEXT("host.example.com"));
	TestEqual(TEXT("IPv6 literal keeps its brackets"), P4SearchCore::ExtractUrlHost(TEXT("http://[::1]:1601/")), TEXT("[::1]"));
	TestEqual(TEXT("Empty URL"), P4SearchCore::ExtractUrlHost(TEXT("")), TEXT(""));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchBuildLoginFailureMessageTest, "P4Search.BuildLoginFailureMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchBuildLoginFailureMessageTest::RunTest(const FString& Parameters)
{
	auto Message = [](const TCHAR* URL, bool bGotResponse, int32 Code, bool bTimedOut)
	{
		return P4SearchCore::BuildLoginFailureMessage(URL, bGotResponse, Code, bTimedOut).ToString();
	};

	const FString ShortHttps = Message(TEXT("https://myserver:1601"), false, 0, false);
	TestTrue(TEXT("https + short host name suggests the fully qualified name"), ShortHttps.Contains(TEXT("fully qualified")));
	TestTrue(TEXT("...and names the host"), ShortHttps.Contains(TEXT("myserver")));

	TestFalse(TEXT("https + fully qualified host doesn't suggest the fully qualified name"),
		Message(TEXT("https://myserver.example.com:1601"), false, 0, false).Contains(TEXT("fully qualified")));
	TestTrue(TEXT("...but does mention the certificate"),
		Message(TEXT("https://myserver.example.com:1601"), false, 0, false).Contains(TEXT("certificate")));
	TestFalse(TEXT("https://localhost isn't treated as a short host name"),
		Message(TEXT("https://localhost:1601"), false, 0, false).Contains(TEXT("fully qualified")));
	TestFalse(TEXT("Plain http doesn't mention certificates"),
		Message(TEXT("http://localhost:1601"), false, 0, false).Contains(TEXT("certificate")));

	TestTrue(TEXT("Timeout is reported as such"), Message(TEXT("https://myserver:1601"), false, 0, true).Contains(TEXT("timed out")));
	TestTrue(TEXT("401 is reported as bad credentials"),
		Message(TEXT("https://myserver.example.com"), true, 401, false).Contains(TEXT("username or password")));
	TestTrue(TEXT("200 without a token points at the Server URL"),
		Message(TEXT("https://myserver.example.com"), true, 200, false).Contains(TEXT("Server URL")));
	TestTrue(TEXT("Other status codes include the code"),
		Message(TEXT("https://myserver.example.com"), true, 503, false).Contains(TEXT("503")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchExtractLicenseStatusOkTest, "P4Search.ExtractLicenseStatusOk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchExtractLicenseStatusOkTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("\"license\" entry reports SUCCESS"),
		P4SearchCore::ExtractLicenseStatusOk(
			TEXT("{\"data\":[{\"name\":\"p4-status\",\"status\":\"SUCCESS\"},")
			TEXT("{\"name\":\"license\",\"status\":\"SUCCESS\"}]}")));

	TestFalse(TEXT("\"license\" entry reports WARNING"),
		P4SearchCore::ExtractLicenseStatusOk(TEXT("{\"data\":[{\"name\":\"license\",\"status\":\"WARNING\"}]}")));

	TestFalse(TEXT("\"license\" entry reports FAIL"),
		P4SearchCore::ExtractLicenseStatusOk(TEXT("{\"data\":[{\"name\":\"license\",\"status\":\"FAIL\"}]}")));

	TestTrue(TEXT("Not JSON at all fails open"),
		P4SearchCore::ExtractLicenseStatusOk(TEXT("not json")));

	TestTrue(TEXT("Valid JSON but no \"data\" key fails open"),
		P4SearchCore::ExtractLicenseStatusOk(TEXT("{\"foo\":\"bar\"}")));

	TestTrue(TEXT("\"data\" present but no \"license\" entry fails open"),
		P4SearchCore::ExtractLicenseStatusOk(TEXT("{\"data\":[{\"name\":\"p4-status\",\"status\":\"FAIL\"}]}")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchNormalizeYearMonthTest, "P4Search.NormalizeYearMonth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchNormalizeYearMonthTest::RunTest(const FString& Parameters)
{
	{
		int32 Year = 2026, Month = 6;
		P4SearchCore::NormalizeYearMonth(Year, Month);
		TestEqual(TEXT("Already-valid month is untouched (year)"), Year, 2026);
		TestEqual(TEXT("Already-valid month is untouched (month)"), Month, 6);
	}
	{
		int32 Year = 2026, Month = 0; // one before January
		P4SearchCore::NormalizeYearMonth(Year, Month);
		TestEqual(TEXT("Month 0 rolls back into December of the previous year (year)"), Year, 2025);
		TestEqual(TEXT("Month 0 rolls back into December of the previous year (month)"), Month, 12);
	}
	{
		int32 Year = 2026, Month = 13; // one past December
		P4SearchCore::NormalizeYearMonth(Year, Month);
		TestEqual(TEXT("Month 13 rolls forward into January of the next year (year)"), Year, 2027);
		TestEqual(TEXT("Month 13 rolls forward into January of the next year (month)"), Month, 1);
	}
	{
		int32 Year = 2026, Month = -23; // 23 months before January -> two years back to January
		P4SearchCore::NormalizeYearMonth(Year, Month);
		TestEqual(TEXT("Multi-year underflow (year)"), Year, 2024);
		TestEqual(TEXT("Multi-year underflow (month)"), Month, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchComputeCalendarDayCellsTest, "P4Search.ComputeCalendarDayCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchComputeCalendarDayCellsTest::RunTest(const FString& Parameters)
{
	// June 2026 has 30 days and starts on a Monday (LeadingBlanks = 0), so day 1 should land
	// in row 1, col 0, and every 7th day thereafter should wrap to col 0 on the next row.
	const TArray<P4SearchCore::FCalendarDayCell> Cells = P4SearchCore::ComputeCalendarDayCells(2026, 6);
	TestEqual(TEXT("June 2026 has 30 day cells"), Cells.Num(), 30);
	TestEqual(TEXT("Day 1 is row 1"), Cells[0].Row, 1);
	TestEqual(TEXT("Day 1 is col 0 (June 2026 starts on a Monday)"), Cells[0].Col, 0);
	TestEqual(TEXT("Day 8 (one week later) is row 2"), Cells[7].Row, 2);
	TestEqual(TEXT("Day 8 is also col 0"), Cells[7].Col, 0);
	TestEqual(TEXT("Last cell's Day matches the day count"), Cells.Last().Day, 30);

	// February 2026 (non-leap year) has 28 days.
	TestEqual(TEXT("February 2026 has 28 day cells"), P4SearchCore::ComputeCalendarDayCells(2026, 2).Num(), 28);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SearchComputeMeshNodeGlobalTransformTest, "P4Search.ComputeMeshNodeGlobalTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FP4SearchComputeMeshNodeGlobalTransformTest::RunTest(const FString& Parameters)
{
	{
		// A two-level parent/child chain: root at (10,0,0), mesh-holding child at (5,0,0)
		// relative to it. Composing pure translations is order-independent, so regardless of
		// FTransform::operator*'s exact convention, the accumulated translation must be their
		// sum — this is a faithful behavior check of the extracted traversal, not a re-test of
		// FTransform's own math.
		GLTF::FNode Root;
		Root.Index = 0;
		Root.ParentIndex = INDEX_NONE;
		Root.Transform = FTransform(FVector(10.0f, 0.0f, 0.0f));

		GLTF::FNode MeshChild;
		MeshChild.Index = 1;
		MeshChild.ParentIndex = 0;
		MeshChild.MeshIndex = 0;
		MeshChild.Transform = FTransform(FVector(5.0f, 0.0f, 0.0f));

		TArray<GLTF::FNode> Nodes;
		Nodes.Add(Root);
		Nodes.Add(MeshChild);

		const FTransform Result = P4SearchCore::ComputeMeshNodeGlobalTransform(Nodes, 0);
		TestEqual(TEXT("Accumulated translation sums the parent chain"),
			Result.GetTranslation(), FVector(15.0f, 0.0f, 0.0f));
	}

	{
		// No node references mesh index 5 -> Identity.
		GLTF::FNode Root;
		Root.Index = 0;
		Root.ParentIndex = INDEX_NONE;
		Root.MeshIndex = 0;
		TArray<GLTF::FNode> Nodes;
		Nodes.Add(Root);

		const FTransform Result = P4SearchCore::ComputeMeshNodeGlobalTransform(Nodes, 5);
		TestTrue(TEXT("No matching node returns Identity"), Result.Equals(FTransform::Identity));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
