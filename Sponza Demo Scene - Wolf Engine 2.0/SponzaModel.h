#pragma once

#include <Image.h>
#include <ObjLoader.h>

class SponzaModel
{
public:
	SponzaModel(std::mutex* vulkanQueueLock);

	const Wolf::Mesh* getMesh() { return m_sponzaLoader->getMesh(); }
	const void getImages(std::vector<Wolf::Image*>& images) { m_sponzaLoader->getImages(images); }

private:
	std::unique_ptr<Wolf::ObjLoader> m_sponzaLoader;
};

