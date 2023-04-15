#pragma once

#include <stdint.h>

namespace Wolf
{
	class Image;
	class Semaphore;
}

class ShadowMaskBasePass
{
public:
	static constexpr uint32_t MASK_COUNT = 2;

	virtual Wolf::Image* getOutput(uint32_t frameIdx) = 0;
	virtual const Wolf::Semaphore* getSemaphore() const = 0;
};

