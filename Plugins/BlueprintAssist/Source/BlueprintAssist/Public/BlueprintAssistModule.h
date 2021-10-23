// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class UBARootObject;
/**
* The public interface to this module
*/
class IBlueprintAssistModule : public IModuleInterface
{
public:
	/**
	* Singleton-like access to this module's interface.  This is just for convenience!
	* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	*
	* @return Returns singleton instance, loading the module on demand if needed
	*/
	static IBlueprintAssistModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IBlueprintAssistModule>("BlueprintAssist");
	}

	/**
	* Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	*
	* @return True if the module is loaded and ready to use
	*/
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("BlueprintAssist");
	}

	virtual UBARootObject* GetRootObject() = 0;

	virtual bool IsUsingUObjects() = 0;
};
