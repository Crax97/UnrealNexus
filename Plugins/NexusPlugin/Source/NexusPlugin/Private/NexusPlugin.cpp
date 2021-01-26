// Copyright Epic Games, Inc. All Rights Reserved.

#include "NexusPlugin.h"
#include "Core.h"
#include "Modules/ModuleManager.h"
#include "nexusfile.h"

#define LOCTEXT_NAMESPACE "FNexusPluginModule"

void FNexusPluginModule::StartupModule()
{

}

void FNexusPluginModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNexusPluginModule, NexusPlugin)