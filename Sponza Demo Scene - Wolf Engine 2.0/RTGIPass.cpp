#include "RTGIPass.h"

#include <Attachment.h>
#include <DescriptorSetGenerator.h>

#include "PreDepthPass.h"

using namespace Wolf;

void RTGIPass::initializeResources(const InitializationContext& context)
{
	// Debug
	{
		m_sphereModel.reset(new ObjectModel(m_vulkanQueueLock, glm::mat4(1.0f), false, "Models/sphere.obj", "", false, 0, nullptr));

		std::vector<SphereInstanceData> spheres;
		spheres.reserve(static_cast<size_t>(PROBE_COUNT.x * PROBE_COUNT.y * PROBE_COUNT.z));
		for (uint32_t xIdx = 0; xIdx < PROBE_COUNT.x; ++xIdx)
		{
			for (uint32_t yIdx = 0; yIdx < PROBE_COUNT.y; ++yIdx)
			{
				for (uint32_t zIdx = 0; zIdx < PROBE_COUNT.z; ++zIdx)
				{
					const glm::vec3 probePos = FIRST_PROBE_POS + SPACE_BETWEEN_PROBES * glm::vec3(xIdx, yIdx, zIdx);
					spheres.push_back({ probePos });
				}
			}
		}

		m_sphereInstanceBuffer.reset(new Buffer(spheres.size() * sizeof(SphereInstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, UpdateRate::NEVER));

		
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
		Vertex3D::getAttributeDescriptions(attributeDescriptions, 0);
		SphereInstanceData::getAttributeDescriptions(attributeDescriptions, 1, attributeDescriptions.size());

		std::vector<VkVertexInputBindingDescription> bindingDescriptions(2);
		Vertex3D::getBindingDescription(bindingDescriptions[0], 0);
		SphereInstanceData::getBindingDescription(bindingDescriptions[1], 1);

		// Shaders
		//pipelineHandlerCreateInfo.vertexShader = "Shaders/rayTracedGlobalIllumination/debug.vert";
		//pipelineHandlerCreateInfo.fragmentShader = "Shaders/rayTracedGlobalIllumination/debug.frag";

		//// Resources
		//m_debugUniformBuffer.reset(new Buffer(sizeof(DebugUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));

		//m_debugDescriptorSetLayoutGenerator.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT, 0);
		//m_debugDescriptorSetLayout.reset(new DescriptorSetLayout(m_debugDescriptorSetLayoutGenerator.getDescriptorLayouts()));

		//DescriptorSetGenerator descriptorSetGenerator(m_debugDescriptorSetLayoutGenerator.getDescriptorLayouts());
		//descriptorSetGenerator.setBuffer(0, *m_debugUniformBuffer);

		//m_debugDescriptorSet.reset(new DescriptorSet(m_debugDescriptorSetLayout->getDescriptorSetLayout(), UpdateRate::NEVER));
		//m_debugDescriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());

		//std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { m_debugDescriptorSetLayout->getDescriptorSetLayout() };
		//pipelineHandlerCreateInfo.descriptorSetLayouts = descriptorSetLayouts;

		//m_pipelineHandler.reset(new SinglePipelineRenderingPassHandler(pipelineHandlerCreateInfo));
	}
}

void RTGIPass::resize(const InitializationContext& context)
{

}

void RTGIPass::record(const RecordContext& context)
{
}

void RTGIPass::submit(const SubmitContext& context)
{
}
