#include "Shader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <glad/gl.h>

Shader::Shader()
{
	m_programID = 0;
}

Shader::~Shader()
{
	if (m_programID != 0)
	{
		glDeleteProgram(m_programID);
	}
}

bool Shader::Load(const std::string& vertPath, const std::string& fragPath)
{
	std::string vertSource = ReadFile(vertPath);
	std::string fragSource = ReadFile(fragPath);
	std::cout << vertSource << std::endl;
	std::cout << fragSource << std::endl;
	if (vertSource.empty() || fragSource.empty())
	{
		return false;
	}

	GLuint vs = CompileShader(GL_VERTEX_SHADER, vertSource);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragSource);

	if (vs == 0 || fs == 0)
	{
		return false;
	}

	m_programID = glCreateProgram();
	glAttachShader(m_programID, vs);
	glAttachShader(m_programID, fs);
	glLinkProgram(m_programID);

	GLint linkStatus;
	glGetProgramiv(m_programID, GL_LINK_STATUS, &linkStatus);
	if (linkStatus == GL_FALSE)
	{
		char infoLog[512];
		glGetProgramInfoLog(m_programID, 512, NULL, infoLog);
		std::cerr << "Error: Shader program linking failed!\n" << infoLog << std::endl;
		glDeleteProgram(m_programID);
		m_programID = 0;
		return false;
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	return true;
}

void Shader::Bind() const
{
	glUseProgram(m_programID);
}

void Shader::Unbind() const
{
	glUseProgram(0);
}

std::string Shader::ReadFile(const std::string& filepath)
{
	std::ifstream file(filepath);
	if (!file.is_open())
	{
		std::cerr << "Error: Could not open shader file: " << filepath << std::endl;
		return "";
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

GLuint Shader::CompileShader(GLenum type, const std::string& source)
{
	GLuint shader = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	GLint compileStatus;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
	if (compileStatus == GL_FALSE)
	{
		char infoLog[512];
		glGetShaderInfoLog(shader, 512, NULL, infoLog);
		std::cerr << "Error: Shader compilation failed! Type: " << type << "\n" << infoLog << std::endl;
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}