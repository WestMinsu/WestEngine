#include "GameObject.h"

GameObject::GameObject()
	: m_transform(this) 
{
}

GameObject::~GameObject()
{
}

void GameObject::Update(float dt)
{
	for (const auto& component : m_components)
	{
		component->Update(dt);
	}
}

void GameObject::Draw()
{
	for (const auto& component : m_components)
	{
		component->Draw();
	}
}