#pragma once
#include <glm/glm.hpp>

class Animation
{
public:
	Animation();
	void Init(int totalFrames, float duration, bool isVertical = false, bool loop = true);
	void Update(float dt);

	glm::vec2 GetUVOffset() const;
	glm::vec2 GetUVScale() const;

private:
	int m_totalFrames;
	int m_currentFrame;
	float m_frameDuration;
	float m_timer;
	bool m_isVertical;
	bool m_loop;
};