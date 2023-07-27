#include "SponzaScene.h"

#include <glm/ext.hpp>
#include <fstream>

#include <ImageFileLoader.h>
#include <MipMapGenerator.h>

#include "GameContext.h"

using namespace Wolf;

SponzaScene::SponzaScene(WolfEngine* wolfInstance, std::mutex* vulkanQueueLock)
{
	m_sponzaModel.reset(new ObjectModel(vulkanQueueLock, glm::scale(glm::mat4(1.0f), glm::vec3(0.01f)), wolfInstance->getHardwareCapabilities().rayTracingAvailable, 
		"Models/sponza/sponza.obj", "Models/sponza", true, 0));
	std::vector<Image*> sponzaImages;
	m_sponzaModel->getImages(sponzaImages);
	m_cubeModel.reset(new ObjectModel(vulkanQueueLock, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)), false, "Models/cube.obj", "Models/", false, 
		sponzaImages.size() / 5));

	m_sceneElements.addObject(m_sponzaModel.get());
	m_sceneElements.addObject(m_cubeModel.get());

	const std::array cubeImageFilenames =
	{
		"Models/sponza/textures/Sponza_Bricks_a_Albedo.tga",
		"Models/sponza/textures/Sponza_Bricks_a_Normal.tga",
		"Models/sponza/textures/Sponza_Bricks_a_Roughness.tga",
		"Models/sponza/textures/Dielectric_metallic.tga",
		"Models/sponza/textures/Dielectric_metallic.tga",

	};
	const std::array cubeImageFormats =
	{
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_R8G8B8A8_UNORM
	};
	for (uint32_t idx = 0; idx < m_cubeImages.size(); ++idx)
	{
		const ImageFileLoader imageFileLoader(cubeImageFilenames[idx]);
		MipMapGenerator mipmapGenerator(imageFileLoader.getPixels(), { static_cast<uint32_t>(imageFileLoader.getWidth()), static_cast<uint32_t>(imageFileLoader.getHeight()) }, cubeImageFormats[idx]);

		CreateImageInfo createImageInfo;
		createImageInfo.extent = { static_cast<uint32_t>(imageFileLoader.getWidth()), static_cast<uint32_t>(imageFileLoader.getHeight()), 1 };
		createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		createImageInfo.format = cubeImageFormats[idx];
		createImageInfo.mipLevelCount = mipmapGenerator.getMipLevelCount();
		createImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		m_cubeImages[idx].reset(new Image(createImageInfo));
		m_cubeImages[idx]->copyCPUBuffer(imageFileLoader.getPixels(), Image::SampledInFragmentShader());

		for (uint32_t mipLevel = 1; mipLevel < mipmapGenerator.getMipLevelCount(); ++mipLevel)
		{
			m_cubeImages[idx]->copyCPUBuffer(mipmapGenerator.getMipLevel(mipLevel), Image::SampledInFragmentShader(mipLevel), mipLevel);
		}

		m_sceneElements.addImage(m_cubeImages[idx].get());
	}

	m_camera.reset(new Camera(glm::vec3(1.4f, 1.2f, 0.3f), glm::vec3(2.0f, 0.9f, -0.3f), glm::vec3(0.0f, 1.0f, 0.0f), 0.01f, 5.0f, 16.0f / 9.0f));
	wolfInstance->setCameraInterface(m_camera.get());

	m_inputHandler.reset(new InputHandler());
	wolfInstance->registerInputHandlerInterface(m_inputHandler.get());

	m_depthPass.reset(new DepthPass(m_sceneElements, true));
	wolfInstance->initializePass(m_depthPass.get());

	m_cascadedShadowMappingPass.reset(new CascadedShadowMapping(m_sceneElements));
	wolfInstance->initializePass(m_cascadedShadowMappingPass.get());

	m_shadowMaskComputePass.reset(new ShadowMaskComputePass(m_depthPass.get(), m_cascadedShadowMappingPass.get()));
	wolfInstance->initializePass(m_shadowMaskComputePass.get());

	if (wolfInstance->getHardwareCapabilities().rayTracingAvailable)
	{
		m_rayTracedShadowsPass.reset(new RayTracedShadowsPass(m_sponzaModel.get(), m_depthPass.get()));
		wolfInstance->initializePass(m_rayTracedShadowsPass.get());
	}

	m_forwardPass.reset(new ForwardPass(m_sceneElements, m_depthPass.get(),
		m_currentPassState.shadowType == ShadowType::CSM ? static_cast<ShadowMaskBasePass*>(m_shadowMaskComputePass.get()) : static_cast<ShadowMaskBasePass*>(m_rayTracedShadowsPass.get())));
	wolfInstance->initializePass(m_forwardPass.get());
}

static uint32_t screenshotId = 0;
static bool requestedScreenshot = false;
void SponzaScene::update(const WolfEngine* wolfInstance, GameContext& gameContext)
{
	if(m_nextPassState.shadowType != m_currentPassState.shadowType)
	{
		wolfInstance->waitIdle();
		m_forwardPass->setShadowMaskPass(m_nextPassState.shadowType == ShadowType::CSM ? static_cast<ShadowMaskBasePass*>(m_shadowMaskComputePass.get()) : static_cast<ShadowMaskBasePass*>(m_rayTracedShadowsPass.get()));
	}

	m_currentPassState = m_nextPassState;

	wolfInstance->updateEvents();

	const auto currentTime = std::chrono::high_resolution_clock::now();
	const long long offsetInMicrosecond = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_startTime).count();
	const float offsetInSeconds = offsetInMicrosecond / 1'000'000.0;
	m_cubeModel->setPosition(glm::vec3(5.0f * glm::sin(offsetInSeconds), 2.0f, 0.0f));

	gameContext.shadowmapScreenshotsRequested = false;
	if(m_inputHandler->keyPressedThisFrame(GLFW_KEY_SPACE))
	{
		requestedScreenshot = true; // delay the request to the next frame

		//std::ifstream infile("Exports/exportPositions.txt");

		//glm::mat4 viewMatrix;
		//glm::mat4 projectionMatrix;

		//uint32_t currentLine = 0;

		//std::string line;
		//while (std::getline(infile, line))
		//{
		//	if(currentLine < 4 * screenshotId)
		//	{
		//		currentLine++;
		//		continue;
		//	}
		//	if (currentLine >= 4 * (screenshotId + 1))
		//		break;

		//	switch (currentLine % 4)
		//	{
		//	case 0:
		//		break; // "Export XX"
		//	case 1:
		//		{
		//			line = line.substr(sizeof("View matrix:"));

		//			float viewMatrixValues[16];
		//			for (float& matrixValue : viewMatrixValues)
		//			{
		//				std::string delimiter = ";";
		//				std::string token = line.substr(0, line.find(delimiter));
		//				line.erase(0, line.find(delimiter) + delimiter.length());

		//				matrixValue = std::stof(token);
		//			}

		//			float* pSource = static_cast<float*>(glm::value_ptr(viewMatrix));
		//			for (int i = 0; i < 16; ++i)
		//				pSource[i] = viewMatrixValues[i];

		//			break;
		//		}
		//	case 2:
		//		{
		//			line = line.substr(sizeof("Projection matrix:"));

		//			float projectionMatrixValues[16];
		//			for (float& matrixValue : projectionMatrixValues)
		//			{
		//				std::string delimiter = ";";
		//				std::string token = line.substr(0, line.find(delimiter));
		//				line.erase(0, line.find(delimiter) + delimiter.length());

		//				matrixValue = std::stof(token);
		//			}

		//			float* pSource = static_cast<float*>(glm::value_ptr(projectionMatrix));
		//			for (int i = 0; i < 16; ++i)
		//				pSource[i] = projectionMatrixValues[i];

		//			break;
		//		}
		//	}
		//	currentLine++;
		//}

		//m_camera->overrideMatrices(viewMatrix, projectionMatrix);
	}
	else if(requestedScreenshot)
	{
		requestedScreenshot = false;
		gameContext.shadowmapScreenshotsRequested = true;

		screenshotId++;
	}

	m_sceneElements.updateUB(*m_camera);
}

void SponzaScene::frame(WolfEngine* wolfInstance) const
{
	std::vector<CommandRecordBase*> passes;
	passes.push_back(m_depthPass.get());
	if(m_currentPassState.shadowType == ShadowType::CSM)
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
