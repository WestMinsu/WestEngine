#pragma once
#include "IGameState.h"
#include "GameObject.h"
#include "Texture.h"
#include <memory>
#include <vector>

class MainGameState : public IGameState
{
public:
	MainGameState();
	~MainGameState();

	void Init() override;
	void Update(float dt) override;
	void Draw() override;
	void Exit() override;

private:
	std::unique_ptr<GameObject> m_playerObject;
	std::unique_ptr<GameObject> m_stoneObject;

	std::unique_ptr<Texture> m_playerIdleTexture;
	std::unique_ptr<Texture> m_staticObjectTexture;
};