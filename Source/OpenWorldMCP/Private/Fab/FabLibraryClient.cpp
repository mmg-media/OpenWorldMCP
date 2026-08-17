// OpenWorldMCP Fab integration - original implementation for the OpenWorld project.

// Compiled out when the engine install lacks the Fab plugin (issue #525) — see OpenWorldMCP.Build.cs.
#if WITH_OPENWORLD_FAB

#include "FabLibraryClient.h"
#include "FabEndpoints.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"

// ---------------------------------------------------------------------------
// FFabLibraryAsset helpers
// ---------------------------------------------------------------------------

bool FFabLibraryAsset::SupportsEngine(const FString& EngineVersion) const
{
	if (EngineVersion.IsEmpty())
	{
		return true;
	}
	for (const FFabProjectVersion& PV : ProjectVersions)
	{
		for (const FString& EV : PV.EngineVersions)
		{
			// Match "5.8" against "5.8" or "UE_5.8" style tokens, lenient on prefix.
			if (EV == EngineVersion || EV.EndsWith(EngineVersion))
			{
				return true;
			}
		}
	}
	// No projectVersions at all → unknown; treat as compatible so we don't hide items on sparse data.
	return ProjectVersions.Num() == 0;
}

TArray<FString> FFabLibraryAsset::AllEngineVersions() const
{
	TArray<FString> Out;
	for (const FFabProjectVersion& PV : ProjectVersions)
	{
		for (const FString& EV : PV.EngineVersions)
		{
			Out.AddUnique(EV);
		}
	}
	return Out;
}

FString FFabLibraryAsset::DeriveAssetType() const
{
	const FString SourceLower = Source.ToLower();
	const FString DistLower = DistributionMethod.ToLower();
	if (SourceLower.Contains(TEXT("quixel")) || SourceLower.Contains(TEXT("megascan")))
	{
		return TEXT("quixel");
	}
	if (DistLower.Contains(TEXT("plugin")))
	{
		return TEXT("plugin");
	}
	if (DistLower.Contains(TEXT("complete_project")) || DistLower.Contains(TEXT("complete project")))
	{
		return TEXT("complete-project");
	}
	if (DistLower.Contains(TEXT("asset_pack")) || DistLower.Contains(TEXT("asset pack")) || DistLower.Contains(TEXT("engine")))
	{
		return TEXT("unreal-engine");
	}
	// glTF/FBX 3D models come through as generic downloads.
	return TEXT("model");
}

FString FFabLibraryAsset::ArtifactIdForEngine(const FString& EngineVersion) const
{
	if (!EngineVersion.IsEmpty())
	{
		for (const FFabProjectVersion& PV : ProjectVersions)
		{
			for (const FString& EV : PV.EngineVersions)
			{
				if ((EV == EngineVersion || EV.EndsWith(EngineVersion)) && !PV.ArtifactId.IsEmpty())
				{
					return PV.ArtifactId;
				}
			}
		}
	}
	return ProjectVersions.Num() > 0 ? ProjectVersions[0].ArtifactId : FString();
}

// ---------------------------------------------------------------------------
// Parsing helpers (shared by every page of the feed)
// ---------------------------------------------------------------------------

namespace
{
	void ParseStringArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Field, Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S))
				{
					Out.Add(S);
				}
			}
		}
	}

	void ParseProjectVersions(const TSharedPtr<FJsonObject>& Item, TArray<FFabProjectVersion>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Item->TryGetArrayField(TEXT("projectVersions"), Arr))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* PVObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(PVObj) || !PVObj->IsValid())
			{
				continue;
			}
			FFabProjectVersion PV;
			// artifactId may be a string or an object depending on the feed — accept either.
			if (!(*PVObj)->TryGetStringField(TEXT("artifactId"), PV.ArtifactId))
			{
				const TSharedPtr<FJsonObject>* ArtObj = nullptr;
				if ((*PVObj)->TryGetObjectField(TEXT("artifactId"), ArtObj) && ArtObj->IsValid())
				{
					(*ArtObj)->TryGetStringField(TEXT("id"), PV.ArtifactId);
				}
			}
			ParseStringArray(*PVObj, TEXT("engineVersions"), PV.EngineVersions);
			ParseStringArray(*PVObj, TEXT("targetPlatforms"), PV.TargetPlatforms);
			ParseStringArray(*PVObj, TEXT("buildVersions"), PV.BuildVersions);
			Out.Add(MoveTemp(PV));
		}
	}

	void ParseImages(const TSharedPtr<FJsonObject>& Item, TArray<FFabLibraryAsset::FImage>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Item->TryGetArrayField(TEXT("images"), Arr))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* ImgObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(ImgObj) || !ImgObj->IsValid())
			{
				continue;
			}
			FFabLibraryAsset::FImage Img;
			(*ImgObj)->TryGetStringField(TEXT("url"), Img.Url);
			(*ImgObj)->TryGetStringField(TEXT("type"), Img.Type);
			(*ImgObj)->TryGetNumberField(TEXT("width"), Img.Width);
			(*ImgObj)->TryGetNumberField(TEXT("height"), Img.Height);
			Out.Add(MoveTemp(Img));
		}
	}

	// Parse one page. Returns the next cursor (empty if none) via OutNextCursor.
	bool ParsePage(const FString& Body, TArray<FFabLibraryAsset>& OutAssets, FString& OutNextCursor, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Library response was not valid JSON.");
			return false;
		}

		const TSharedPtr<FJsonObject>* Cursors = nullptr;
		if (Root->TryGetObjectField(TEXT("cursors"), Cursors) && Cursors->IsValid())
		{
			(*Cursors)->TryGetStringField(TEXT("next"), OutNextCursor);
		}

		const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
		if (!Root->TryGetArrayField(TEXT("results"), Results))
		{
			// No results array is only an error if there's also no cursor — treat empty page as success.
			return true;
		}
		for (const TSharedPtr<FJsonValue>& V : *Results)
		{
			const TSharedPtr<FJsonObject>* ItemPtr = nullptr;
			if (!V.IsValid() || !V->TryGetObject(ItemPtr) || !ItemPtr->IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& Item = *ItemPtr;
			FFabLibraryAsset A;
			Item->TryGetStringField(TEXT("assetId"), A.AssetId);
			Item->TryGetStringField(TEXT("assetNamespace"), A.AssetNamespace);
			Item->TryGetStringField(TEXT("title"), A.Title);
			Item->TryGetStringField(TEXT("description"), A.Description);
			Item->TryGetStringField(TEXT("listingType"), A.ListingType);
			Item->TryGetStringField(TEXT("seller"), A.Seller);
			Item->TryGetStringField(TEXT("source"), A.Source);
			Item->TryGetStringField(TEXT("distributionMethod"), A.DistributionMethod);
			Item->TryGetStringField(TEXT("url"), A.Url);
			ParseProjectVersions(Item, A.ProjectVersions);
			ParseImages(Item, A.Images);
			if (!A.AssetId.IsEmpty())
			{
				OutAssets.Add(MoveTemp(A));
			}
		}
		return true;
	}
}

// ---------------------------------------------------------------------------
// Async, chain-driven HTTP GET. The owned-library feed is paged via cursors.next and each page can
// take several seconds on fab.com, so fetching is done with the standard async-request pattern (same
// as the engine Fab plugin's QueueSyncRequest). A single in-flight chain is tracked file-statically;
// the completion delegate is invoked on the game thread (IHttpRequest delivers that way by default).
// ---------------------------------------------------------------------------

namespace
{
	struct FChainState
	{
		FString BaseUrl;
		FString EpicAccountId;
		FString Bearer;
		int32 PageSize = 100;
		FString Cursor;
		TArray<FFabLibraryAsset> Assets;
		int32 HttpCode = 0;
		bool bActive = false;
		bool bRetriedThisPage = false;
		bool bHasRequestedPage = false;
	};

	FChainState& Chain()
	{
		static FChainState State;
		return State;
	}

	void ContinueChain(FOpenWorldFabLibrary::FOpenWorldFabLibraryDone OnDone);

	// Begin the next page request, or finish the chain when the cursor is exhausted. The FIRST page
	// is requested with an empty cursor (that's how the feed starts); only after at least one page
	// does an empty cursor mean "no more pages".
	void StartNextPage(FOpenWorldFabLibrary::FOpenWorldFabLibraryDone OnDone)
	{
		FChainState& C = Chain();
		if (C.bHasRequestedPage && C.Cursor.IsEmpty())
		{
			C.bActive = false;
			if (OnDone) { OnDone(true, MoveTemp(C.Assets), FString()); }
			return;
		}
		C.bHasRequestedPage = true;

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetVerb(TEXT("GET"));
		Req->SetURL(OpenWorldMCP::Fab::LibraryUrl(C.BaseUrl, C.EpicAccountId, C.PageSize, C.Cursor));
		Req->SetHeader(TEXT("accept"), TEXT("application/json"));
		Req->SetHeader(TEXT("User-Agent"), TEXT("Fab"));
		Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *C.Bearer));
		Req->OnProcessRequestComplete().BindLambda(
			[OnDone](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
			{
				FChainState& State = Chain();
				if (!State.bActive)
				{
					return;   // cancelled mid-flight — ignore a stale completion
				}

				int32 Code = 0;
				FString Body;
				if (bOk && Resp.IsValid())
				{
					Code = Resp->GetResponseCode();
					Body = Resp->GetContentAsString();
				}
				State.HttpCode = Code;

				// Fab intermittently challenges the first headless request (HTTP 403), then accepts the
				// identical follow-up. Retry exactly once; never loop on a persistent challenge.
				if (Code == 403 && !State.bRetriedThisPage)
				{
					State.bRetriedThisPage = true;
					ContinueChain(OnDone);
					return;
				}

				if (!bOk || Code < 200 || Code >= 300)
				{
					State.bActive = false;
					const FString Err = Code == 401 || Code == 403
						? FString::Printf(TEXT("Fab library request was rejected (HTTP %d) — the auth token may be expired or lack fab.com scope."), Code)
						: FString::Printf(TEXT("Fab library request failed (HTTP %d)."), Code);
					if (OnDone) { OnDone(false, MoveTemp(State.Assets), Err); }
					return;
				}

				FString NextCursor;
				FString ParseErr;
				if (!ParsePage(Body, State.Assets, NextCursor, ParseErr))
				{
					State.bActive = false;
					if (OnDone) { OnDone(false, MoveTemp(State.Assets), ParseErr); }
					return;
				}
				State.Cursor = NextCursor;
				State.bRetriedThisPage = false;
				StartNextPage(OnDone);
			});
		Req->ProcessRequest();
	}

	void ContinueChain(FOpenWorldFabLibrary::FOpenWorldFabLibraryDone OnDone)
	{
		StartNextPage(OnDone);
	}
}

bool FOpenWorldFabLibrary::IsFetching()
{
	return Chain().bActive;
}

void FOpenWorldFabLibrary::Cancel()
{
	Chain().bActive = false;
}

void FOpenWorldFabLibrary::FetchAsync(const FString& BaseUrl, const FString& EpicAccountId, const FString& BearerToken,
                                      int32 PageSize, FOpenWorldFabLibraryDone OnDone)
{
	if (EpicAccountId.IsEmpty() || BearerToken.IsEmpty())
	{
		if (OnDone) { OnDone(false, TArray<FFabLibraryAsset>(), TEXT("Missing Epic account id or auth token.")); }
		return;
	}

	FChainState& C = Chain();
	if (C.bActive)
	{
		return;   // a fetch is already running; the caller should poll IsFetching() instead
	}

	C = FChainState();
	C.BaseUrl = BaseUrl;
	C.EpicAccountId = EpicAccountId;
	C.Bearer = BearerToken;
	C.PageSize = PageSize > 0 ? PageSize : 100;
	C.bActive = true;
	C.bRetriedThisPage = false;
	C.bHasRequestedPage = false;

	// The first request uses an empty cursor; the chain reads the next cursor from each page's
	// response and continues until the feed reports none.
	StartNextPage(OnDone);
}

#endif // WITH_OPENWORLD_FAB