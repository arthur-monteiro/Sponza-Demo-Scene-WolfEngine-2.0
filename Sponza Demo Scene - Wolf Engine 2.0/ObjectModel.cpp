#include "ObjectModel.h"

#include <BindlessDescriptor.h>
#include <CameraInterface.h>
#include <RenderMeshList.h>
#include <Timer.h>

#include "CommonLayout.h"
#include "GameContext.h"

using namespace Wolf;

ObjectModel::ObjectModel(std::mutex* vulkanQueueLock, const glm::mat4& transform, bool buildAccelerationStructuresForRayTracing, const std::string& filename, const std::string& mtlFolder, bool loadMaterials, 
	uint32_t materialIdOffset, const BindlessDescriptor* bindlessDescriptor)
{
	Timer timer(filename + " loading");

	m_modelDescriptorSetLayoutGenerator.reset(new LazyInitSharedResource<DescriptorSetLayoutGenerator, ObjectModel>([this](std::unique_ptr<DescriptorSetLayoutGenerator>& descriptorSetLayoutGenerator)
		{
			descriptorSetLayoutGenerator.reset(new DescriptorSetLayoutGenerator);
			descriptorSetLayoutGenerator->addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0); // matrices
		}));

	m_modelDescriptorSetLayout.reset(new LazyInitSharedResource<DescriptorSetLayout, ObjectModel>([this](std::unique_ptr<DescriptorSetLayout>& descriptorSetLayout)
		{
			descriptorSetLayout.reset(new DescriptorSetLayout(m_modelDescriptorSetLayoutGenerator->getResource()->getDescriptorLayouts()));
		}));

	m_matricesUniformBuffer.reset(new Buffer(sizeof(MatricesUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));
	m_descriptorSet.reset(new DescriptorSet(m_modelDescriptorSetLayout->getResource()->getDescriptorSetLayout(), UpdateRate::NEVER));
	DescriptorSetGenerator descriptorSetGenerator(m_modelDescriptorSetLayoutGenerator->getResource()->getDescriptorLayouts());
	descriptorSetGenerator.setBuffer(0, *m_matricesUniformBuffer);
	m_descriptorSet->update(descriptorSetGenerator.getDescriptorSetCreateInfo());

	ModelLoadingInfo modelLoadingInfo;
	modelLoadingInfo.filename = filename;
	modelLoadingInfo.mtlFolder = mtlFolder;
	modelLoadingInfo.vulkanQueueLock = vulkanQueueLock;
	modelLoadingInfo.loadMaterials = loadMaterials;
	modelLoadingInfo.materialIdOffset = materialIdOffset;
	if (buildAccelerationStructuresForRayTracing)
	{
		VkBufferUsageFlags rayTracingFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		modelLoadingInfo.additionalVertexBufferUsages = rayTracingFlags;
		modelLoadingInfo.additionalIndexBufferUsages = rayTracingFlags;
	}
	ObjLoader::loadObject(m_modelData, modelLoadingInfo);

	m_transform = transform;

	if (buildAccelerationStructuresForRayTracing)
	{
		vulkanQueueLock->lock();
		buildAccelerationStructures();
		vulkanQueueLock->unlock();
	}
}

void ObjectModel::addMeshesToRenderList(RenderMeshList& renderMeshList) const
{
	RenderMeshList::MeshToRenderInfo meshToRenderInfo(m_modelData.mesh.get(), m_pipelineSet);
	meshToRenderInfo.descriptorSets.push_back({ m_descriptorSet.get(), 0 });
	renderMeshList.addMeshToRender(meshToRenderInfo);
}

void ObjectModel::updateGraphic(const Wolf::CameraInterface& camera, const GameContext& gameContext) const
{
	MatricesUBData mvp;
	mvp.model = m_transform;

	m_matricesUniformBuffer->transferCPUMemory(&mvp, sizeof(mvp), 0);
}

void ObjectModel::buildAccelerationStructures()
{
	BottomLevelAccelerationStructureCreateInfo blasCreateInfo;
	blasCreateInfo.buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	std::vector<GeometryInfo> geometries(1);
	geometries[0].mesh = getMesh();
	blasCreateInfo.geometryInfos = geometries;
	m_blas.reset(new BottomLevelAccelerationStructure(blasCreateInfo));

	BLASInstance blasInstance;
	blasInstance.bottomLevelAS = m_blas.get();
	blasInstance.hitGroupIndex = 0;
	blasInstance.transform = m_transform;
	blasInstance.instanceID = 0;
	std::vector<BLASInstance> blasInstances = { blasInstance };
	m_tlas.reset(new TopLevelAccelerationStructure(blasInstances));
}