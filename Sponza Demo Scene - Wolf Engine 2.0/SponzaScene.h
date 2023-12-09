#pragma once

#include <FirstPersonCamera.h>
#include <WolfEngine.h>

#include "CascadedShadowMapping.h"
#include "PreDepthPass.h"
#include "ForwardPass.h"
#include "InputHandler.h"
#include "ModelBase.h"
#include "RayTracedShadowsPass.h"
#include "RTGIPass.h"
#include "ShadowMaskComputePass.h"
#include "TemporalAntiAliasingPass.h"

struct GameContext;

class SponzaScene
{
public:
	SponzaScene(Wolf::WolfEngine* wolfInstance, std::mutex* vulkanQueueLock);

	void update(Wolf::WolfEngine* wolfInstance, GameContext& gameContext);
	void frame(Wolf::WolfEngine* wolfInstance);

	enum class ShadowType
	{
		CSM, RayTraced
	};
	void setShadowType(ShadowType shadowType) { m_nextPassState.shadowType = shadowType; }

	void setDebugMode(ForwardPass::DebugMode debugMode) { m_nextPassState.debugMode = debugMode; }

private:
	void initializePipelineSets(const Wolf::WolfEngine* wolfInstance, const Wolf::ResourceNonOwner<ShadowMaskBasePass>& shadowMaskPass);

	std::chrono::high_resolution_clock::time_point m_startTime = std::chrono::high_resolution_clock::now();
	
	std::unique_ptr<Wolf::ModelBase> m_sponzaModel;
	std::unique_ptr<Wolf::ModelBase> m_cubeModel;
	std::unique_ptr<Wolf::TopLevelAccelerationStructure> m_tlas;

	std::unique_ptr<Wolf::FirstPersonCamera> m_camera;
	bool m_isLocked = false;

	// Pipeline sets
	std::unique_ptr<Wolf::PipelineSet> m_sponzaPipelineSet;

	// PreDepth
	Wolf::ResourceUniqueOwner<PreDepthPass> m_preDepthPass;

	// Shadows
	Wolf::ResourceUniqueOwner<CascadedShadowMapping> m_cascadedShadowMappingPass;
	Wolf::ResourceUniqueOwner<ShadowMaskComputePass> m_shadowMaskComputePass;
	Wolf::ResourceUniqueOwner<RayTracedShadowsPass> m_rayTracedShadowsPass;

	// Global Illumination
	Wolf::ResourceUniqueOwner<RTGIPass> m_rayTracedGlobalIlluminationPass;

	// Direct lighting
	Wolf::ResourceUniqueOwner<ForwardPass> m_forwardPass;

	// Post process
	Wolf::ResourceUniqueOwner<TemporalAntiAliasingPass> m_taaComposePass;

	struct PassState
	{
		ShadowType shadowType = ShadowType::CSM;
		ForwardPass::DebugMode debugMode = ForwardPass::DebugMode::None;
	};

	PassState m_currentPassState;
	PassState m_nextPassState;
};