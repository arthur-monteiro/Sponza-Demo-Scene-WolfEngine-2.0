#pragma once

#include <glm/glm.hpp>

struct GameContext
{
	glm::vec3 sunDirection;
	glm::vec3 sunColor;

	GameContext(const glm::vec3& defaultSunDirection, const glm::vec3& defaultSunColor)
	{
		sunDirection = defaultSunDirection;
		sunColor = defaultSunColor;
	}
};