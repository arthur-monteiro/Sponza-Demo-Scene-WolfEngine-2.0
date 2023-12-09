#include "TemporalAntiAliasingPass.h"

#include <DescriptorSetGenerator.h>
#include <Image.h>

#include "CameraInterface.h"
#include "CameraList.h"
#include "CommonLayout.h"
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

	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT,                             0); // previous image
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT,                             1); // result image
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT,                            2); // uniform buffer
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 3, 1); // input depth
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT,                             4); // velocity
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));

	if (m_forwardPass->getOutputImageCount() != 2)
		Debug::sendError("TAA assumes forward pass uses 2 output images (current and previous)");

	m_uniformBuffer.reset(new Buffer(sizeof(ReprojectionUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::EACH_FRAME));

	createPipeline();

	for (uint32_t i = 0; i < m_forwardPass->getOutputImageCount(); ++i)
		m_descriptorSets[i].reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
	updateDescriptorSets();
}

void TemporalAntiAliasingPass::resize(const InitializationContext& context)
{
	updateDescriptorSets();
}

void TemporalAntiAliasingPass::record(const RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);
	const uint32_t currentImageIdx = context.currentFrameIdx % m_forwardPass->getOutputImageCount();
	ResourceNonOwner<Image> currentOutputImage = m_forwardPass->getOutputImage(currentImageIdx);
	const CameraInterface* camera = context.cameraList->getCamera(CommonCameraIndices::CAMERA_IDX_ACTIVE);

	/* Update data */
	ReprojectionUBData reprojectionUBData;
	reprojectionUBData.invView = glm::inverse(camera->getViewMatrix());
	reprojectionUBData.invProjection = glm::inverse(camera->getProjectionMatrix());
	reprojectionUBData.previousMVPMatrix = camera->getProjectionMatrix() * camera->getPreviousViewMatrix();

	const float near = camera->getNear();
	const float far = camera->getFar();
	reprojectionUBData.projectionParams.x = far / (far - near);
	reprojectionUBData.projectionParams.y = (-far * near) / (far - near);
	reprojectionUBData.screenSize = glm::uvec2(currentOutputImage->getExtent().width, currentOutputImage->getExtent().height);
	reprojectionUBData.enableTAA = gameContext->enableTAA;

	m_uniformBuffer->transferCPUMemory(&reprojectionUBData, sizeof(reprojectionUBData), 0, context.commandBufferIdx);

	/* Command buffer record */
	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	DebugMarker::beginRegion(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), DebugMarker::computePassDebugColor, "TAA Compose Compute Pass");

	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipelineLayout(), 0, 1,
		m_descriptorSets[currentImageIdx]->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline->getPipeline());

	constexpr VkExtent3D dispatchGroups = { 16, 16, 1 };
	const uint32_t groupSizeX = currentOutputImage->getExtent().width % dispatchGroups.width != 0 ? currentOutputImage->getExtent().width / dispatchGroups.width + 1 : currentOutputImage->getExtent().width / dispatchGroups.width;
	const uint32_t groupSizeY = currentOutputImage->getExtent().height % dispatchGroups.height != 0 ? currentOutputImage->getExtent().height / dispatchGroups.height + 1 : currentOutputImage->getExtent().height / dispatchGroups.height;
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

	copyRegion.extent = currentOutputImage->getExtent();

	Image::TransitionLayoutInfo transitionLayoutInfoToTransferSrc{};
	transitionLayoutInfoToTransferSrc.baseMipLevel = 0;
	transitionLayoutInfoToTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	transitionLayoutInfoToTransferSrc.dstLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	transitionLayoutInfoToTransferSrc.dstPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	transitionLayoutInfoToTransferSrc.levelCount = 1;
	currentOutputImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToTransferSrc);

	Image::TransitionLayoutInfo transitionLayoutInfoToTransferDst{};
	transitionLayoutInfoToTransferDst.baseMipLevel = 0;
	transitionLayoutInfoToTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transitionLayoutInfoToTransferDst.dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transitionLayoutInfoToTransferDst.dstPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	transitionLayoutInfoToTransferDst.levelCount = 1;
	context.swapchainImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToTransferDst);

	vkCmdCopyImage(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), currentOutputImage->getImage(), currentOutputImage->getImageLayout(0), context.swapchainImage->getImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

	Image::TransitionLayoutInfo transitionLayoutInfoToGeneral{};
	transitionLayoutInfoToGeneral.baseMipLevel = 0;
	transitionLayoutInfoToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	transitionLayoutInfoToGeneral.dstLayout = VK_IMAGE_LAYOUT_GENERAL;
	transitionLayoutInfoToGeneral.dstPipelineStageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	transitionLayoutInfoToGeneral.levelCount = 1;
	currentOutputImage->transitionImageLayout(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), transitionLayoutInfoToGeneral);

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

void TemporalAntiAliasingPass::updateDescriptorSets() const
{
	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());

	descriptorSetGenerator.setBuffer(2, *m_uniformBuffer);

	DescriptorSetGenerator::ImageDescription preDepthImageDesc;
	preDepthImageDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	preDepthImageDesc.imageView = m_preDepthPass->getCopy()->getDefaultImageView();
	descriptorSetGenerator.setImage(3, preDepthImageDesc);

	DescriptorSetGenerator::ImageDescription velocityImageDesc;
	velocityImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	velocityImageDesc.imageView = m_forwardPass->getVelocityImage()->getDefaultImageView();
	descriptorSetGenerator.setImage(4, velocityImageDesc);

	for (uint32_t i = 0; i < m_forwardPass->getOutputImageCount(); ++i)
	{
		DescriptorSetGenerator::ImageDescription previousOutputImageDesc;
		previousOutputImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		previousOutputImageDesc.imageView = m_forwardPass->getOutputImage((i + 1) % m_forwardPass->getOutputImageCount())->getDefaultImageView();
		descriptorSetGenerator.setImage(0, previousOutputImageDesc);

		DescriptorSetGenerator::ImageDescription outputImageDesc;
		outputImageDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		outputImageDesc.imageView = m_forwardPass->getOutputImage(i)->getDefaultImageView();
		descriptorSetGenerator.setImage(1, outputImageDesc);

		m_descriptorSets[i]->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
	}
}
