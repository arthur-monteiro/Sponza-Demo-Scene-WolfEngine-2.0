#include "LoadingScreenUniquePass.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <Attachment.h>
#include <DescriptorSetGenerator.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Image.h>
#include <ImageFileLoader.h>
#include <FrameBuffer.h>

#include "RenderMeshList.h"
#include "Vertex2DTextured.h"

using namespace Wolf;

void LoadingScreenUniquePass::initializeResources(const Wolf::InitializationContext& context)
{
	Attachment color({ context.swapChainWidth, context.swapChainHeight }, context.swapChainFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, nullptr);

	m_renderPass.reset(new RenderPass({ color }));

	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));

	m_frameBuffers.resize(context.swapChainImageCount);
	for (uint32_t i = 0; i < context.swapChainImageCount; ++i)
	{
		color.imageView = context.swapChainImages[i]->getDefaultImageView();
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { color }));
	}

	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT));

	DescriptorSetLayoutGenerator loadingScreenDescriptorSetLayoutGenerator;
	loadingScreenDescriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	m_loadingScreenDescriptorSetLayout.reset(new DescriptorSetLayout(loadingScreenDescriptorSetLayoutGenerator.getDescriptorLayouts()));

	DescriptorSetLayoutGenerator loadingIconDescriptorSetLayoutGenerator;
	loadingIconDescriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT, 0);
	loadingIconDescriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	m_loadingIconDescriptorSetLayout.reset(new DescriptorSetLayout(loadingIconDescriptorSetLayoutGenerator.getDescriptorLayouts()));

	ImageFileLoader loadingScreenFileLoader("Textures/loadingScreen.jpg");
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { (uint32_t)loadingScreenFileLoader.getWidth(), (uint32_t)loadingScreenFileLoader.getHeight(), 1 };
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	createImageInfo.mipLevelCount = 1;
	createImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	m_loadingScreenTexture.reset(new Image(createImageInfo));
	m_loadingScreenTexture->copyCPUBuffer(loadingScreenFileLoader.getPixels(), Image::SampledInFragmentShader());

	ImageFileLoader loadingIconFileLoader("Textures/loadingIcon.png");
	createImageInfo.extent = { (uint32_t)loadingIconFileLoader.getWidth(), (uint32_t)loadingIconFileLoader.getHeight(), 1 };
	m_loadingIconTexture.reset(new Image(createImageInfo));
	m_loadingIconTexture->copyCPUBuffer(loadingIconFileLoader.getPixels(), Image::SampledInFragmentShader());

	m_sampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1.0f, VK_FILTER_LINEAR));

	m_loadingIconUniformBuffer.reset(new Buffer(sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::EACH_FRAME));

	DescriptorSetGenerator loadingScreenDescriptorSetGenerator(loadingScreenDescriptorSetLayoutGenerator.getDescriptorLayouts());
	loadingScreenDescriptorSetGenerator.setCombinedImageSampler(0, m_loadingScreenTexture->getImageLayout(), m_loadingScreenTexture->getDefaultImageView(), *m_sampler);

	m_loadingScreenDescriptorSet.reset(new DescriptorSet(m_loadingScreenDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
	m_loadingScreenDescriptorSet->update(loadingScreenDescriptorSetGenerator.getDescriptorSetCreateInfo());

	m_loadingScreenVertexShaderParser.reset(new ShaderParser("Shaders/loadingScreen/loadingScreen.vert"));
	m_loadingScreenFragmentShaderParser.reset(new ShaderParser("Shaders/loadingScreen/loadingScreen.frag"));

	DescriptorSetGenerator loadingIconDescriptorSetGenerator(loadingIconDescriptorSetLayoutGenerator.getDescriptorLayouts());
	loadingIconDescriptorSetGenerator.setBuffer(0, *m_loadingIconUniformBuffer);
	loadingIconDescriptorSetGenerator.setCombinedImageSampler(1, m_loadingIconTexture->getImageLayout(), m_loadingIconTexture->getDefaultImageView(), *m_sampler);

	m_loadingIconDescriptorSet.reset(new DescriptorSet(m_loadingIconDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
	m_loadingIconDescriptorSet->update(loadingIconDescriptorSetGenerator.getDescriptorSetCreateInfo());

	m_loadingIconVertexShaderParser.reset(new ShaderParser("Shaders/loadingScreen/loadingIcon.vert"));
	m_loadingIconFragmentShaderParser.reset(new ShaderParser("Shaders/loadingScreen/loadingIcon.frag"));

	createPipelines(context.swapChainWidth, context.swapChainHeight);

	// Load fullscreen rect
	std::vector<Vertex2DTextured> vertices =
	{
		{ glm::vec2(-1.0f, -1.0f), glm::vec2(0.0f, 0.0f) }, // top left
		{ glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 0.0f) }, // top right
		{ glm::vec2(-1.0f, 1.0f), glm::vec2(0.0f, 1.0f) }, // bot left
		{ glm::vec2(1.0f, 1.0f),glm::vec2(1.0f, 1.0f) } // bot right
	};

	std::vector<uint32_t> indices =
	{
		0, 2, 1,
		2, 3, 1
	};

	m_rectMesh.reset(new Mesh(vertices, indices));
}

void LoadingScreenUniquePass::resize(const Wolf::InitializationContext& context)
{
	m_renderPass->setExtent({ context.swapChainWidth, context.swapChainHeight });

	m_frameBuffers.clear();
	m_frameBuffers.resize(context.swapChainImageCount);

	Attachment color({ context.swapChainWidth, context.swapChainHeight }, context.swapChainFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, nullptr);
	for (uint32_t i = 0; i < context.swapChainImageCount; ++i)
	{
		color.imageView = context.swapChainImages[i]->getDefaultImageView();
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { color }));
	}

	createPipelines(context.swapChainWidth, context.swapChainHeight);
}

void LoadingScreenUniquePass::record(const Wolf::RecordContext& context)
{
	/* Update */
	const std::chrono::steady_clock::time_point currentTimer = std::chrono::steady_clock::now();
	const float timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTimer - m_startTimer).count() / 1'000.0f;
	const glm::mat4 transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.8f, 0.75f, 0.0f)), timeDiff * 2.0f, glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec3(0.15f, 0.15f, 1.0f));
	m_loadingIconUniformBuffer->transferCPUMemory(&transform, sizeof(transform), 0, context.commandBufferIdx);

	/* Command buffer record */
	const uint32_t frameBufferIdx = context.swapChainImageIdx;

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	std::vector<VkClearValue> clearValues(2);
	clearValues[0] = { 1.0f };
	clearValues[1] = { 0.1f, 0.1f, 0.1f, 1.0f };
	m_renderPass->beginRenderPass(m_frameBuffers[frameBufferIdx]->getFramebuffer(), clearValues, m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_loadingScreenPipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_loadingScreenPipeline->getPipelineLayout(), 0, 1, m_loadingScreenDescriptorSet->getDescriptorSet(), 0, nullptr);
	m_rectMesh->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), RenderMeshList::NO_CAMERA_IDX);

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_loadingIconPipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_loadingIconPipeline->getPipelineLayout(), 0, 1, m_loadingIconDescriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);
	m_rectMesh->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), RenderMeshList::NO_CAMERA_IDX);

	m_renderPass->endRenderPass(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void LoadingScreenUniquePass::submit(const Wolf::SubmitContext& context)
{
	const std::vector<const Semaphore*> waitSemaphores{ context.swapChainImageAvailableSemaphore, context.userInterfaceImageAvailableSemaphore };
	const std::vector<VkSemaphore> signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, context.frameFence);

	bool anyShaderModified = m_loadingScreenVertexShaderParser->compileIfFileHasBeenModified();
	if (m_loadingScreenFragmentShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;
	if (m_loadingIconVertexShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;
	if (m_loadingIconFragmentShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipelines(m_swapChainWidth, m_swapChainHeight);
	}
}

void LoadingScreenUniquePass::createPipelines(uint32_t width, uint32_t height)
{
	// Loading screen
	{
		RenderingPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

		// Programming stages
		pipelineCreateInfo.shaderCreateInfos.resize(2);
		m_loadingScreenVertexShaderParser->readCompiledShader(pipelineCreateInfo.shaderCreateInfos[0].shaderCode);
		pipelineCreateInfo.shaderCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		m_loadingScreenFragmentShaderParser->readCompiledShader(pipelineCreateInfo.shaderCreateInfos[1].shaderCode);
		pipelineCreateInfo.shaderCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

		// IA
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
		Vertex2DTextured::getAttributeDescriptions(attributeDescriptions, 0);
		pipelineCreateInfo.vertexInputAttributeDescriptions = attributeDescriptions;

		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0] = {};
		Vertex2DTextured::getBindingDescription(bindingDescriptions[0], 0);
		pipelineCreateInfo.vertexInputBindingDescriptions = bindingDescriptions;

		// Resources
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_loadingScreenDescriptorSetLayout->getDescriptorSetLayout() };
		pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

		// Viewport
		pipelineCreateInfo.extent = { width, height };

		// Color Blend
		std::vector<RenderingPipelineCreateInfo::BLEND_MODE> blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::OPAQUE };
		pipelineCreateInfo.blendModes = blendModes;

		m_loadingScreenPipeline.reset(new Pipeline(pipelineCreateInfo));
	}

	// Loading Icon
	{
		RenderingPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

		// Programming stages
		pipelineCreateInfo.shaderCreateInfos.resize(2);
		m_loadingIconVertexShaderParser->readCompiledShader(pipelineCreateInfo.shaderCreateInfos[0].shaderCode);
		pipelineCreateInfo.shaderCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		m_loadingIconFragmentShaderParser->readCompiledShader(pipelineCreateInfo.shaderCreateInfos[1].shaderCode);
		pipelineCreateInfo.shaderCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

		// IA
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
		Vertex2DTextured::getAttributeDescriptions(attributeDescriptions, 0);
		pipelineCreateInfo.vertexInputAttributeDescriptions = attributeDescriptions;

		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0] = {};
		Vertex2DTextured::getBindingDescription(bindingDescriptions[0], 0);
		pipelineCreateInfo.vertexInputBindingDescriptions = bindingDescriptions;

		// Resources
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_loadingIconDescriptorSetLayout->getDescriptorSetLayout() };
		pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

		// Viewport
		pipelineCreateInfo.extent = { width, height };

		// Color Blend
		std::vector<RenderingPipelineCreateInfo::BLEND_MODE> blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::TRANS_ALPHA };
		pipelineCreateInfo.blendModes = blendModes;

		m_loadingIconPipeline.reset(new Pipeline(pipelineCreateInfo));
	}

	m_swapChainWidth = width;
	m_swapChainHeight = height;
}
