#pragma once
#include <vector>

typedef unsigned int GLuint;
typedef unsigned int GLenum;

class IndexBuffer
{
public:
	IndexBuffer();
	~IndexBuffer();

	void Init(const std::vector<unsigned int>& indices);

	void Bind() const;
	void Unbind() const;

	unsigned int GetCount() const { return m_count; }

private:
	GLuint m_bufferID;
	unsigned int m_count;
};