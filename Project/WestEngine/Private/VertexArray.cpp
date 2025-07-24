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

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	m_vertexBuffer = vertexBuffer;
}

void VertexArray::Bind() const
{
	glBindVertexArray(m_arrayID);
}

void VertexArray::Unbind() const
{
	glBindVertexArray(0);
}