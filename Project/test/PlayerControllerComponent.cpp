#include "PlayerControllerComponent.h"
#include "Engine.h"
#include <Gameobject.h>

PlayerControllerComponent::PlayerControllerComponent(GameObject* owner)
	: IComponent(owner)
{
	m_velocityX = 0.f;
	m_velocityY = 0.f;
	m_speed = 300.f;
	m_dashSpeed = 800.f;
	m_jumpStrength = 600.f;
	m_gravity = -1200.f;
	m_isGrounded = true;
	m_isDashing = false;
}

PlayerControllerComponent::~PlayerControllerComponent()
{
}

void PlayerControllerComponent::Init()
{
}

void PlayerControllerComponent::Update(float dt)
{
	InputManager& input = WestEngine::GetInstance().GetInputManager();
	Transform& transform = m_owner->GetTransform();
	float currentSpeed = m_isDashing ? m_dashSpeed : m_speed;

	if (m_isGrounded)
	{
		if (input.IsKeyPressed(KEY_A))
		{
			m_owner->GetComponent<AudioSourceComponent>()->Play("footstep");
			m_velocityX = -currentSpeed;
		}
		else if (input.IsKeyPressed(KEY_D))
			m_velocityX = currentSpeed;
		else
			m_velocityX = 0;

		if (input.IsKeyTriggered(KEY_SPACE))
		{
			m_velocityY = m_jumpStrength;
			m_isGrounded = false;
		}
	}

	if (!m_isGrounded)
	{
		m_velocityY += m_gravity * dt;
	}
	
	glm::vec2 currentPos = transform.GetPosition();
	currentPos.x += m_velocityX * dt;
	currentPos.y += m_velocityY * dt;

	if (currentPos.y < 0.f)
	{
		currentPos.y = 0.f;
		m_isGrounded = true;
		m_velocityY = 0.f;
	}

	transform.SetPosition(currentPos);
}
