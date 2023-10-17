#pragma once

#include <BottomLevelAccelerationStructure.h>
#include <Image.h>
#include <ObjLoader.h>
#include <TopLevelAccelerationStructure.h>

class ObjectModel
{
public:
	ObjectModel(std::mutex* vulkanQueueLock, const glm::mat4& transform, bool buildAccelerationStructures, const std::string& filename, const std::string& mtlFolder, bool loadMaterials, uint32_t materialIdOffset);

	const Wolf::Mesh* getMesh() const { return m_modelData.mesh.get(); }
	void getImages(std::vector<Wolf::Image*>& images) const { m_modelData.getImages(images); }
	const glm::mat4& getTransform() const { return m_transform; }
	const Wolf::TopLevelAccelerationStructure& getTLAS() const { return *m_tlas; }

	void setPosition(glm::vec3 position) { m_transform[3] = glm::vec4(position, 1.0f); }

private:
	void buildAccelerationStructures();

private:
	Wolf::ModelData m_modelData;
	glm::mat4 m_transform;

	std::unique_ptr<Wolf::BottomLevelAccelerationStructure> m_blas;
	std::unique_ptr<Wolf::TopLevelAccelerationStructure> m_tlas;
};