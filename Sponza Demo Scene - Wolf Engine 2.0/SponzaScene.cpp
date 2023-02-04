#include "SponzaScene.h"

using namespace Wolf;

SponzaScene::SponzaScene(WolfEngine* wolfInstance, std::mutex* vulkanQueueLock)
{
	m_sponzaModel.reset(new SponzaModel(vulkanQueueLock));

	std::vector<Wolf::Image*> sponzaImages;
	m_sponzaModel->getImages(sponzaImages);

	m_depthPass.reset(new DepthPass(m_sponzaModel->getMesh()));
	wolfInstance->initializePass(m_depthPass.get());

	m_forwardPass.reset(new ForwardPass(m_sponzaModel->getMesh(), sponzaImages, m_depthPass->getSemaphore()));
	wolfInstance->initializePass(m_forwardPass.get());

	m_camera.reset(new Camera(glm::vec3(1.4f, 1.2f, 0.3f), glm::vec3(2.0f, 0.9f, -0.3f), glm::vec3(0.0f, 1.0f, 0.0f), 0.01f, 5.0f, 16.0f / 9.0f));
	wolfInstance->setCameraInterface(m_camera.get());
}

void SponzaScene::update()
{
}

void SponzaScene::frame(Wolf::WolfEngine* wolfInstance)
{
	std::vector<Wolf::PassBase*> passes(2);
	passes[0] = m_depthPass.get();
	passes[1] = m_forwardPass.get();

	wolfInstance->frame(passes, m_forwardPass->getSemaphore());
}