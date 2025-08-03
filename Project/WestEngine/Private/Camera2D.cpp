#include "Camera2D.h"
#include <glm/gtc/matrix_transform.hpp>

Camera2D::Camera2D(float screenWidth, float screenHeight)
	: m_screenWidth(screenWidth), m_screenHeight(screenHeight),
	m_position(0.0f, 0.0f), m_zoom(1.0f), m_needsUpdate(true)
{
	RecalculateMatrices();
}

Camera2D::~Camera2D()
{
}

void Camera2D::Update()
{
	if (m_needsUpdate)
	{
		RecalculateMatrices();
		m_needsUpdate = false;
	}
}

void Camera2D::RecalculateMatrices()
{
	float halfWidth = m_screenWidth * 0.5f * m_zoom;
	float halfHeight = m_screenHeight * 0.5f * m_zoom;

	m_projectionMatrix = glm::ortho(
		m_position.x - halfWidth,
		m_position.x + halfWidth,
		m_position.y - halfHeight,
		m_position.y + halfHeight,
		-1.0f, 1.0f);

	m_viewMatrix = glm::mat4(1.0f);
}