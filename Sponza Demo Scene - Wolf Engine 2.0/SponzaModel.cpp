#include "SponzaModel.h"

#include <Timer.h>

using namespace Wolf;

SponzaModel::SponzaModel(std::mutex* vulkanQueueLock)
{
	Timer timer("Sponza loading");

	ModelLoadingInfo sponzaLoadingInfo;
	sponzaLoadingInfo.filename = "Models/sponza/sponza.obj";
	sponzaLoadingInfo.mtlFolder = "Models/sponza";
	sponzaLoadingInfo.vulkanQueueLock = vulkanQueueLock;
	m_sponzaLoader.reset(new ObjLoader(sponzaLoadingInfo));
}
