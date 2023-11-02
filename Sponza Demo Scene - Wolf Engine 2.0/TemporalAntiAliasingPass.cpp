#include "TemporalAntiAliasingPass.h"

#include <DescriptorSetGenerator.h>
#include <Image.h>

#include "CameraInterface.h"
#include "DebugMarker.h"
#include "PreDepthPass.h"
#include "ForwardPass.h"
#include "GameContext.h"

using namespace Wolf;

void TemporalAntiAliasingPass::initializeResources(const InitializationContext& context)
{
	m_commandBuffer.reset(new CommandBuffer(QueueType::COMPUTE, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));

	m_computeShaderParser.reset(new ShaderParser("Shaders/TAA/shader.comp"));

	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT, 0); // previous image
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT, 1); // result image
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT, 2); // uniform buffer
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 3, 1); // input depth
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_uniformBuffer.reset(new Buffer(sizeof(ReprojectionUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::EACH_FRAME));

	createPipeline();
	createOutputImages(context.swapChainWidth, context.swapChainHeight);

	for (uint32_t i = 0; i < m_outputImages.size(); ++i)
		m_descriptorSets[i].reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
	updateDescriptorSets();
}

void TemporalAntiAliasingPass::resize(const InitializationContext& context)
{
}

void TemporalAntiAliasingPass::record(const RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);
	const uint32_t currentImageIdx = context.currentFrameIdx % m_outputImages.size();

	/* Update data */
	ReprojectionUBData reprojectionUBData;
	reprojectionUBData.invView = glm::inverse(context.camera->getViewMatrix());
	reprojectionUBData.invProjection = glm::inverse(context.camera->getProjectionMatrix());
	reprojectionUBData.previousMVPMatrix = context.camera->getProjectionMatrix() * context.camera->getPreviousViewMatrix();

	const float near = context.camera->getNear();
	const float far = context.camera->getFar();
	reprojectionUBData.projectionParams.x = far / (far - near);
	reprojectionUBData.projectionParams.y = (-far * near) / (far - near);
	reprojectionUBData.screenSize = glm::uvec2(m_outputImages[currentImageIdx]->getExtent().width, m_outputImages[currentImageIdx]->getExtent().height);
	reprojectionUBData.enableTAA = gameContext->pixelJitter != glm::vec2(0.0f, 0.0f);

	m_uniformBuffer->transferCPUMemory(&reprojectionUBData, sizeof(reprojectionUBData), 0, context.commandBufferIdx);

	/* Command buffer record */
	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::computePassDebugColor, "TAA Compose Compute Pass");

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipelineLayout(), 0, 1,
		m_descriptorSets[currentImageIdx]->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipeline());

	constexpr VkExtent3D dispatchGroups = { 16, 16, 1 };
	const uint32_t groupSizeX = m_outputImages[currentImageIdx]->getExtent().width % dispatchGroups.width != 0 ? m_outputImages[currentImageIdx]->getExtent().width / dispatchGroups.width + 1 : m_outputImages[currentImageIdx]->getExtent().width / dispatchGroups.width;
	const uint32_t groupSizeY = m_outputImages[currentImageIdx]->getExtent().height % dispatchGroups.height != 0 ? m_outputImages[currentImageIdx]->getExtent().height / dispatchGroups.height + 1 : m_outputImages[currentImageIdx]->getExtent().height / dispatchGroups.height;
	vkCmdDispatch(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), groupSizeX, groupSizeY, dispatchGroups.depth);

	DebugMarker::endRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx));

	VkImageCopy copyRegion{};

	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.srcSubresource.baseArrayLayer = 0;
	copyRegion.srcSubresource.mipLevel = 0;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.srcOffset = { 0, 0, 0 };

	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.dstSubresource.baseArrayLayer = 0;
	copyRegion.dstSubresource.mipLevel = 0;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.dstOffset = { 0, 0, 0 };

	copyRegion.extent = m_outputImages[currentImageIdx]->getExtent();

	Image::TransitionLayoutInfo transitionLayoutInfoToTransferSrc{};
	transitionLayoutInfoToTransferSrc.baseMipLevel = 0;
	transitionLayoutInfoToTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	transitionLayoutInfoToTransferSrc.dstLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	transitionLayoutInfoToTransferSrc.dstPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	transitionLayoutInfoToTransferSrc.levelCount = 1;
	m_outputImages[currentImageIdx]->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToTransferSrc);

	Image::TransitionLayoutInfo transitionLayoutInfoToTransferDst{};
	transitionLayoutInfoToTransferDst.baseMipLevel = 0;
	transitionLayoutInfoToTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transitionLayoutInfoToTransferDst.dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transitionLayoutInfoToTransferDst.dstPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	transitionLayoutInfoToTransferDst.levelCount = 1;
	context.swapchainImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToTransferDst);

	vkCmdCopyImage(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), m_outputImages[currentImageIdx]->getImage(), m_outputImages[currentImageIdx]->getImageLayout(0), context.swapchainImage->getImage(), 
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

	Image::TransitionLayoutInfo transitionLayoutInfoToGeneral{};
	transitionLayoutInfoToGeneral.baseMipLevel = 0;
	transitionLayoutInfoToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	transitionLayoutInfoToGeneral.dstLayout = VK_IMAGE_LAYOUT_GENERAL;
	transitionLayoutInfoToGeneral.dstPipelineStageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	transitionLayoutInfoToGeneral.levelCount = 1;
	m_outputImages[currentImageIdx]->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToGeneral);

	Image::TransitionLayoutInfo transitionLayoutInfoToPresent{};
	transitionLayoutInfoToPresent.baseMipLevel = 0;
	transitionLayoutInfoToPresent.dstAccessMask = 0;
	transitionLayoutInfoToPresent.dstLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	transitionLayoutInfoToPresent.dstPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	transitionLayoutInfoToPresent.levelCount = 1;
	context.swapchainImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToPresent);

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void TemporalAntiAliasingPass::submit(const SubmitContext& context)
{
	const std::vector waitSemaphores{ context.swapChainImageAvailableSemaphore, m_forwardPass->getSemaphore() };
	const std::vector signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, context.frameFence);
	
	if (m_computeShaderParser->compileIfFileHasBeenModified())
	{
		vkDeviceWaitIdle(context.device);
		createPipeline();
	}
}

std::vector<Image*> TemporalAntiAliasingPass::getImages() const
{
	std::vector<Image*> r(m_outputImages.size());
	for(uint32_t i = 0; i < m_outputImages.size(); ++i)
		r[i] = m_outputImages[i].get();

	return r;
}

void TemporalAntiAliasingPass::createPipeline()
{
	// Compute shader parser
	std::vector<char> computeShaderCode;
	m_computeShaderParser->readCompiledShader(computeShaderCode);

	ShaderCreateInfo computeShaderCreateInfo;
	computeShaderCreateInfo.shaderCode = computeShaderCode;
	computeShaderCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts(1);
	descriptorSetLayouts[0] = m_descriptorSetLayout->getDescriptorSetLayout();
	m_pipeline.reset(new Pipeline(computeShaderCreateInfo, descriptorSetLayouts));
}

void TemporalAntiAliasingPass::createOutputImages(uint32_t width, uint32_t height)
{
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { width, height, 1 };
	createImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	createImageInfo.mipLevelCount = 1;
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	for (std::unique_ptr<Image>& m_outputImage : m_outputImages)
	{
		m_outputImage.reset(new Image(createImageInfo));
		m_outputImage->setImageLayout({ VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });
	}
}

void TemporalAntiAliasingPass::updateDescriptorSets() const
{
	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());

	descriptorSetGenerator.setBuffer(2, *m_uniformBuffer);

	DescriptorSetGenerator::ImageDescription preDepthImageDesc;
	preDepthImageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	preDepthImageDesc.imageView = m_preDepthPass->getCopy()->getDefaultImageView();
	descriptorSetGenerator.setImage(3, preDepthImageDesc);

	for (uint32_t i = 0; i < m_outputImages.size(); ++i)
	{
		DescriptorSetGenerator::ImageDescription previousOutputImageDesc;
		previousOutputImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		previousOutputImageDesc.imageView = m_outputImages[(i + 1) % m_outputImages.size()]->getDefaultImageView();
		descriptorSetGenerator.setImage(0, previousOutputImageDesc);

		DescriptorSetGenerator::ImageDescription outputImageDesc;
		outputImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputImageDesc.imageView = m_outputImages[i]->getDefaultImageView();
		descriptorSetGenerator.setImage(1, outputImageDesc);

		m_descriptorSets[i]->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}
}
