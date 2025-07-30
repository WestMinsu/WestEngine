#include "Scene.h"

Scene::Scene()
{
}

Scene::~Scene()
{
}

void Scene::Init()
{
}

void Scene::Update(float dt)
{
	for (const auto& obj : m_gameObjects)
	{
		obj->Update(dt);
	}
}

void Scene::Draw()
{
	for (const auto& obj : m_gameObjects)
	{
		obj->Draw();
	}
}

void Scene::Exit()
{
	m_gameObjects.clear();
}

GameObject* Scene::AddGameObject()
{
	m_gameObjects.emplace_back(std::make_unique<GameObject>());
	return m_gameObjects.back().get();
}