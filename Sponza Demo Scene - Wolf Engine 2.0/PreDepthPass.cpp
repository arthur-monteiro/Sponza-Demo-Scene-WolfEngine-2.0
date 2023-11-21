#include "PreDepthPass.h"

#include <DescriptorSetLayoutGenerator.h>
#include <DescriptorSetGenerator.h>
#include <ModelLoader.h>
#include <Timer.h>

#include "CommonLayout.h"
#include "RenderMeshList.h"

using namespace Wolf;

void PreDepthPass::initializeResources(const Wolf::InitializationContext& context)
{
	Timer timer("Depth pass initialization");

	m_swapChainWidth = context.swapChainWidth;
	m_swapChainHeight = context.swapChainHeight;

	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));

	DepthPassBase::initializeResources(context);

	createCopyImage(context.depthFormat);
}

void PreDepthPass::resize(const Wolf::InitializationContext& context)
{
	m_swapChainWidth = context.swapChainWidth;
	m_swapChainHeight = context.swapChainHeight;

	DepthPassBase::resize(context);

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
	context.renderMeshList->draw(context, m_commandBuffer->getCommandBuffer(context.commandBufferIdx), m_renderPass.get(), CommonPipelineIndices::PIPELINE_IDX_PRE_DEPTH, CommonCameraIndices::CAMERA_IDX_ACTIVE, 
		{});
}

VkCommandBuffer PreDepthPass::getCommandBuffer(const RecordContext& context)
{
	return m_commandBuffer->getCommandBuffer(context.commandBufferIdx);
}
