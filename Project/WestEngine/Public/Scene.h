#pragma once
#include "GameObject.h"
#include <vector>
#include <memory>

class Scene
{
public:
	Scene();
	virtual ~Scene();

	virtual void Init();
	virtual void Update(float dt);
	virtual void Draw();
	virtual void Exit();

	GameObject* AddGameObject();

protected:
	std::vector<std::unique_ptr<GameObject>> m_gameObjects;
};