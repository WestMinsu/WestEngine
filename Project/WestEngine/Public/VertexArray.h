#pragma once
#include "VertexBuffer.h"
#include "IndexBuffer.h" 
#include <memory>

class VertexArray
{
public:
	VertexArray();
	~VertexArray();

	void Init();

	void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer);
	void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer); 

	const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const { return m_indexBuffer; }

	void Bind() const;
	void Unbind() const;

private:
	GLuint m_arrayID;
	std::shared_ptr<VertexBuffer> m_vertexBuffer;
	std::shared_ptr<IndexBuffer> m_indexBuffer; 
};