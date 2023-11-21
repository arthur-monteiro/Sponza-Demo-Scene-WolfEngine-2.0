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
	void frame(Wolf::WolfEngine* wolfInstance) const;

	enum class ShadowType
	{
		CSM, RayTraced
	};
	void setShadowType(ShadowType shadowType) { m_nextPassState.shadowType = shadowType; }

	void setDebugMode(ForwardPass::DebugMode debugMode) { m_nextPassState.debugMode = debugMode; }

private:
	void initializePipelineSets(const Wolf::WolfEngine* wolfInstance, const ShadowMaskBasePass* shadowMaskPass);

	std::chrono::high_resolution_clock::time_point m_startTime = std::chrono::high_resolution_clock::now();
	
	std::unique_ptr<Wolf::ModelBase> m_sponzaModel;
	std::unique_ptr<Wolf::ModelBase> m_cubeModel;
	std::unique_ptr<Wolf::TopLevelAccelerationStructure> m_tlas;

	std::unique_ptr<Wolf::FirstPersonCamera> m_camera;
	bool m_isLocked = false;

	// Pipeline sets
	std::unique_ptr<Wolf::PipelineSet> m_sponzaPipelineSet;

	// PreDepth
	std::unique_ptr<PreDepthPass> m_preDepthPass;

	// Shadows
	std::unique_ptr<CascadedShadowMapping> m_cascadedShadowMappingPass;
	std::unique_ptr<ShadowMaskComputePass> m_shadowMaskComputePass;
	std::unique_ptr<RayTracedShadowsPass> m_rayTracedShadowsPass;

	// Direct lighting
	std::unique_ptr<ForwardPass> m_forwardPass;

	// Global Illumination
	std::unique_ptr<RTGIPass> m_rayTracedGlobalIlluminationPass;

	// Post process
	std::unique_ptr<TemporalAntiAliasingPass> m_taaComposePass;

	struct PassState
	{
		ShadowType shadowType = ShadowType::CSM;
		ForwardPass::DebugMode debugMode = ForwardPass::DebugMode::None;
	};

	PassState m_currentPassState;
	PassState m_nextPassState;
};