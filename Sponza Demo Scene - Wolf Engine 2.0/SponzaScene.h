#pragma once

#include <WolfEngine.h>

#include "Camera.h"
#include "CascadedShadowMapping.h"
#include "DepthPass.h"
#include "InputHandler.h"
#include "ForwardPass.h"
#include "RayTracedShadowsPass.h"
#include "SceneElements.h"
#include "ShadowMaskComputePass.h"

class GameContext;

class SponzaScene
{
public:
	SponzaScene(Wolf::WolfEngine* wolfInstance, std::mutex* vulkanQueueLock);

	void update(const Wolf::WolfEngine* wolfInstance, GameContext& gameContext);
	void frame(Wolf::WolfEngine* wolfInstance) const;

	enum class ShadowType
	{
		CSM, RayTraced
	};
	void setShadowType(ShadowType shadowType) { m_nextPassState.shadowType = shadowType; }

private:
	std::chrono::high_resolution_clock::time_point m_startTime = std::chrono::high_resolution_clock::now();

	SceneElements m_sceneElements;
	std::unique_ptr<ObjectModel> m_sponzaModel;
	std::unique_ptr<ObjectModel> m_cubeModel;
	std::array<std::unique_ptr<Wolf::Image>, 5> m_cubeImages;
	std::unique_ptr<Camera> m_camera;
	std::unique_ptr<InputHandler> m_inputHandler;
	bool m_isLocked = false;

	// PreDepth
	std::unique_ptr<DepthPass> m_depthPass;

	// Shadows
	std::unique_ptr<CascadedShadowMapping> m_cascadedShadowMappingPass;
	std::unique_ptr<ShadowMaskComputePass> m_shadowMaskComputePass;
	std::unique_ptr<RayTracedShadowsPass> m_rayTracedShadowsPass;

	// Direct lighting
	std::unique_ptr<ForwardPass> m_forwardPass;

	struct PassState
	{
		ShadowType shadowType = ShadowType::CSM;
	};

	PassState m_currentPassState;
	PassState m_nextPassState;
};