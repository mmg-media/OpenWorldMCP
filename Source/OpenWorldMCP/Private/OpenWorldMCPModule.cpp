#include "OpenWorldMCPModule.h"

#include "Misc/CoreDelegates.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectIterator.h"
#include "OpenWorldFoliageService.h"

#define LOCTEXT_NAMESPACE "FOpenWorldMCPModule"

void FOpenWorldMCPModule::StartupModule()
{
	if (UToolsetRegistry::IsAvailable())
	{
		RegisterToolsets();
	}
	else
	{
		// The ToolsetRegistry is only available after engine init, so defer registration
		// until the engine startup has completed.
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FOpenWorldMCPModule::RegisterToolsets);
	}
}

void FOpenWorldMCPModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	UnregisterToolsets();
}

void FOpenWorldMCPModule::RegisterToolsets()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (!UToolsetRegistry::IsAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("OpenWorldMCP: ToolsetRegistry not available; toolsets not registered."));
		return;
	}

	TArray<UClass*> ToolsetClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UToolsetDefinition::StaticClass()) &&
			Class->GetOuterUPackage() &&
			Class->GetOuterUPackage()->GetName().StartsWith(TEXT("/Script/OpenWorldMCP")))
		{
			bool bHasAICallable = false;
			for (TFieldIterator<UFunction> FnIt(Class, EFieldIteratorFlags::IncludeSuper); FnIt; ++FnIt)
			{
				if (FnIt->HasMetaData(TEXT("AICallable")))
				{
					bHasAICallable = true;
					break;
				}
			}
			if (bHasAICallable)
			{
				ToolsetClasses.Add(Class);
			}
		}
	}

	for (UClass* Class : ToolsetClasses)
	{
		UToolsetRegistry::RegisterToolsetClass(Class);
	}

	UE_LOG(LogTemp, Display, TEXT("OpenWorldMCP: registered %d toolset class(es) with ToolsetRegistry."), ToolsetClasses.Num());
}

void FOpenWorldMCPModule::UnregisterToolsets()
{
	if (!UToolsetRegistry::IsAvailable())
	{
		return;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UToolsetDefinition::StaticClass()) &&
			Class->GetOuterUPackage() &&
			Class->GetOuterUPackage()->GetName().StartsWith(TEXT("/Script/OpenWorldMCP")))
		{
			UToolsetRegistry::UnregisterToolsetClass(Class);
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOpenWorldMCPModule, OpenWorldMCP)
