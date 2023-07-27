#include "ForwardPass.h"

#include <filesystem>
#include <fstream>

#include <Attachment.h>
#include <CameraInterface.h>
#include <DebugMarker.h>
#include <DescriptorSetGenerator.h>
#include <Image.h>
#include <FrameBuffer.h>
#include <ObjLoader.h>
#include <Timer.h>

#include "DepthPass.h"
#include "GameContext.h"
#include "ShadowMaskComputePass.h"
#include "SceneElements.h"
#include "Vertex2DTextured.h"

using namespace Wolf;

ForwardPass::ForwardPass(const SceneElements& sceneElements, DepthPass* preDepthPass, ShadowMaskBasePass* shadowMaskPass) : m_sceneElements(sceneElements)
{
	m_preDepthPassSemaphore = preDepthPass->getSemaphore();
	m_preDepthPass = preDepthPass;
	m_shadowMaskPass = shadowMaskPass;
}

void ForwardPass::initializeResources(const InitializationContext& context)
{
	Timer timer("Forward pass initialization");

	Attachment depth({ context.swapChainWidth, context.swapChainHeight }, context.depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		m_preDepthPass->getOutput()->getDefaultImageView());
	depth.loadOperation = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	Attachment color({ context.swapChainWidth, context.swapChainHeight }, context.swapChainFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, nullptr);

	m_renderPass.reset(new RenderPass({ depth, color }));

	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));

	m_frameBuffers.resize(context.swapChainImageCount);
	for (uint32_t i = 0; i < context.swapChainImageCount; ++i)
	{
		color.imageView = context.swapChainImages[i]->getDefaultImageView();
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { depth, color }));
	}

	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT));

	createDescriptorSetLayout();

	m_vertexShaderParser.reset(new ShaderParser("Shaders/shader.vert"));

	std::vector<std::string> conditionalBlocks;
	m_shadowMaskPass->getConditionalBlocksToEnableWhenReadingMask(conditionalBlocks);
	m_fragmentShaderParser.reset(new ShaderParser("Shaders/shader.frag", conditionalBlocks));

	m_userInterfaceVertexShaderParser.reset(new ShaderParser("Shaders/UI.vert"));
	m_userInterfaceFragmentShaderParser.reset(new ShaderParser("Shaders/UI.frag"));

	// Object resources
	{
		m_sampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, 11, VK_FILTER_LINEAR));
		m_lightUniformBuffer.reset(new Buffer(sizeof(LightUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

		createDescriptorSets(true);
	}

	// UI resources
	{
		m_userInterfaceDescriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		m_userInterfaceDescriptorSetLayout.reset(new DescriptorSetLayout(m_userInterfaceDescriptorSetLayoutGenerator.getDescriptorLayouts()));

		DescriptorSetGenerator descriptorSetGenerator(m_userInterfaceDescriptorSetLayoutGenerator.getDescriptorLayouts());
		descriptorSetGenerator.setCombinedImageSampler(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.userInterfaceImage->getDefaultImageView(), *m_sampler.get());

		m_userInterfaceDescriptorSet.reset(new DescriptorSet(m_userInterfaceDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
		m_userInterfaceDescriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());

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

	createPipelines(context.swapChainWidth, context.swapChainHeight);
}

void ForwardPass::resize(const Wolf::InitializationContext& context)
{
	m_renderPass->setExtent({ context.swapChainWidth, context.swapChainHeight });

	m_frameBuffers.clear();
	m_frameBuffers.resize(context.swapChainImageCount);

	Attachment depth({ context.swapChainWidth, context.swapChainHeight }, context.depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		m_preDepthPass->getOutput()->getDefaultImageView());
	depth.loadOperation = VK_ATTACHMENT_LOAD_OP_LOAD;
	Attachment color({ context.swapChainWidth, context.swapChainHeight }, context.swapChainFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, nullptr);
	for (uint32_t i = 0; i < context.swapChainImageCount; ++i)
	{
		color.imageView = context.swapChainImages[i]->getDefaultImageView();
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { depth, color }));
	}

	createPipelines(context.swapChainWidth, context.swapChainHeight);

	DescriptorSetGenerator descriptorSetGenerator(m_userInterfaceDescriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setCombinedImageSampler(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.userInterfaceImage->getDefaultImageView(), *m_sampler.get());
	m_userInterfaceDescriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
}

void ForwardPass::record(const Wolf::RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);
	const uint32_t currentMaskIdx = context.currentFrameIdx % ShadowMaskComputePass::MASK_COUNT;

	LightUBData lightUBData;
	lightUBData.colorDirectionalLight = gameContext->sunColor;
	lightUBData.directionDirectionalLight = glm::transpose(glm::inverse(context.camera->getViewMatrix())) * glm::vec4(gameContext->sunDirection, 1.0f);
	lightUBData.outputSize = glm::uvec2(m_preDepthPass->getOutput()->getExtent().width, m_preDepthPass->getOutput()->getExtent().height);
	lightUBData.near = context.camera->getNear();
	lightUBData.far = context.camera->getFar();
	m_lightUniformBuffer->transferCPUMemory(&lightUBData, sizeof(lightUBData), 0 /* srcOffet */);

	/* Command buffer record */
	const uint32_t frameBufferIdx = context.swapChainImageIdx;

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::renderPassDebugColor, "Forward pass");

	m_preDepthPass->getOutput()->setImageLayoutWithoutOperation(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // at this point, preDepthPass should have set layout with render pass
	m_preDepthPass->getOutput()->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT });

	std::vector<VkClearValue> clearValues(2);
	clearValues[0] = { 0.0f };
	clearValues[1] = { 0.1f, 0.1f, 0.1f, 1.0f };
	m_renderPass->beginRenderPass(m_frameBuffers[frameBufferIdx]->getFramebuffer(), clearValues, m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1,
		m_descriptorSets[currentMaskIdx]->getDescriptorSet(), 0, nullptr);

	m_sceneElements.drawMeshes(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	/* UI */
	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_userInterfacePipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_userInterfacePipeline->getPipelineLayout(), 0, 1,
		m_userInterfaceDescriptorSet->getDescriptorSet(), 0, nullptr);
	m_fullscreenRect->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_renderPass->endRenderPass(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	DebugMarker::endRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void ForwardPass::submit(const Wolf::SubmitContext& context)
{
	const std::vector waitSemaphores{ m_shadowMaskPass->getSemaphore() };
	const std::vector signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, context.frameFence);

	bool anyShaderModified = m_vertexShaderParser->compileIfFileHasBeenModified();
	std::vector<std::string> conditionalBlocks;
	m_shadowMaskPass->getConditionalBlocksToEnableWhenReadingMask(conditionalBlocks);
	if (m_fragmentShaderParser->compileIfFileHasBeenModified(conditionalBlocks))
		anyShaderModified = true;

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createDescriptorSetLayout();
		createDescriptorSets(true);
		createPipelines(m_swapChainWidth, m_swapChainHeight);
	}
}

void ForwardPass::setShadowMaskPass(ShadowMaskBasePass* shadowMaskPass)
{
	m_shadowMaskPass = shadowMaskPass;
}

void ForwardPass::createPipelines(uint32_t width, uint32_t height)
{
	// Sponza
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
		Vertex3D::getAttributeDescriptions(attributeDescriptions, 0);
		pipelineCreateInfo.vertexInputAttributeDescriptions = attributeDescriptions;

		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0] = {};
		Vertex3D::getBindingDescription(bindingDescriptions[0], 0);
		pipelineCreateInfo.vertexInputBindingDescriptions = bindingDescriptions;

		// Resources
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout->getDescriptorSetLayout() };
		pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

		// Viewport
		pipelineCreateInfo.extent = { width, height };

		// Color Blend
		std::vector<RenderingPipelineCreateInfo::BLEND_MODE> blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::OPAQUE };
		pipelineCreateInfo.blendModes = blendModes;

		// Depth testing
		pipelineCreateInfo.enableDepthWrite = VK_FALSE;
		pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;

		pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;

		m_pipeline.reset(new Pipeline(pipelineCreateInfo));
	}

	// UI
	{
		RenderingPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

		// Programming stages
		std::vector<char> vertexShaderCode;
		m_userInterfaceVertexShaderParser->readCompiledShader(vertexShaderCode);
		std::vector<char> fragmentShaderCode;
		m_userInterfaceFragmentShaderParser->readCompiledShader(fragmentShaderCode);

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

		// Viewport
		pipelineCreateInfo.extent = { width, height };

		// Resources
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_userInterfaceDescriptorSetLayout->getDescriptorSetLayout() };
		pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

		// Color Blend
		std::vector<RenderingPipelineCreateInfo::BLEND_MODE> blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::TRANS_ALPHA };
		pipelineCreateInfo.blendModes = blendModes;

		m_userInterfacePipeline.reset(new Pipeline(pipelineCreateInfo));
	}

	m_swapChainWidth = width;
	m_swapChainHeight = height;
}

void ForwardPass::createDescriptorSetLayout()
{
	m_descriptorSetLayoutGenerator.reset();
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT, 0); // matrices
	m_descriptorSetLayoutGenerator.addSampler(VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 2, m_sceneElements.getImageCount());
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_FRAGMENT_BIT, 3);
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT, 4); // light ub
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 5, 1); // depth buffer
	if (m_shadowMaskPass->getDenoisingPatternImage())
	{
		m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 6, 1); // denoising sampling pattern	
	}
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));
}

void ForwardPass::createDescriptorSets(bool forceReset)
{
	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setBuffer(0, m_sceneElements.getMatricesUB());
	descriptorSetGenerator.setSampler(1, *m_sampler);

	std::vector<DescriptorSetGenerator::ImageDescription> imageDescriptions;
	for (uint32_t i = 0; i < m_sceneElements.getImageCount(); ++i)
	{
		imageDescriptions.resize(imageDescriptions.size() + 1);
		imageDescriptions.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageDescriptions.back().imageView = m_sceneElements.getImageView(i);
	}
	descriptorSetGenerator.setImages(2, imageDescriptions);
	DescriptorSetGenerator::ImageDescription shadowMaskDesc;
	descriptorSetGenerator.setBuffer(4, *m_lightUniformBuffer);

	DescriptorSetGenerator::ImageDescription depthBufferDescription;
	depthBufferDescription.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	depthBufferDescription.imageView = m_preDepthPass->getCopy()->getDefaultImageView();
	descriptorSetGenerator.setImages(5, { depthBufferDescription });

	if(Image* denoisingPatternImage = m_shadowMaskPass->getDenoisingPatternImage())
	{
		DescriptorSetGenerator::ImageDescription denoisingPatternTextureDescription;
		denoisingPatternTextureDescription.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		denoisingPatternTextureDescription.imageView = denoisingPatternImage->getDefaultImageView();
		descriptorSetGenerator.setImages(6, { denoisingPatternTextureDescription });
	}

	for (uint32_t i = 0; i < ShadowMaskComputePass::MASK_COUNT; ++i)
	{
		shadowMaskDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		shadowMaskDesc.imageView = m_shadowMaskPass->getOutput(i)->getDefaultImageView();
		descriptorSetGenerator.setImage(3, shadowMaskDesc);

		if (!m_descriptorSets[i] || forceReset)
			m_descriptorSets[i].reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
		m_descriptorSets[i]->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}
}
