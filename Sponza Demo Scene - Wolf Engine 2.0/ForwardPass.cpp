#include "ForwardPass.h"

#include <fstream>

#include <Attachment.h>
#include <CameraList.h>
#include <DebugMarker.h>
#include <DescriptorSetGenerator.h>
#include <Image.h>
#include <FrameBuffer.h>
#include <ModelLoader.h>
#include <Timer.h>

#include "CommonLayout.h"
#include "PreDepthPass.h"
#include "GameContext.h"
#include "ShadowMaskComputePass.h"
#include "Vertex2DTextured.h"
#include "RenderMeshList.h"
#include "RTGIPass.h"

using namespace Wolf;

VkDescriptorSetLayout CommonDescriptorLayouts::g_commonForwardDescriptorSetLayout;

ForwardPass::ForwardPass(const ResourceNonOwner<PreDepthPass>& preDepthPass, const Wolf::ResourceNonOwner<ShadowMaskBasePass>& shadowMaskPass, const Wolf::ResourceNonOwner<RTGIPass>& rayTracedGIPass)
	: m_preDepthPass(preDepthPass), m_shadowMaskPass(shadowMaskPass), m_rayTracedGIPass(rayTracedGIPass)
{
	m_preDepthPassSemaphore = preDepthPass->getSemaphore();
}

void ForwardPass::initializeResources(const InitializationContext& context)
{
	Timer timer("Forward pass initialization");

	createOutputImages(context.swapChainWidth, context.swapChainHeight);

	Attachment depth({ context.swapChainWidth, context.swapChainHeight }, context.depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		m_preDepthPass->getOutput()->getDefaultImageView());
	depth.loadOperation = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	Attachment color({ context.swapChainWidth, context.swapChainHeight }, m_outputImages[0]->getFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		nullptr);
	Attachment velocity({ m_velocityImage->getExtent().width, m_velocityImage->getExtent().height }, m_velocityImage->getFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_STORE_OP_STORE, 
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, m_velocityImage->getDefaultImageView());

	m_renderPass.reset(new RenderPass({ depth, color, velocity }));

	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));

	m_frameBuffers.resize(m_outputImages.size());
	for (uint32_t i = 0; i < m_outputImages.size(); ++i)
	{
		color.imageView = m_outputImages[i]->getDefaultImageView();
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { depth, color, velocity }));
	}

	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));

	createDescriptorSetLayout();

	m_userInterfaceVertexShaderParser.reset(new ShaderParser("Shaders/UI.vert"));
	m_userInterfaceFragmentShaderParser.reset(new ShaderParser("Shaders/UI.frag"));

	// Object resources
	{
		m_sampler.reset(new Sampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, 11, VK_FILTER_LINEAR));
		m_lightUniformBuffer.reset(new Buffer(sizeof(LightUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

		createDescriptorSets(true);
	}

	// UI and debug resources
	{
		m_drawFullScreenImageDescriptorSetLayoutGenerator.addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		m_drawFullScreenImageDescriptorSetLayout.reset(new DescriptorSetLayout(m_drawFullScreenImageDescriptorSetLayoutGenerator.getDescriptorLayouts()));

		DescriptorSetGenerator userInterfaceDescriptorSetGenerator(m_drawFullScreenImageDescriptorSetLayoutGenerator.getDescriptorLayouts());
		userInterfaceDescriptorSetGenerator.setCombinedImageSampler(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.userInterfaceImage->getDefaultImageView(), *m_sampler);

		m_userInterfaceDescriptorSet.reset(new DescriptorSet(m_drawFullScreenImageDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
		m_userInterfaceDescriptorSet->update(userInterfaceDescriptorSetGenerator.getDescriptorSetCreateInfo());

		// Load fullscreen rect
		const std::vector<Vertex2DTextured> vertices =
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

	createUIPipeline(context.swapChainWidth, context.swapChainHeight);
}

void ForwardPass::resize(const Wolf::InitializationContext& context)
{
	m_renderPass->setExtent({ context.swapChainWidth, context.swapChainHeight });

	m_frameBuffers.clear();
	m_frameBuffers.resize(context.swapChainImageCount);

	createOutputImages(context.swapChainWidth, context.swapChainHeight);

	Attachment depth({ context.swapChainWidth, context.swapChainHeight }, context.depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		m_preDepthPass->getOutput()->getDefaultImageView());
	depth.loadOperation = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	Attachment color({ context.swapChainWidth, context.swapChainHeight }, m_outputImages[0]->getFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		nullptr);
	Attachment velocity({ m_velocityImage->getExtent().width, m_velocityImage->getExtent().height }, m_velocityImage->getFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ATTACHMENT_STORE_OP_STORE,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, m_velocityImage->getDefaultImageView());

	for (uint32_t i = 0; i < m_outputImages.size(); ++i)
	{
		color.imageView = m_outputImages[i]->getDefaultImageView();
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { depth, color, velocity }));
	}

	createUIPipeline(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSets(false);

	DescriptorSetGenerator descriptorSetGenerator(m_drawFullScreenImageDescriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setCombinedImageSampler(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.userInterfaceImage->getDefaultImageView(), *m_sampler);
	m_userInterfaceDescriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
}

void ForwardPass::record(const Wolf::RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);
	const uint32_t currentMaskIdx = context.currentFrameIdx % ShadowMaskComputePass::MASK_COUNT;
	const CameraInterface* camera = context.cameraList->getCamera(CommonCameraIndices::CAMERA_IDX_ACTIVE);

	LightUBData lightUBData;
	lightUBData.colorDirectionalLight = gameContext->sunColor;
	lightUBData.directionDirectionalLight = glm::transpose(glm::inverse(camera->getViewMatrix())) * glm::vec4(gameContext->sunDirection, 1.0f);
	lightUBData.outputSize = glm::uvec2(m_preDepthPass->getOutput()->getExtent().width, m_preDepthPass->getOutput()->getExtent().height);
	m_lightUniformBuffer->transferCPUMemory(&lightUBData, sizeof(lightUBData), 0 /* srcOffet */);

	/* Command buffer record */
	const uint32_t frameBufferIdx = context.currentFrameIdx % m_outputImages.size();

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::renderPassDebugColor, "Forward pass");

	m_preDepthPass->getOutput()->setImageLayoutWithoutOperation(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // at this point, preDepthPass should have set layout with render pass
	m_preDepthPass->getOutput()->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT });

	if (m_usedDebugImage)
		m_usedDebugImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), Image::SampledInFragmentShader(0));

	std::vector<VkClearValue> clearValues(3);
	clearValues[0] = { 0.0f };
	clearValues[1] = { 0.1f, 0.1f, 0.1f, 1.0f };
	clearValues[2] = { 0.1f, 0.1f, 0.1f, 1.0f };
	m_renderPass->beginRenderPass(m_frameBuffers[frameBufferIdx]->getFramebuffer(), clearValues, m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	context.renderMeshList->draw(context, m_commandBuffer->getCommandBuffer(context.commandBufferIdx), m_renderPass.get(), CommonPipelineIndices::PIPELINE_IDX_FORWARD, CommonCameraIndices::CAMERA_IDX_ACTIVE,
		{
			{ 3, m_descriptorSets[currentMaskIdx].get() }
		});

	/* UI and debug */
	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawFullScreenImagePipeline->getPipeline());

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawFullScreenImagePipeline->getPipelineLayout(), 0, 1,
		m_userInterfaceDescriptorSet->getDescriptorSet(), 0, nullptr);
	m_fullscreenRect->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), RenderMeshList::NO_CAMERA_IDX);

	if (m_usedDebugImage)
	{
		vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawFullScreenImagePipeline->getPipelineLayout(), 0, 1,
			m_debugDescriptorSet->getDescriptorSet(), 0, nullptr);
		m_fullscreenRect->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), RenderMeshList::NO_CAMERA_IDX);
	}

	m_renderPass->endRenderPass(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	if (m_usedDebugImage)
		m_usedDebugImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });

	DebugMarker::endRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void ForwardPass::submit(const SubmitContext& context)
{
	const std::vector waitSemaphores{ m_shadowMaskPass->getSemaphore(), context.userInterfaceImageAvailableSemaphore };
	const std::vector signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	bool anyShaderModified = m_userInterfaceVertexShaderParser->compileIfFileHasBeenModified();
	if (m_userInterfaceFragmentShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createDescriptorSetLayout();
		createDescriptorSets(true);
		createUIPipeline(m_swapChainWidth, m_swapChainHeight);
	}
}

void ForwardPass::setShadowMaskPass(const ResourceNonOwner<ShadowMaskBasePass>& shadowMaskPass)
{
	m_shadowMaskPass = shadowMaskPass;
	createDescriptorSetLayout();
	createDescriptorSets(true);
}

void ForwardPass::setDebugMode(DebugMode debugMode)
{
	switch (debugMode)
	{
		case DebugMode::None:
			m_usedDebugImage = nullptr;
			break;
		case DebugMode::Shadows:
			m_usedDebugImage = m_shadowMaskPass->getDebugImage();
			break;
	}

	if (m_usedDebugImage)
		createOrUpdateDebugDescriptorSet();
}

void ForwardPass::createOutputImages(uint32_t width, uint32_t height)
{
	// Color
	{
		CreateImageInfo createImageInfo;
		createImageInfo.extent = { width, height, 1 };
		createImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		createImageInfo.mipLevelCount = 1;
		createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		createImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		for (ResourceUniqueOwner<Image>& m_outputImage : m_outputImages)
		{
			m_outputImage.reset(new Image(createImageInfo));
			m_outputImage->setImageLayout({ VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });
		}
	}

	// Velocity
	{
		CreateImageInfo createImageInfo;
		createImageInfo.extent = { width, height, 1 };
		createImageInfo.format = VK_FORMAT_R16G16_SFLOAT;
		createImageInfo.mipLevelCount = 1;
		createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		createImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		m_velocityImage.reset(new Image(createImageInfo));
	}
}

void ForwardPass::createUIPipeline(uint32_t width, uint32_t height)
{
	// UI
	{
		RenderingPipelineCreateInfo pipelineCreateInfo;
		pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

		// Programming stages
		pipelineCreateInfo.shaderCreateInfos.resize(2);
		m_userInterfaceVertexShaderParser->readCompiledShader(pipelineCreateInfo.shaderCreateInfos[0].shaderCode);
		pipelineCreateInfo.shaderCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		m_userInterfaceFragmentShaderParser->readCompiledShader(pipelineCreateInfo.shaderCreateInfos[1].shaderCode);
		pipelineCreateInfo.shaderCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

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
		pipelineCreateInfo.descriptorSetLayouts = { m_drawFullScreenImageDescriptorSetLayout->getDescriptorSetLayout() };

		// Color Blend
		pipelineCreateInfo.blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::TRANS_ALPHA, RenderingPipelineCreateInfo::BLEND_MODE::TRANS_ALPHA };

		m_drawFullScreenImagePipeline.reset(new Pipeline(pipelineCreateInfo));
	}

	m_swapChainWidth = width;
	m_swapChainHeight = height;
}

void ForwardPass::createDescriptorSetLayout()
{
	m_descriptorSetLayoutGenerator.reset();
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_FRAGMENT_BIT, 3); // shadow mask
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT, 4); // light ub
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 5, 1); // depth buffer
	if (m_shadowMaskPass->getDenoisingPatternImage())
	{
		m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 6, 1); // denoising sampling pattern	
	}
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));
	CommonDescriptorLayouts::g_commonForwardDescriptorSetLayout = m_descriptorSetLayout->getDescriptorSetLayout();
}

void ForwardPass::createDescriptorSets(bool forceReset)
{
	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());
	
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

void ForwardPass::createOrUpdateDebugDescriptorSet()
{
	DescriptorSetGenerator debugDescriptorSetGenerator(m_drawFullScreenImageDescriptorSetLayoutGenerator.getDescriptorLayouts());
	debugDescriptorSetGenerator.setCombinedImageSampler(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_usedDebugImage->getDefaultImageView(), *m_sampler);

	if (!m_debugDescriptorSet)
		m_debugDescriptorSet.reset(new DescriptorSet(m_drawFullScreenImageDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
	m_debugDescriptorSet->update(debugDescriptorSetGenerator.getDescriptorSetCreateInfo());
}
