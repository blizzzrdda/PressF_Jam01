// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistGlobals.h"

#include "Misc/LazySingleton.h"

DEFINE_LOG_CATEGORY(LogBlueprintAssist);

FBAGlobals& FBAGlobals::Get()
{
	return TLazySingleton<FBAGlobals>::Get();
}