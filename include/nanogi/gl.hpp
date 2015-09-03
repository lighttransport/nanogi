/*
	nanogi - A small reference GI renderer

	Copyright (c) 2015 Light Transport Entertainment Inc.
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	notice, this list of conditions and the following disclaimer in the
	documentation and/or other materials provided with the distribution.
	* Neither the name of the <organization> nor the
	names of its contributors may be used to endorse or promote products
	derived from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
	ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
	DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once
#ifndef NANOGI_GL_H
#define NANOGI_GL_H

#include <nanogi/basic.hpp>
#include <GL/glew.h>

NGI_NAMESPACE_BEGIN

namespace GLUtils
{
	void CheckGLErrors(const char* filename, const int line)
	{
		int err;
		if ((err = glGetError()) != GL_NO_ERROR)
		{
			std::string errstr;
			switch (err)
			{
				case GL_INVALID_ENUM:					{ errstr = "GL_INVALID_ENUM"; break; }
				case GL_INVALID_VALUE:					{ errstr = "GL_INVALID_VALUE"; break; }
				case GL_INVALID_OPERATION:				{ errstr = "GL_INVALID_OPERATION"; break; }
				case GL_INVALID_FRAMEBUFFER_OPERATION:	{ errstr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break; }
				case GL_OUT_OF_MEMORY:					{ errstr = "GL_OUT_OF_MEMORY"; break; }
			}
			NGI_LOG_ERROR(errstr);
		}
	}

	void DebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam)
	{
		std::string sourceString;
		std::string typeString;
		std::string severityString;

		switch (source)
		{
			case GL_DEBUG_SOURCE_API:				{ sourceString = "OpenGL"; break; }
			case GL_DEBUG_SOURCE_WINDOW_SYSTEM:		{ sourceString = "Windows"; break; }
			case GL_DEBUG_SOURCE_SHADER_COMPILER:	{ sourceString = "Shader Compiler"; break; }
			case GL_DEBUG_SOURCE_THIRD_PARTY:		{ sourceString = "Third Party"; break; }
			case GL_DEBUG_SOURCE_APPLICATION:		{ sourceString = "Application"; break; }
			case GL_DEBUG_SOURCE_OTHER:				{ sourceString = "Other"; break; }
		}

		switch (type)
		{
			case GL_DEBUG_TYPE_ERROR:				{ typeString = "Error"; break; }
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:	{ typeString = "Deprecated behavior"; break; }
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:	{ typeString = "Undefined behavior"; break; }
			case GL_DEBUG_TYPE_PORTABILITY:			{ typeString = "Portability"; break; }
			case GL_DEBUG_TYPE_PERFORMANCE:			{ typeString = "Performance"; break; }
			case GL_DEBUG_TYPE_OTHER:				{ typeString = "Message"; break; }
			case GL_DEBUG_TYPE_MARKER:				{ typeString = "Marker"; break; }
			case GL_DEBUG_TYPE_PUSH_GROUP:			{ typeString = "Push group"; break; }
			case GL_DEBUG_TYPE_POP_GROUP:			{ typeString = "Pop group"; break; }
		}

		switch (severity)
		{
			case GL_DEBUG_SEVERITY_HIGH:			{ severityString = "High"; break; }
			case GL_DEBUG_SEVERITY_MEDIUM:			{ severityString = "Medium"; break; }
			case GL_DEBUG_SEVERITY_LOW:				{ severityString = "Low"; break; }
			case GL_DEBUG_SEVERITY_NOTIFICATION:	{ severityString = "Notification"; break; }
		}

		const auto str = boost::str(boost::format("%s: %s(%s) %d: %s") % sourceString % typeString % severityString % id % message);
		NGI_LOG_INFO(str);
	}
}

#define NGI_GL_CHECK_ERRORS() GLUtils::CheckGLErrors(__FILE__, __LINE__)
#define NGI_GL_OFFSET_OF(Type, Element) ((size_t)&(((Type*)0)->Element))

namespace GLResourceType
{
	enum
	{
		None				= 0,
		Pipeline			= 1<<0,
		Program				= 1<<1,
		ArrayBuffer			= 1<<2,
		ElementArrayBuffer	= 1<<3,
		VertexArray			= 1<<4,
		Texture2D			= 1<<5,
		Buffer				= ArrayBuffer | ElementArrayBuffer,
		Texture				= Texture2D,
		Bindable			= Pipeline | Texture
	};
};

class GLResource
{
public:

	#pragma region Create & Destory

	void Create(int type)
	{
		Type = type;
		
		if (Type == GLResourceType::Pipeline)
		{
			glGenProgramPipelines(1, &Name);
		}
		else if (Type == GLResourceType::Program)
		{
			Name = glCreateProgram();
			Data.Program.Stages = 0;
		}
		else if ((Type & GLResourceType::Buffer) > 0)
		{
			glGenBuffers(1, &Name);
			if (Type == GLResourceType::ArrayBuffer)
			{
				Data.Buffer.Target = GL_ARRAY_BUFFER;
			}
			else if (Type == GLResourceType::ElementArrayBuffer)
			{
				Data.Buffer.Target = GL_ELEMENT_ARRAY_BUFFER;
			}
		}
		else if (Type == GLResourceType::VertexArray)
		{
			glGenVertexArrays(1, &Name);
		}
		else if ((Type & GLResourceType::Texture) > 0)
		{
			glGenTextures(1, &Name);
			if (Type == GLResourceType::Texture2D)
			{
				Data.Texture.Target = GL_TEXTURE_2D;
			}
		}

		NGI_GL_CHECK_ERRORS();
	}

	void Destory()
	{
		if (Type == GLResourceType::Pipeline)			{ glDeleteProgramPipelines(1, &Name); }
		else if (Type == GLResourceType::Program)		{ glDeleteProgram(Name); }
		else if ((Type & GLResourceType::Buffer) > 0)	{ glDeleteBuffers(1, &Name); }
		else if (Type == GLResourceType::VertexArray)	{ glDeleteVertexArrays(1, &Name); }
		else if (Type == GLResourceType::Texture)		{ glDeleteTextures(1, &Name); }
		NGI_GL_CHECK_ERRORS();
	}

	#pragma endregion

public:

	#pragma region For bindable resrouces

	void Bind() const
	{
		if ((Type & GLResourceType::Bindable) == 0) { NGI_LOG_ERROR("Invalid type"); return; }
		if (Type == GLResourceType::Pipeline) { glBindProgramPipeline(Name); }
		if ((Type & GLResourceType::Texture) > 0) { glBindTexture(Data.Texture.Target, Name); }
		NGI_GL_CHECK_ERRORS();
	}

	void Unbind() const
	{
		if ((Type & GLResourceType::Bindable) == 0) { NGI_LOG_ERROR("Invalid type"); return; }
		if (Type == GLResourceType::Pipeline) { glBindProgramPipeline(0); }
		if ((Type & GLResourceType::Texture) > 0) { glBindTexture(Data.Texture.Target, 0); }
		NGI_GL_CHECK_ERRORS();
	}

	#pragma endregion

public:
	
	#pragma region Getters

	int GetType()		{ return Type; }
	GLuint GetName()	{ return Name; }

	#pragma endregion

public:

	#pragma region Program type specific functions

	bool CompileString(GLenum shaderType, const std::string& content)
	{
		if (Type != GLResourceType::Program)
		{
			NGI_LOG_ERROR("Invalid type");
			return false;
		}

		// Create & Compile
		GLuint shaderID = glCreateShader(shaderType);
		const char* contentPtr = content.c_str();

		glShaderSource(shaderID, 1, &contentPtr, NULL);
		glCompileShader(shaderID);

		GLint ret;
		glGetShaderiv(shaderID, GL_COMPILE_STATUS, &ret);
		if (ret == 0)
		{
			int length;
			glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &length);

			std::vector<char> v(length);
			glGetShaderInfoLog(shaderID, length, NULL, &v[0]);
			glDeleteShader(shaderID);

			NGI_LOG_ERROR(std::string(&v[0]));
			return false;
		}

		// Attach to program
		glAttachShader(Name, shaderID);
		glProgramParameteri(Name, GL_PROGRAM_SEPARABLE, GL_TRUE);
		glDeleteShader(shaderID);

		// Add a flag for later use
		switch (shaderType)
		{
			case GL_VERTEX_SHADER:			{ Data.Program.Stages |= GL_VERTEX_SHADER_BIT; break; }
			case GL_TESS_CONTROL_SHADER:	{ Data.Program.Stages |= GL_TESS_CONTROL_SHADER_BIT; break; }
			case GL_TESS_EVALUATION_SHADER:	{ Data.Program.Stages |= GL_TESS_EVALUATION_SHADER_BIT; break; }
			case GL_GEOMETRY_SHADER:		{ Data.Program.Stages |= GL_GEOMETRY_SHADER_BIT; break; }
			case GL_FRAGMENT_SHADER:		{ Data.Program.Stages |= GL_FRAGMENT_SHADER_BIT; break; }
			case GL_COMPUTE_SHADER:			{ Data.Program.Stages |= GL_COMPUTE_SHADER_BIT; break; }
		}

		NGI_GL_CHECK_ERRORS();
		return true;
	}

	bool Link()
	{
		if (Type != GLResourceType::Program)
		{
			NGI_LOG_ERROR("Invalid type");
			return false;
		}

		glLinkProgram(Name);

		GLint ret;
		glGetProgramiv(Name, GL_LINK_STATUS, &ret);

		if (ret == GL_FALSE)
		{
			GLint length;
			glGetProgramiv(Name, GL_INFO_LOG_LENGTH, &length);

			std::vector<char> v(length);
			glGetProgramInfoLog(Name, length, NULL, &v[0]);

			NGI_LOG_ERROR(std::string(&v[0]));
			return false;
		}

		NGI_GL_CHECK_ERRORS();
		return true;
	}

	void SetUniform(const std::string& name, int v)
	{
		if (Type != GLResourceType::Program) { NGI_LOG_ERROR("Invalid type"); return; }
		glProgramUniform1i(Name, GetOrCreateUniformName(name), v);
		NGI_GL_CHECK_ERRORS();
	}

	void SetUniform(const std::string& name, const glm::vec3& v)
	{
		if (Type != GLResourceType::Program) { NGI_LOG_ERROR("Invalid type"); return; }
		glProgramUniform3fv(Name, GetOrCreateUniformName(name), 1, &v.x);
		NGI_GL_CHECK_ERRORS();
	}

	void SetUniform(const std::string& name, const glm::vec4& v)
	{
		if (Type != GLResourceType::Program) { NGI_LOG_ERROR("Invalid type"); return; }
		glProgramUniform4fv(Name, GetOrCreateUniformName(name), 1, &v.x);
		NGI_GL_CHECK_ERRORS();
	}

	void SetUniform(const std::string& name, const glm::mat4& mat)
	{
		if (Type != GLResourceType::Program) { NGI_LOG_ERROR("Invalid type"); return; }
		glProgramUniformMatrix4fv(Name, GetOrCreateUniformName(name), 1, GL_FALSE, glm::value_ptr(mat));
		NGI_GL_CHECK_ERRORS();
	}

	void SetUniform(const std::string& name, const float* mat)
	{
		if (Type != GLResourceType::Program) { NGI_LOG_ERROR("Invalid type"); return; }
		glProgramUniformMatrix4fv(Name, GetOrCreateUniformName(name), 1, GL_FALSE, mat);
		NGI_GL_CHECK_ERRORS();
	}

	GLuint GetOrCreateUniformName(const std::string& name)
	{
		if (Type != GLResourceType::Program)
		{
			NGI_LOG_ERROR("Invalid type");
			return 0;
		}
		
		auto& locationMap = Data.Program.UniformLocationMap;
		auto it = locationMap.find(name);
		if (it == locationMap.end())
		{
			GLuint loc = glGetUniformLocation(Name, name.c_str());
			locationMap[name] = loc;
			return loc;
		}

		return it->second;
	}

	#pragma endregion

public:

	#pragma region Pipeline type specific functions

	void AddProgram(const GLResource& program)
	{
		if (Type != GLResourceType::Pipeline || program.Type != GLResourceType::Program) { NGI_LOG_ERROR("Invalid type"); return; }
		glUseProgramStages(Name, program.Data.Program.Stages, program.Name);
		NGI_GL_CHECK_ERRORS();
	}

	#pragma endregion

public:

	#pragma region Buffer type specific functions

	bool Allocate(GLsizeiptr size, const GLvoid* data, GLenum usage)
	{
		if ((Type & GLResourceType::Buffer) == 0) { NGI_LOG_ERROR("Invalid type"); return false; }
		glBindBuffer(Data.Buffer.Target, Name);
		glBufferData(Data.Buffer.Target, size, data, usage);
		glBindBuffer(Data.Buffer.Target, 0);
		NGI_GL_CHECK_ERRORS();
		return true;
	}

	void* MapBuffer(GLenum access) const
	{
		if ((Type & GLResourceType::Buffer) == 0) { NGI_LOG_ERROR("Invalid type"); return nullptr; }
		glBindBuffer(Data.Buffer.Target, Name);
		auto* p = glMapBuffer(Data.Buffer.Target, access);
		NGI_GL_CHECK_ERRORS();
		return p;
	}

	void UnmapBuffer() const
	{
		if ((Type & GLResourceType::Buffer) == 0) { NGI_LOG_ERROR("Invalid type"); return; }
		glUnmapBuffer(Data.Buffer.Target);
		glBindBuffer(Data.Buffer.Target, 0);
		NGI_GL_CHECK_ERRORS();
	}

	int BufferSize() const
	{
		if ((Type & GLResourceType::Buffer) == 0) { NGI_LOG_ERROR("Invalid type"); return false; }
		GLint v;
		glBindBuffer(Data.Buffer.Target, Name);
		glGetBufferParameteriv(Data.Buffer.Target, GL_BUFFER_SIZE, &v);
		glBindBuffer(Data.Buffer.Target, 0);
		NGI_GL_CHECK_ERRORS();
		return v;
	}

	#pragma endregion

public:

	#pragma region Vertex array type specific functions

	void AddVertexAttribute(const GLResource& v, GLuint index, GLuint componentNum, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* start) const
	{
		if (Type != GLResourceType::VertexArray || v.Type != GLResourceType::ArrayBuffer) { NGI_LOG_ERROR("Invalid type"); return; }
		glBindVertexArray(Name);
		glBindBuffer(GL_ARRAY_BUFFER, v.Name);
		glVertexAttribPointer(index, componentNum, type, normalized, stride, start);
		glEnableVertexAttribArray(index);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		NGI_GL_CHECK_ERRORS();
	}

	void Draw(GLenum mode, int offset, int count) const
	{
		if (Type != GLResourceType::VertexArray) { NGI_LOG_ERROR("Invalid type"); return; }
		glBindVertexArray(Name);
		glDrawArrays(mode, offset, count);
		glBindVertexArray(0);
		NGI_GL_CHECK_ERRORS();
	}

	void Draw(GLenum mode, const GLResource& ibo, int count) const
	{
		if (Type != GLResourceType::VertexArray || ibo.Type != GLResourceType::ElementArrayBuffer) { NGI_LOG_ERROR("Invalid type"); return; }
		glBindVertexArray(Name);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo.Name);
		glDrawElements(mode, count, GL_UNSIGNED_INT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		NGI_GL_CHECK_ERRORS();
	}

	void Draw(GLenum mode, const GLResource& ibo) const
	{
		Draw(mode, ibo, ibo.BufferSize() / sizeof(GLuint));
	}

	#pragma endregion

private:

	int Type = GLResourceType::None;
	GLuint Name;

private:

	struct
	{
		struct
		{
			GLbitfield Stages;
			std::unordered_map<std::string, GLuint> UniformLocationMap;
		} Program;

		struct
		{
			GLenum Target;
		} Buffer;

		struct
		{
			GLenum Target;
		} Texture;
	} Data;

};

NGI_NAMESPACE_END

#endif // NANOGI_GL_H