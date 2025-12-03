// Licensed under the MIT License. See LICENSE file in the project root.

#include "ClassicBloomSubsystem.h"
#include "BloomFXComponent.h"
#include "ClassicBloomShaders.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "PixelShaderUtils.h"

// ============================================================================
// FClassicBloomSceneViewExtension Implementation
// ============================================================================

FClassicBloomSceneViewExtension::FClassicBloomSceneViewExtension(const FAutoRegister& AutoRegister, UClassicBloomSubsystem* InSubsystem)
	: FSceneViewExtensionBase(AutoRegister)
	, WeakSubsystem(InSubsystem)
{
}

void FClassicBloomSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	// now do rendering in the PostProcessPass_RenderThread instead
}

void FClassicBloomSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Filter out unwanted views at subscription time
	const FSceneViewFamily* Family = View.Family;
	if (!Family)
	{
		return;
	}

	// Skip editor preview scenes (material editor, mesh editor, etc.)
	if (Family->Scene && Family->Scene->GetWorld())
	{
		UWorld* World = Family->Scene->GetWorld();
		
		// Only allow Game, Editor, and PIE worlds
		if (World->WorldType != EWorldType::Game && 
			World->WorldType != EWorldType::Editor &&
			World->WorldType != EWorldType::PIE)
		{
			// Skip other world types (material preview, thumbnail, etc.)
			return;
		}
	}

	// Skip if post-processing is disabled (common in material previews)
	if (!Family->EngineShowFlags.PostProcessing)
	{
		return;
	}

	// Skip if rendering is disabled or in wireframe mode
	if (!Family->EngineShowFlags.Rendering || Family->EngineShowFlags.Wireframe)
	{
		return;
	}

	// Get subsystem to check which pass to subscribe to
	UClassicBloomSubsystem* Subsystem = WeakSubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	// Find active component and get desired post-process pass
	EPostProcessingPass DesiredPass = EPostProcessingPass::MotionBlur; // Default
	bool bEnableDebug = false;
	
	const TArray<TWeakObjectPtr<UBloomFXComponent>>& Components = Subsystem->GetBloomComponents();
	for (const TWeakObjectPtr<UBloomFXComponent>& CompPtr : Components)
	{
		if (CompPtr.IsValid() && CompPtr->IsActive())
		{
			// Convert our custom enum to engine enum
			switch (CompPtr->PostProcessPass)
			{
				case EBloomPostProcessPass::Tonemap:
					DesiredPass = EPostProcessingPass::Tonemap;
					break;
				case EBloomPostProcessPass::MotionBlur:
					DesiredPass = EPostProcessingPass::MotionBlur;
					break;
				case EBloomPostProcessPass::FXAA:
					DesiredPass = EPostProcessingPass::FXAA;
					break;
				case EBloomPostProcessPass::VisualizeDepthOfField:
					DesiredPass = EPostProcessingPass::VisualizeDepthOfField;
					break;
				default:
					DesiredPass = EPostProcessingPass::MotionBlur;
					break;
			}
			bEnableDebug = CompPtr->bEnableDebugLogging;
			break;
		}
	}

	// Subscribe to the selected post-process pass
	if (PassId == DesiredPass)
	{
		// Prevent double-application in PIE mode
		// SubscribeToPostProcessingPass can be called multiple times (once per view)
		// Check if callbacks array is empty - if not, already subscribed for this pass
		// This prevents the effect from being applied multiple times
		if (InOutPassCallbacks.Num() > 0)
		{
			// Already have callbacks for this pass, skip to prevent double-application
			if (bEnableDebug)
			{
				static double LastSkipLogTime = 0.0;
				double CurrentTime = FPlatformTime::Seconds();
				if (CurrentTime - LastSkipLogTime > 2.0)
				{
					LastSkipLogTime = CurrentTime;
					UE_LOG(LogTemp, Warning, TEXT("ClassicBloom: Skipped duplicate subscription (already %d callbacks), preventing double-application"), 
						InOutPassCallbacks.Num());
				}
			}
			return;
		}
		
		// Throttled debug logging (once per second max)
		if (bEnableDebug)
		{
			static double LastSubscribeLogTime = 0.0;
			double CurrentTime = FPlatformTime::Seconds();
			if (CurrentTime - LastSubscribeLogTime > 1.0)
			{
				LastSubscribeLogTime = CurrentTime;
				UE_LOG(LogTemp, Log, TEXT("ClassicBloom: Subscribed to pass %d (WorldType: %d, PassEnabled: %d)"),
					(int32)PassId, Family->Scene ? (int32)Family->Scene->GetWorld()->WorldType : -1, bIsPassEnabled);
			}
		}
		
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FClassicBloomSceneViewExtension::PostProcessPass_RenderThread));
	}
}

bool FClassicBloomSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!WeakSubsystem.IsValid())
	{
		return false;
	}

	// Check any active bloom components
	const TArray<TWeakObjectPtr<UBloomFXComponent>>& Components = WeakSubsystem->GetBloomComponents();
	bool bHasActiveComponents = false;
	for (const TWeakObjectPtr<UBloomFXComponent>& Component : Components)
	{
		if (Component.IsValid() && Component->IsActive())
		{
			bHasActiveComponents = true;
			break;
		}
	}

	return bHasActiveComponents;
}

FScreenPassTexture FClassicBloomSceneViewExtension::PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	check(IsInRenderingThread());

	// Get the scene color input first - return this if skip rendering
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// Additional safety check: Only apply to game/editor viewports
	// Skip material previews, thumbnail renders, reflection captures, etc.
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	
	// Check for special view types that should NOT have bloom
	if (ViewInfo.bIsReflectionCapture || 
		ViewInfo.bIsSceneCapture ||
		!ViewInfo.bIsViewInfo)
	{
		// Return scene color unchanged for special views
		return SceneColor;
	}
	
	// Check engine show flags - skip if rendering is disabled or if this is in an incompatible mode
	if (ViewInfo.Family->EngineShowFlags.Rendering == 0 ||
		ViewInfo.Family->EngineShowFlags.PostProcessing == 0 ||
		ViewInfo.Family->EngineShowFlags.Wireframe != 0)
	{
		// Skip rendering in wireframe or when post-processing is disabled
		return SceneColor;
	}

	// Cast to FViewInfo to access shader map and view properties
	const FViewInfo* ViewInfoPtr = static_cast<const FViewInfo*>(&View);
	if (!ViewInfoPtr || !ViewInfoPtr->ShaderMap)
	{
		// If shader map is not available, just return scene color unchanged
		return SceneColor;
	}

	// Get subsystem and components
	UClassicBloomSubsystem* Subsystem = WeakSubsystem.Get();
	if (!Subsystem)
	{
		return SceneColor;
	}

	// Get bloom settings from active components
	const TArray<TWeakObjectPtr<UBloomFXComponent>>& Components = Subsystem->GetBloomComponents();
	UBloomFXComponent* ActiveComponent = nullptr;
	
	for (const TWeakObjectPtr<UBloomFXComponent>& CompPtr : Components)
	{
		if (CompPtr.IsValid() && CompPtr->IsActive())
		{
			ActiveComponent = CompPtr.Get();
			break;
		}
	}

	// Check if have any active effect to process
	if (!ActiveComponent || ActiveComponent->BloomIntensity <= 0.0f)
	{
		return SceneColor;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "ClassicBloom");

	const FIntPoint SceneColorExtent = SceneColor.Texture->Desc.Extent;
	const FIntRect ViewRect = SceneColor.ViewRect;  // Use SceneColor.ViewRect consistently
	const FGlobalShaderMap* GlobalShaderMap = ViewInfoPtr->ShaderMap;

	// Check if debug logging is enabled (throttled to avoid spam)
	bool bDebugLog = ActiveComponent->bEnableDebugLogging;
	static double LastLogTime = 0.0;
	double CurrentTime = FPlatformTime::Seconds();
	bool bShouldLog = bDebugLog && (CurrentTime - LastLogTime > 1.0); // Log once per second max
	if (bShouldLog)
	{
		LastLogTime = CurrentTime;
		
		// Convert enum to pass name
		const TCHAR* PassName = TEXT("Unknown");
		switch (ActiveComponent->PostProcessPass)
		{
			case EBloomPostProcessPass::Tonemap: PassName = TEXT("Tonemap"); break;
			case EBloomPostProcessPass::MotionBlur: PassName = TEXT("MotionBlur"); break;
			case EBloomPostProcessPass::FXAA: PassName = TEXT("FXAA"); break;
			case EBloomPostProcessPass::VisualizeDepthOfField: PassName = TEXT("VisualizeDOF"); break;
		}
		
		const TCHAR* WorldTypeName = TEXT("Unknown");
		bool bIsGameWorld = false;
		bool bIsPIE = false;
		if (ViewInfo.Family->Scene && ViewInfo.Family->Scene->GetWorld())
		{
			UWorld* World = ViewInfo.Family->Scene->GetWorld();
			bIsGameWorld = World->IsGameWorld();
			switch (World->WorldType)
			{
				case EWorldType::None: WorldTypeName = TEXT("None"); break;
				case EWorldType::Game: WorldTypeName = TEXT("Game"); break;
				case EWorldType::Editor: WorldTypeName = TEXT("Editor"); break;
				case EWorldType::PIE: WorldTypeName = TEXT("PIE"); bIsPIE = true; break;
				case EWorldType::EditorPreview: WorldTypeName = TEXT("EditorPreview"); break;
				case EWorldType::GamePreview: WorldTypeName = TEXT("GamePreview"); break;
				case EWorldType::Inactive: WorldTypeName = TEXT("Inactive"); break;
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("========== CUSTOM BLOOM DEBUG =========="));
		UE_LOG(LogTemp, Warning, TEXT("WORLD INFO:"));
		UE_LOG(LogTemp, Warning, TEXT("  WorldType: %s | IsGameWorld: %d | IsPIE: %d"), WorldTypeName, bIsGameWorld, bIsPIE);
		UE_LOG(LogTemp, Warning, TEXT("  PostProcessPass: %s"), PassName);
		UE_LOG(LogTemp, Warning, TEXT("VIEW INFO:"));
		UE_LOG(LogTemp, Warning, TEXT("  ViewRect: [%d,%d] -> [%d,%d] (Size: %dx%d)"), 
			ViewRect.Min.X, ViewRect.Min.Y,
			ViewRect.Max.X, ViewRect.Max.Y,
			ViewRect.Width(), ViewRect.Height());
		UE_LOG(LogTemp, Warning, TEXT("  Extent: %dx%d"), SceneColorExtent.X, SceneColorExtent.Y);
		UE_LOG(LogTemp, Warning, TEXT("  bIsGameView: %d | bIsSceneCapture: %d | bIsReflectionCapture: %d"),
			ViewInfo.bIsGameView, ViewInfo.bIsSceneCapture, ViewInfo.bIsReflectionCapture);
		UE_LOG(LogTemp, Warning, TEXT("BLOOM SETTINGS:"));
		UE_LOG(LogTemp, Warning, TEXT("  BloomIntensity: %.3f | BloomThreshold: %.3f | BloomSize: %.3f"),
			ActiveComponent->BloomIntensity, ActiveComponent->BloomThreshold, ActiveComponent->BloomSize);
		UE_LOG(LogTemp, Warning, TEXT("  bUseSceneColor: %d | BloomTint: (%.2f, %.2f, %.2f)"),
			ActiveComponent->bUseSceneColor, 
			ActiveComponent->BloomTint.R, ActiveComponent->BloomTint.G, ActiveComponent->BloomTint.B);
		UE_LOG(LogTemp, Warning, TEXT("BLOOM MODE:"));
		UE_LOG(LogTemp, Warning, TEXT("  BloomMode: %d (0=Standard, 1=DirectionalGlare, 2=Kawase, 3=SoftFocus)"),
			(int32)ActiveComponent->BloomMode);
		UE_LOG(LogTemp, Warning, TEXT("QUALITY:"));
		UE_LOG(LogTemp, Warning, TEXT("  DownsampleScale: %.2f | BlurPasses: %d | BlurSamples: %d"),
			ActiveComponent->DownsampleScale, ActiveComponent->BlurPasses, ActiveComponent->BlurSamples);
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
	}

	// Validate viewport rect
	if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0)
	{
		// Invalid viewport, return scene color unchanged
		return SceneColor;
	}

	// Step 1: Extract bright pixels (downsample based on quality setting)
	float DownsampleScale = FMath::Clamp(ActiveComponent->DownsampleScale, 0.25f, 2.0f);
	int32 Divisor = FMath::Max(1, FMath::RoundToInt(2.0f / DownsampleScale));
	
	// Use ViewRect (actual content size) for both extent AND rect
	// This ensures texture extent == viewport size, avoiding UV mapping issues
	// when the source texture has padding (extent > viewport)
	FIntPoint DownsampledExtent = FIntPoint::DivideAndRoundUp(FIntPoint(ViewRect.Width(), ViewRect.Height()), Divisor);
	
	// Rect starts at (0,0) and matches extent exactly
	FIntRect DownsampledRect = FIntRect(FIntPoint::ZeroValue, DownsampledExtent);

	// Validate downsampled rect
	if (DownsampledRect.Width() <= 0 || DownsampledRect.Height() <= 0)
	{
		// Invalid downsampled viewport, return scene color unchanged
		return SceneColor;
	}

	FRDGTextureDesc BrightPassDesc = FRDGTextureDesc::Create2D(
		DownsampledExtent,
		PF_FloatR11G11B10,
		FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_RenderTargetable);

	FRDGTextureRef BrightPassTexture = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.BrightPass"));

	// Bright pass shader
	{
		TShaderMapRef<FClassicBloomBrightPassPS> PixelShader(GlobalShaderMap);
		
		// Validate shader is available
		if (!PixelShader.IsValid())
		{
			// Shader not compiled or available, return scene color unchanged
			return SceneColor;
		}
		
		// Determine effective threshold based on bloom mode
		// Soft focus needs the entire scene, so use very low threshold
		// Regular bloom only needs highlights
		float EffectiveThreshold = ActiveComponent->BloomThreshold;
		bool bIsSoftFocusMode = (ActiveComponent->BloomMode == EBloomMode::SoftFocus);
		
		if (bIsSoftFocusMode)
		{
			// Soft focus mode: capture full scene with very low threshold
			EffectiveThreshold = 0.01f;
		}
		
		// Debug log threshold
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("THRESHOLD CALCULATION:"));
			UE_LOG(LogTemp, Warning, TEXT("  BloomThreshold: %.3f | Effective: %.3f"),
				ActiveComponent->BloomThreshold, EffectiveThreshold);
		}
		
		FClassicBloomBrightPassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassicBloomBrightPassPS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneColorTexture = SceneColor.Texture;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->InputViewportSizeAndInvSize = FVector4f(ViewRect.Width(), ViewRect.Height(), 1.0f / ViewRect.Width(), 1.0f / ViewRect.Height());
		PassParameters->OutputViewportSizeAndInvSize = FVector4f(DownsampledRect.Width(), DownsampledRect.Height(), 1.0f / DownsampledRect.Width(), 1.0f / DownsampledRect.Height());
		
		// Create FScreenTransform to map SvPosition to scene color texture UV
		// This properly handles viewport offsets using UE's standard approach:
		// 1. TexelPosition -> ViewportUV: Maps output SvPosition [ViewRect.Min, ViewRect.Max] to [0,1]
		// 2. ViewportUV -> TextureUV: Maps [0,1] to actual texture UV coordinates
		// NOTE: Use actual texture extent for proper UV mapping, not just rect size
		FScreenPassTextureViewport OutputViewport(DownsampledExtent, DownsampledRect);
		FScreenPassTextureViewport InputViewport(FIntPoint(SceneColor.Texture->Desc.Extent.X, SceneColor.Texture->Desc.Extent.Y), SceneColor.ViewRect);
		PassParameters->SvPositionToInputTextureUV = (
			FScreenTransform::ChangeTextureBasisFromTo(OutputViewport, FScreenTransform::ETextureBasis::TexelPosition, FScreenTransform::ETextureBasis::ViewportUV) *
			FScreenTransform::ChangeTextureBasisFromTo(InputViewport, FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV));
		
		PassParameters->BloomThreshold = EffectiveThreshold;
		PassParameters->BloomIntensity = 1.0f; // No longer used in shader, but keep for compatibility
		PassParameters->RenderTargets[0] = FRenderTargetBinding(BrightPassTexture, ERenderTargetLoadAction::EClear);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GlobalShaderMap,
			RDG_EVENT_NAME("BrightPass"),
			PixelShader,
			PassParameters,
			DownsampledRect);
	}

	// Step 2 & 3: Blur passes - Gaussian, Directional Glare, or Kawase bloom
	FRDGTextureRef BlurredBloomTexture = nullptr;
	
	// Check bloom mode from component
	bool bUseDirectionalGlare = (ActiveComponent->BloomMode == EBloomMode::DirectionalGlare);
	bool bUseKawaseBloom = (ActiveComponent->BloomMode == EBloomMode::Kawase);
	bool bUseSoftFocus = (ActiveComponent->BloomMode == EBloomMode::SoftFocus);
	
	if (bUseDirectionalGlare)
	{
		// Directional glare: Apply directional streaks from bright areas
		int32 NumStreaks = FMath::Clamp(ActiveComponent->GlareStreakCount, 2, 16);
		float StreakLength = FMath::Clamp((float)ActiveComponent->GlareStreakLength, 5.0f, 200.0f);
		float RotationOffset = ActiveComponent->GlareRotationOffset;
		float Falloff = FMath::Clamp(ActiveComponent->GlareFalloff, 0.5f, 10.0f);
		
		// Scale streak length for downsampled resolution
		float ScaledStreakLength = StreakLength / (float)Divisor;
		
		// Create streak textures (we'll process up to 4 at a time, then accumulate)
		TArray<FRDGTextureRef> StreakTextures;
		StreakTextures.Reserve(NumStreaks);
		
		// Calculate angle step for even distribution
		float AngleStep = 360.0f / (float)NumStreaks;
		
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("DIRECTIONAL GLARE SETTINGS:"));
			UE_LOG(LogTemp, Warning, TEXT("  NumStreaks: %d | StreakLength: %.1f (scaled: %.1f)"), NumStreaks, StreakLength, ScaledStreakLength);
			UE_LOG(LogTemp, Warning, TEXT("  RotationOffset: %.1f | Falloff: %.2f | AngleStep: %.1f"), RotationOffset, Falloff, AngleStep);
		}
		
		TShaderMapRef<FClassicBloomGlareStreakPS> GlareStreakShader(GlobalShaderMap);
		
		if (!GlareStreakShader.IsValid())
		{
			if (bShouldLog)
			{
				UE_LOG(LogTemp, Warning, TEXT("ClassicBloom: Glare streak shader not available, falling back to standard"));
			}
			// Fall back to standard blur
			bUseDirectionalGlare = false;
		}
		else
		{
			// Process each streak direction
			for (int32 i = 0; i < NumStreaks; ++i)
			{
				float Angle = (AngleStep * (float)i) + RotationOffset;
				float RadAngle = FMath::DegreesToRadians(Angle);
				FVector2f Direction(FMath::Cos(RadAngle), FMath::Sin(RadAngle));
				
				FRDGTextureRef StreakTexture = GraphBuilder.CreateTexture(BrightPassDesc, *FString::Printf(TEXT("ClassicBloom.Streak%d"), i));
				StreakTextures.Add(StreakTexture);
				
				FClassicBloomGlareStreakPS::FParameters* StreakParams = GraphBuilder.AllocParameters<FClassicBloomGlareStreakPS::FParameters>();
				StreakParams->View = View.ViewUniformBuffer;
				StreakParams->SourceTexture = BrightPassTexture;
				StreakParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				StreakParams->BufferSizeAndInvSize = FVector4f(DownsampledExtent.X, DownsampledExtent.Y, 1.0f / DownsampledExtent.X, 1.0f / DownsampledExtent.Y);
				StreakParams->StreakDirection = Direction;
				StreakParams->StreakLength = ScaledStreakLength;
				StreakParams->StreakFalloff = Falloff;
				StreakParams->RenderTargets[0] = FRenderTargetBinding(StreakTexture, ERenderTargetLoadAction::EClear);
				
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("GlareStreak%d", i),
					GlareStreakShader,
					StreakParams,
					DownsampledRect);
			}
			
			// Accumulate all streaks into final glare texture
			// Process in batches of 4 if we have more than 4 streaks
			TShaderMapRef<FClassicBloomGlareAccumulatePS> GlareAccumShader(GlobalShaderMap);
			
			if (GlareAccumShader.IsValid())
			{
				FRDGTextureRef AccumTexture = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.GlareAccum"));
				
				// For simplicity, accumulate first 4 streaks, then blend more in subsequent passes if needed
				int32 StreaksToProcess = FMath::Min(NumStreaks, 4);
				
				FClassicBloomGlareAccumulatePS::FParameters* AccumParams = GraphBuilder.AllocParameters<FClassicBloomGlareAccumulatePS::FParameters>();
				AccumParams->View = View.ViewUniformBuffer;
				AccumParams->StreakTexture0 = StreakTextures[0];
				AccumParams->StreakTexture1 = StreaksToProcess >= 2 ? StreakTextures[1] : StreakTextures[0];
				AccumParams->StreakTexture2 = StreaksToProcess >= 3 ? StreakTextures[2] : StreakTextures[0];
				AccumParams->StreakTexture3 = StreaksToProcess >= 4 ? StreakTextures[3] : StreakTextures[0];
				AccumParams->StreakSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				AccumParams->GlareViewportSizeAndInvSize = FVector4f(DownsampledRect.Width(), DownsampledRect.Height(), 1.0f / DownsampledRect.Width(), 1.0f / DownsampledRect.Height());
				AccumParams->NumStreaks = StreaksToProcess;
				AccumParams->RenderTargets[0] = FRenderTargetBinding(AccumTexture, ERenderTargetLoadAction::EClear);
				
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("GlareAccumulate"),
					GlareAccumShader,
					AccumParams,
					DownsampledRect);
				
				// If we have more than 4 streaks, continue accumulating
				if (NumStreaks > 4)
				{
					FRDGTextureRef PrevAccum = AccumTexture;
					
					for (int32 BatchStart = 4; BatchStart < NumStreaks; BatchStart += 3)
					{
						// Blend previous accumulation with next batch of streaks
						FRDGTextureRef NextAccum = GraphBuilder.CreateTexture(BrightPassDesc, *FString::Printf(TEXT("ClassicBloom.GlareAccum%d"), BatchStart));
						
						int32 StreaksInBatch = FMath::Min(3, NumStreaks - BatchStart);
						
						AccumParams = GraphBuilder.AllocParameters<FClassicBloomGlareAccumulatePS::FParameters>();
						AccumParams->View = View.ViewUniformBuffer;
						AccumParams->StreakTexture0 = PrevAccum; // Previous accumulation
						AccumParams->StreakTexture1 = StreakTextures[BatchStart];
						AccumParams->StreakTexture2 = StreaksInBatch >= 2 ? StreakTextures[BatchStart + 1] : StreakTextures[BatchStart];
						AccumParams->StreakTexture3 = StreaksInBatch >= 3 ? StreakTextures[BatchStart + 2] : StreakTextures[BatchStart];
						AccumParams->StreakSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
						AccumParams->GlareViewportSizeAndInvSize = FVector4f(DownsampledRect.Width(), DownsampledRect.Height(), 1.0f / DownsampledRect.Width(), 1.0f / DownsampledRect.Height());
						AccumParams->NumStreaks = 1 + StreaksInBatch; // 1 for prev accum + new streaks
						AccumParams->RenderTargets[0] = FRenderTargetBinding(NextAccum, ERenderTargetLoadAction::EClear);
						
						FPixelShaderUtils::AddFullscreenPass(
							GraphBuilder,
							GlobalShaderMap,
							RDG_EVENT_NAME("GlareAccumulate%d", BatchStart),
							GlareAccumShader,
							AccumParams,
							DownsampledRect);
						
						PrevAccum = NextAccum;
					}
					
					AccumTexture = PrevAccum;
				}
				
				// Apply a light Gaussian blur to smooth the glare
				FRDGTextureRef GlareBlurTemp = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.GlareBlurTemp"));
				BlurredBloomTexture = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.GlareBlurred"));
				
				// Horizontal blur
				{
					FClassicBloomBlurPS::FParameters* BlurParams = GraphBuilder.AllocParameters<FClassicBloomBlurPS::FParameters>();
					BlurParams->View = View.ViewUniformBuffer;
					BlurParams->SourceTexture = AccumTexture;
					BlurParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					BlurParams->BufferSizeAndInvSize = FVector4f(DownsampledExtent.X, DownsampledExtent.Y, 1.0f / DownsampledExtent.X, 1.0f / DownsampledExtent.Y);
					BlurParams->BlurDirection = FVector2f(1.0f, 0.0f);
					BlurParams->BlurRadius = ActiveComponent->BloomSize * 0.05f; // Lighter blur for glare
					BlurParams->RenderTargets[0] = FRenderTargetBinding(GlareBlurTemp, ERenderTargetLoadAction::EClear);
					
					TShaderMapRef<FClassicBloomBlurPS> BlurShader(GlobalShaderMap);
					FPixelShaderUtils::AddFullscreenPass(GraphBuilder, GlobalShaderMap, RDG_EVENT_NAME("GlareBlurH"), BlurShader, BlurParams, DownsampledRect);
				}
				
				// Vertical blur
				{
					FClassicBloomBlurPS::FParameters* BlurParams = GraphBuilder.AllocParameters<FClassicBloomBlurPS::FParameters>();
					BlurParams->View = View.ViewUniformBuffer;
					BlurParams->SourceTexture = GlareBlurTemp;
					BlurParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					BlurParams->BufferSizeAndInvSize = FVector4f(DownsampledExtent.X, DownsampledExtent.Y, 1.0f / DownsampledExtent.X, 1.0f / DownsampledExtent.Y);
					BlurParams->BlurDirection = FVector2f(0.0f, 1.0f);
					BlurParams->BlurRadius = ActiveComponent->BloomSize * 0.05f;
					BlurParams->RenderTargets[0] = FRenderTargetBinding(BlurredBloomTexture, ERenderTargetLoadAction::EClear);
					
					TShaderMapRef<FClassicBloomBlurPS> BlurShader(GlobalShaderMap);
					FPixelShaderUtils::AddFullscreenPass(GraphBuilder, GlobalShaderMap, RDG_EVENT_NAME("GlareBlurV"), BlurShader, BlurParams, DownsampledRect);
				}
			}
			else
			{
				// Accumulate shader not available, fall back to standard
				bUseDirectionalGlare = false;
			}
		}
	}
	
	// ========================================================================
	// Kawase Bloom Mode
	// ========================================================================
	if (bUseKawaseBloom && !BlurredBloomTexture)
	{
		TShaderMapRef<FClassicBloomKawaseDownsamplePS> KawaseDownsampleShader(GlobalShaderMap);
		TShaderMapRef<FClassicBloomKawaseUpsamplePS> KawaseUpsampleShader(GlobalShaderMap);
		
		if (!KawaseDownsampleShader.IsValid() || !KawaseUpsampleShader.IsValid())
		{
			if (bShouldLog)
			{
				UE_LOG(LogTemp, Warning, TEXT("ClassicBloom: Kawase shaders not available, falling back to standard"));
			}
			bUseKawaseBloom = false;
		}
		else
		{
			// Configure Kawase bloom parameters
			int32 MipCount = FMath::Clamp(ActiveComponent->KawaseMipCount, 3, 8);
			float FilterRadius = FMath::Clamp(ActiveComponent->KawaseFilterRadius, 0.0001f, 0.01f);
			bool bSoftThreshold = ActiveComponent->bKawaseSoftThreshold;
			float ThresholdKnee = bSoftThreshold ? FMath::Clamp(ActiveComponent->KawaseThresholdKnee, 0.0f, 1.0f) : 0.0f;
			
			if (bShouldLog)
			{
				UE_LOG(LogTemp, Warning, TEXT("KAWASE BLOOM SETTINGS:"));
				UE_LOG(LogTemp, Warning, TEXT("  MipCount: %d | FilterRadius: %.5f"), MipCount, FilterRadius);
				UE_LOG(LogTemp, Warning, TEXT("  SoftThreshold: %d | ThresholdKnee: %.2f"), bSoftThreshold, ThresholdKnee);
				UE_LOG(LogTemp, Warning, TEXT("  BloomThreshold: %.2f | BloomIntensity: %.2f"), ActiveComponent->BloomThreshold, ActiveComponent->BloomIntensity);
			}
			
			// Create mip chain textures
			TArray<FRDGTextureRef> MipTextures;
			TArray<FIntPoint> MipExtents;
			TArray<FIntRect> MipRects;
			
			MipTextures.Reserve(MipCount);
			MipExtents.Reserve(MipCount);
			MipRects.Reserve(MipCount);
			
			// Calculate mip sizes (each mip is half the resolution of the previous)
			FIntPoint CurrentExtent = DownsampledExtent;
			FIntRect CurrentRect = DownsampledRect;
			
			for (int32 Mip = 0; Mip < MipCount; ++Mip)
			{
				// Halve the resolution for each mip
				CurrentExtent = FIntPoint::DivideAndRoundUp(CurrentExtent, 2);
				CurrentRect = FIntRect(FIntPoint::ZeroValue, FIntPoint::DivideAndRoundUp(FIntPoint(CurrentRect.Width(), CurrentRect.Height()), 2));
				
				// Ensure minimum size
				CurrentExtent.X = FMath::Max(CurrentExtent.X, 1);
				CurrentExtent.Y = FMath::Max(CurrentExtent.Y, 1);
				CurrentRect.Max.X = FMath::Max(CurrentRect.Max.X, 1);
				CurrentRect.Max.Y = FMath::Max(CurrentRect.Max.Y, 1);
				
				FRDGTextureDesc MipDesc = FRDGTextureDesc::Create2D(
					CurrentExtent,
					PF_FloatR11G11B10,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_RenderTargetable);
				
				FRDGTextureRef MipTexture = GraphBuilder.CreateTexture(MipDesc, *FString::Printf(TEXT("ClassicBloom.KawaseMip%d"), Mip));
				MipTextures.Add(MipTexture);
				MipExtents.Add(CurrentExtent);
				MipRects.Add(CurrentRect);
			}
			
			// ================================================================
			// DOWNSAMPLE PASS: Create the mip pyramid
			// Kawase bloom works directly on scene color (not BrightPass)
			// Threshold is applied only in the first Kawase downsample pass
			// This is the authentic physically-based bloom approach
			// ================================================================
			FRDGTextureRef DownsampleSource = SceneColor.Texture; // Use scene color directly
			FIntPoint SourceExtent = SceneColorExtent;
			FIntRect SourceRect = SceneColor.ViewRect; // Track source viewport for FScreenTransform
			
			for (int32 Mip = 0; Mip < MipCount; ++Mip)
			{
				FClassicBloomKawaseDownsamplePS::FParameters* DownParams = GraphBuilder.AllocParameters<FClassicBloomKawaseDownsamplePS::FParameters>();
				DownParams->View = View.ViewUniformBuffer;
				DownParams->SourceTexture = DownsampleSource;
				DownParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				DownParams->SourceSizeAndInvSize = FVector4f(SourceExtent.X, SourceExtent.Y, 1.0f / SourceExtent.X, 1.0f / SourceExtent.Y);
				// Output size is the destination mip size (where we're rendering to)
				DownParams->OutputSizeAndInvSize = FVector4f(MipExtents[Mip].X, MipExtents[Mip].Y, 1.0f / MipExtents[Mip].X, 1.0f / MipExtents[Mip].Y);
				
				// Create FScreenTransform to map output SvPosition to source texture UV
				// This properly handles viewport offsets (especially important for Mip 0 which samples from SceneColor)
				FScreenPassTextureViewport OutputViewport(MipExtents[Mip], MipRects[Mip]);
				FScreenPassTextureViewport SourceViewport(SourceExtent, SourceRect);
				DownParams->SvPositionToSourceUV = (
					FScreenTransform::ChangeTextureBasisFromTo(OutputViewport, FScreenTransform::ETextureBasis::TexelPosition, FScreenTransform::ETextureBasis::ViewportUV) *
					FScreenTransform::ChangeTextureBasisFromTo(SourceViewport, FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV));
				
				DownParams->BloomThreshold = ActiveComponent->BloomThreshold;
				DownParams->ThresholdKnee = ThresholdKnee;
				DownParams->MipLevel = Mip;
				DownParams->bUseKarisAverage = (Mip == 0) ? 1 : 0; // Only apply Karis on first mip
				DownParams->RenderTargets[0] = FRenderTargetBinding(MipTextures[Mip], ERenderTargetLoadAction::EClear);
				
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("KawaseDownsample_Mip%d", Mip),
					KawaseDownsampleShader,
					DownParams,
					MipRects[Mip]);
				
				// Use this mip as source for next (our mips have extent==viewport now)
				DownsampleSource = MipTextures[Mip];
				SourceExtent = MipExtents[Mip];
				SourceRect = MipRects[Mip];
			}
			
			// ================================================================
			// UPSAMPLE PASS: Progressive upsample with additive blend
			// ================================================================
			// Start from the smallest mip and work up
			// E' = E (smallest mip stays as-is, it's just copied)
			// D' = D + blur(E')
			// C' = C + blur(D')
			// etc.
			
			// Create upsample target textures (one for each mip level except the last)
			TArray<FRDGTextureRef> UpsampleTextures;
			UpsampleTextures.Reserve(MipCount - 1);
			
			for (int32 Mip = MipCount - 2; Mip >= 0; --Mip)
			{
				FRDGTextureDesc UpsampleDesc = FRDGTextureDesc::Create2D(
					MipExtents[Mip],
					PF_FloatR11G11B10,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_RenderTargetable);
				
				FRDGTextureRef UpsampleTexture = GraphBuilder.CreateTexture(UpsampleDesc, *FString::Printf(TEXT("ClassicBloom.KawaseUpsample%d"), Mip));
				UpsampleTextures.Add(UpsampleTexture);
			}
			
			// The first upsample source is the smallest mip (no processing needed)
			FRDGTextureRef UpsampleSource = MipTextures[MipCount - 1];
			FIntPoint UpsampleSourceExtent = MipExtents[MipCount - 1];
			
			int32 UpsampleIdx = 0;
			for (int32 Mip = MipCount - 2; Mip >= 0; --Mip)
			{
				FClassicBloomKawaseUpsamplePS::FParameters* UpParams = GraphBuilder.AllocParameters<FClassicBloomKawaseUpsamplePS::FParameters>();
				UpParams->View = View.ViewUniformBuffer;
				UpParams->SourceTexture = UpsampleSource;
				UpParams->PreviousMipTexture = MipTextures[Mip]; // The larger mip blending into
				UpParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				// Output size is the destination mip size (where rendering to)
				UpParams->OutputSizeAndInvSize = FVector4f(MipExtents[Mip].X, MipExtents[Mip].Y, 1.0f / MipExtents[Mip].X, 1.0f / MipExtents[Mip].Y);
				UpParams->FilterRadius = FilterRadius;
				UpParams->RenderTargets[0] = FRenderTargetBinding(UpsampleTextures[UpsampleIdx], ERenderTargetLoadAction::EClear);
				
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("KawaseUpsample_Mip%d", Mip),
					KawaseUpsampleShader,
					UpParams,
					MipRects[Mip]);
				
				// Use this as source for next upsample iteration
				UpsampleSource = UpsampleTextures[UpsampleIdx];
				UpsampleSourceExtent = MipExtents[Mip];
				++UpsampleIdx;
			}
			
			// The final upsample texture is our bloom result
			// We need to upsample it back to the original bloom texture size
			if (UpsampleTextures.Num() > 0)
			{
				// Create final output texture at original downsampled size
				BlurredBloomTexture = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.KawaseBlurred"));
				
				// Final upsample pass to original resolution
				// Blend with MipTextures[0] (first downsampled mip from scene color with threshold applied)
				FClassicBloomKawaseUpsamplePS::FParameters* FinalUpParams = GraphBuilder.AllocParameters<FClassicBloomKawaseUpsamplePS::FParameters>();
				FinalUpParams->View = View.ViewUniformBuffer;
				FinalUpParams->SourceTexture = UpsampleTextures.Last();
				FinalUpParams->PreviousMipTexture = MipTextures[0]; // Blend with first Kawase mip (has threshold applied)
				FinalUpParams->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				// Output size is the final bloom texture size
				FinalUpParams->OutputSizeAndInvSize = FVector4f(DownsampledExtent.X, DownsampledExtent.Y, 1.0f / DownsampledExtent.X, 1.0f / DownsampledExtent.Y);
				FinalUpParams->FilterRadius = FilterRadius;
				FinalUpParams->RenderTargets[0] = FRenderTargetBinding(BlurredBloomTexture, ERenderTargetLoadAction::EClear);
				
				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("KawaseUpsample_Final"),
					KawaseUpsampleShader,
					FinalUpParams,
					DownsampledRect);
			}
			else
			{
				// Fallback if something went wrong - use first mip as bloom
				BlurredBloomTexture = MipTextures.Num() > 0 ? MipTextures[0] : BrightPassTexture;
			}
			
			if (bShouldLog)
			{
				UE_LOG(LogTemp, Warning, TEXT("KAWASE BLOOM COMPLETED:"));
				UE_LOG(LogTemp, Warning, TEXT("  Generated %d mips, %d upsample passes"), MipCount, UpsampleTextures.Num());
			}
		}
	}
	
	// Standard Gaussian blur mode (or fallback from directional glare/Kawase if they failed)
	if (!BlurredBloomTexture)
	{
		int32 NumBlurPasses = FMath::Clamp(ActiveComponent->BlurPasses, 1, 4);
		FRDGTextureRef BlurSource = BrightPassTexture;
		FRDGTextureRef BlurTempTexture = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.BlurTemp"));
		BlurredBloomTexture = GraphBuilder.CreateTexture(BrightPassDesc, TEXT("ClassicBloom.Blurred"));

		for (int32 PassIndex = 0; PassIndex < NumBlurPasses; ++PassIndex)
		{
			// Horizontal pass
			{
				FClassicBloomBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassicBloomBlurPS::FParameters>();
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->SourceTexture = BlurSource;
				PassParameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PassParameters->BufferSizeAndInvSize = FVector4f(DownsampledExtent.X, DownsampledExtent.Y, 1.0f / DownsampledExtent.X, 1.0f / DownsampledExtent.Y);
				PassParameters->BlurDirection = FVector2f(1.0f, 0.0f); // Horizontal
				PassParameters->BlurRadius = ActiveComponent->BloomSize * 0.1f;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(BlurTempTexture, ERenderTargetLoadAction::EClear);

				TShaderMapRef<FClassicBloomBlurPS> PixelShader(GlobalShaderMap);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("BlurHorizontal"),
					PixelShader,
					PassParameters,
					DownsampledRect);
			}

			// Vertical pass
			{
				FClassicBloomBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassicBloomBlurPS::FParameters>();
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->SourceTexture = BlurTempTexture;
				PassParameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PassParameters->BufferSizeAndInvSize = FVector4f(DownsampledExtent.X, DownsampledExtent.Y, 1.0f / DownsampledExtent.X, 1.0f / DownsampledExtent.Y);
				PassParameters->BlurDirection = FVector2f(0.0f, 1.0f); // Vertical
				PassParameters->BlurRadius = ActiveComponent->BloomSize * 0.1f;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(BlurredBloomTexture, ERenderTargetLoadAction::EClear);

				TShaderMapRef<FClassicBloomBlurPS> PixelShader(GlobalShaderMap);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GlobalShaderMap,
					RDG_EVENT_NAME("BlurVertical"),
					PixelShader,
					PassParameters,
					DownsampledRect);
			}

			// Use output as source for next pass iteration
			BlurSource = BlurredBloomTexture;
		}
	}

	// Step 4: Composite bloom back onto scene color
	// Use override output if provided, otherwise create new
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
		OutputDesc.ClearValue = FClearValueBinding::Black;
		OutputDesc.Flags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("ClassicBloom.Output"));
		// Important: Use exact same rect as scene color to prevent misalignment
		Output = FScreenPassRenderTarget(OutputTexture, SceneColor.ViewRect, ERenderTargetLoadAction::ENoAction);
	}

	// Note: handle HDR/LDR compensation in the shader itself via adaptive scaling
	// This provides more consistent results than trying to detect game mode in C++
	
	{
		FClassicBloomCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassicBloomCompositePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneColorTexture = SceneColor.Texture;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->BloomTexture = BlurredBloomTexture;
		PassParameters->BloomSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		// Use exact viewport rect dimensions
		PassParameters->OutputViewportSizeAndInvSize = FVector4f(Output.ViewRect.Width(), Output.ViewRect.Height(), 1.0f / Output.ViewRect.Width(), 1.0f / Output.ViewRect.Height());
		
		// Create FScreenTransform for proper SvPosition to texture UV mapping
		// Output viewport: where we're rendering to (determines SvPosition range)
		FScreenPassTextureViewport OutputViewport(FIntPoint(Output.Texture->Desc.Extent.X, Output.Texture->Desc.Extent.Y), Output.ViewRect);
		// Scene color viewport: source scene color texture
		FScreenPassTextureViewport SceneColorViewport(FIntPoint(SceneColor.Texture->Desc.Extent.X, SceneColor.Texture->Desc.Extent.Y), SceneColor.ViewRect);
		// Bloom viewport: generated bloom texture
		// The bloom was rendered to DownsampledRect within DownsampledExtent texture
		FScreenPassTextureViewport BloomViewport(DownsampledExtent, DownsampledRect);
		
		// Transform: SvPosition -> ViewportUV [0,1] -> SceneColor TextureUV
		PassParameters->SvPositionToSceneColorUV = (
			FScreenTransform::ChangeTextureBasisFromTo(OutputViewport, FScreenTransform::ETextureBasis::TexelPosition, FScreenTransform::ETextureBasis::ViewportUV) *
			FScreenTransform::ChangeTextureBasisFromTo(SceneColorViewport, FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV));
		
		// Transform: SvPosition -> ViewportUV [0,1] -> Bloom TextureUV
		PassParameters->SvPositionToBloomUV = (
			FScreenTransform::ChangeTextureBasisFromTo(OutputViewport, FScreenTransform::ETextureBasis::TexelPosition, FScreenTransform::ETextureBasis::ViewportUV) *
			FScreenTransform::ChangeTextureBasisFromTo(BloomViewport, FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV));
		
		// For Soft Focus mode, pass 0 for bloom intensity (uses SoftFocusIntensity instead)
		// For other modes, pass the bloom intensity normally
		PassParameters->BloomIntensity = bUseSoftFocus ? 0.0f : ActiveComponent->BloomIntensity;
	
	// Debug log final intensity values
	if (bShouldLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("FINAL SHADER PARAMETERS:"));
		UE_LOG(LogTemp, Warning, TEXT("  BloomMode: %d (SoftFocus: %d) | BloomIntensity: %.3f"), (int32)ActiveComponent->BloomMode, bUseSoftFocus, PassParameters->BloomIntensity);
		UE_LOG(LogTemp, Warning, TEXT("  SoftFocusIntensity: %.3f"),
			bUseSoftFocus ? ActiveComponent->BloomIntensity : 0.0f);
		UE_LOG(LogTemp, Warning, TEXT("  bUseAdaptiveScaling: %d"), ActiveComponent->bUseAdaptiveBrightnessScaling);
		UE_LOG(LogTemp, Warning, TEXT("VIEWPORT MAPPING:"));
		UE_LOG(LogTemp, Warning, TEXT("  Output: Extent[%d,%d] ViewRect[%d,%d]-[%d,%d]"), 
			Output.Texture->Desc.Extent.X, Output.Texture->Desc.Extent.Y,
			Output.ViewRect.Min.X, Output.ViewRect.Min.Y, Output.ViewRect.Max.X, Output.ViewRect.Max.Y);
		UE_LOG(LogTemp, Warning, TEXT("  SceneColor: Extent[%d,%d] ViewRect[%d,%d]-[%d,%d]"),
			SceneColor.Texture->Desc.Extent.X, SceneColor.Texture->Desc.Extent.Y,
			SceneColor.ViewRect.Min.X, SceneColor.ViewRect.Min.Y, SceneColor.ViewRect.Max.X, SceneColor.ViewRect.Max.Y);
		UE_LOG(LogTemp, Warning, TEXT("  Bloom: Extent[%d,%d] Rect[%d,%d]-[%d,%d]"),
			DownsampledExtent.X, DownsampledExtent.Y,
			DownsampledRect.Min.X, DownsampledRect.Min.Y, DownsampledRect.Max.X, DownsampledRect.Max.Y);
		UE_LOG(LogTemp, Warning, TEXT("  SvPosToSceneColorUV: Scale[%.4f,%.4f] Bias[%.4f,%.4f]"),
			PassParameters->SvPositionToSceneColorUV.Scale.X, PassParameters->SvPositionToSceneColorUV.Scale.Y,
			PassParameters->SvPositionToSceneColorUV.Bias.X, PassParameters->SvPositionToSceneColorUV.Bias.Y);
		UE_LOG(LogTemp, Warning, TEXT("  SvPosToBloomUV: Scale[%.4f,%.4f] Bias[%.4f,%.4f]"),
			PassParameters->SvPositionToBloomUV.Scale.X, PassParameters->SvPositionToBloomUV.Scale.Y,
			PassParameters->SvPositionToBloomUV.Bias.X, PassParameters->SvPositionToBloomUV.Bias.Y);
	}
	
	// Encode bUseSceneColor in the alpha channel of BloomTint (0.0 = use tint, 1.0 = use scene color)
	FLinearColor TintWithFlag = ActiveComponent->BloomTint;
	TintWithFlag.A = ActiveComponent->bUseSceneColor ? 1.0f : 0.0f;
	PassParameters->BloomTint = FVector4f(TintWithFlag);
		// Pass blend mode as float (0-5)
		PassParameters->BloomBlendMode = (float)ActiveComponent->BloomBlendMode;
		// Pass saturation boost
		PassParameters->BloomSaturation = ActiveComponent->BloomSaturation;
		// Pass highlight protection settings
		PassParameters->bProtectHighlights = ActiveComponent->bProtectHighlights ? 1.0f : 0.0f;
		PassParameters->HighlightProtection = ActiveComponent->HighlightProtection;
		// For Soft Focus mode, use BloomIntensity as the soft focus intensity
		PassParameters->SoftFocusIntensity = bUseSoftFocus ? ActiveComponent->BloomIntensity : 0.0f;
		// Pack soft focus tuning parameters into a vector4
		PassParameters->SoftFocusParams = FVector4f(
			ActiveComponent->SoftFocusOverlayMultiplier,
			ActiveComponent->SoftFocusBlendStrength,
			ActiveComponent->SoftFocusSoftLightMultiplier,
			ActiveComponent->SoftFocusFinalBlend
		);
		// Pass adaptive scaling flag
		PassParameters->bUseAdaptiveScaling = ActiveComponent->bUseAdaptiveBrightnessScaling ? 1.0f : 0.0f;
		// Pass debug flags
		PassParameters->bShowBloomOnly = ActiveComponent->bShowBloomOnly ? 1.0f : 0.0f;
		PassParameters->bShowGammaCompensation = ActiveComponent->bShowGammaCompensation ? 1.0f : 0.0f;
		// Pass world type and manual game mode scale
		bool bIsGameWorld = ViewInfo.Family->Scene && ViewInfo.Family->Scene->GetWorld() && ViewInfo.Family->Scene->GetWorld()->IsGameWorld();
		PassParameters->bIsGameWorld = bIsGameWorld ? 1.0f : 0.0f;
		PassParameters->GameModeBloomScale = ActiveComponent->GameModeBloomScale;
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FClassicBloomCompositePS> PixelShader(GlobalShaderMap);

		// Validate shader is available
		if (!PixelShader.IsValid())
		{
			if (bShouldLog)
			{
				UE_LOG(LogTemp, Warning, TEXT("ClassicBloom: Composite shader not available"));
			}
			return SceneColor;
		}

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GlobalShaderMap,
			RDG_EVENT_NAME("CompositeBloom"),
			PixelShader,
			PassParameters,
			Output.ViewRect);  // Use Output.ViewRect instead of SceneColorRect to ensure perfect alignment
	}

	// Log success if debug logging is enabled (already throttled above)
	if (bShouldLog)
	{
		UE_LOG(LogTemp, Log, TEXT("ClassicBloom: Successfully rendered bloom (Downsampled: %dx%d, Output ViewRect: [%d,%d]-[%d,%d])"), 
			DownsampledRect.Width(), DownsampledRect.Height(),
			Output.ViewRect.Min.X, Output.ViewRect.Min.Y, Output.ViewRect.Max.X, Output.ViewRect.Max.Y);
	}
	
	// Return the output (either override or our created texture)
	return MoveTemp(Output);
}

// ============================================================================
// UClassicBloomSubsystem Implementation
// ============================================================================

void UClassicBloomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Create and register the scene view extension
	SceneViewExtension = FSceneViewExtensions::NewExtension<FClassicBloomSceneViewExtension>(this);
}

void UClassicBloomSubsystem::Deinitialize()
{
	// Unregister the scene view extension
	SceneViewExtension.Reset();

	Super::Deinitialize();
}

void UClassicBloomSubsystem::RegisterBloomComponent(UBloomFXComponent* Component)
{
	if (Component)
	{
		BloomComponents.AddUnique(Component);
	}
}

void UClassicBloomSubsystem::UnregisterBloomComponent(UBloomFXComponent* Component)
{
	if (Component)
	{
		BloomComponents.Remove(Component);
	}
}
