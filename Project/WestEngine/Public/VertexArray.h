#pragma once
#include "VertexBuffer.h"
#include <memory>

class VertexArray
{
public:
	VertexArray();
	~VertexArray();

	void Init();

	void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer);

	void Bind() const;
	void Unbind() const;

private:
	GLuint m_arrayID;
	std::shared_ptr<VertexBuffer> m_vertexBuffer;
};