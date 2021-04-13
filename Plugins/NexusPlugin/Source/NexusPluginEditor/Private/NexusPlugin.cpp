// Copyright Epic Games, Inc. All Rights Reserved.

#include "NexusPluginEditor.h"
#include "Core.h"
#include "Modules/ModuleManager.h"
#include "nexusfile.h"

#define LOCTEXT_NAMESPACE "FNexusPluginEditorModule"

void FNexusPluginEditorModule::StartupModule()
{

}

void FNexusPluginEditorModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNexusPluginEditorModule, NexusPluginEditor)