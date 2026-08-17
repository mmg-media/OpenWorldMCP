// OpenWorldMCP Fab integration - original implementation for the OpenWorld project.

#pragma once

#include "CoreMinimal.h"

/** One engine/version entry from a library item's projectVersions[]. */
struct FFabProjectVersion
{
	FString ArtifactId;
	TArray<FString> EngineVersions;
	TArray<FString> TargetPlatforms;
	TArray<FString> BuildVersions;
};

/** A single owned Fab library asset, parsed from the /ue/library feed. */
struct FFabLibraryAsset
{
	FString AssetId;
	FString AssetNamespace;
	FString Title;
	FString Description;
	FString ListingType;
	FString Seller;
	FString Source;              // e.g. "quixel", "sketchfab", "unreal-engine"
	FString DistributionMethod;  // e.g. "asset_pack", "complete_project", "engine_plugin"
	FString Url;
	TArray<FFabProjectVersion> ProjectVersions;

	struct FImage { FString Url; FString Type; int32 Width = 0; int32 Height = 0; };
	TArray<FImage> Images;

	/** True if any projectVersion advertises support for EngineVersion (e.g. "5.8"). */
	bool SupportsEngine(const FString& EngineVersion) const;

	/** All engine versions this asset advertises across its projectVersions, de-duplicated. */
	TArray<FString> AllEngineVersions() const;

	/** Best-effort asset type used to route import (derived from source/distributionMethod/listingType). */
	FString DeriveAssetType() const;

	/** The artifactId of the projectVersion supporting EngineVersion (or the first, or empty). */
	FString ArtifactIdForEngine(const FString& EngineVersion) const;
};

/**
 * Fetches the signed-in account's owned Fab library asynchronously, paging via cursors.next.
 * Mirrors the engine Fab plugin's request (FabMyFolderIntegration.cpp): GET
 * {base}/e/accounts/{id}/ue/library?count=N, headers accept + Bearer. Because the owned-library
 * feed can be large (hundreds of assets → many pages, each page taking several seconds), the fetch
 * runs in the background using the same async-request pattern as the engine plugin; callers poll via
 * IsFetching() and take the result from FetchAsync's completion delegate.
 */
class FOpenWorldFabLibrary
{
public:
	/** Completion callback: (bOk, assets, error). Delivered on the game thread. */
	using FOpenWorldFabLibraryDone = TFunction<void(bool /*bOk*/, TArray<FFabLibraryAsset> /*Assets*/, FString /*Error*/)>;

	/**
	 * Begin fetching the library. Idempotent per session: if a fetch is already running this is a no-op
	 * (the existing fetch's completion callback still fires). Results are delivered on the game thread.
	 * @param BaseUrl        e.g. https://www.fab.com
	 * @param EpicAccountId  stringified EOS account id
	 * @param BearerToken    EOS access token
	 * @param PageSize       'count' per request (100 keeps single pages fast — the feed is slow)
	 * @param OnDone         completion callback (bOk, assets, error)
	 */
	static void FetchAsync(const FString& BaseUrl, const FString& EpicAccountId, const FString& BearerToken,
	                       int32 PageSize, FOpenWorldFabLibraryDone OnDone);

	/** True while a library fetch is in flight. */
	static bool IsFetching();

	/** Cancels an in-flight fetch; the completion delegate will not fire. */
	static void Cancel();
};
