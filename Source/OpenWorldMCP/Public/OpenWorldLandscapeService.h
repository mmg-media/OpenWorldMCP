#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "OpenWorldLandscapeService.generated.h"

USTRUCT(BlueprintType)
struct FOpenWorldLandscapeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Landscape")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Landscape")
	FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct FOpenWorldHeightResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Landscape")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Landscape")
	float Height = 0.f;
};

UCLASS(BlueprintType)
class OPENWORLDMCP_API UOpenWorldLandscapeService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * List all landscape actors in the current level.
	 *
	 * @return Array of landscape labels
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Landscape")
	static TArray<FString> ListLandscapes();

	/**
	 * Sample the landscape height at a world position (physics trace down).
	 *
	 * @param WorldX - World X
	 * @param WorldY - World Y
	 * @return Height result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Landscape")
	static FOpenWorldHeightResult GetHeightAtLocation(float WorldX, float WorldY);

	/**
	 * Raise or lower the terrain around a world position with a smooth falloff.
	 *
	 * @param LandscapeName - Label or name of the landscape actor
	 * @param WorldX - Center X
	 * @param WorldY - Center Y
	 * @param Radius - Brush radius in world units
	 * @param HeightDelta - Height change in world units (positive = raise, negative = lower)
	 * @param bSmoothFalloff - Smooth cosine falloff instead of flat brush
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Landscape")
	static FOpenWorldLandscapeResult SculptAtLocation(
		const FString& LandscapeName,
		float WorldX,
		float WorldY,
		float Radius,
		float HeightDelta,
		bool bSmoothFalloff = true);

	/**
	 * Flatten the terrain around a world position to a target height.
	 *
	 * @param LandscapeName - Label or name of the landscape actor
	 * @param WorldX - Center X
	 * @param WorldY - Center Y
	 * @param Radius - Brush radius in world units
	 * @param TargetHeight - Absolute target height in world units
	 * @param bSmoothFalloff - Smooth cosine falloff
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Landscape")
	static FOpenWorldLandscapeResult FlattenAtLocation(
		const FString& LandscapeName,
		float WorldX,
		float WorldY,
		float Radius,
		float TargetHeight,
		bool bSmoothFalloff = true);

	/**
	 * Add Perlin-like noise to the terrain in a circular region.
	 *
	 * @param LandscapeName - Label or name of the landscape actor
	 * @param WorldCenterX - Center X
	 * @param WorldCenterY - Center Y
	 * @param Radius - Affected radius in world units
	 * @param Amplitude - Max height variation in world units
	 * @param Frequency - Noise frequency (0.001-0.01 typical)
	 * @param Seed - Random seed
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Landscape")
	static FOpenWorldLandscapeResult ApplyNoise(
		const FString& LandscapeName,
		float WorldCenterX,
		float WorldCenterY,
		float Radius,
		float Amplitude,
		float Frequency = 0.005f,
		int32 Seed = 0);

	/**
	 * Raise a smooth radial mountain.
	 *
	 * @param LandscapeName - Label or name of the landscape actor
	 * @param CenterX - Center X
	 * @param CenterY - Center Y
	 * @param Radius - Base radius in world units
	 * @param Height - Peak height delta in world units
	 * @param Sharpness - Profile power (1.0 cosine, 2.0 peaked, 0.5 wide)
	 * @return Result
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Landscape")
	static FOpenWorldLandscapeResult CreateMountain(
		const FString& LandscapeName,
		float CenterX,
		float CenterY,
		float Radius,
		float Height,
		float Sharpness = 1.0f);

	/**
	 * Apply a per-vertex height operation inside a world-space rectangle, gated by a mask.
	 * The op receives the current height in WORLD units plus the world X/Y, and returns the
	 * new height in world units. Vertices whose mask returns false are left untouched.
	 * Shared with the zone tools so terrain can be edited inside arbitrary footprints.
	 */
	static FOpenWorldLandscapeResult ApplyMaskedHeightOp(
		const FString& LandscapeName,
		float MinWorldX,
		float MinWorldY,
		float MaxWorldX,
		float MaxWorldY,
		const TFunction<bool(float WorldX, float WorldY)>& InMask,
		const TFunction<float(float CurrentWorldZ, float WorldX, float WorldY)>& Op);

	/**
	 * Box-blur the terrain heights inside a world-space rectangle, gated by a mask.
	 * Each pass averages a vertex with its 4 neighbours. Vertices whose mask returns false
	 * keep their original height.
	 */
	static FOpenWorldLandscapeResult SmoothMaskedHeights(
		const FString& LandscapeName,
		float MinWorldX,
		float MinWorldY,
		float MaxWorldX,
		float MaxWorldY,
		int32 Passes,
		const TFunction<bool(float WorldX, float WorldY)>& InMask);

	/** Deterministic smooth Perlin-like noise in [-1,1]. */
	static float Noise2D(float X, float Y, int32 Seed);

private:
	static class ALandscape* FindLandscape(const FString& LandscapeName);
};
