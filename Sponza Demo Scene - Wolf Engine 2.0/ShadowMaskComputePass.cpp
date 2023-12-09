#include "ShadowMaskComputePass.h"

#include <random>

#include <CameraInterface.h>
#include <DescriptorSetGenerator.h>
#include <ModelLoader.h>

#include "CameraList.h"
#include "CommonLayout.h"
#include "DebugMarker.h"
#include "GraphicCameraInterface.h"
#include "PreDepthPass.h"

using namespace Wolf;

ShadowMaskComputePass::ShadowMaskComputePass(const Wolf::ResourceNonOwner<PreDepthPass>& preDepthPass, const Wolf::ResourceNonOwner<CascadedShadowMapping>& csmPass) :
	m_preDepthPass(preDepthPass), m_csmPass(csmPass)
{
}

void ShadowMaskComputePass::initializeResources(const Wolf::InitializationContext& context)
{
	m_commandBuffer.reset(new CommandBuffer(QueueType::COMPUTE, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));

	m_computeShaderParser.reset(new ShaderParser("Shaders/cascadedShadowMapping/shader.comp", {}, 1));

	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0, 1); // input depth
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT, 1);
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2, CascadedShadowMapping::CASCADE_COUNT); // cascade depth images
	m_descriptorSetLayoutGenerator.addSampler(VK_SHADER_STAGE_COMPUTE_BIT, 3);
	m_descriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT, 4); // noise map
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT, 5); // previous output mask
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT, 6); // output mask
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_uniformBuffer.reset(new Buffer(sizeof(ShadowUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::EACH_FRAME));
	m_shadowMapsSampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, VK_FILTER_LINEAR));

	// Noise
	CreateImageInfo noiseImageCreateInfo;
	noiseImageCreateInfo.extent = { NOISE_TEXTURE_SIZE_PER_SIDE, NOISE_TEXTURE_SIZE_PER_SIDE, NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE * NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE };
	noiseImageCreateInfo.format = VK_FORMAT_R32G32_SFLOAT;
	noiseImageCreateInfo.mipLevelCount = 1;
	noiseImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	m_noiseImage.reset(new Image(noiseImageCreateInfo));

	std::vector<float> noiseData(NOISE_TEXTURE_SIZE_PER_SIDE * NOISE_TEXTURE_SIZE_PER_SIDE * NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE * NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE * 2);
	for (int texY = 0; texY < NOISE_TEXTURE_SIZE_PER_SIDE; ++texY)
	{
		for (int texX = 0; texX < NOISE_TEXTURE_SIZE_PER_SIDE; ++texX)
		{
			for (int v = 0; v < 4; ++v)
			{
				for (int u = 0; u < 4; u++)
				{
					float x = (static_cast<float>(u) + 0.5f + jitter()) / static_cast<float>(NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE);
					float y = (static_cast<float>(v) + 0.5f + jitter()) / static_cast<float>(NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE);

					const uint32_t patternIdx = u + v * NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE;
					const uint32_t idx = texX + texY * NOISE_TEXTURE_SIZE_PER_SIDE + patternIdx * (NOISE_TEXTURE_SIZE_PER_SIDE * NOISE_TEXTURE_SIZE_PER_SIDE);

					constexpr float PI = 3.14159265358979323846264338327950288f;
					noiseData[2 * idx] = sqrtf(y)* cosf(2.0f * PI * x);
					noiseData[2 * idx + 1] = sqrtf(y)* sinf(2.0f * PI * x);
				}
			}
		}
	}
	m_noiseImage->copyCPUBuffer(reinterpret_cast<unsigned char*>(noiseData.data()), Image::SampledInFragmentShader());

	m_noiseSampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, 1.0f, VK_FILTER_NEAREST));

	createPipeline();
	createOutputImages(context.swapChainWidth, context.swapChainHeight);

	for(uint32_t i = 0; i < MASK_COUNT; ++i)
		m_descriptorSets[i].reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
	updateDescriptorSet();

	for (float& noiseRotation : m_noiseRotations)
	{
		noiseRotation = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 3.14f * 2.0f;
	}
}

void ShadowMaskComputePass::resize(const Wolf::InitializationContext& context)
{
	createOutputImages(context.swapChainWidth, context.swapChainHeight);
	updateDescriptorSet();
}

void ShadowMaskComputePass::record(const Wolf::RecordContext& context)
{
	uint32_t currentMaskIdx = context.currentFrameIdx % MASK_COUNT;
	const CameraInterface* camera = context.cameraList->getCamera(CommonCameraIndices::CAMERA_IDX_ACTIVE);

	/* Update data */
	ShadowUBData shadowUBData;
		shadowUBData.cascadeSplits = glm::vec4(m_csmPass->getCascadeSplit(0), m_csmPass->getCascadeSplit(1), m_csmPass->getCascadeSplit(2), m_csmPass->getCascadeSplit(3));
	for (uint32_t cascadeIdx = 0; cascadeIdx < CascadedShadowMapping::CASCADE_COUNT; ++cascadeIdx)
	{
		m_csmPass->getCascadeMatrix(cascadeIdx, shadowUBData.cascadeMatrices[cascadeIdx]);
	}

	const glm::vec2 referenceScale = glm::vec2(glm::length(shadowUBData.cascadeMatrices[0][0]), glm::length(shadowUBData.cascadeMatrices[0][1]));
	for (uint32_t cascadeIdx = 0; cascadeIdx < CascadedShadowMapping::CASCADE_COUNT / 2; ++cascadeIdx)
	{
		glm::vec2 cascadeScale1 = glm::vec2(glm::length(shadowUBData.cascadeMatrices[2 * cascadeIdx][0]), glm::length(shadowUBData.cascadeMatrices[2 * cascadeIdx][1]));
		glm::vec2 cascadeScale2 = glm::vec2(glm::length(shadowUBData.cascadeMatrices[2 * cascadeIdx + 1][0]), glm::length(shadowUBData.cascadeMatrices[2 * cascadeIdx + 1][1]));
		shadowUBData.cascadeScales[cascadeIdx] = glm::vec4(cascadeScale1 / referenceScale, cascadeScale2 / referenceScale);
	}

	shadowUBData.cascadeTextureSize = glm::uvec4(m_csmPass->getCascadeTextureSize(0), m_csmPass->getCascadeTextureSize(1), m_csmPass->getCascadeTextureSize(2), m_csmPass->getCascadeTextureSize(3));

	shadowUBData.noiseRotation = m_noiseRotations[context.currentFrameIdx % m_noiseRotations.size()];
	shadowUBData.screenSize = glm::uvec2(m_outputMasks[currentMaskIdx]->getExtent().width, m_outputMasks[currentMaskIdx]->getExtent().height);

	m_uniformBuffer->transferCPUMemory((void*)&shadowUBData, sizeof(shadowUBData), 0 /* srcOffet */, context.commandBufferIdx);

	/* Command buffer record */
	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::computePassDebugColor, "Shadow Mask Compute Pass");

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipelineLayout(), 0, 1, 
		m_descriptorSets[currentMaskIdx]->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipelineLayout(), 1, 1,
		camera->getDescriptorSet()->getDescriptorSet(), 0, nullptr);

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipeline());

	constexpr VkExtent3D dispatchGroups = { 16, 16, 1 };
	const uint32_t groupSizeX = m_outputMasks[currentMaskIdx]->getExtent().width % dispatchGroups.width != 0 ? m_outputMasks[currentMaskIdx]->getExtent().width / dispatchGroups.width + 1 : m_outputMasks[currentMaskIdx]->getExtent().width / dispatchGroups.width;
	const uint32_t groupSizeY = m_outputMasks[currentMaskIdx]->getExtent().height % dispatchGroups.height != 0 ? m_outputMasks[currentMaskIdx]->getExtent().height / dispatchGroups.height + 1 : m_outputMasks[currentMaskIdx]->getExtent().height / dispatchGroups.height;
	vkCmdDispatch(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), groupSizeX, groupSizeY, dispatchGroups.depth);

	DebugMarker::endRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void ShadowMaskComputePass::submit(const Wolf::SubmitContext& context)
{
	const std::vector waitSemaphores{ m_csmPass->getSemaphore(), m_preDepthPass->getSemaphore() };
	const std::vector signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	if (m_computeShaderParser->compileIfFileHasBeenModified())
	{
		vkDeviceWaitIdle(context.device);
		createPipeline();
	}
}

void ShadowMaskComputePass::createOutputImages(uint32_t width, uint32_t height)
{
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { width, height, 1 };
	createImageInfo.format = VK_FORMAT_R32G32_SFLOAT;
	createImageInfo.mipLevelCount = 1;
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	for (uint32_t i = 0; i < MASK_COUNT; ++i)
	{
		m_outputMasks[i].reset(new Image(createImageInfo));
		m_outputMasks[i]->setImageLayout({ VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });
	}
}

void ShadowMaskComputePass::createPipeline()
{
	// Compute shader parser
	std::vector<char> computeShaderCode;
	m_computeShaderParser->readCompiledShader(computeShaderCode);

	ShaderCreateInfo computeShaderCreateInfo;
	computeShaderCreateInfo.shaderCode = computeShaderCode;
	computeShaderCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts(2);
	descriptorSetLayouts[0] = m_descriptorSetLayout->getDescriptorSetLayout();
	descriptorSetLayouts[1] = GraphicCameraInterface::getDescriptorSetLayout();
	m_pipeline.reset(new Pipeline(computeShaderCreateInfo, descriptorSetLayouts));
}

void ShadowMaskComputePass::updateDescriptorSet() const
{
	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());

	DescriptorSetGenerator::ImageDescription preDepthImageDesc;
	preDepthImageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	preDepthImageDesc.imageView = m_preDepthPass->getOutput()->getDefaultImageView();
	descriptorSetGenerator.setImage(0, preDepthImageDesc);
	descriptorSetGenerator.setBuffer(1, *m_uniformBuffer);
	std::vector<DescriptorSetGenerator::ImageDescription> shadowMapImageDescriptions(CascadedShadowMapping::CASCADE_COUNT);
	for (uint32_t i = 0; i < CascadedShadowMapping::CASCADE_COUNT; ++i)
	{
		shadowMapImageDescriptions[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		shadowMapImageDescriptions[i].imageView = m_csmPass->getShadowMap(i)->getDefaultImageView();
	}
	descriptorSetGenerator.setImages(2, shadowMapImageDescriptions);
	descriptorSetGenerator.setSampler(3, *m_shadowMapsSampler);
	descriptorSetGenerator.setCombinedImageSampler(4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_noiseImage->getDefaultImageView(), *m_noiseSampler);

	for (uint32_t i = 0; i < MASK_COUNT; ++i)
	{
		DescriptorSetGenerator::ImageDescription previousOutputImageDesc;
		previousOutputImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		previousOutputImageDesc.imageView = m_outputMasks[(i + 1) % MASK_COUNT]->getDefaultImageView();
		descriptorSetGenerator.setImage(5, previousOutputImageDesc);

		DescriptorSetGenerator::ImageDescription outputImageDesc;
		outputImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputImageDesc.imageView = m_outputMasks[i]->getDefaultImageView();
		descriptorSetGenerator.setImage(6, outputImageDesc);

		m_descriptorSets[i]->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}
}

float ShadowMaskComputePass::jitter()
{
	static std::default_random_engine generator;
	static std::uniform_real_distribution distrib(-0.5f, 0.5f);
	return distrib(generator);
}
