#include "CascadedShadowMapping.h"

#include <CameraInterface.h>
#include <DescriptorSetGenerator.h>
#include <ObjLoader.h>

#include "DepthPass.h"
#include "GameContext.h"

using namespace Wolf;

CascadeDepthPass::CascadeDepthPass(const Wolf::InitializationContext& context, const Wolf::Mesh* sponzaMesh, uint32_t width, uint32_t height, const Wolf::CommandBuffer* commandBuffer, VkDescriptorSetLayout descriptorSetLayout,
	const Wolf::ShaderParser* vertexShaderParser, const Wolf::DescriptorSetLayoutGenerator& descriptorSetLayoutGenerator) : m_width(width), m_height(height)
{
	m_sponzaMesh = sponzaMesh;
	m_commandBuffer = commandBuffer;
	m_descriptorSetLayout = descriptorSetLayout;
	m_vertexShaderParser = vertexShaderParser;

	DepthPassBase::initializeResources(context);

	createPipeline();

	m_uniformBuffer.reset(new Buffer(sizeof(UBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

	DescriptorSetGenerator descriptorSetGenerator(descriptorSetLayoutGenerator.getDescriptorLayouts());
	descriptorSetGenerator.setBuffer(0, *m_uniformBuffer.get());

	m_descriptorSet.reset(new DescriptorSet(m_descriptorSetLayout, UpdateRate::NEVER));
	m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());
}

void CascadeDepthPass::setMVP(const glm::mat4& mvp)
{
	m_mvpData.mvp = mvp;
	m_uniformBuffer->transferCPUMemory((void*)&m_mvpData, sizeof(m_mvpData), 0 /* srcOffet */);
}

void CascadeDepthPass::shaderChanged()
{
	createPipeline();
}

void CascadeDepthPass::recordDraws(const Wolf::RecordContext& context)
{
	VkCommandBuffer commandBuffer = getCommandBuffer(context);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1, m_descriptorSet->getDescriptorSet(), 0, nullptr);

	m_sponzaMesh->draw(commandBuffer);
}

VkCommandBuffer CascadeDepthPass::getCommandBuffer(const Wolf::RecordContext& context)
{
	return m_commandBuffer->getCommandBuffer(context.commandBufferIdx);
}

void CascadeDepthPass::createPipeline()
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

	// Raster
	pipelineCreateInfo.depthBiasConstantFactor = 4.0f;
	pipelineCreateInfo.depthBiasSlopeFactor = 2.5f;

	// Resources
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_descriptorSetLayout };
	pipelineCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

	// Viewport
	pipelineCreateInfo.extent = { getWidth(), getHeight() };

	// Cull mode
	pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;

	m_pipeline.reset(new Pipeline(pipelineCreateInfo));
}

CascadedShadowMapping::CascadedShadowMapping(const Wolf::Mesh* sponzaMesh)
{
	m_sponzaMesh = sponzaMesh;
}

void CascadedShadowMapping::initializeResources(const Wolf::InitializationContext& context)
{
	m_commandBuffer.reset(new CommandBuffer(QueueType::GRAPHIC, false /* isTransient */));
	m_semaphore.reset(new Semaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));

	DescriptorSetLayoutGenerator descriptorSetLayoutGenerator;
	descriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT, 0);
	m_descriptorSetLayout.reset(new DescriptorSetLayout(descriptorSetLayoutGenerator.getDescriptorLayouts()));

	m_vertexShaderParser.reset(new ShaderParser("Shaders/cascadedShadowMapping/shader.vert"));

	for (uint32_t i = 0; i < m_cascadeDepthPasses.size(); ++i)
	{
		m_cascadeDepthPasses[i].reset(new CascadeDepthPass(context, m_sponzaMesh, m_cascadeTextureSize[i], m_cascadeTextureSize[i], m_commandBuffer.get(), m_descriptorSetLayout->getDescriptorSetLayout(), m_vertexShaderParser.get(), descriptorSetLayoutGenerator));
	}

	float near = context.camera->getNear();
	float far = context.camera->getFar(); // we don't render shadows on all the range
	uint32_t cascadeIdx = 0;
	for (float i(1.0f / CASCADE_COUNT); i <= 1.0f; i += 1.0f / CASCADE_COUNT)
	{
		float d_uni = glm::mix(near, far, i);
		float d_log = near * glm::pow((far / near), i);

		m_cascadeSplits[cascadeIdx] = (glm::mix(d_uni, d_log, 0.5f));
		cascadeIdx++;
	}
}

void CascadedShadowMapping::resize(const Wolf::InitializationContext& context)
{
	// Nothing to do
}

void CascadedShadowMapping::record(const Wolf::RecordContext& context)
{
	const GameContext* gameContext = (const GameContext*)context.gameContext;

	/* Update */
	float lastSplitDist = context.camera->getNear();
	for (int cascade(0); cascade < CASCADE_COUNT; ++cascade)
	{
		const float startCascade = lastSplitDist;
		const float endCascade = m_cascadeSplits[cascade];

		float radius = (endCascade - startCascade) / 2.0f;

		const float ar = context.swapchainImage[0].getExtent().height / static_cast<float>(context.swapchainImage[0].getExtent().width);
		const float cosHalfHFOV = static_cast<float>(glm::cos((context.camera->getFOV() * (1.0f / ar)) / 2.0f));
		const float b = endCascade / cosHalfHFOV;
		radius = glm::sqrt(b * b + (startCascade + radius) * (startCascade + radius) - 2.0f * b * startCascade * cosHalfHFOV) * 0.75f;

		const float texelPerUnit = static_cast<float>(m_cascadeTextureSize[cascade]) / (radius * 2.0f);
		glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(texelPerUnit));
		glm::mat4 lookAt = scaleMat * glm::lookAt(glm::vec3(0.0f), -gameContext->sunDirection, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 lookAtInv = glm::inverse(lookAt);

		glm::vec3 frustumCenter = context.camera->getPosition() + (context.camera->getOrientation() * startCascade + context.camera->getOrientation() * endCascade) / 2.0f;
		frustumCenter = lookAt * glm::vec4(frustumCenter, 1.0f);
		frustumCenter.x = static_cast<float>(floor(frustumCenter.x));
		frustumCenter.y = static_cast<float>(floor(frustumCenter.y));
		frustumCenter = lookAtInv * glm::vec4(frustumCenter, 1.0f);

		glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - 50.0f * glm::normalize(gameContext->sunDirection), frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));

		glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, -30.0f * 6.0f, 30.0f * 6.0f);
		m_cascadeDepthPasses[cascade]->setMVP(proj * lightViewMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(0.01f)));

		lastSplitDist += m_cascadeSplits[cascade];
	}

	/* Command buffer record */
	m_commandBuffer->beginCommandBuffer(context.commandBufferIdx);

	for (uint32_t i = 0; i < m_cascadeDepthPasses.size(); ++i)
	{
		m_cascadeDepthPasses[i]->record(context);
	}

	m_commandBuffer->endCommandBuffer(context.commandBufferIdx);
}

void CascadedShadowMapping::submit(const Wolf::SubmitContext& context)
{
	std::vector<const Semaphore*> waitSemaphores{ };
	std::vector<VkSemaphore> signalSemaphores{ m_semaphore->getSemaphore() };
	m_commandBuffer->submit(context.commandBufferIdx, waitSemaphores, signalSemaphores, VK_NULL_HANDLE);

	bool anyShaderModified = m_vertexShaderParser->compileIfFileHasBeenModified();

	if (anyShaderModified)
	{
		vkDeviceWaitIdle(context.device);
		for (auto& cascade : m_cascadeDepthPasses)
			cascade->shaderChanged();
	}
}