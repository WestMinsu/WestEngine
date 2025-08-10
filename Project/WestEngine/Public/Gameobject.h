#pragma once
#include <vector>
#include <memory>
#include "IComponent.h"
#include "Transform.h"

class GameObject
{
public:
	GameObject();
	~GameObject();

	void Update(float dt);
	void Draw();

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		auto newComponent = std::make_unique<T>(this);
		newComponent->Init(std::forward<Args>(args)...);
		T* componentPtr = newComponent.get();
		m_components.push_back(std::move(newComponent));
		return componentPtr;
	}

	template<typename T>
	T* GetComponent()
	{
		for (const auto& component : m_components)
		{
			T* target = dynamic_cast<T*>(component.get());
			if (target)
			{
				return target;
			}
		}
		return nullptr;
	}

	Transform& GetTransform() { return m_transform; }

private:
	Transform m_transform;
	std::vector<std::unique_ptr<IComponent>> m_components;
};