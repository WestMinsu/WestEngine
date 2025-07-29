#include <glad/gl.h>
#include "VertexArray.h"


VertexArray::VertexArray()
{
	m_arrayID = 0;
}

VertexArray::~VertexArray()
{
	if (m_arrayID != 0)
	{
		glDeleteVertexArrays(1, &m_arrayID);
	}
}

void VertexArray::Init()
{
	glGenVertexArrays(1, &m_arrayID);
}

void VertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer)
{
	glBindVertexArray(m_arrayID);
	vertexBuffer->Bind();

	const int stride = 5 * sizeof(float);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	m_vertexBuffer = vertexBuffer;
}

void VertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer)
{
	glBindVertexArray(m_arrayID);
	indexBuffer->Bind();
	m_indexBuffer = indexBuffer;
}

void VertexArray::Bind() const
{
	glBindVertexArray(m_arrayID);
}

void VertexArray::Unbind() const
{
	glBindVertexArray(0);
}