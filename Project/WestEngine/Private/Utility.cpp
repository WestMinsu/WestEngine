#include "Utility.h"

bool CheckAABBCollision(const glm::vec2& pos1, const glm::vec2& size1, const glm::vec2& pos2, const glm::vec2& size2)
{
	float left1 = pos1.x - size1.x / 2.0f;
	float right1 = pos1.x + size1.x / 2.0f;
	float top1 = pos1.y + size1.y / 2.0f;
	float bottom1 = pos1.y - size1.y / 2.0f;

	float left2 = pos2.x - size2.x / 2.0f;
	float right2 = pos2.x + size2.x / 2.0f;
	float top2 = pos2.y + size2.y / 2.0f;
	float bottom2 = pos2.y - size2.y / 2.0f;

	if (right1 > left2 && left1 < right2 && top1 > bottom2 && bottom1 < top2)
	{
		return true; 
	}

	return false;
}
