#include "ObjectModel.h"

#include <Timer.h>

using namespace Wolf;

ObjectModel::ObjectModel(std::mutex* vulkanQueueLock, const glm::mat4& transform, bool buildAccelerationStructuresForRayTracing, const std::string& filename, const std::string& mtlFolder, bool loadMaterials, uint32_t materialIdOffset)
{
	Timer timer(filename + " loading");

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
