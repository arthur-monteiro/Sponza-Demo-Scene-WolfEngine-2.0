#include "SponzaScene.h"

#include <glm/ext.hpp>
#include <fstream>

#include <ImageFileLoader.h>
#include <MipMapGenerator.h>

#include "CommonLayout.h"
#include "GameContext.h"

using namespace Wolf;

SponzaScene::SponzaScene(WolfEngine* wolfInstance, std::mutex* vulkanQueueLock)
{
	m_camera.reset(new FirstPersonCamera(glm::vec3(1.4f, 1.2f, 0.3f), glm::vec3(2.0f, 0.9f, -0.3f), glm::vec3(0.0f, 1.0f, 0.0f), 0.01f, 5.0f, 16.0f / 9.0f));

	ModelLoadingInfo modelLoadingInfo;
	modelLoadingInfo.filename = "Models/sponza/sponza.obj";
	modelLoadingInfo.mtlFolder = "Models/sponza";
	modelLoadingInfo.vulkanQueueLock = vulkanQueueLock;
	modelLoadingInfo.loadMaterials = true;
	modelLoadingInfo.materialIdOffset = 1;
	if (wolfInstance->isRayTracingAvailable())
	{
		VkBufferUsageFlags rayTracingFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		modelLoadingInfo.additionalVertexBufferUsages = rayTracingFlags;
		modelLoadingInfo.additionalIndexBufferUsages = rayTracingFlags;
	}
	m_sponzaModel.reset(new ModelBase(modelLoadingInfo, wolfInstance->isRayTracingAvailable(), wolfInstance->getBindlessDescriptor()));
	m_sponzaModel->setTransform(glm::scale(glm::vec3(0.01f)));

	modelLoadingInfo.filename = "Models/cube.obj";
	modelLoadingInfo.mtlFolder = "Models";
	modelLoadingInfo.loadMaterials = false;
	modelLoadingInfo.materialIdOffset = 0;
	m_cubeModel.reset(new ModelBase(modelLoadingInfo, wolfInstance->isRayTracingAvailable(), wolfInstance->getBindlessDescriptor()));

	if (wolfInstance->isRayTracingAvailable())
	{
		BLASInstance blasInstance;
		blasInstance.bottomLevelAS = m_sponzaModel->getBLAS();
		blasInstance.hitGroupIndex = 0;
		blasInstance.transform = m_sponzaModel->getTransform();
		blasInstance.instanceID = 0;
		std::vector<BLASInstance> blasInstances = { blasInstance };
		m_tlas.reset(new TopLevelAccelerationStructure(blasInstances));
	}

	m_preDepthPass.reset(new PreDepthPass(true));
	wolfInstance->initializePass(m_preDepthPass.createNonOwnerResource<CommandRecordBase>());

	m_cascadedShadowMappingPass.reset(new CascadedShadowMapping);
	wolfInstance->initializePass(m_cascadedShadowMappingPass.createNonOwnerResource<CommandRecordBase>());

	m_shadowMaskComputePass.reset(new ShadowMaskComputePass(m_preDepthPass.createNonOwnerResource(), m_cascadedShadowMappingPass.createNonOwnerResource()));
	wolfInstance->initializePass(m_shadowMaskComputePass.createNonOwnerResource<CommandRecordBase>());

	if (wolfInstance->isRayTracingAvailable())
	{
		m_rayTracedShadowsPass.reset(new RayTracedShadowsPass(m_tlas.get(), m_preDepthPass.createNonOwnerResource()));
		wolfInstance->initializePass(m_rayTracedShadowsPass.createNonOwnerResource<CommandRecordBase>());
	}

	const ResourceNonOwner<ShadowMaskBasePass> shadowPass = m_currentPassState.shadowType == ShadowType::CSM ? m_shadowMaskComputePass.createNonOwnerResource<ShadowMaskBasePass>() : 
		m_rayTracedShadowsPass.createNonOwnerResource<ShadowMaskBasePass>();

	m_rayTracedGlobalIlluminationPass.reset(new RTGIPass(m_preDepthPass.createNonOwnerResource(), vulkanQueueLock));

	m_forwardPass.reset(new ForwardPass(m_preDepthPass.createNonOwnerResource(), shadowPass,
		m_rayTracedGlobalIlluminationPass.createNonOwnerResource()));
	wolfInstance->initializePass(m_forwardPass.createNonOwnerResource<CommandRecordBase>());
	
	m_taaComposePass.reset(new TemporalAntiAliasingPass(m_preDepthPass.createNonOwnerResource(), m_forwardPass.createNonOwnerResource()));
	wolfInstance->initializePass(m_taaComposePass.createNonOwnerResource<CommandRecordBase>());

	wolfInstance->initializePass(m_rayTracedGlobalIlluminationPass.createNonOwnerResource<CommandRecordBase>());

	m_sponzaModel->updateGraphic();
	m_sponzaModel->updateGraphic(); // call twice to set previous matrix
	m_cubeModel->updateGraphic();

	initializePipelineSets(wolfInstance, shadowPass);
}

static uint32_t screenshotId = 0;
static bool requestedScreenshot = false;
void SponzaScene::update(WolfEngine* wolfInstance, GameContext& gameContext)
{
	// Handle pass state changes
	const PassState nextPassState = m_nextPassState; // copy info as 'm_nextPassState' can be changed between here and line 'm_currentPassState = nextPassState;'
	if(nextPassState.shadowType != m_currentPassState.shadowType)
	{
		wolfInstance->waitIdle();
		const ResourceNonOwner<ShadowMaskBasePass> shadowPass = nextPassState.shadowType == ShadowType::CSM ? m_shadowMaskComputePass.createNonOwnerResource<ShadowMaskBasePass>() :
			m_rayTracedShadowsPass.createNonOwnerResource<ShadowMaskBasePass>();
		m_forwardPass->setShadowMaskPass(shadowPass);
		m_forwardPass->setDebugMode(nextPassState.debugMode); // changing shadow type might change debug image
		initializePipelineSets(wolfInstance, shadowPass);
		m_rayTracedGlobalIlluminationPass->initializeDebugPipelineSet();
	}
	else if (nextPassState.debugMode != m_currentPassState.debugMode)
	{
		wolfInstance->waitIdle();
		m_forwardPass->setDebugMode(nextPassState.debugMode);
	}
	m_currentPassState = nextPassState;

	// Add meshes
	m_sponzaModel->addMeshToRenderList(wolfInstance->getRenderMeshList());
	m_cubeModel->addMeshToRenderList(wolfInstance->getRenderMeshList());
	if (m_currentPassState.debugMode == ForwardPass::DebugMode::RTGI)
		m_rayTracedGlobalIlluminationPass->addDebugMeshesToRenderList(wolfInstance->getRenderMeshList());

	// Add cameras
	m_camera->setEnableJittering(gameContext.enableTAA);
	wolfInstance->getCameraList().addCameraForThisFrame(m_camera.get(), CommonCameraIndices::CAMERA_IDX_ACTIVE);
	if (m_currentPassState.shadowType == ShadowType::CSM)
	{
		m_cascadedShadowMappingPass->addCamerasForThisFrame(wolfInstance->getCameraList());
	}

	wolfInstance->updateBeforeFrame();

	const auto currentTime = std::chrono::high_resolution_clock::now();
	const long long offsetInMicrosecond = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_startTime).count();
	const float offsetInSeconds = static_cast<float>(offsetInMicrosecond) / 1'000'000.0f;
	m_cubeModel->setPosition(glm::vec3(5.0f * glm::sin(offsetInSeconds), 2.0f, 0.0f));
	m_cubeModel->updateGraphic();

	gameContext.shadowmapScreenshotsRequested = false;
	if(wolfInstance->getInputHandler()->keyPressedThisFrame(GLFW_KEY_ESCAPE))
	{
		m_isLocked = !m_isLocked;
		wolfInstance->evaluateUserInterfaceScript(m_isLocked ? "setVisibility(true)" : "setVisibility(false)");
		m_camera->setLocked(m_isLocked);
	}
	if(wolfInstance->getInputHandler()->keyPressedThisFrame(GLFW_KEY_SPACE))
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
}

void SponzaScene::frame(WolfEngine* wolfInstance)
{
	std::vector<ResourceNonOwner<CommandRecordBase>> passes;
	passes.push_back(m_preDepthPass.createNonOwnerResource<CommandRecordBase>());
	if(m_currentPassState.shadowType == ShadowType::CSM)
	{
		passes.push_back(m_cascadedShadowMappingPass.createNonOwnerResource<CommandRecordBase>());
		passes.push_back(m_shadowMaskComputePass.createNonOwnerResource<CommandRecordBase>());
	}
	else
	{
		passes.push_back(m_rayTracedShadowsPass.createNonOwnerResource<CommandRecordBase>());
	}
	//passes.push_back(m_rayTracedGlobalIlluminationPass.get());
	passes.push_back(m_forwardPass.createNonOwnerResource<CommandRecordBase>());
	passes.push_back(m_taaComposePass.createNonOwnerResource<CommandRecordBase>());

	wolfInstance->frame(passes, m_taaComposePass->getSemaphore());
}

void SponzaScene::initializePipelineSets(const Wolf::WolfEngine* wolfInstance, const Wolf::ResourceNonOwner<ShadowMaskBasePass>& shadowMaskPass)
{
	m_sponzaPipelineSet.reset(new PipelineSet);

	PipelineSet::PipelineInfo pipelineInfo;

	/* PreDepth */
	pipelineInfo.shaderInfos.resize(1);
	pipelineInfo.shaderInfos[0].shaderFilename = "Shaders/shader.vert";
	pipelineInfo.shaderInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;

	// IA
	Vertex3D::getAttributeDescriptions(pipelineInfo.vertexInputAttributeDescriptions, 0);

	pipelineInfo.vertexInputBindingDescriptions.resize(1);
	Vertex3D::getBindingDescription(pipelineInfo.vertexInputBindingDescriptions[0], 0);

	// Resources
	pipelineInfo.descriptorSetLayouts = { m_sponzaModel->getDescriptorSetLayout()};
	pipelineInfo.cameraDescriptorSlot = 1;
	pipelineInfo.bindlessDescriptorSlot = 2;

	// Color Blend
	pipelineInfo.blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::OPAQUE, RenderingPipelineCreateInfo::BLEND_MODE::OPAQUE };

	m_sponzaPipelineSet->addPipeline(pipelineInfo, CommonPipelineIndices::PIPELINE_IDX_PRE_DEPTH);

	/* Shadow maps */
	pipelineInfo.depthBiasConstantFactor = 4.0f;
	pipelineInfo.depthBiasSlopeFactor = 2.5f;
	m_sponzaPipelineSet->addPipeline(pipelineInfo, CommonPipelineIndices::PIPELINE_IDX_SHADOW_MAP);
	pipelineInfo.depthBiasConstantFactor = 0.0f;
	pipelineInfo.depthBiasSlopeFactor = 0.0f;

	/* Forward */
	pipelineInfo.shaderInfos.resize(2);
	pipelineInfo.shaderInfos[1].shaderFilename = "Shaders/shader.frag";
	pipelineInfo.shaderInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shadowMaskPass->getConditionalBlocksToEnableWhenReadingMask(pipelineInfo.shaderInfos[1].conditionBlocksToInclude);

	pipelineInfo.descriptorSetLayouts = { m_sponzaModel->getDescriptorSetLayout(), CommonDescriptorLayouts::g_commonForwardDescriptorSetLayout};

	m_sponzaPipelineSet->addPipeline(pipelineInfo, CommonPipelineIndices::PIPELINE_IDX_FORWARD);

	m_sponzaModel->setPipelineSet(m_sponzaPipelineSet.get());
	m_cubeModel->setPipelineSet(m_sponzaPipelineSet.get());
}
