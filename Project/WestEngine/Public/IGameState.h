#pragma once

class IGameState
{
public:
	virtual ~IGameState() {};

	virtual void Init() = 0;
	virtual void Update(float dt) = 0;
	virtual void Draw() = 0;
	virtual void Exit() = 0;
};