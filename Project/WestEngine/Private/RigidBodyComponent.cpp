#include "RigidbodyComponent.h"
#include "Gameobject.h"
#include <iostream>
RigidbodyComponent::RigidbodyComponent(GameObject* owner)
	: IComponent(owner)
{
	m_velocity = { 0.f, 0.f };
	m_gravity = -1200.f;
}

RigidbodyComponent::~RigidbodyComponent()
{
}

void RigidbodyComponent::Update(float dt)
{
	Transform& transform = m_owner->GetTransform();
	
	glm::vec2 currentPos = transform.GetPosition();
	currentPos += m_velocity * dt;

	transform.SetPosition(currentPos);
}