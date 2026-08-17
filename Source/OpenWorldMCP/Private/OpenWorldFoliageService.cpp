#include "OpenWorldFoliageService.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Editor.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LevelEditorViewport.h"
#include "Math/UnrealMathUtility.h"
#include "EngineUtils.h"

static bool IsValidScatterRequest(const FString& MeshPath, int32 Count)
{
	return !MeshPath.IsEmpty() && Count > 0;
}

static FOpenWorldScatterResult ScatterError(const FString& Message)
{
	FOpenWorldScatterResult Result;
	Result.bSuccess = false;
	Result.InstancesSpawned = 0;
	Result.ErrorMessage = Message;
	return Result;
}

UWorld* UOpenWorldFoliageService::GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

/**
 * Spawn an actor with an InstancedStaticMeshComponent for the given transforms.
 * Shared by the foliage and zone toolsets.
 */
static AActor* SpawnInstancedMesh(
	const FString& MeshPath,
	const TArray<FTransform>& Transforms,
	const FString& ActorLabel,
	UWorld* World)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh || Transforms.Num() == 0)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*ActorLabel);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!Actor)
	{
		Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	}

	if (!Actor)
	{
		return nullptr;
	}

	Actor->SetActorLabel(ActorLabel, false);

	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Actor);
	ISM->SetupAttachment(Actor->GetRootComponent() ? Actor->GetRootComponent() : nullptr);
	ISM->RegisterComponent();
	ISM->SetStaticMesh(Mesh);

	for (const FTransform& T : Transforms)
	{
		ISM->AddInstance(T, false);
	}
	ISM->UpdateBounds();
	ISM->MarkRenderStateDirty();
	return Actor;
}

/**
 * Traces straight down onto the landscape surface using a physics line trace.
 * Deterministic and avoids foliage subsystem interactions.
 */
bool UOpenWorldFoliageService::TraceToLandscape(UWorld* World, float X, float Y, FVector& OutLocation, FVector& OutNormal)
{
	if (!World)
	{
		return false;
	}

	const FVector Start(X, Y, 50000.f);
	const FVector End(X, Y, -50000.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OpenWorldFoliageTrace), /*bTraceComplex=*/true);
	Params.bReturnFaceIndex = false;

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (HitActor && HitActor->IsA<ALandscape>())
		{
			OutLocation = Hit.ImpactPoint;
			OutNormal = Hit.ImpactNormal;
			return true;
		}
	}

	return false;
}

FOpenWorldScatterResult UOpenWorldFoliageService::ScatterInternal(
	const FString& MeshPath,
	const TArray<FVector2D>& Candidates,
	int32 Count,
	float MinScale,
	float MaxScale,
	bool bAlignToNormal,
	bool bRandomYaw,
	int32 Seed,
	const FString& ActorLabel)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ScatterError(TEXT("No editor world available"));
	}

	if (Candidates.Num() == 0)
	{
		return ScatterError(TEXT("No candidate positions generated"));
	}

	FRandomStream Stream(Seed != 0 ? Seed : FMath::Rand());
	const int32 NumToSpawn = FMath::Min(Count, Candidates.Num());

	TArray<FTransform> Transforms;
	Transforms.Reserve(NumToSpawn);

	int32 Spawned = 0;
	for (int32 i = 0; i < Candidates.Num() && Spawned < NumToSpawn; ++i)
	{
		const FVector2D& P = Candidates[i];
		FVector Location;
		FVector Normal;
		if (!TraceToLandscape(World, P.X, P.Y, Location, Normal))
		{
			continue;
		}

		FQuat Rotation = FQuat::Identity;
		if (bRandomYaw)
		{
			Rotation = FQuat(FRotator(0.f, Stream.FRandRange(0.f, 360.f), 0.f));
		}
		if (bAlignToNormal && !Normal.IsNearlyZero())
		{
			const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
			Rotation = Align * Rotation;
		}

		const float Scale = Stream.FRandRange(MinScale, MaxScale);
		Transforms.Add(FTransform(Rotation, Location, FVector(Scale)));

		++Spawned;
	}

	if (Transforms.Num() == 0)
	{
		return ScatterError(TEXT("No landscape surface found under candidates"));
	}

	SpawnInstancedMesh(MeshPath, Transforms, ActorLabel, World);

	FOpenWorldScatterResult Result;
	Result.bSuccess = true;
	Result.InstancesSpawned = Transforms.Num();
	Result.ErrorMessage = TEXT("");
	return Result;
}

FOpenWorldScatterResult UOpenWorldFoliageService::ScatterOnLandscape(
	const FString& MeshPath,
	float WorldCenterX,
	float WorldCenterY,
	float Radius,
	int32 Count,
	float MinScale,
	float MaxScale,
	bool bAlignToNormal,
	bool bRandomYaw,
	int32 Seed,
	const FString& ActorLabel)
{
	if (!IsValidScatterRequest(MeshPath, Count))
	{
		return ScatterError(TEXT("MeshPath empty or Count <= 0"));
	}
	if (Radius <= 0.f)
	{
		return ScatterError(TEXT("Radius must be > 0"));
	}

	// Poisson-disk-like candidate generation: simple random-in-circle, oversampled.
	const int32 NumCandidates = FMath::Max(Count * 2, 16);
	TArray<FVector2D> Candidates;
	Candidates.Reserve(NumCandidates);

	FRandomStream Stream(Seed != 0 ? Seed : FMath::Rand());
	for (int32 i = 0; i < NumCandidates; ++i)
	{
		const float Angle = Stream.FRandRange(0.f, 2.f * PI);
		const float R = Radius * FMath::Sqrt(Stream.FRandRange(0.f, 1.f));
		Candidates.Add(FVector2D(
			WorldCenterX + R * FMath::Cos(Angle),
			WorldCenterY + R * FMath::Sin(Angle)));
	}

	return ScatterInternal(MeshPath, Candidates, Count, MinScale, MaxScale, bAlignToNormal, bRandomYaw, Seed, ActorLabel);
}

FOpenWorldScatterResult UOpenWorldFoliageService::ScatterRectOnLandscape(
	const FString& MeshPath,
	float WorldMinX,
	float WorldMinY,
	float WorldMaxX,
	float WorldMaxY,
	int32 Count,
	float MinScale,
	float MaxScale,
	bool bAlignToNormal,
	bool bRandomYaw,
	int32 Seed,
	const FString& ActorLabel)
{
	if (!IsValidScatterRequest(MeshPath, Count))
	{
		return ScatterError(TEXT("MeshPath empty or Count <= 0"));
	}
	if (WorldMaxX <= WorldMinX || WorldMaxY <= WorldMinY)
	{
		return ScatterError(TEXT("Invalid rectangle bounds"));
	}

	const int32 NumCandidates = FMath::Max(Count * 2, 16);
	TArray<FVector2D> Candidates;
	Candidates.Reserve(NumCandidates);

	FRandomStream Stream(Seed != 0 ? Seed : FMath::Rand());
	for (int32 i = 0; i < NumCandidates; ++i)
	{
		Candidates.Add(FVector2D(
			Stream.FRandRange(WorldMinX, WorldMaxX),
			Stream.FRandRange(WorldMinY, WorldMaxY)));
	}

	return ScatterInternal(MeshPath, Candidates, Count, MinScale, MaxScale, bAlignToNormal, bRandomYaw, Seed, ActorLabel);
}

FOpenWorldScatterResult UOpenWorldFoliageService::ScatterWeightedInternal(
	const TArray<FString>& MeshPaths,
	const TArray<float>& MeshWeights,
	const TArray<FVector2D>& Candidates,
	int32 Count,
	float MinScale,
	float MaxScale,
	bool bAlignToNormal,
	bool bRandomYaw,
	int32 Seed,
	const FString& ActorLabel)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ScatterError(TEXT("No editor world available"));
	}

	if (MeshPaths.Num() == 0 || MeshWeights.Num() != MeshPaths.Num())
	{
		return ScatterError(TEXT("MeshPaths/Weights empty or mismatched"));
	}

	// Normalize weights into a cumulative range [0,1).
	float TotalWeight = 0.f;
	for (const float W : MeshWeights)
	{
		TotalWeight += FMath::Max(0.f, W);
	}
	if (TotalWeight <= 0.f)
	{
		return ScatterError(TEXT("Total weight is zero"));
	}

	TArray<float> Cumulative;
	Cumulative.Reserve(MeshPaths.Num());
	float Acc = 0.f;
	for (const float W : MeshWeights)
	{
		Acc += FMath::Max(0.f, W) / TotalWeight;
		Cumulative.Add(Acc);
	}

	const int32 NumToSpawn = FMath::Min(Count, Candidates.Num());
	if (NumToSpawn <= 0)
	{
		return ScatterError(TEXT("No candidate positions generated"));
	}

	FRandomStream Stream(Seed != 0 ? Seed : FMath::Rand());

	// Per-mesh transform lists.
	TArray<TArray<FTransform>> PerMeshTransforms;
	PerMeshTransforms.SetNum(MeshPaths.Num());
	for (TArray<FTransform>& Transforms : PerMeshTransforms)
	{
		Transforms.Reserve(NumToSpawn);
	}

	int32 Spawned = 0;
	for (int32 i = 0; i < Candidates.Num() && Spawned < NumToSpawn; ++i)
	{
		const FVector2D& P = Candidates[i];
		FVector Location;
		FVector Normal;
		if (!TraceToLandscape(World, P.X, P.Y, Location, Normal))
		{
			continue;
		}

		// Weighted mesh selection.
		const float Roll = Stream.FRandRange(0.f, 1.f);
		int32 MeshIndex = 0;
		for (int32 m = 0; m < Cumulative.Num(); ++m)
		{
			if (Roll < Cumulative[m])
			{
				MeshIndex = m;
				break;
			}
		}

		FQuat Rotation = FQuat::Identity;
		if (bRandomYaw)
		{
			Rotation = FQuat(FRotator(0.f, Stream.FRandRange(0.f, 360.f), 0.f));
		}
		if (bAlignToNormal && !Normal.IsNearlyZero())
		{
			const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
			Rotation = Align * Rotation;
		}

		const float Scale = Stream.FRandRange(MinScale, MaxScale);
		PerMeshTransforms[MeshIndex].Add(FTransform(Rotation, Location, FVector(Scale)));
		++Spawned;
	}

	if (Spawned == 0)
	{
		return ScatterError(TEXT("No landscape surface found under candidates"));
	}

	// Spawn one ISM actor per mesh that received instances.
	int32 TotalSpawned = 0;
	for (int32 m = 0; m < PerMeshTransforms.Num(); ++m)
	{
		if (PerMeshTransforms[m].Num() == 0)
		{
			continue;
		}
		const FString Label = MeshPaths.Num() > 1
			? FString::Printf(TEXT("%s_%d"), *ActorLabel, m)
			: ActorLabel;
		AActor* Actor = SpawnInstancedMesh(MeshPaths[m], PerMeshTransforms[m], Label, World);
		if (Actor)
		{
			TotalSpawned += PerMeshTransforms[m].Num();
		}
	}

	FOpenWorldScatterResult Result;
	Result.bSuccess = TotalSpawned > 0;
	Result.InstancesSpawned = TotalSpawned;
	Result.ErrorMessage = TotalSpawned > 0 ? TEXT("") : TEXT("Failed to spawn meshes");
	return Result;
}
