#include "DepthPass.h"

#include <CameraInterface.h>
#include <DescriptorSetGenerator.h>
#include <ObjLoader.h>
#include <Timer.h>

using namespace Wolf;

DepthPass::DepthPass(const Wolf::Mesh* sponzaMesh)
{
	m_sponzaMesh = sponzaMesh;
}

void DepthPass::initializeResources(const Wolf::InitializationContext& context)
{
	Timer timer("Depth pass initialization");

	createDepthImage(context);

	Attachment depth({ context.swapChainWidth, context.swapChainHeight }, context.depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		m_depthImage->getDefaultImageView());
	
	m_renderPass.reset(new RenderPass({ depth }));

	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));

	m_frameBuffers.resize(context.swapChainImageCount);
	for (uint32_t i = 0; i < context.swapChainImageCount; ++i)
	{
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { depth }));
	}

	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT));

	DescriptorSetLayoutGenerator descriptorSetLayoutGenerator;
	descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT, 0);
	m_descriptorSetLayout.reset(new DescriptorSetLayout(descriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_vertexShaderParser.reset(new ShaderParser("Shaders/shader.vert"));

	// Sponza resources
	{

		m_uniformBuffer.reset(new Buffer(sizeof(UBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

		DescriptorSetGenerator descriptorSetGenerator(descriptorSetLayoutGenerator.getDescriptorLayouts());
		descriptorSetGenerator.setBuffer(0, *m_uniformBuffer.get());

		m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
		m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}

	createPipeline(context.swapChainWidth, context.swapChainHeight);
}

void DepthPass::resize(const Wolf::InitializationContext& context)
{
	createDepthImage(context);
	m_renderPass->setExtent({ context.swapChainWidth, context.swapChainHeight });

	m_frameBuffers.clear();
	m_frameBuffers.resize(context.swapChainImageCount);

	Attachment depth({ context.swapChainWidth, context.swapChainHeight }, context.depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		m_depthImage->getDefaultImageView());
	for (uint32_t i = 0; i < context.swapChainImageCount; ++i)
	{
		m_frameBuffers[i].reset(new Framebuffer(m_renderPass->getRenderPass(), { depth }));
	}

	createPipeline(context.swapChainWidth, context.swapChainHeight);
}

void DepthPass::record(const Wolf::RecordContext& context)
{
	/* Update */
	UBData mvp;
	constexpr float near = 0.1f;
	constexpr float far = 100.0f;
	mvp.projection = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, near, far);
	mvp.projection[1][1] *= -1;
	mvp.model = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
	mvp.view = context.camera->getViewMatrix();
	m_uniformBuffer->transferCPUMemory((void*)&mvp, sizeof(mvp), 0 /* srcOffet */ /*, context.commandBufferIdx*/);

	/* Command buffer record */
	uint32_t frameBufferIdx = context.swapChainImageIdx;

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	std::vector<VkClearValue> clearValues(1);
	clearValues[0] = { 1.0f };
	m_renderPass->beginRenderPass(m_frameBuffers[frameBufferIdx]->getFramebuffer(), clearValues, m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(/*context.commandBufferIdx*/), 0, nullptr);

	m_sponzaMesh->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_renderPass->endRenderPass(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void DepthPass::submit(const Wolf::SubmitContext& context)
{
	std::vector<const Semaphore*> waitSemaphores{ context.imageAvailableSemaphore };
	std::vector<VkSemaphore> signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	bool anyShaderModified = m_vertexShaderParser->compileIfFileHasBeenModified();

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipeline(m_swapChainWidth, m_swapChainHeight);
	}
}

void DepthPass::createDepthImage(const Wolf::InitializationContext& context)
{
	CreateImageInfo depthImageCreateInfo;
	depthImageCreateInfo.format = context.depthFormat;
	depthImageCreateInfo.extent.width = context.swapChainWidth;
	depthImageCreateInfo.extent.height = context.swapChainHeight;
	depthImageCreateInfo.extent.depth = 1;
	depthImageCreateInfo.mipLevelCount = 1;
	depthImageCreateInfo.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	m_depthImage.reset(new Image(depthImageCreateInfo));
}

void DepthPass::createPipeline(uint32_t width, uint32_t height)
{
	RenderingPipelineCreateInfo pipelineCreateInfo;
	pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

	// Programming stages
	std::vector<char> vertexShaderCode;
	m_vertexShaderParser->readCompiledShader(vertexShaderCode);

	std::vector<ShaderCreateInfo> shaders(1);
	shaders[0].shaderCode = vertexShaderCode;
	shaders[0].stage = VK_SHADER_STAGE_VERTEX_BIT;

	pipelineCreateInfo.shaderCreateInfos = shaders;

	// IA
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	Wolf::Vertex3D::getAttributeDescriptions(attributeDescriptions, 0);
	pipelineCreateInfo.vertexInputAttributeDescriptions = attributeDescriptions;

	std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
	bindingDescriptions[0] = {};
	Wolf::Vertex3D::getBindingDescription(bindingDescriptions[0], 0);
	pipelineCreateInfo.vertexInputBindingDescriptions = bindingDescriptions;

	// Resources
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout->getDescriptorSetLayout() };
	pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

	// Viewport
	pipelineCreateInfo.extent = { width, height };

	m_pipeline.reset(new Pipeline(pipelineCreateInfo));
}
