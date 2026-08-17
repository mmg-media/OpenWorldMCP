#include "OpenWorldPythonService.h"

#include "IPythonScriptPlugin.h"

FOpenWorldPythonResult UOpenWorldPythonService::ExecutePython(const FString& Command)
{
	FOpenWorldPythonResult Result;
	Result.bSuccess = false;

	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (!Python || !Python->IsPythonAvailable())
	{
		Result.Output = TEXT("PythonScriptPlugin not available. Enable it in Edit > Plugins > Python Script Plugin.");
		return Result;
	}

	FPythonCommandEx PyCommand;
	PyCommand.Command = Command;
	PyCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;

	const bool bSuccess = Python->ExecPythonCommandEx(PyCommand);
	Result.bSuccess = bSuccess;
	Result.Output = PyCommand.CommandResult;
	return Result;
}

bool UOpenWorldPythonService::IsPythonAvailable()
{
	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	return Python && Python->IsPythonAvailable();
}
