#include "LoadingScreenUniquePass.h"

#include <Attachment.h>
#include <DescriptorSetGenerator.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Image.h>
#include <ImageFileLoader.h>
#include <FrameBuffer.h>

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

	DescriptorSetLayoutGenerator descriptorSetLayoutGenerator;
	descriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	m_descriptorSetLayout.reset(new DescriptorSetLayout(descriptorSetLayoutGenerator.getDescriptorLayouts()));

	ImageFileLoader imageFileLoader("Textures/loadingScreen.jpg");
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { (uint32_t)imageFileLoader.getWidth(), (uint32_t)imageFileLoader.getHeight(), 1 };
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.format = imageFileLoader.getFormat();
	createImageInfo.mipLevelCount = 1;
	createImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	m_loadingScreenTexture.reset(new Image(createImageInfo));
	m_loadingScreenTexture->copyCPUBuffer(imageFileLoader.getPixels());

	m_sampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1.0f, VK_FILTER_LINEAR));

	DescriptorSetGenerator descriptorSetGenerator(descriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setCombinedImageSampler(0, m_loadingScreenTexture->getImageLayout(), m_loadingScreenTexture->getDefaultImageView(), *m_sampler.get());

	m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
	m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());

	m_vertexShaderParser.reset(new ShaderParser("Shaders/loadingScreen/shader.vert"));
	m_fragmentShaderParser.reset(new ShaderParser("Shaders/loadingScreen/shader.frag"));

	createPipeline(context.swapChainWidth, context.swapChainHeight);

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

	m_fullscreenRect.reset(new Mesh(vertices, indices));
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

	createPipeline(context.swapChainWidth, context.swapChainHeight);
}

void LoadingScreenUniquePass::record(const Wolf::RecordContext& context)
{
	/* Command buffer record */
	uint32_t frameBufferIdx = context.swapChainImageIdx;

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	std::vector<VkClearValue> clearValues(2);
	clearValues[0] = { 1.0f };
	clearValues[1] = { 0.1f, 0.1f, 0.1f, 1.0f };
	m_renderPass->beginRenderPass(m_frameBuffers[frameBufferIdx]->getFramebuffer(), clearValues, m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

	m_fullscreenRect->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_renderPass->endRenderPass(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void LoadingScreenUniquePass::submit(const Wolf::SubmitContext& context)
{
	std::vector<const Semaphore*> waitSemaphores{ context.imageAvailableSemaphore };
	std::vector<VkSemaphore> signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, context.frameFence);

	bool anyShaderModified = m_vertexShaderParser->compileIfFileHasBeenModified();
	if (m_fragmentShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipeline(m_swapChainWidth, m_swapChainHeight);
	}
}

void LoadingScreenUniquePass::createPipeline(uint32_t width, uint32_t height)
{
	RenderingPipelineCreateInfo pipelineCreateInfo;
	pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

	// Programming stages
	std::vector<char> vertexShaderCode;
	m_vertexShaderParser->readCompiledShader(vertexShaderCode);
	std::vector<char> fragmentShaderCode;
	m_fragmentShaderParser->readCompiledShader(fragmentShaderCode);

	std::vector<ShaderCreateInfo> shaders(2);
	shaders[0].shaderCode = vertexShaderCode;
	shaders[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaders[1].shaderCode = fragmentShaderCode;
	shaders[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

	pipelineCreateInfo.shaderCreateInfos = shaders;

	// IA
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	Vertex2DTextured::getAttributeDescriptions(attributeDescriptions, 0);
	pipelineCreateInfo.vertexInputAttributeDescriptions = attributeDescriptions;

	std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
	bindingDescriptions[0] = {};
	Vertex2DTextured::getBindingDescription(bindingDescriptions[0], 0);
	pipelineCreateInfo.vertexInputBindingDescriptions = bindingDescriptions;

	// Resources
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout->getDescriptorSetLayout() };
	pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

	// Viewport
	pipelineCreateInfo.extent = { width, height };

	// Color Blend
	std::vector<RenderingPipelineCreateInfo::BLEND_MODE> blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::OPAQUE };
	pipelineCreateInfo.blendModes = blendModes;

	m_pipeline.reset(new Pipeline(pipelineCreateInfo));

	m_swapChainWidth = width;
	m_swapChainHeight = height;
}
