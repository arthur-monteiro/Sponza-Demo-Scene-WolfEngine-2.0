#include "RayTracedShadowsPass.h"

#include <fstream>
#include <random>

#include <CameraInterface.h>
#include <CommandBuffer.h>
#include <DescriptorSetGenerator.h>
#include <DescriptorSetLayoutGenerator.h>
#include <RayTracingShaderGroupGenerator.h>

#include "CameraList.h"
#include "CommonLayout.h"
#include "DebugMarker.h"
#include "PreDepthPass.h"
#include "GameContext.h"
#include "GraphicCameraInterface.h"
#include "ObjectModel.h"

using namespace Wolf;

RayTracedShadowsPass::RayTracedShadowsPass(const ObjectModel* sponzaModel, PreDepthPass* preDepthPass)
{
	m_sponzaModel = sponzaModel;
	m_preDepthPass = preDepthPass;
}

void RayTracedShadowsPass::initializeResources(const InitializationContext& context)
{
	m_commandBuffer.reset(new CommandBuffer(QueueType::RAY_TRACING, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR));

	m_rayGenShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rgen", {}, 1));
	m_rayMissShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rmiss"));
	m_closestHitShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rchit"));

	m_descriptorSetLayoutGenerator.addAccelerationStructure(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0); // TLAS
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_RAYGEN_BIT_KHR,                                                1); // output image
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR,                    2, 1); // input depth
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_RAYGEN_BIT_KHR,											      3); // uniform buffer
	m_descriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_RAYGEN_BIT_KHR,                                        4); // noise map
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_uniformBuffer.reset(new Buffer(sizeof(ShadowUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

	// Noise
	CreateImageInfo noiseImageCreateInfo;
	noiseImageCreateInfo.extent = { NOISE_TEXTURE_SIZE_PER_SIDE, NOISE_TEXTURE_SIZE_PER_SIDE, NOISE_TEXTURE_VECTOR_COUNT };
	noiseImageCreateInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	noiseImageCreateInfo.mipLevelCount = 1;
	noiseImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	m_noiseImage.reset(new Image(noiseImageCreateInfo));

	std::vector<float> noiseData(NOISE_TEXTURE_SIZE_PER_SIDE * NOISE_TEXTURE_SIZE_PER_SIDE * NOISE_TEXTURE_VECTOR_COUNT * 4);
	for (int texY = 0; texY < NOISE_TEXTURE_SIZE_PER_SIDE; ++texY)
	{
		for (int texX = 0; texX < NOISE_TEXTURE_SIZE_PER_SIDE; ++texX)
		{
			for (int i = 0; i < NOISE_TEXTURE_VECTOR_COUNT; ++i)
			{
				glm::vec3 offsetDirection(jitter(), 2.0f * jitter() - 1.0f, jitter()); // each jitter returns a different value
				offsetDirection = glm::normalize(offsetDirection);
				
				const uint32_t idx = texX + texY * NOISE_TEXTURE_SIZE_PER_SIDE + i * (NOISE_TEXTURE_SIZE_PER_SIDE * NOISE_TEXTURE_SIZE_PER_SIDE);

				noiseData[4 * idx    ] = offsetDirection.x;
				noiseData[4 * idx + 1] = offsetDirection.y;
				noiseData[4 * idx + 2] = offsetDirection.z;
			}
		}
	}
	m_noiseImage->copyCPUBuffer(reinterpret_cast<unsigned char*>(noiseData.data()), Image::SampledInFragmentShader());

	m_noiseSampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, 1.0f, VK_FILTER_NEAREST));

	// Denoise
	CreateImageInfo denoiseSamplingPatternImageCreateInfo;
	denoiseSamplingPatternImageCreateInfo.extent = { DENOISE_TEXTURE_SIZE, 1, 1 };
	denoiseSamplingPatternImageCreateInfo.format = VK_FORMAT_R32G32_SFLOAT;
	denoiseSamplingPatternImageCreateInfo.mipLevelCount = 1;
	denoiseSamplingPatternImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	m_denoiseSamplingPattern.reset(new Image(denoiseSamplingPatternImageCreateInfo));

	const uint32_t samplingPointCountPerSide = static_cast<uint32_t>(glm::sqrt(DENOISE_TEXTURE_SIZE));
	constexpr float distanceBetweenSamples = 0.003f;
	const float startingSamplePointOffset = -static_cast<int>(samplingPointCountPerSide / 2.0f) * distanceBetweenSamples;

	std::array<glm::vec2, DENOISE_TEXTURE_SIZE> samplingPoints;
	for(uint32_t x = 0; x < samplingPointCountPerSide; ++x)
	{
		for(uint32_t y = 0; y < samplingPointCountPerSide; ++y)
		{
			samplingPoints[x + y * samplingPointCountPerSide] = glm::vec2(x, y) * distanceBetweenSamples + glm::vec2(startingSamplePointOffset);
		}
	}
	m_denoiseSamplingPattern->copyCPUBuffer(reinterpret_cast<unsigned char*>(samplingPoints.data()), { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT });

	// Debug
	m_debugComputeShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/debug.comp", {}, 1));

	m_debugDescriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT, 0); // output
	m_debugDescriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1, 1); // denoising sampling pattern
	m_debugDescriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT, 2); // uniform buffer
	m_debugDescriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 3, 1); // input depth
	m_debugDescriptorSetLayout.reset(new DescriptorSetLayout(m_debugDescriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_debugUniformBuffer.reset(new Buffer(sizeof(DebugUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

	createPipelines();
	createOutputImages(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSet();
}

void RayTracedShadowsPass::resize(const InitializationContext& context)
{
	createOutputImages(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSet();
}

static bool willDoScreenshotsThisFrame = false;
static uint32_t frameCounter = 0;
static uint32_t imageCounter = 101;
void RayTracedShadowsPass::record(const RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);
	const CameraInterface* camera = context.cameraList->getCamera(CommonCameraIndices::CAMERA_IDX_ACTIVE);

	if (gameContext->shadowmapScreenshotsRequested)
	{
		willDoScreenshotsThisFrame = true;
		saveMaskToFile("Exports/noisyImage_" + std::to_string(imageCounter) + ".jpg");

		//m_preDepthPass->getOutput()->exportToFile("depthMap.jpg");

		frameCounter = 16;
	}
	else
	{
		if(willDoScreenshotsThisFrame)
		{
			frameCounter--;

			if (frameCounter == 0)
			{
				saveMaskToFile("Exports/cleanImage_" + std::to_string(imageCounter) + ".jpg");
				willDoScreenshotsThisFrame = false;

				std::ofstream exportPositionsFile;

				exportPositionsFile.open("Exports/exportPositions.txt", std::ios_base::app); // append instead of overwrite
				exportPositionsFile << "Export " << imageCounter << "\n";
				exportPositionsFile << "View matrix: ";
				for(uint32_t i = 0; i < 4; ++i)
					for(uint32_t j = 0; j < 4; ++j)
						exportPositionsFile << camera->getViewMatrix()[i][j] << ";";

				exportPositionsFile << "\nProjection matrix: ";
				for (uint32_t i = 0; i < 4; ++i)
					for (uint32_t j = 0; j < 4; ++j)
						exportPositionsFile << camera->getProjectionMatrix()[i][j] << ";";

				exportPositionsFile << "\nSun phi/theta: ";
				exportPositionsFile << gameContext->sunPhi << ";" << gameContext->sunTheta;

				exportPositionsFile << "\n\n";

				imageCounter++;
			}
		}
	}

	/* Update data */
	ShadowUBData shadowUBData;
	shadowUBData.sunDirectionAndNoiseIndex = glm::vec4(-gameContext->sunDirection, context.currentFrameIdx % NOISE_TEXTURE_VECTOR_COUNT);
	shadowUBData.drawWithoutNoiseFrameIndex = frameCounter;
	shadowUBData.sunAreaAngle = gameContext->sunAreaAngle;

	m_uniformBuffer->transferCPUMemory(&shadowUBData, sizeof(shadowUBData), 0, context.commandBufferIdx);

	DebugUBData debugUBData;
	debugUBData.worldSpaceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
	debugUBData.pixelUV = glm::vec2(0.5f, 0.5f);
	debugUBData.patternSize = glm::vec2(m_denoiseSamplingPattern->getExtent().width, m_denoiseSamplingPattern->getExtent().height);
	debugUBData.outputImageSize = glm::vec2(m_debugOutputImage->getExtent().width, m_debugOutputImage->getExtent().height);

	m_debugUniformBuffer->transferCPUMemory(&debugUBData, sizeof(debugUBData), 0, context.commandBufferIdx);

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::rayTracePassDebugColor, "Ray Trace Shadow Pass");

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipelineLayout(), 0, 1, 
		m_descriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipelineLayout(), 1, 1,
		camera->getDescriptorSet()->getDescriptorSet(), 0, nullptr);

	VkStridedDeviceAddressRegionKHR rgenRegion{};
	rgenRegion.deviceAddress = m_shaderBindingTable->getBuffer().getBufferDeviceAddress();
	rgenRegion.stride = m_shaderBindingTable->getBaseAlignment();
	rgenRegion.size = m_shaderBindingTable->getBaseAlignment();

	VkStridedDeviceAddressRegionKHR rmissRegion{};
	rmissRegion.deviceAddress = m_shaderBindingTable->getBuffer().getBufferDeviceAddress() + rgenRegion.size;
	rmissRegion.stride = m_shaderBindingTable->getBaseAlignment();
	rmissRegion.size = m_shaderBindingTable->getBaseAlignment();

	VkStridedDeviceAddressRegionKHR rhitRegion{};
	rhitRegion.deviceAddress = m_shaderBindingTable->getBuffer().getBufferDeviceAddress() + rgenRegion.size + rmissRegion.size;
	rhitRegion.stride = m_shaderBindingTable->getBaseAlignment();
	rhitRegion.size = m_shaderBindingTable->getBaseAlignment();

	constexpr VkStridedDeviceAddressRegionKHR callRegion{};

	vkCmdTraceRaysKHR(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), &rgenRegion,
		&rmissRegion,
		&rhitRegion,
		&callRegion, m_outputMask->getExtent().width, m_outputMask->getExtent().height, 1);

	DebugMarker::endRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	VkClearColorValue black = { 0.0f, 0.0f, 0.0f };
	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT , 0, 1, 0, 1 };
	vkCmdClearColorImage(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), m_debugOutputImage->getImage(), VK_IMAGE_LAYOUT_GENERAL, &black, 1, &range);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::rayTracePassDebugColor, "Debug ray Trace shadow denoising");

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_debugPipeline->getPipelineLayout(), 0, 1,
		m_debugDescriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_debugPipeline->getPipelineLayout(), 1, 1,
		camera->getDescriptorSet()->getDescriptorSet(), 0, nullptr);

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_debugPipeline->getPipeline());

	constexpr VkExtent3D dispatchGroups = { 16, 1, 1 };
	const uint32_t groupSizeX = m_denoiseSamplingPattern->getExtent().width % dispatchGroups.width != 0 ? m_denoiseSamplingPattern->getExtent().width / dispatchGroups.width + 1 : m_denoiseSamplingPattern->getExtent().width / dispatchGroups.width;
	vkCmdDispatch(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), groupSizeX, dispatchGroups.height, dispatchGroups.depth);

	DebugMarker::endRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void RayTracedShadowsPass::submit(const SubmitContext& context)
{
	const std::vector waitSemaphores{ m_preDepthPass->getSemaphore() };
	const std::vector signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	bool anyShaderModified = m_rayGenShaderParser->compileIfFileHasBeenModified();
	if (m_rayMissShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;
	if (m_closestHitShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;
	if (m_debugComputeShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipelines();
	}
}

void RayTracedShadowsPass::saveMaskToFile(const std::string& filename) const
{
	m_outputMask->exportToFile(filename);
}

void RayTracedShadowsPass::createPipelines()
{
	RayTracingShaderGroupGenerator shaderGroupGenerator;
	shaderGroupGenerator.addRayGenShaderStage(0);
	shaderGroupGenerator.addMissShaderStage(1);
	HitGroup hitGroup;
	hitGroup.closestHitShaderIdx = 2;
	shaderGroupGenerator.addHitGroup(hitGroup);

	RayTracingPipelineCreateInfo pipelineCreateInfo;

	std::vector<char> rayGenShaderCode;
	m_rayGenShaderParser->readCompiledShader(rayGenShaderCode);
	std::vector<char> rayMissShaderCode;
	m_rayMissShaderParser->readCompiledShader(rayMissShaderCode);
	std::vector<char> closestHitShaderCode;
	m_closestHitShaderParser->readCompiledShader(closestHitShaderCode);

	std::vector<ShaderCreateInfo> shaders(3);
	shaders[0].shaderCode = rayGenShaderCode;
	shaders[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	shaders[1].shaderCode = rayMissShaderCode;
	shaders[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	shaders[2].shaderCode = closestHitShaderCode;
	shaders[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	pipelineCreateInfo.shaderCreateInfos = shaders;

	pipelineCreateInfo.shaderGroupsCreateInfos = shaderGroupGenerator.getShaderGroups();

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout->getDescriptorSetLayout(), GraphicCameraInterface::getDescriptorSetLayout() };
	m_pipeline.reset(new Pipeline(pipelineCreateInfo, descriptorSetLayouts));

	m_shaderBindingTable.reset(new ShaderBindingTable(static_cast<uint32_t>(shaders.size()), m_pipeline->getPipeline()));

	// Debug
	std::vector<char> debugComputeShaderCode;
	m_debugComputeShaderParser->readCompiledShader(debugComputeShaderCode);

	ShaderCreateInfo debugComputeShaderCreateInfo;
	debugComputeShaderCreateInfo.shaderCode = debugComputeShaderCode;
	debugComputeShaderCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;

	std::vector<VkDescriptorSetLayout> debugDescriptorSetLayouts = { m_debugDescriptorSetLayout->getDescriptorSetLayout(), GraphicCameraInterface::getDescriptorSetLayout() };
	m_debugPipeline.reset(new Pipeline(debugComputeShaderCreateInfo, debugDescriptorSetLayouts));
}

void RayTracedShadowsPass::createDescriptorSet()
{
	DescriptorSetGenerator::ImageDescription outputImageDesc(VK_IMAGE_LAYOUT_GENERAL, m_outputMask->getDefaultImageView());
	DescriptorSetGenerator::ImageDescription preDepthImageDesc{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_preDepthPass->getCopy()->getDefaultImageView() };

	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setAccelerationStructure(0, m_sponzaModel->getTLAS());
	descriptorSetGenerator.setImage(1, outputImageDesc);
	descriptorSetGenerator.setImage(2, preDepthImageDesc);
	descriptorSetGenerator.setBuffer(3, *m_uniformBuffer);
	descriptorSetGenerator.setCombinedImageSampler(4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_noiseImage->getDefaultImageView(), *m_noiseSampler);

	if (!m_descriptorSet)
		m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
	m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());

	DescriptorSetGenerator debugDescriptorSetGenerator(m_debugDescriptorSetLayoutGenerator.getDescriptorLayouts());
	DescriptorSetGenerator::ImageDescription debugOutputImageDesc{ VK_IMAGE_LAYOUT_GENERAL, m_debugOutputImage->getDefaultImageView() };
	debugDescriptorSetGenerator.setImage(0, debugOutputImageDesc);
	DescriptorSetGenerator::ImageDescription denoisingPatternTextureDescription { VK_IMAGE_LAYOUT_GENERAL, m_denoiseSamplingPattern->getDefaultImageView() };
	debugDescriptorSetGenerator.setImage(1, denoisingPatternTextureDescription);
	debugDescriptorSetGenerator.setBuffer(2, *m_debugUniformBuffer);
	debugDescriptorSetGenerator.setImage(3, preDepthImageDesc);

	if (!m_debugDescriptorSet)
		m_debugDescriptorSet.reset(new DescriptorSet(m_debugDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
	m_debugDescriptorSet->update(debugDescriptorSetGenerator.getDescriptorSetCreateInfo());
}

void RayTracedShadowsPass::createOutputImages(uint32_t width, uint32_t height)
{
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { width, height, 1 };
	createImageInfo.format = VK_FORMAT_R32_SFLOAT;
	createImageInfo.mipLevelCount = 1;
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
	m_outputMask.reset(new Image(createImageInfo));
	m_outputMask->setImageLayout({ VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR });

	// Debug
	createImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	createImageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // needed to clear the image
	createImageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	m_debugOutputImage.reset(new Image(createImageInfo));
	m_debugOutputImage->setImageLayout({ VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });
}

float RayTracedShadowsPass::jitter()
{
	static std::default_random_engine generator;
	static std::uniform_real_distribution distrib(-0.5f, 0.5f);
	return distrib(generator);
}
