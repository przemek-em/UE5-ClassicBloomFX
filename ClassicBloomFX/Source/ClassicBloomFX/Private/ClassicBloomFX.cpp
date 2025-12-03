// Licensed under the MIT License. See LICENSE file in the project root.

#include "ClassicBloomFX.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FClassicBloomFXModule"

void FClassicBloomFXModule::StartupModule()
{
	// Register shader directory
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ClassicBloomFX"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/ClassicBloomFX"), PluginShaderDir);
}

void FClassicBloomFXModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FClassicBloomFXModule, ClassicBloomFX)
