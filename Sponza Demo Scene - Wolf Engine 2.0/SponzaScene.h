#pragma once

#include <WolfEngine.h>

#include "Camera.h"
#include "CascadedShadowMapping.h"
#include "DepthPass.h"
#include "ForwardPass.h"
#include "RayTracedShadowsPass.h"
#include "ShadowMaskComputePass.h"
#include "SponzaModel.h"

class SponzaScene
{
public:
	SponzaScene(Wolf::WolfEngine* wolfInstance, std::mutex* vulkanQueueLock);

	void update(const Wolf::WolfEngine* wolfInstance);
	void frame(Wolf::WolfEngine* wolfInstance) const;

private:
	std::unique_ptr<SponzaModel> m_sponzaModel;
	std::unique_ptr<Camera> m_camera;

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
		enum class ShadowType
		{
			CSM, RayTraced
		} shadowType = ShadowType::CSM;
	};

	PassState m_CurrentPassState;
	PassState m_NextPassState;
};