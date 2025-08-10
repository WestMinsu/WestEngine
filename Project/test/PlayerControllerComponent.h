#pragma once
#include "IComponent.h"
class RigidbodyComponent;

class PlayerControllerComponent : public IComponent
{
public:
	PlayerControllerComponent(GameObject* owner);
	~PlayerControllerComponent();

	void Init() override;
	void Update(float dt) override;

private:
	/*float m_velocityX;
	float m_velocityY;*/
	float m_speed;
	float m_dashSpeed;
	float m_jumpStrength;
	bool m_isDashing;

	RigidbodyComponent* m_rigidbody;
};