#pragma once
#include "Scene.h"
#include "Texture.h"
#include <memory>

class TestScene : public Scene
{
public:
	TestScene();
	~TestScene();

	void Init() override;
	void Update(float dt) override;
	void Draw() override;
	void Exit() override;

private:
	GameObject* m_playerObject;
	std::unique_ptr<Texture> m_playerTexture;
};