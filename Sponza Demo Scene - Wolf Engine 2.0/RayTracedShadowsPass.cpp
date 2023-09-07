#include "RayTracedShadowsPass.h"

#include <fstream>
#include <random>

#include <CameraInterface.h>
#include <CommandBuffer.h>
#include <DescriptorSetGenerator.h>
#include <DescriptorSetLayoutGenerator.h>
#include <RayTracingShaderGroupGenerator.h>

#include "DebugMarker.h"
#include "DepthPass.h"
#include "GameContext.h"
#include "ObjectModel.h"

using namespace Wolf;

RayTracedShadowsPass::RayTracedShadowsPass(const ObjectModel* sponzaModel, DepthPass* preDepthPass)
{
	m_sponzaModel = sponzaModel;
	m_preDepthPass = preDepthPass;
}

void RayTracedShadowsPass::initializeResources(const InitializationContext& context)
{
	m_commandBuffer.reset(new CommandBuffer(QueueType::RAY_TRACING, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR));

	m_rayGenShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rgen"));
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

	const uint32_t samplingPointCountPerSide = glm::sqrt(DENOISE_TEXTURE_SIZE);
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

	createPipeline();
	createOutputImage(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSet();
}

void RayTracedShadowsPass::resize(const InitializationContext& context)
{
	createOutputImage(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSet();
}

static bool willDoScreenshotsThisFrame = false;
static uint32_t frameCounter = 0;
static uint32_t imageCounter = 101;
void RayTracedShadowsPass::record(const RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);

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
						exportPositionsFile << context.camera->getViewMatrix()[i][j] << ";";

				exportPositionsFile << "\nProjection matrix: ";
				for (uint32_t i = 0; i < 4; ++i)
					for (uint32_t j = 0; j < 4; ++j)
						exportPositionsFile << context.camera->getProjectionMatrix()[i][j] << ";";

				exportPositionsFile << "\nSun phi/theta: ";
				exportPositionsFile << gameContext->sunPhi << ";" << gameContext->sunTheta;

				exportPositionsFile << "\n\n";

				imageCounter++;
			}
		}
	}

	/* Update data */
	ShadowUBData shadowUBData;
	shadowUBData.invModelView = glm::inverse(context.camera->getViewMatrix());
	shadowUBData.invProjection = glm::inverse(context.camera->getProjectionMatrix());
	const float near = context.camera->getNear();
	const float far = context.camera->getFar();
	shadowUBData.projectionParams.x = far / (far - near);
	shadowUBData.projectionParams.y = (-far * near) / (far - near);
	shadowUBData.sunDirectionAndNoiseIndex = glm::vec4(-gameContext->sunDirection, context.currentFrameIdx % NOISE_TEXTURE_VECTOR_COUNT);
	shadowUBData.drawWithoutNoiseFrameIndex = frameCounter;
	shadowUBData.sunAreaAngle = gameContext->sunAreaAngle;

	m_uniformBuffer->transferCPUMemory(&shadowUBData, sizeof(shadowUBData), 0 /* srcOffet */, context.commandBufferIdx);

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::rayTracePassDebugColor, "Ray Trace Shadow Pass");

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

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

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipeline();
	}
}

void RayTracedShadowsPass::saveMaskToFile(const std::string& filename) const
{
	m_outputMask->exportToFile(filename);
}

void RayTracedShadowsPass::createPipeline()
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

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout->getDescriptorSetLayout() };
	m_pipeline.reset(new Pipeline(pipelineCreateInfo, descriptorSetLayouts));

	m_shaderBindingTable.reset(new ShaderBindingTable(static_cast<uint32_t>(shaders.size()), m_pipeline->getPipeline()));
}

void RayTracedShadowsPass::createDescriptorSet()
{
	DescriptorSetGenerator::ImageDescription outputImageDesc(VK_IMAGE_LAYOUT_GENERAL, m_outputMask->getDefaultImageView());
	DescriptorSetGenerator::ImageDescription preDepthImageDesc{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_preDepthPass->getOutput()->getDefaultImageView() };

	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setAccelerationStructure(0, m_sponzaModel->getTLAS());
	descriptorSetGenerator.setImage(1, outputImageDesc);
	descriptorSetGenerator.setImage(2, preDepthImageDesc);
	descriptorSetGenerator.setBuffer(3, *m_uniformBuffer);
	descriptorSetGenerator.setCombinedImageSampler(4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_noiseImage->getDefaultImageView(), *m_noiseSampler);

	if (!m_descriptorSet)
		m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
	m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
}

void RayTracedShadowsPass::createOutputImage(uint32_t width, uint32_t height)
{
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { width, height, 1 };
	createImageInfo.format = VK_FORMAT_R32_SFLOAT;
	createImageInfo.mipLevelCount = 1;
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	m_outputMask.reset(new Image(createImageInfo));
	m_outputMask->setImageLayout({ VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR });
}

float RayTracedShadowsPass::jitter()
{
	static std::default_random_engine generator;
	static std::uniform_real_distribution distrib(-0.5f, 0.5f);
	return distrib(generator);
}
