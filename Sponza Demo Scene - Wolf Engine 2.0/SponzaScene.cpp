#include "SponzaScene.h"

using namespace Wolf;

SponzaScene::SponzaScene(WolfEngine* wolfInstance, std::mutex* vulkanQueueLock)
{
	m_sponzaModel.reset(new SponzaModel(vulkanQueueLock, glm::scale(glm::mat4(1.0f), glm::vec3(0.01f)), wolfInstance->getHardwareCapabilities().rayTracingAvailable));

	std::vector<Wolf::Image*> sponzaImages;
	m_sponzaModel->getImages(sponzaImages);

	m_camera.reset(new Camera(glm::vec3(1.4f, 1.2f, 0.3f), glm::vec3(2.0f, 0.9f, -0.3f), glm::vec3(0.0f, 1.0f, 0.0f), 0.01f, 5.0f, 16.0f / 9.0f));
	wolfInstance->setCameraInterface(m_camera.get());

	m_depthPass.reset(new DepthPass(m_sponzaModel.get()));
	wolfInstance->initializePass(m_depthPass.get());

	m_cascadedShadowMappingPass.reset(new CascadedShadowMapping(m_sponzaModel->getMesh()));
	wolfInstance->initializePass(m_cascadedShadowMappingPass.get());

	m_shadowMaskComputePass.reset(new ShadowMaskComputePass(m_depthPass.get(), m_cascadedShadowMappingPass.get()));
	wolfInstance->initializePass(m_shadowMaskComputePass.get());

	if (wolfInstance->getHardwareCapabilities().rayTracingAvailable)
	{
		m_rayTracedShadowsPass.reset(new RayTracedShadowsPass(m_sponzaModel.get(), m_depthPass.get()));
		wolfInstance->initializePass(m_rayTracedShadowsPass.get());
	}

	m_forwardPass.reset(new ForwardPass(m_sponzaModel.get(), sponzaImages, m_depthPass.get(), 
		m_CurrentPassState.shadowType == PassState::ShadowType::CSM ? static_cast<ShadowMaskBasePass*>(m_shadowMaskComputePass.get()) : static_cast<ShadowMaskBasePass*>(m_rayTracedShadowsPass.get())));
	wolfInstance->initializePass(m_forwardPass.get());
}

void SponzaScene::update(const WolfEngine* wolfInstance)
{
	if(m_NextPassState.shadowType != m_CurrentPassState.shadowType)
	{
		wolfInstance->waitIdle();
		m_forwardPass->setShadowMaskPass(m_NextPassState.shadowType == PassState::ShadowType::CSM ? static_cast<ShadowMaskBasePass*>(m_shadowMaskComputePass.get()) : static_cast<ShadowMaskBasePass*>(m_rayTracedShadowsPass.get()));
	}

	m_CurrentPassState = m_NextPassState;
}

void SponzaScene::frame(WolfEngine* wolfInstance) const
{
	std::vector<CommandRecordBase*> passes;
	passes.push_back(m_depthPass.get());
	if(m_CurrentPassState.shadowType == PassState::ShadowType::CSM)
	{
		passes.push_back(m_cascadedShadowMappingPass.get());
		passes.push_back(m_shadowMaskComputePass.get());
	}
	else
	{
		passes.push_back(m_rayTracedShadowsPass.get());
	}
	passes.push_back(m_forwardPass.get());

	wolfInstance->frame(passes, m_forwardPass->getSemaphore());
}
