#pragma once

#include <WolfEngine.h>

#include "Camera.h"
#include "CascadedShadowMapping.h"
#include "DepthPass.h"
#include "ForwardPass.h"
#include "ShadowMaskComputePass.h"
#include "SponzaModel.h"

class SponzaScene
{
public:
	SponzaScene(Wolf::WolfEngine* wolfInstance, std::mutex* vulkanQueueLock);

	void update();
	void frame(Wolf::WolfEngine* wolfInstance);

private:
	std::unique_ptr<SponzaModel> m_sponzaModel;
	std::unique_ptr<Camera> m_camera;

	std::unique_ptr<DepthPass> m_depthPass;
	std::unique_ptr<CascadedShadowMapping> m_cascadedShadowMappingPass;
	std::unique_ptr<ShadowMaskComputePass> m_shadowMaskComputePass;
	std::unique_ptr<ForwardPass> m_forwardPass;
};