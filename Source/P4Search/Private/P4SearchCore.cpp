// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4SearchCore.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/DateTime.h"
#include "GLTFAsset.h"

namespace P4SearchCore
{
	FString FormatFileSize(int64 Bytes)
	{
		if (Bytes < 1024)
		{
			return FString::Printf(TEXT("%lld B"), Bytes);
		}
		if (Bytes < 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1f KB"), Bytes / 1024.0);
		}
		return FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0));
	}

	// Label -> /search/dsl 'sort' parameter. Field names here are the endpoint's friendly
	// aliases, not raw Elasticsearch paths: the service maps e.g. fileName -> fileName.keyword
	// and rejects the raw path, so "fileName.keyword" here would produce a 400.
	// Keep in sync with GetSortOptionLabels below.
	static const TMap<FString, FString>& GetSortParamsByLabel()
	{
		static const TMap<FString, FString> Map = {
			{ TEXT("Relevance"),     TEXT("")               },
			{ TEXT("Newest Change"), TEXT("change:desc")    },
			{ TEXT("Oldest Change"), TEXT("change:asc")     },
			{ TEXT("Largest"),       TEXT("fileSize:desc")  },
			{ TEXT("Smallest"),      TEXT("fileSize:asc")   },
			{ TEXT("Name A-Z"),      TEXT("fileName:asc")   },
			{ TEXT("Name Z-A"),      TEXT("fileName:desc")  },
		};
		return Map;
	}

	const TArray<FString>& GetSortOptionLabels()
	{
		// Explicit order: a TMap's iteration order is unspecified, and the combo's first entry
		// is the default, so it cannot be left to chance.
		static const TArray<FString> Labels = {
			TEXT("Relevance"),
			TEXT("Newest Change"),
			TEXT("Oldest Change"),
			TEXT("Largest"),
			TEXT("Smallest"),
			TEXT("Name A-Z"),
			TEXT("Name Z-A"),
		};
		return Labels;
	}

	FString BuildSortParam(const FString& Label)
	{
		if (const FString* Found = GetSortParamsByLabel().Find(Label))
		{
			return *Found;
		}
		return FString();
	}

	FString BuildSearchDslQuery(const FString& SearchDepotPath, const FString& Ext, const FString& SearchDetected,
		const FString& SearchDate, int32 SizeFromMB, int32 SizeToMB, bool bHeadOnly)
	{
		const bool bAllTypes = (Ext == TEXT("*"));

		FString SearchQuery;
		if (!SearchDepotPath.IsEmpty())
		{
			SearchQuery += FString::Printf(TEXT("+depotPath: %s "), *SearchDepotPath);
		}
		SearchQuery += TEXT("+model: asset");
		if (!bAllTypes)
		{
			// "ALL3D"/"ALL2D"/"ALLAUDIO" are sidebar pseudo-options, not real extensions (see
			// AllThreeDOption/AllTwoDOption/AllAudioOption in P4Search.h) -- each expands to every
			// extension in that category, space-separated, matching the DSL grammar's `ext:` rule
			// (ext : 'ext' COLON (value)+), which ORs multiple values. Keep this trio in sync with
			// P4Search.cpp's ThreeDExtensions/AudioExtensions sets and the 2D group's exclusion list.
			FString ExtToken;
			bool bMultiValueExt = false;
			if (Ext == TEXT("ALL3D"))
			{
				ExtToken = TEXT("fbx obj glb usd abc");
				bMultiValueExt = true;
			}
			else if (Ext == TEXT("ALL2D"))
			{
				ExtToken = TEXT("jpeg png bmp");
				bMultiValueExt = true;
			}
			else if (Ext == TEXT("ALLAUDIO"))
			{
				ExtToken = TEXT("wav mp3 ogg flac aif opus");
				bMultiValueExt = true;
			}
			else
			{
				ExtToken = Ext.ToLower();
			}
			// Every other clause in this query is "+"-prefixed (required) -- "+ext:" with
			// multiple values would require the same file to match every one of them
			// simultaneously, which none ever can. Dropping "+" here instead makes this the
			// query's only should-type clause, and minimumShouldMatch(1) on the compiled
			// query already makes "at least one should clause matches" required overall, so
			// this still behaves as a required OR-of-extensions, not an optional filter.
			SearchQuery += FString::Printf(TEXT(" %sext:%s"), bMultiValueExt ? TEXT("") : TEXT("+"), *ExtToken);
		}
		if (!SearchDetected.IsEmpty() && SearchDetected != TEXT("*"))
		{
			SearchQuery += FString::Printf(TEXT(" +text: %s"), *SearchDetected);
		}
		if (!SearchDate.IsEmpty())
		{
			SearchQuery += FString::Printf(TEXT(" +date: %s"), *SearchDate);
		}
		if (SizeFromMB > 0 && SizeToMB > 0)
		{
			SearchQuery += FString::Printf(TEXT(" +size: %dMB %dMB"), SizeFromMB, SizeToMB);
		}
		else if (SizeFromMB > 0)
		{
			SearchQuery += FString::Printf(TEXT(" +size: %dMB"), SizeFromMB);
		}
		else if (SizeToMB > 0)
		{
			// The "size:" grammar rule requires a fromSize token — "0MB" as the lower
			// bound is a no-op (fileSize is never negative) but keeps the query valid.
			SearchQuery += FString::Printf(TEXT(" +size: 0MB %dMB"), SizeToMB);
		}
		if (bHeadOnly)
		{
			// The DSL grammar's revision/rev field only recognizes the literal "latest"
			// for this (fromRange/toRange : num | latest) — "head" isn't a valid token.
			SearchQuery += TEXT(" +rev: latest");
		}
		return SearchQuery;
	}

	FSearchHit ParseSearchHit(const TSharedRef<FJsonObject>& Source)
	{
		FSearchHit Hit;
		Hit.DepotFile = Source->GetStringField(TEXT("depotFile"));
		Hit.FileName = Source->GetStringField(TEXT("fileName"));
		Hit.Rev = (int32)Source->GetNumberField(TEXT("rev"));
		Hit.Change = (int32)Source->GetNumberField(TEXT("change"));
		Hit.FileSizeBytes = (int64)Source->GetNumberField(TEXT("fileSize"));
		Hit.FileSizeStr = FormatFileSize(Hit.FileSizeBytes);
		Hit.bIsHead = true; // stays true (no warning shown) if the field is ever absent
		Source->TryGetBoolField(TEXT("head"), Hit.bIsHead);

		// Each entry is {"tag": "...", "confidence": ...}; only the tag names get displayed.
		const TArray<TSharedPtr<FJsonValue>>* DetectedArray = nullptr;
		if (Source->TryGetArrayField(TEXT("detected"), DetectedArray) && DetectedArray)
		{
			TArray<FString> Tags;
			for (const TSharedPtr<FJsonValue>& Entry : *DetectedArray)
			{
				FString Tag;
				if (Entry->AsObject()->TryGetStringField(TEXT("tag"), Tag))
				{
					Tags.Add(Tag);
				}
			}
			Hit.DetectedTagsStr = FString::Join(Tags, TEXT(", "));
		}
		return Hit;
	}

	int32 ExtractTotalResults(const TSharedRef<FJsonObject>& JsonObject)
	{
		int32 TotalResults = 0;
		const TSharedPtr<FJsonObject>* HitsOuter = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("hits"), HitsOuter) && HitsOuter)
		{
			const TSharedPtr<FJsonObject>* TotalObj = nullptr;
			if ((*HitsOuter)->TryGetObjectField(TEXT("total"), TotalObj) && TotalObj)
			{
				(*TotalObj)->TryGetNumberField(TEXT("value"), TotalResults);
			}
		}
		return TotalResults;
	}

	FString ExtractJsonStatusMessage(const FString& ResponseBody)
	{
		FString Message;
		TSharedPtr<FJsonObject> ErrorJson = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<TCHAR>> ErrorReader = TJsonReaderFactory<TCHAR>::Create(ResponseBody);
		if (FJsonSerializer::Deserialize(ErrorReader, ErrorJson) && ErrorJson.IsValid())
		{
			const TSharedPtr<FJsonObject>* StatusObj = nullptr;
			if (ErrorJson->TryGetObjectField(TEXT("status"), StatusObj) && StatusObj)
			{
				(*StatusObj)->TryGetStringField(TEXT("message"), Message);
			}
		}
		return Message;
	}

	TOptional<FRibbonMeta> ParseRibbonMeta(const TSharedRef<FJsonObject>& MetaJson)
	{
		FRibbonMeta Meta;
		Meta.SpriteWidth = (int32)MetaJson->GetNumberField(TEXT("spriteWidth"));
		Meta.SpriteHeight = (int32)MetaJson->GetNumberField(TEXT("spriteHeight"));

		const TArray<TSharedPtr<FJsonValue>>* SpritesArray = nullptr;
		if (Meta.SpriteWidth <= 0 || Meta.SpriteHeight <= 0
			|| !MetaJson->TryGetArrayField(TEXT("sprites"), SpritesArray) || !SpritesArray || SpritesArray->Num() == 0)
		{
			return TOptional<FRibbonMeta>();
		}

		Meta.Frames.Reserve(SpritesArray->Num());
		for (const TSharedPtr<FJsonValue>& Entry : *SpritesArray)
		{
			TSharedPtr<FJsonObject> SpriteObj = Entry->AsObject();
			if (!SpriteObj.IsValid())
			{
				continue;
			}
			FRibbonFrameRect Frame;
			Frame.X = (int32)SpriteObj->GetNumberField(TEXT("x"));
			Frame.Y = (int32)SpriteObj->GetNumberField(TEXT("y"));
			Meta.Frames.Add(Frame);
		}
		if (Meta.Frames.Num() == 0)
		{
			return TOptional<FRibbonMeta>();
		}
		return Meta;
	}

	TArray<FBox2f> ComputeRibbonFrameUVs(const TArray<FRibbonFrameRect>& Frames, int32 SpriteWidth, int32 SpriteHeight,
		int32 TextureWidth, int32 TextureHeight)
	{
		TArray<FBox2f> FrameUVs;
		FrameUVs.Reserve(Frames.Num());
		for (const FRibbonFrameRect& Frame : Frames)
		{
			const float MinU = Frame.X / (float)TextureWidth;
			const float MinV = Frame.Y / (float)TextureHeight;
			const float MaxU = (Frame.X + SpriteWidth) / (float)TextureWidth;
			const float MaxV = (Frame.Y + SpriteHeight) / (float)TextureHeight;
			FrameUVs.Add(FBox2f(FVector2f(MinU, MinV), FVector2f(MaxU, MaxV)));
		}
		return FrameUVs;
	}

	FString FormatSceneRefs(const TArray<TSharedPtr<FJsonValue>>& RefsArray)
	{
		TArray<FString> Descriptions;
		Descriptions.Reserve(RefsArray.Num());
		for (const TSharedPtr<FJsonValue>& Entry : RefsArray)
		{
			if (!Entry.IsValid())
			{
				continue;
			}

			FString AsString;
			if (Entry->TryGetString(AsString) && !AsString.IsEmpty())
			{
				Descriptions.Add(AsString);
				continue;
			}

			const TSharedPtr<FJsonObject>* AsObject = nullptr;
			if (Entry->TryGetObject(AsObject) && AsObject && AsObject->IsValid())
			{
				FString Found;
				for (const TCHAR* Key : { TEXT("depotFile"), TEXT("file"), TEXT("path"), TEXT("scene") })
				{
					if ((*AsObject)->TryGetStringField(Key, Found) && !Found.IsEmpty())
					{
						break;
					}
				}
				if (!Found.IsEmpty())
				{
					Descriptions.Add(Found);
					continue;
				}
			}

			Descriptions.Add(Entry->AsString());
		}

		return Descriptions.Num() > 0 ? FString::Join(Descriptions, TEXT(", ")) : TEXT("None");
	}

	FString FormatSceneStats(const TSharedRef<FJsonObject>& StatsJson)
	{
		// (JSON field, display label) — "objects" is deprecated on SceneStatsModel, skipped.
		static const TPair<const TCHAR*, const TCHAR*> StatFields[] = {
			{ TEXT("triangles"),  TEXT("triangles")  },
			{ TEXT("vertices"),   TEXT("vertices")   },
			{ TEXT("edges"),      TEXT("edges")      },
			{ TEXT("faces"),      TEXT("faces")      },
			{ TEXT("materials"),  TEXT("materials")  },
			{ TEXT("meshes"),     TEXT("meshes")      },
			{ TEXT("textures"),   TEXT("textures")   },
			{ TEXT("armatures"),  TEXT("armatures")  },
			{ TEXT("animations"), TEXT("animations") },
			{ TEXT("uv_maps"),    TEXT("UV maps")    },
		};

		TArray<FString> Descriptions;
		for (const TPair<const TCHAR*, const TCHAR*>& Field : StatFields)
		{
			double Value = 0.0;
			if (StatsJson->TryGetNumberField(Field.Key, Value) && Value > 0.0)
			{
				Descriptions.Add(FString::Printf(TEXT("%s %s"),
					*FText::AsNumber((int64)Value).ToString(), Field.Value));
			}
		}

		return Descriptions.Num() > 0 ? FString::Join(Descriptions, TEXT(", ")) : TEXT("None");
	}

	FString ExtractLoginAccessToken(const FString& ResponseBody)
	{
		FString Token;
		TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(ResponseBody);
		if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
		{
			const TSharedPtr<FJsonObject>* DataObj = nullptr;
			if (Json->TryGetObjectField(TEXT("data"), DataObj) && DataObj)
			{
				(*DataObj)->TryGetStringField(TEXT("accessToken"), Token);
			}
		}
		return Token;
	}

	bool ExtractLicenseStatusOk(const FString& ResponseBody)
	{
		TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* DataArray = nullptr;
		if (!Json->TryGetArrayField(TEXT("data"), DataArray) || !DataArray)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& Entry : *DataArray)
		{
			const TSharedPtr<FJsonObject>* EntryObj = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(EntryObj) || !EntryObj)
			{
				continue;
			}
			FString Name;
			(*EntryObj)->TryGetStringField(TEXT("name"), Name);
			if (Name != TEXT("license"))
			{
				continue;
			}
			FString Status;
			(*EntryObj)->TryGetStringField(TEXT("status"), Status);
			return Status == TEXT("SUCCESS");
		}

		return true;
	}

	FString ExtractUrlHost(const FString& Url)
	{
		FString Rest = Url.TrimStartAndEnd();
		int32 SchemeEnd = Rest.Find(TEXT("://"));
		if (SchemeEnd != INDEX_NONE)
		{
			Rest.RightChopInline(SchemeEnd + 3);
		}

		int32 PathStart = INDEX_NONE;
		Rest.FindChar(TEXT('/'), PathStart);
		const FString Authority = PathStart == INDEX_NONE ? Rest : Rest.Left(PathStart);

		// Drop any user-info ("user@host") — not expected in a Server URL, but it would
		// otherwise be mistaken for part of the host.
		int32 AtIndex = INDEX_NONE;
		const FString HostPort = Authority.FindLastChar(TEXT('@'), AtIndex) ? Authority.RightChop(AtIndex + 1) : Authority;

		if (HostPort.StartsWith(TEXT("[")))
		{
			int32 CloseBracket = INDEX_NONE;
			return HostPort.FindChar(TEXT(']'), CloseBracket) ? HostPort.Left(CloseBracket + 1) : HostPort;
		}

		int32 PortStart = INDEX_NONE;
		return HostPort.FindChar(TEXT(':'), PortStart) ? HostPort.Left(PortStart) : HostPort;
	}

	FText BuildLoginFailureMessage(const FString& ServerURL, bool bGotResponse, int32 ResponseCode, bool bTimedOut)
	{
		const FString Host = ExtractUrlHost(ServerURL);
		const FText HostText = FText::FromString(Host.IsEmpty() ? ServerURL : Host);

		if (!bGotResponse)
		{
			if (bTimedOut)
			{
				return FText::Format(NSLOCTEXT("FP4SearchModule", "LoginFailedTimedOut",
					"Login failed: timed out connecting to {0}. Check the Server URL and that the server is running."),
					HostText);
			}

			// A TLS certificate is issued for the server's full DNS name, so an https URL using a
			// short name that only resolves via the local DNS search domain (e.g. "myserver"
			// rather than "myserver.example.com") fails certificate validation before the
			// request is ever sent. The HTTP module doesn't expose why a request failed beyond
			// a coarse reason, so this is inferred from the URL rather than the error itself.
			const bool bIsHttps = ServerURL.TrimStart().StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
			const bool bIsShortHostName = !Host.IsEmpty() && !Host.Contains(TEXT("."))
				&& !Host.StartsWith(TEXT("[")) && !Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase);
			if (bIsHttps && bIsShortHostName)
			{
				return FText::Format(NSLOCTEXT("FP4SearchModule", "LoginFailedShortHostName",
					"Login failed: couldn't connect securely to {0}. The server's certificate must match the host name, "
					"so try the server's fully qualified name (e.g. {0}.example.com) in the Server URL."),
					HostText);
			}
			if (bIsHttps)
			{
				return FText::Format(NSLOCTEXT("FP4SearchModule", "LoginFailedConnectionHttps",
					"Login failed: couldn't connect to {0}. Check the Server URL, that the server is running, "
					"and that its certificate is trusted and matches the host name."),
					HostText);
			}
			return FText::Format(NSLOCTEXT("FP4SearchModule", "LoginFailedConnection",
				"Login failed: couldn't connect to {0}. Check the Server URL and that the server is running."),
				HostText);
		}

		if (ResponseCode == 401)
		{
			return NSLOCTEXT("FP4SearchModule", "LoginFailedCredentials", "Login failed: incorrect username or password.");
		}

		if (ResponseCode == 200)
		{
			// A 200 without an access token in the body usually means the URL reached something
			// other than a P4 Search server (a proxy or a different service on that port).
			return FText::Format(NSLOCTEXT("FP4SearchModule", "LoginFailedNoToken",
				"Login failed: unexpected response from {0}. Check the Server URL points to a P4 Search server."),
				HostText);
		}

		return FText::Format(NSLOCTEXT("FP4SearchModule", "LoginFailedHttpStatus",
			"Login failed: the server returned HTTP {0}. See the Output Log for details."),
			FText::AsNumber(ResponseCode));
	}

	void NormalizeYearMonth(int32& Year, int32& Month)
	{
		while (Month < 1)
		{
			Month += 12;
			Year--;
		}
		while (Month > 12)
		{
			Month -= 12;
			Year++;
		}
	}

	TArray<FCalendarDayCell> ComputeCalendarDayCells(int32 Year, int32 Month)
	{
		const int32 NumDaysInMonth = FDateTime::DaysInMonth(Year, Month);
		const FDateTime FirstOfMonth(Year, Month, 1);
		const int32 LeadingBlanks = static_cast<int32>(FirstOfMonth.GetDayOfWeek()); // Monday = 0

		TArray<FCalendarDayCell> Cells;
		Cells.Reserve(NumDaysInMonth);
		for (int32 Day = 1; Day <= NumDaysInMonth; ++Day)
		{
			const int32 CellIndex = LeadingBlanks + Day - 1;
			FCalendarDayCell Cell;
			Cell.Day = Day;
			Cell.Row = 1 + (CellIndex / 7);
			Cell.Col = CellIndex % 7;
			Cells.Add(Cell);
		}
		return Cells;
	}

	FTransform ComputeMeshNodeGlobalTransform(const TArray<GLTF::FNode>& Nodes, int32 TargetMeshIndex)
	{
		for (const GLTF::FNode& Node : Nodes)
		{
			if (Node.MeshIndex != TargetMeshIndex)
			{
				continue;
			}
			FTransform Global = FTransform::Identity;
			for (int32 NodeIndex = Node.Index; Nodes.IsValidIndex(NodeIndex); NodeIndex = Nodes[NodeIndex].ParentIndex)
			{
				Global = Global * Nodes[NodeIndex].Transform;
			}
			return Global;
		}
		return FTransform::Identity;
	}
}
