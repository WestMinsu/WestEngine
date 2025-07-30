#pragma once
#include "Scene.h"
#include <vector>
#include <memory>

class StateManager
{
public:
	StateManager();
	~StateManager();

	void PushState(std::unique_ptr<Scene> state);
	void PopState();
	void ChangeState(std::unique_ptr<Scene> state);

	void Update(float dt);
	void Draw();

private:
	std::vector<std::unique_ptr<Scene>> m_states;
};