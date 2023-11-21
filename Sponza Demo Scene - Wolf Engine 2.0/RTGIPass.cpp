#include "RTGIPass.h"

#include <Attachment.h>
#include <DescriptorSetGenerator.h>
#include <glm/gtx/transform.hpp>

#include "CommonLayout.h"
#include "PreDepthPass.h"
#include "RenderMeshList.h"

using namespace Wolf;

void RTGIPass::addDebugMeshesToRenderList(RenderMeshList& renderMeshList) const
{
	m_sphereModel->addMeshesToRenderList(renderMeshList, { m_sphereInstanceBuffer.get(), m_sphereInstanceCount });
}

void RTGIPass::updateGraphic() const
{
	m_sphereModel->updateGraphic();
}

void RTGIPass::initializeResources(const InitializationContext& context)
{
	// Debug
	{
		ModelLoadingInfo modelLoadingInfo;
		modelLoadingInfo.filename = "Models/sphere.obj";
		modelLoadingInfo.mtlFolder = "Models/";
		modelLoadingInfo.vulkanQueueLock = m_vulkanQueueLock;
		modelLoadingInfo.loadMaterials = false;
		modelLoadingInfo.materialIdOffset = 1;
		m_sphereModel.reset(new ModelBase(modelLoadingInfo, false, nullptr));
		m_sphereModel->setTransform(glm::scale(glm::vec3(0.005f)));

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
		m_sphereInstanceBuffer->transferCPUMemoryWithStagingBuffer(spheres.data(), spheres.size() * sizeof(SphereInstanceData));
		m_sphereInstanceCount = static_cast<uint32_t>(spheres.size());

		initializeDebugPipelineSet();
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

void RTGIPass::initializeDebugPipelineSet()
{
	m_debugPipelineSet.reset(new PipelineSet);
	PipelineSet::PipelineInfo pipelineInfo;

	/* PreDepth */
	pipelineInfo.shaderInfos.resize(1);
	pipelineInfo.shaderInfos[0].shaderFilename = "Shaders/rayTracedGlobalIllumination/debug.vert";
	pipelineInfo.shaderInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;

	// IA
	Vertex3D::getAttributeDescriptions(pipelineInfo.vertexInputAttributeDescriptions, 0);
	SphereInstanceData::getAttributeDescriptions(pipelineInfo.vertexInputAttributeDescriptions, 1, static_cast<uint32_t>(pipelineInfo.vertexInputAttributeDescriptions.size()));

	pipelineInfo.vertexInputBindingDescriptions.resize(2);
	Vertex3D::getBindingDescription(pipelineInfo.vertexInputBindingDescriptions[0], 0);
	SphereInstanceData::getBindingDescription(pipelineInfo.vertexInputBindingDescriptions[1], 1);

	// Resources
	pipelineInfo.descriptorSetLayouts = { m_sphereModel->getDescriptorSetLayout() };
	pipelineInfo.cameraDescriptorSlot = 1;
	pipelineInfo.bindlessDescriptorSlot = 2;

	// Color Blend
	pipelineInfo.blendModes = { RenderingPipelineCreateInfo::BLEND_MODE::OPAQUE };

	pipelineInfo.cullMode = VK_CULL_MODE_NONE;

	m_debugPipelineSet->addPipeline(pipelineInfo, CommonPipelineIndices::PIPELINE_IDX_PRE_DEPTH);

	/* Shadow maps */
	m_debugPipelineSet->addEmptyPipeline(CommonPipelineIndices::PIPELINE_IDX_SHADOW_MAP);

	/* Forward */
	pipelineInfo.shaderInfos.resize(2);
	pipelineInfo.shaderInfos[1].shaderFilename = "Shaders/rayTracedGlobalIllumination/debug.frag";
	pipelineInfo.shaderInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

	pipelineInfo.descriptorSetLayouts = { m_sphereModel->getDescriptorSetLayout(), CommonDescriptorLayouts::g_commonForwardDescriptorSetLayout };

	m_debugPipelineSet->addPipeline(pipelineInfo, CommonPipelineIndices::PIPELINE_IDX_FORWARD);

	m_sphereModel->setPipelineSet(m_debugPipelineSet.get());
}
