#include "Animation.h"

Animation::Animation()
	: m_totalFrames(1), m_currentFrame(0), m_frameDuration(0.1f), m_timer(0.0f), m_isVertical(false), m_loop(true)
{
}

void Animation::Init(int totalFrames, float duration, bool isVertical, bool loop)
{
	m_totalFrames = totalFrames > 0 ? totalFrames : 1;
	m_frameDuration = duration / m_totalFrames;
	m_isVertical = isVertical;
	m_loop = loop;
}

void Animation::Update(float dt)
{
	m_timer += dt;
	if (m_timer >= m_frameDuration)
	{
		m_timer -= m_frameDuration;
		m_currentFrame++;
		if (m_currentFrame >= m_totalFrames)
		{
			m_currentFrame = m_loop ? 0 : m_totalFrames - 1;
		}
	}
}

glm::vec2 Animation::GetUVOffset() const
{
	if (m_isVertical)
	{
		return { 0.0f, static_cast<float>(m_currentFrame) / m_totalFrames };
	}
	else
	{
		return { static_cast<float>(m_currentFrame) / m_totalFrames, 0.0f };
	}
}

glm::vec2 Animation::GetUVScale() const
{
	if (m_isVertical)
	{
		return { 1.0f, 1.0f / m_totalFrames };
	}
	else
	{
		return { 1.0f / m_totalFrames, 1.0f };
	}
}