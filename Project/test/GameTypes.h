#pragma once
#include <string>
#include <glm/glm.hpp>

class Texture;
enum class SpriteSheetOrientation { HORIZONTAL, VERTICAL };

struct AnimData
{
	std::string texturePath;
	Texture* pTexture = nullptr;
	int frameCount = 0;
	SpriteSheetOrientation orientation = SpriteSheetOrientation::VERTICAL;
	float frameDuration = 0.1f;
	bool loop = true;
};