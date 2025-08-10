#pragma once

#include <glm/glm.hpp>

struct AttackHitbox
{
	glm::vec2 pos;
	glm::vec2 size;
	int damage;
	float angle = 0.f;
};