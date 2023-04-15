#include "DepthPass.h"

#include <CameraInterface.h>
#include <DescriptorSetGenerator.h>
#include <ObjLoader.h>
#include <Timer.h>

#include "SponzaModel.h"

using namespace Wolf;

DepthPass::DepthPass(const SponzaModel* sponzaModel)
{
	m_sponzaModel = sponzaModel;
}

void DepthPass::initializeResources(const Wolf::InitializationContext& context)
{
	Timer timer("Depth pass initialization");

	m_swapChainWidth = context.swapChainWidth;
	m_swapChainHeight = context.swapChainHeight;
	m_vertexShaderParser.reset(new ShaderParser("Shaders/shader.vert"));

	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));

	DescriptorSetLayoutGenerator descriptorSetLayoutGenerator;
	descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT, 0);
	m_descriptorSetLayout.reset(new DescriptorSetLayout(descriptorSetLayoutGenerator.getDescriptorLayouts()));

	// Sponza resources
	{
		m_uniformBuffer.reset(new Buffer(sizeof(UBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::EACH_FRAME));

		DescriptorSetGenerator descriptorSetGenerator(descriptorSetLayoutGenerator.getDescriptorLayouts());
		descriptorSetGenerator.setBuffer(0, *m_uniformBuffer.get());

		m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
		m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}

	DepthPassBase::initializeResources(context);

	createPipeline();
}

void DepthPass::resize(const Wolf::InitializationContext& context)
{
	m_swapChainWidth = context.swapChainWidth;
	m_swapChainHeight = context.swapChainHeight;

	DepthPassBase::resize(context);

	createPipeline();
}

void DepthPass::record(const Wolf::RecordContext& context)
{
	/* Update */
	UBData mvp;
	constexpr float near = 0.1f;
	constexpr float far = 100.0f;
	mvp.projection = context.camera->getProjection();
	mvp.model = m_sponzaModel->getTransform();
	mvp.view = context.camera->getViewMatrix();
	m_uniformBuffer->transferCPUMemory((void*)&mvp, sizeof(mvp), 0 /* srcOffet */ , context.commandBufferIdx);

	/* Command buffer record */
	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DepthPassBase::record(context);

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
		createPipeline();
	}
}

void DepthPass::createPipeline()
{
	RenderingPipelineCreateInfo pipelineCreateInfo;
	pipelineCreateInfo.renderPass = m_renderPass->getRenderPass();

	// Programming stages
	std::vector<ShaderCreateInfo> shaders(1);
	std::vector<char> vertexShaderCode;
	m_vertexShaderParser->readCompiledShader(vertexShaderCode);
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
	pipelineCreateInfo.extent = { getWidth(), getHeight() };

	m_pipeline.reset(new Pipeline(pipelineCreateInfo));
}

void DepthPass::recordDraws(const RecordContext& context)
{
	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

	m_sponzaModel->getMesh()->draw(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));
}

VkCommandBuffer DepthPass::getCommandBuffer(const RecordContext& context)
{
	return m_commandBuffer->getCommandBuffer(context.commandBufferIdx);
}
