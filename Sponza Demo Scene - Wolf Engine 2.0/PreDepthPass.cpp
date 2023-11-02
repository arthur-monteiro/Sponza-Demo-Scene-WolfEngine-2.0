#include "PreDepthPass.h"

#include <DescriptorSetLayoutGenerator.h>
#include <DescriptorSetGenerator.h>
#include <ObjLoader.h>
#include <RenderPass.h>
#include <Timer.h>

#include "ObjectModel.h"
#include "SceneElements.h"

using namespace Wolf;

PreDepthPass::PreDepthPass(const SceneElements& sceneElements, bool copyOutput) : m_sceneElements(sceneElements), m_copyOutput(copyOutput)
{

}

void PreDepthPass::initializeResources(const Wolf::InitializationContext& context)
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
		DescriptorSetGenerator descriptorSetGenerator(descriptorSetLayoutGenerator.getDescriptorLayouts());
		descriptorSetGenerator.setBuffer(0, m_sceneElements.getMatricesUB());

		m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
		m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}

	DepthPassBase::initializeResources(context);

	createPipeline();

	createCopyImage(context.depthFormat);
}

void PreDepthPass::resize(const Wolf::InitializationContext& context)
{
	m_swapChainWidth = context.swapChainWidth;
	m_swapChainHeight = context.swapChainHeight;

	DepthPassBase::resize(context);

	createPipeline();

	createCopyImage(context.depthFormat);
}

void PreDepthPass::record(const Wolf::RecordContext& context)
{
	/* Command buffer record */
	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DepthPassBase::record(context);

	VkImageCopy copyRegion{};

	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	copyRegion.srcSubresource.baseArrayLayer = 0;
	copyRegion.srcSubresource.mipLevel = 0;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.srcOffset = { 0, 0, 0 };

	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	copyRegion.dstSubresource.baseArrayLayer = 0;
	copyRegion.dstSubresource.mipLevel = 0;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.dstOffset = { 0, 0, 0 };

	copyRegion.extent = m_copyImage->getExtent();

	m_depthImage->setImageLayoutWithoutOperation(getFinalLayout()); // at this point, preDepthPass should have set layout with render pass
	m_copyImage->recordCopyGPUImage(*m_depthImage, copyRegion, m_commandBuffer->getCommandBuffer(context.commandBufferIdx));
	m_depthImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void PreDepthPass::submit(const Wolf::SubmitContext& context)
{
	const std::vector<const Semaphore*> waitSemaphores{ };
	const std::vector<VkSemaphore> signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	bool anyShaderModified = m_vertexShaderParser->compileIfFileHasBeenModified();

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipeline();
	}
}

void PreDepthPass::createPipeline()
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

void PreDepthPass::createCopyImage(VkFormat format)
{
	CreateImageInfo depthCopyImageCreateInfo;
	depthCopyImageCreateInfo.format = format;
	depthCopyImageCreateInfo.extent.width = getWidth();
	depthCopyImageCreateInfo.extent.height = getHeight();
	depthCopyImageCreateInfo.extent.depth = 1;
	depthCopyImageCreateInfo.mipLevelCount = 1;
	depthCopyImageCreateInfo.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthCopyImageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	m_copyImage.reset(new Image(depthCopyImageCreateInfo));
}

void PreDepthPass::recordDraws(const RecordContext& context)
{
	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(), 0, nullptr);

	m_sceneElements.drawMeshes(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));
}

VkCommandBuffer PreDepthPass::getCommandBuffer(const RecordContext& context)
{
	return m_commandBuffer->getCommandBuffer(context.commandBufferIdx);
}
