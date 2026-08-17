#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "OpenWorldPythonService.generated.h"

USTRUCT(BlueprintType)
struct FOpenWorldPythonResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Python")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "OpenWorld|Python")
	FString Output;
};

UCLASS(BlueprintType)
class OPENWORLDMCP_API UOpenWorldPythonService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Execute a Python command inside the Unreal Editor.
	 * Runs literal Python code (e.g. "import unreal; print(unreal.EditorLevelLibrary.get_all_level_actors())").
	 * Works only with the PythonScriptPlugin enabled.
	 *
	 * @param Command - Python code to execute
	 * @return Result with success flag and captured output
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Python")
	static FOpenWorldPythonResult ExecutePython(const FString& Command);

	/**
	 * Check whether the Python scripting plugin is available and initialized.
	 *
	 * @return True if Python can execute commands
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "OpenWorld|Python")
	static bool IsPythonAvailable();
};
