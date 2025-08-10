#include "PlayerControllerComponent.h"
#include "Engine.h"
#include "Gameobject.h"
#include <iostream>

PlayerControllerComponent::PlayerControllerComponent(GameObject* owner)
	: IComponent(owner)
{
	//m_velocityX = 0.f;
	//m_velocityY = 0.f;
	m_speed = 300.f;
	m_dashSpeed = 800.f;
	m_jumpStrength = 600.f;
	m_isDashing = false;
}

PlayerControllerComponent::~PlayerControllerComponent()
{
}

void PlayerControllerComponent::Init()
{
	m_rigidbody = m_owner->GetComponent<RigidbodyComponent>();
}

void PlayerControllerComponent::Update(float dt)
{
	if (!m_rigidbody)
	{
		std::cout << "no rigidbody" << std::endl;
		return;
	}

	InputManager& input = WestEngine::GetInstance().GetInputManager();

	float currentSpeed = m_isDashing ? m_dashSpeed : m_speed;

	float velocityX = 0;

	if (input.IsKeyPressed(KEY_A))
	{
		velocityX = -currentSpeed;
		m_owner->GetComponent<AudioSourceComponent>()->Play("footstep");
	}
	else if (input.IsKeyPressed(KEY_D))
		velocityX = currentSpeed;

	m_rigidbody->SetVelocity({ velocityX, m_rigidbody->GetVelocity().y });

	if (input.IsKeyTriggered(KEY_SPACE))
	{
		// TODO: jump
		// m_rigidbody->SetVelocityY(m_jumpStrength);
	}

}
