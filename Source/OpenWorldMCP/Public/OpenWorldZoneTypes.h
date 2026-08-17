#pragma once

#include "CoreMinimal.h"
#include "OpenWorldZoneTypes.generated.h"

/**
 * A single marked region in the level. Regions are stored as actors (AZoneRegionActor) so they
 * persist across editor restarts and can be queried by MCP tools. The footprint is stored as
 * world-space X/Y points: either two corners of an axis-aligned rectangle, or an arbitrary
 * polygon (closed, first == last not required).
 */
USTRUCT(BlueprintType)
struct FOpenWorldZone
{
	GENERATED_BODY()

	/** Stable id of the region actor. */
	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	FString Id;

	/** Display label. */
	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	FString Label;

	/** True if the footprint is a polygon (false = axis-aligned rectangle). */
	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	bool bIsPolygon = false;

	/** World-space X/Y points of the footprint. For a rectangle: min/max corners. */
	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	TArray<FVector2D> Points;

	/** Display color used when rendering the region in the editor viewport. */
	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	FLinearColor Color = FLinearColor(0.15f, 0.8f, 0.15f, 1.0f);

	/** World Z used to draw the footprint marker (approximate ground height). */
	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Zone")
	float GroundZ = 0.0f;
};
