#include <WolfEngine.h>

#include "SystemManager.h"

int main()
{
	const std::unique_ptr<SystemManager> s(new SystemManager);
	s->run();

	return 0;
}