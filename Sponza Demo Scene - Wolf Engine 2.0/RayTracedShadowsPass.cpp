#include "RayTracedShadowsPass.h"

#include <CameraInterface.h>
#include <CommandBuffer.h>
#include <DescriptorSetGenerator.h>
#include <DescriptorSetLayoutGenerator.h>
#include <RayTracingShaderGroupGenerator.h>

#include "DepthPass.h"
#include "GameContext.h"
#include "SponzaModel.h"

using namespace Wolf;

RayTracedShadowsPass::RayTracedShadowsPass(const SponzaModel* sponzaModel, DepthPass* preDepthPass)
{
	m_sponzaModel = sponzaModel;
	m_preDepthPass = preDepthPass;
}

void RayTracedShadowsPass::initializeResources(const InitializationContext& context)
{
	m_commandBuffer.reset(new CommandBuffer(QueueType::RAY_TRACING, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR));

	m_rayGenShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rgen"));
	m_rayMissShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rmiss"));
	m_closestHitShaderParser.reset(new ShaderParser("Shaders/rayTracedShadows/shader.rchit"));

	m_descriptorSetLayoutGenerator.addAccelerationStructure(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0); // TLAS
	m_descriptorSetLayoutGenerator.addStorageImage(VK_SHADER_STAGE_RAYGEN_BIT_KHR,                                                1); // output image
	m_descriptorSetLayoutGenerator.addImages(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR,                    2, 1); // input depth
	m_descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_RAYGEN_BIT_KHR,											      3); // uniform buffer
	m_descriptorSetLayout.reset(new DescriptorSetLayout(m_descriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_uniformBuffer.reset(new Buffer(sizeof(ShadowUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::EACH_FRAME));

	createPipeline();
	createOutputImage(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSet();
}

void RayTracedShadowsPass::resize(const InitializationContext& context)
{
	createOutputImage(context.swapChainWidth, context.swapChainHeight);
	createDescriptorSet();
}

void RayTracedShadowsPass::record(const RecordContext& context)
{
	const GameContext* gameContext = static_cast<const GameContext*>(context.gameContext);

	/* Update data */
	ShadowUBData shadowUBData;
	shadowUBData.invModelView = glm::inverse(context.camera->getViewMatrix());
	shadowUBData.invProjection = glm::inverse(context.camera->getProjection());
	const float near = context.camera->getNear();
	const float far = context.camera->getFar();
	shadowUBData.projectionParams.x = far / (far - near);
	shadowUBData.projectionParams.y = (-far * near) / (far - near);
	shadowUBData.sunDirection = glm::vec4(-gameContext->sunDirection, 1.0f);
	m_uniformBuffer->transferCPUMemory((void*)&shadowUBData, sizeof(shadowUBData), 0 /* srcOffet */, context.commandBufferIdx);

	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	vkCmdBindPipeline(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipeline());
	vkCmdBindDescriptorSets(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(context.commandBufferIdx), 0, nullptr);

	VkStridedDeviceAddressRegionKHR rgenRegion{};
	rgenRegion.deviceAddress = m_shaderBindingTable->getBuffer().getBufferDeviceAddress();
	rgenRegion.stride = m_shaderBindingTable->getBaseAlignment();
	rgenRegion.size = m_shaderBindingTable->getBaseAlignment();

	VkStridedDeviceAddressRegionKHR rmissRegion{};
	rmissRegion.deviceAddress = m_shaderBindingTable->getBuffer().getBufferDeviceAddress() + rgenRegion.size;
	rmissRegion.stride = m_shaderBindingTable->getBaseAlignment();
	rmissRegion.size = m_shaderBindingTable->getBaseAlignment();

	VkStridedDeviceAddressRegionKHR rhitRegion{};
	rhitRegion.deviceAddress = m_shaderBindingTable->getBuffer().getBufferDeviceAddress() + rgenRegion.size + rmissRegion.size;
	rhitRegion.stride = m_shaderBindingTable->getBaseAlignment();
	rhitRegion.size = m_shaderBindingTable->getBaseAlignment();

	constexpr VkStridedDeviceAddressRegionKHR callRegion{};

	vkCmdTraceRaysKHR(m_commandBuffer->getCommandBuffer(context.commandBufferIdx), &rgenRegion,
		&rmissRegion,
		&rhitRegion,
		&callRegion, m_outputMask->getExtent().width, m_outputMask->getExtent().height, 1);

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void RayTracedShadowsPass::submit(const SubmitContext& context)
{
	const std::vector waitSemaphores{ m_preDepthPass->getSemaphore() };
	const std::vector signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	bool anyShaderModified = m_rayGenShaderParser->compileIfFileHasBeenModified();
	if (m_rayMissShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;
	if (m_closestHitShaderParser->compileIfFileHasBeenModified())
		anyShaderModified = true;

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		createPipeline();
	}
}

void RayTracedShadowsPass::createPipeline()
{
	RayTracingShaderGroupGenerator shaderGroupGenerator;
	shaderGroupGenerator.addRayGenShaderStage(0);
	shaderGroupGenerator.addMissShaderStage(1);
	HitGroup hitGroup;
	hitGroup.closestHitShaderIdx = 2;
	shaderGroupGenerator.addHitGroup(hitGroup);

	RayTracingPipelineCreateInfo pipelineCreateInfo;

	std::vector<char> rayGenShaderCode;
	m_rayGenShaderParser->readCompiledShader(rayGenShaderCode);
	std::vector<char> rayMissShaderCode;
	m_rayMissShaderParser->readCompiledShader(rayMissShaderCode);
	std::vector<char> closestHitShaderCode;
	m_closestHitShaderParser->readCompiledShader(closestHitShaderCode);

	std::vector<ShaderCreateInfo> shaders(3);
	shaders[0].shaderCode = rayGenShaderCode;
	shaders[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	shaders[1].shaderCode = rayMissShaderCode;
	shaders[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	shaders[2].shaderCode = closestHitShaderCode;
	shaders[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	pipelineCreateInfo.shaderCreateInfos = shaders;

	pipelineCreateInfo.shaderGroupsCreateInfos = shaderGroupGenerator.getShaderGroups();

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout->getDescriptorSetLayout() };
	m_pipeline.reset(new Pipeline(pipelineCreateInfo, descriptorSetLayouts));

	m_shaderBindingTable.reset(new ShaderBindingTable(static_cast<uint32_t>(shaders.size()), m_pipeline->getPipeline()));
}

void RayTracedShadowsPass::createDescriptorSet()
{
	DescriptorSetGenerator::ImageDescription outputImageDesc(VK_IMAGE_LAYOUT_GENERAL, m_outputMask->getDefaultImageView());
	DescriptorSetGenerator::ImageDescription preDepthImageDesc{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_preDepthPass->getOutput()->getDefaultImageView() };

	DescriptorSetGenerator descriptorSetGenerator(m_descriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setAccelerationStructure(0, m_sponzaModel->getTLAS());
	descriptorSetGenerator.setImage(1, outputImageDesc);
	descriptorSetGenerator.setImage(2, preDepthImageDesc);
	descriptorSetGenerator.setBuffer(3, *m_uniformBuffer);

	if (!m_descriptorSet)
		m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout->getDescriptorSetLayout(), UpdateRate::EACH_FRAME));
	m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
}

void RayTracedShadowsPass::createOutputImage(uint32_t width, uint32_t height)
{
	CreateImageInfo createImageInfo;
	createImageInfo.extent = { width, height, 1 };
	createImageInfo.format = VK_FORMAT_R32G32_SFLOAT;
	createImageInfo.mipLevelCount = 1;
	createImageInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	createImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	m_outputMask.reset(new Image(createImageInfo));
	m_outputMask->setImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
}
