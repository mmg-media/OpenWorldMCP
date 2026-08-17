#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldZoneTypes.h"
#include "ZoneRegionActor.generated.h"

class USceneComponent;
class UBillboardComponent;

/**
 * Persistent marker for a marked zone/region in the level. These are spawned by the zone
 * marking mode (mouse drag = rectangle, click points = polygon) or programmatically via MCP.
 * They survive editor restarts and are queried by the OpenWorld Zone MCP tools.
 */
UCLASS(BlueprintType, hidecategories = (Collision, Physics, Input, Movement, LOD, Rendering))
class OPENWORLDMCP_API AZoneRegionActor : public AActor
{
	GENERATED_BODY()

public:
	AZoneRegionActor();

	/** The zone data this actor represents. */
	UPROPERTY(BlueprintReadOnly, Category = "OpenWorld|Zone", EditAnywhere)
	FOpenWorldZone Zone;

	/** Recompute the actor transform/scale to visually match the zone footprint. */
	void SyncToZone();

	//~ Begin AActor
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End AActor

protected:
	UPROPERTY(VisibleAnywhere, Category = "OpenWorld|Zone")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "OpenWorld|Zone")
	TObjectPtr<UBillboardComponent> Icon;
};
