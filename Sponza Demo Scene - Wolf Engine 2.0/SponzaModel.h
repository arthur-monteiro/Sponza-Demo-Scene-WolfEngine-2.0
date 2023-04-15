#pragma once

#include <BottomLevelAccelerationStructure.h>
#include <Image.h>
#include <ObjLoader.h>
#include <TopLevelAccelerationStructure.h>

class SponzaModel
{
public:
	SponzaModel(std::mutex* vulkanQueueLock, const glm::mat4& transform, bool buildAccelerationStructures);

	const Wolf::Mesh* getMesh() const { return m_sponzaLoader->getMesh(); }
	const void getImages(std::vector<Wolf::Image*>& images) { m_sponzaLoader->getImages(images); }
	const glm::mat4& getTransform() const { return m_transform; }
	const Wolf::TopLevelAccelerationStructure& getTLAS() const { return *m_tlas.get(); }

private:
	void buildAccelerationStructures();

private:
	std::unique_ptr<Wolf::ObjLoader> m_sponzaLoader;
	glm::mat4 m_transform;

	std::unique_ptr<Wolf::BottomLevelAccelerationStructure> m_blas;
	std::unique_ptr<Wolf::TopLevelAccelerationStructure> m_tlas;
};