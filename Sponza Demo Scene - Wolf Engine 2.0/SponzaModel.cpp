#include "SponzaModel.h"

#include <Timer.h>

using namespace Wolf;

SponzaModel::SponzaModel(std::mutex* vulkanQueueLock, const glm::mat4& transform, bool buildAccelerationStructuresForRayTracing)
{
	Timer timer("Sponza loading");

	ModelLoadingInfo sponzaLoadingInfo;
	sponzaLoadingInfo.filename = "Models/sponza/sponza.obj";
	sponzaLoadingInfo.mtlFolder = "Models/sponza";
	sponzaLoadingInfo.vulkanQueueLock = vulkanQueueLock;
	if (buildAccelerationStructuresForRayTracing)
	{
		VkBufferUsageFlags rayTracingFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		sponzaLoadingInfo.additionalVertexBufferUsages = rayTracingFlags;
		sponzaLoadingInfo.additionalIndexBufferUsages = rayTracingFlags;
	}
	m_sponzaLoader.reset(new ObjLoader(sponzaLoadingInfo));

	m_transform = transform;

	if (buildAccelerationStructuresForRayTracing)
	{
		buildAccelerationStructures();
	}
}

void SponzaModel::buildAccelerationStructures()
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