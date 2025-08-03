#pragma once

class GameObject;

class IComponent
{
public:
	IComponent(GameObject* owner) : m_owner(owner) {}
	virtual ~IComponent() {}

	virtual void Init() {}
	virtual void Update(float dt) {}
	virtual void Draw() {}

protected:
	GameObject* m_owner;
};