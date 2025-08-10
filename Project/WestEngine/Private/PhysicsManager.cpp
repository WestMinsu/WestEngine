#include "PhysicsManager.h"
#include "Utility.h"
#include <iostream>

PhysicsManager::PhysicsManager()
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::AddCollider(BoxColliderComponent* collider)
{
	m_colliders.push_back(collider);
}

void PhysicsManager::RemoveCollider(BoxColliderComponent* collider)
{
	for (auto it = m_colliders.begin(); it != m_colliders.end(); ++it)
	{
		if (*it == collider)
		{
			m_colliders.erase(it);
			return;
		}
	}
}

void PhysicsManager::CheckCollisions()
{
	for (size_t i = 0; i < m_colliders.size(); i++)
	{
		for (size_t j = i + 1; j < m_colliders.size(); j++)
		{
			AttackHitbox box1 = m_colliders[i]->GetHitbox();
			AttackHitbox box2 = m_colliders[j]->GetHitbox();

			if (CheckAABBCollision(box1.pos, box1.size, box2.pos, box2.size))
			{

				std::cout << "Collision Detected!" << std::endl;
			}
		}
	}
}