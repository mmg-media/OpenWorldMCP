#pragma once

#include "Modules/ModuleManager.h"

class FOpenWorldMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterToolsets();
	void UnregisterToolsets();

	FDelegateHandle PostEngineInitHandle;
};
