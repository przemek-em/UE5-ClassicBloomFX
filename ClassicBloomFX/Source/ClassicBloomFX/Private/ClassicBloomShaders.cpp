// Licensed under the MIT License. See LICENSE file in the project root.

#include "ClassicBloomShaders.h"
#include "ShaderParameterUtils.h"

// Implement the pixel shaders
IMPLEMENT_GLOBAL_SHADER(FClassicBloomBrightPassPS, "/Plugin/ClassicBloomFX/Private/ClassicBloomShaders.usf", "BrightPassPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FClassicBloomBlurPS, "/Plugin/ClassicBloomFX/Private/ClassicBloomBlur.usf", "GaussianBlurPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FClassicBloomCompositePS, "/Plugin/ClassicBloomFX/Private/ClassicBloomComposite.usf", "CompositeBloomPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FClassicBloomGlareStreakPS, "/Plugin/ClassicBloomFX/Private/ClassicBloomGlare.usf", "GlareStreakPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FClassicBloomGlareAccumulatePS, "/Plugin/ClassicBloomFX/Private/ClassicBloomGlare.usf", "GlareAccumulatePS", SF_Pixel);

// Kawase bloom shaders
IMPLEMENT_GLOBAL_SHADER(FClassicBloomKawaseDownsamplePS, "/Plugin/ClassicBloomFX/Private/ClassicBloomKawase.usf", "KawaseDownsamplePS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FClassicBloomKawaseUpsamplePS, "/Plugin/ClassicBloomFX/Private/ClassicBloomKawase.usf", "KawaseUpsamplePS", SF_Pixel);
