#include "StateManager.h"

StateManager::StateManager() {}
StateManager::~StateManager()
{
	while (!m_states.empty())
	{
		PopState();
	}
}

void StateManager::PushState(std::unique_ptr<Scene> state)
{
	m_states.push_back(std::move(state));
	m_states.back()->Init();
}

void StateManager::PopState()
{
	if (!m_states.empty())
	{
		m_states.back()->Exit();
		m_states.pop_back();
	}
}

void StateManager::ChangeState(std::unique_ptr<Scene> state)
{
	if (!m_states.empty())
	{
		PopState();
	}
	PushState(std::move(state));
}

void StateManager::Update(float dt)
{
	if (!m_states.empty())
	{
		m_states.back()->Update(dt);
	}
}

void StateManager::Draw()
{
	if (!m_states.empty())
	{
		m_states.back()->Draw();
	}
}