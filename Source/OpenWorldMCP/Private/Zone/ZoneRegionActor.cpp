#include "Zone/ZoneRegionActor.h"

#include "Components/SceneComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

AZoneRegionActor::AZoneRegionActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Icon = CreateDefaultSubobject<UBillboardComponent>(TEXT("Icon"));
	Icon->SetupAttachment(SceneRoot);
	Icon->SetHiddenInGame(true);
	Icon->bIsScreenSizeScaled = true;

	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> IconTexture(TEXT("/Engine/EditorResources/S_Sphere.S_Sphere"));
		if (IconTexture.Succeeded())
		{
			Icon->Sprite = IconTexture.Object;
		}
	}
}

void AZoneRegionActor::SyncToZone()
{
	if (!SceneRoot)
	{
		return;
	}

	const float Z = Zone.GroundZ;
	if (Zone.bIsPolygon)
	{
		FBox2D Bounds(ForceInit);
		for (const FVector2D& P : Zone.Points)
		{
			Bounds += P;
		}
		const FVector2D Center = Bounds.GetCenter();
		SceneRoot->SetWorldLocation(FVector(Center.X, Center.Y, Z));
		Icon->SetWorldLocation(FVector(Center.X, Center.Y, Z + 200.f));
	}
	else if (Zone.Points.Num() >= 2)
	{
		const FVector2D Min = Zone.Points[0];
		const FVector2D Max = Zone.Points[1];
		const FVector2D Center = (Min + Max) * 0.5f;
		SceneRoot->SetWorldLocation(FVector(Center.X, Center.Y, Z));
		Icon->SetWorldLocation(FVector(Center.X, Center.Y, Z + 200.f));
	}
}

void AZoneRegionActor::PostLoad()
{
	Super::PostLoad();
	SyncToZone();
}

void AZoneRegionActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncToZone();
}
