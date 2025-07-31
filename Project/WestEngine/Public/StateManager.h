#pragma once
#include <vector>
#include <memory>
#include "IGameState.h"

class StateManager
{
public:
	StateManager();
	~StateManager();

	void PushState(std::unique_ptr<IGameState> state);
	void PopState();
	void ChangeState(std::unique_ptr<IGameState> state);
	void Update(float dt);
	void Draw();

private:
	std::vector<std::unique_ptr<IGameState>> m_states;
};