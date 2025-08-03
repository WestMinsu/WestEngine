#pragma once
#include <glm/glm.hpp>

class Camera2D
{
public:
	Camera2D(float screenWidth, float screenHeight);
	~Camera2D();

	void SetPosition(const glm::vec2& position) 
	{ 
		m_position = position; m_needsUpdate = true; 
	}
	const glm::vec2& GetPosition() const
	{ 
		return m_position;
	}

	void SetZoom(float zoom) 
	{
		m_zoom = zoom;
		m_needsUpdate = true; 
	}
	float GetZoom() const { return m_zoom; }

	const glm::mat4& GetViewMatrix() const
	{ 
		return m_viewMatrix;
	}
	const glm::mat4& GetProjectionMatrix() const 
	{
		return m_projectionMatrix; 
	}

	void Update();

private:
	void RecalculateMatrices();

	float m_screenWidth, m_screenHeight;
	glm::vec2 m_position;
	float m_zoom;

	glm::mat4 m_viewMatrix;
	glm::mat4 m_projectionMatrix;

	bool m_needsUpdate; 
};