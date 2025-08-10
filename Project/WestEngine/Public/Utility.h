#pragma once
#include <glm/glm.hpp> 

bool CheckAABBCollision(const glm::vec2& pos1, const glm::vec2& size1,
    const glm::vec2& pos2, const glm::vec2& size2);