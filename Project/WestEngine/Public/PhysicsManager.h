#pragma once
#include "BoxColliderComponent.h"
#include <vector>

class PhysicsManager
{
public:
	PhysicsManager();
	~PhysicsManager();

	void AddCollider(BoxColliderComponent* collider);
	void RemoveCollider(BoxColliderComponent* collider);

	void CheckCollisions();

private:
	std::vector<BoxColliderComponent*> m_colliders;
};