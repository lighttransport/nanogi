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
#ifndef NANOGI_MACROS_H
#define NANOGI_MACROS_H

#ifndef NDEBUG
	#define NGI_DEBUG_MODE 1
#else
	#define NGI_DEBUG_MODE 0
#endif

#ifdef _WIN32
	#define NGI_PLATFORM_WINDOWS 1
#else
	#define NGI_PLATFORM_WINDOWS 0
#endif

#ifdef __linux
	#define NGI_PLATFORM_LINUX 1
#else
	#define NGI_PLATFORM_LINUX 0
#endif

#ifdef __APPLE__
	#define NGI_PLATFORM_APPLE 1
#else
	#define NGI_PLATFORM_APPLE 0
#endif

#ifdef _MSC_VER
	#define NGI_COMPILER_MSVC 1
#else
	#define NGI_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) || defined(__MINGW32__)
	#define NGI_COMPILER_GCC 1
#else
	#define NGI_COMPILER_GCC 0
#endif

#ifdef __clang__
	#define NGI_COMPILER_CLANG 1
	#if NGI_COMPILER_GCC
		// clang defines __GNUC__
		#undef NGI_COMPILER_GCC
		#define NGI_COMPILER_GCC 0
	#endif
#else
	#define NGI_COMPILER_CLANG 0
#endif

#if NGI_PLATFORM_WINDOWS
	#define NOMINMAX
	#define WIN32_LEAN_AND_MEAN
	#pragma warning(disable:4819)	// Level 1. Character that cannot be represented
	#pragma warning(disable:4996)	// Level 3. _SCL_SECURE_NO_WARNINGS
	#pragma warning(disable:4290)	// Level 3. Exception specification ignored
	#pragma warning(disable:4201)	// Level 4. Nonstandard extension used : nameless struct/union
	#pragma warning(disable:4512)	// Level 4. Cannot generate an assignment operator for the given class
	#pragma warning(disable:4127)	// Level 4. Conditional expression is constant
	#pragma warning(disable:4510)	// Level 4. Default constructor could not be generated
	#pragma warning(disable:4610)	// Level 4. User-defined constructor required
	#pragma warning(disable:4100)	// Level 4. Unreferenced formal parameter
	#pragma warning(disable:4505)	// Level 4. Unreferenced local function has been removed
	#pragma warning(disable:4324)	// Level 4. Structure was padded due to __declspec(align())
#endif

#define NGI_TOKENPASTE(x, y) x ## y
#define NGI_TOKENPASTE2(x, y) NGI_TOKENPASTE(x, y)
#define NGI_STRINGIFY(x) #x
#define NGI_STRINGIFY2(x) NGI_STRINGIFY(x)
#define NGI_UNUSED(x) (void)x

#define NGI_ENUM_TYPE_MAP(EnumType)																				\
	template <typename T>																						\
	class EnumTypeMap;																							\
	template <>																									\
	class EnumTypeMap<EnumType> {																				\
	public:																										\
		EnumTypeMap() {																							\
			for (size_t i = 0; i < sizeof(EnumType##_String) / sizeof(EnumType##_String[0]); i++)				\
				TypeMap[EnumType##_String[i]] = (EnumType)(i);													\
		}																										\
		static EnumType ToEnum(const std::string& s) { return Instance().TypeMap[s]; }							\
		static EnumTypeMap<EnumType>& Instance() { static EnumTypeMap<EnumType> instance; return instance; }	\
	private:																									\
		std::unordered_map<std::string, EnumType> TypeMap;														\
	}

#define NGI_ENUM_TO_STRING(EnumType, EnumValue)  EnumType##_String[(int)(EnumValue)]
#define NGI_STRING_TO_ENUM(EnumType, EnumString) EnumTypeMap<EnumType>::ToEnum(EnumString)

#if NGI_PLATFORM_WINDOWS
	#define NGI_PRAGMA(x) __pragma(x)
#else
	#define NGI_PRAGMA(x) _Pragma(NGI_STRINGIFY(x))
#endif

#define NGI_NAMESPACE_BEGIN namespace nanogi {
#define NGI_NAMESPACE_END }

#endif // NANOGI_MACROS_H