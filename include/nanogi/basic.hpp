/*
	nanogi - A small, reference GI renderer

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
#ifndef NANOGI_BASIC_H
#define NANOGI_BASIC_H

#include <nanogi/macros.hpp>

#include <iostream>
#include <functional>
#include <thread>
#include <string>
#include <atomic>
#include <mutex>
#include <random>
#include <unordered_map>
#include <chrono>

#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/scoped_array.hpp>
#include <boost/optional.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/filesystem.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/math/constants/constants.hpp>
#include <boost/lexical_cast.hpp>
#pragma warning(push)
#pragma warning(disable:4267)
#include <boost/asio.hpp>
#pragma warning(pop)

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#if NGI_PLATFORM_WINDOWS
#include <Windows.h>
#endif

#include <FreeImage.h>
#include <tbb/tbb.h>

#include <nanogi/tinyexr.h>

NGI_NAMESPACE_BEGIN

#pragma region Constants

namespace
{
	const double Pi = boost::math::constants::pi<double>();
	const double InvPi = 1.0 / Pi;
	const double Inf = std::numeric_limits<double>::max();
	const float  InfF = std::numeric_limits<float>::max();
	const float  EpsF = 1e-4f;
}

#pragma endregion

#pragma region Logger

enum class LogType
{
	Error,
	Warn,
	Info,
	Debug
};

class Logger
{
public:

	static Logger* Instance();

public:

	void Run()
	{
		ioThread = std::thread(boost::bind(&boost::asio::io_service::run, &io));
	}

	void Stop()
	{
		if (ioThread.joinable())
		{
			work.reset();
			ioThread.join();
		}
	}

public:

	void Log(LogType type, const std::string& message, int line, bool inplace)
	{
		int threadId;
		{
			const auto id = boost::lexical_cast<std::string>(std::this_thread::get_id());
			tbb::concurrent_hash_map<std::string, int>::accessor a;
			if (threadIdMap.insert(a, id))
			{
				a->second = threadIdMapCount++;
			}
			threadId = a->second;
		}

		io.post([this, type, message, line, threadId, inplace]()
		{
			// Fill spaces to erase previous message
			if (prevMessageIsInplace)
			{
				int consoleWidth;
				const int DefaultConsoleWidth = 100;
				#if NGI_PLATFORM_WINDOWS
				{
					HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
					CONSOLE_SCREEN_BUFFER_INFO screenBufferInfo;
					if (!GetConsoleScreenBufferInfo(consoleHandle, &screenBufferInfo))
					{
						consoleWidth = DefaultConsoleWidth;
					}
					else
					{
						consoleWidth = screenBufferInfo.dwSize.X - 1;
					}
				}
				#elif NGI_PLATFORM_LINUX
				{
					struct winsize w;
					if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) < 0)
					{
						consoleWidth = DefaultConsoleWidth;
					}
					else
					{
						consoleWidth = w.ws_col;
					}
				}
				#endif
				std::cout << std::string(consoleWidth, ' ') << "\r";
			}

			// Print message
			BeginTextColor(type);
			const auto text = GenerateMessage(type, message, line, threadId);
			if (inplace)
			{
				std::cout << text << "\r";
				std::cout.flush();
				prevMessageIsInplace = true;
			}
			else
			{
				std::cout << text << std::endl;
				prevMessageIsInplace = false;
			}
			EndTextColor();
		});
	}

	void IncreaseIndentation() { io.post([this](){ AddIndentation(1); }); }
	void DecreaseIndentation() { io.post([this](){ AddIndentation(-1); }); }

private:

	void AddIndentation(int delta)
	{
		Indentation += delta;
		if (Indentation > 0)
		{
			IndentationString = std::string(4 * Indentation, '.') + " ";
		}
		else
		{
			Indentation = 0;
			IndentationString = "";
		}
	}

	std::string GenerateMessage(LogType type, const std::string& message, int line, int threadId) const
	{
		const std::string LogTypeString[] = { "ERROR", "WARN", "INFO", "DEBUG" };
		const auto now = std::chrono::high_resolution_clock::now();
		const double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - LogStartTime).count() / 1000.0;
		return boost::str(boost::format("| %-5s %.3f | @%4d | #%2d | %s%s") % LogTypeString[(int)(type)] % elapsed % line % threadId % IndentationString % message);
	}

	void BeginTextColor(LogType type)
	{
		#if NGI_PLATFORM_WINDOWS
		HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		WORD colorFlag = 0;
		switch (type)
		{
			case LogType::Error: { colorFlag = FOREGROUND_RED | FOREGROUND_INTENSITY; break; }
			case LogType::Warn:  { colorFlag = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break; }
			case LogType::Info:  { colorFlag = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break; }
			case LogType::Debug: { colorFlag = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break; }
		}
		SetConsoleTextAttribute(consoleHandle, colorFlag);
		#elif LM_PLATFORM_LINUX
		switch (type)
		{
			case LogType::Error: { std::cout << "\033[31m"; break; }
			case LogType::Warn:  { std::cout << "\033[33m"; break; }
			case LogType::Info:  { std::cout << "\033[37m"; break; }
			case LogType::Debug: { std::cout << "\033[137m"; break; }
		}
		#endif
	}

	void EndTextColor()
	{
		#if NGI_PLATFORM_WINDOWS
		HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		#elif NGI_PLATFORM_LINUX
		std::cout << "\033[0m";
		#endif
	}

private:

	static std::atomic<Logger*> instance;
	static std::mutex mutex;

private:

	boost::asio::io_service io;
	std::unique_ptr<boost::asio::io_service::work> work{new boost::asio::io_service::work(io)};
	std::thread ioThread;

private:

	std::string InplaceText;
	std::chrono::high_resolution_clock::time_point LogStartTime = std::chrono::high_resolution_clock::now();
	int Indentation = 0;
	std::string IndentationString;
	bool prevMessageIsInplace = false;
	tbb::concurrent_hash_map<std::string, int> threadIdMap;
	std::atomic<int> threadIdMapCount;

};

std::atomic<Logger*> Logger::instance;
std::mutex Logger::mutex;

Logger* Logger::Instance()
{
	auto* p = instance.load(std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_acquire);
	if (p == nullptr)
	{
		std::lock_guard<std::mutex> lock(mutex);
		p = instance.load(std::memory_order_relaxed);
		if (p == nullptr)
		{
			p = new Logger;
			std::atomic_thread_fence(std::memory_order_release);
			instance.store(p, std::memory_order_relaxed);
		}
	}
	return p;
}

class LogIndenter
{
public:
	LogIndenter()  { Logger::Instance()->IncreaseIndentation(); }
	~LogIndenter() { Logger::Instance()->DecreaseIndentation(); }
};

#define NGI_LOG_RUN()			Logger::Instance()->Run()
#define NGI_LOG_STOP()			Logger::Instance()->Stop()
#define NGI_LOG_ERROR(message)   Logger::Instance()->Log(LogType::Error, message, __LINE__, false)
#define NGI_LOG_WARN(message)    Logger::Instance()->Log(LogType::Warn,  message, __LINE__, false)
#define NGI_LOG_INFO(message)    Logger::Instance()->Log(LogType::Info,  message, __LINE__, false)
#define NGI_LOG_DEBUG(message)   Logger::Instance()->Log(LogType::Debug, message, __LINE__, false)
#define NGI_LOG_INPLACE(message) Logger::Instance()->Log(LogType::Info,  message, __LINE__, true)
#define NGI_LOG_INPLACE_END()    std::cout << std::endl
#define NGI_LOG_INDENTER()       LogIndenter _logIndenter

#pragma endregion

#pragma region Floating point exception handling

#if NGI_PLATFORM_WINDOWS

namespace
{

	void SETransFunc(unsigned int code, PEXCEPTION_POINTERS data)
	{
		std::string desc;
		switch (code)
		{
			case EXCEPTION_ACCESS_VIOLATION:			{ desc = "EXCEPTION_ACCESS_VIOLATION";			break; }
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:		{ desc = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";		break; }
			case EXCEPTION_BREAKPOINT:					{ desc = "EXCEPTION_BREAKPOINT";				break; }
			case EXCEPTION_DATATYPE_MISALIGNMENT:		{ desc = "EXCEPTION_DATATYPE_MISALIGNMENT";		break; }
			case EXCEPTION_FLT_DENORMAL_OPERAND:		{ desc = "EXCEPTION_FLT_DENORMAL_OPERAND";		break; }
			case EXCEPTION_FLT_DIVIDE_BY_ZERO:			{ desc = "EXCEPTION_FLT_DIVIDE_BY_ZERO";		break; }
			case EXCEPTION_FLT_INEXACT_RESULT:			{ desc = "EXCEPTION_FLT_INEXACT_RESULT";		break; }
			case EXCEPTION_FLT_INVALID_OPERATION:		{ desc = "EXCEPTION_FLT_INVALID_OPERATION";		break; }
			case EXCEPTION_FLT_OVERFLOW:				{ desc = "EXCEPTION_FLT_OVERFLOW";				break; }
			case EXCEPTION_FLT_STACK_CHECK:				{ desc = "EXCEPTION_FLT_STACK_CHECK";			break; }
			case EXCEPTION_FLT_UNDERFLOW:				{ desc = "EXCEPTION_FLT_UNDERFLOW";				break; }
			case EXCEPTION_ILLEGAL_INSTRUCTION:			{ desc = "EXCEPTION_ILLEGAL_INSTRUCTION";		break; }
			case EXCEPTION_IN_PAGE_ERROR:				{ desc = "EXCEPTION_IN_PAGE_ERROR";				break; }
			case EXCEPTION_INT_DIVIDE_BY_ZERO:			{ desc = "EXCEPTION_INT_DIVIDE_BY_ZERO";		break; }
			case EXCEPTION_INT_OVERFLOW:				{ desc = "EXCEPTION_INT_OVERFLOW";				break; }
			case EXCEPTION_INVALID_DISPOSITION:			{ desc = "EXCEPTION_INVALID_DISPOSITION";		break; }
			case EXCEPTION_NONCONTINUABLE_EXCEPTION:	{ desc = "EXCEPTION_NONCONTINUABLE_EXCEPTION";	break; }
			case EXCEPTION_PRIV_INSTRUCTION:			{ desc = "EXCEPTION_PRIV_INSTRUCTION";			break; }
			case EXCEPTION_SINGLE_STEP:					{ desc = "EXCEPTION_SINGLE_STEP";				break; }
			case EXCEPTION_STACK_OVERFLOW:				{ desc = "EXCEPTION_STACK_OVERFLOW";			break; }
		}

		NGI_LOG_ERROR("Structured exception is detected");
		NGI_LOG_INDENTER();
		NGI_LOG_ERROR("Exception code    : " + boost::str(boost::format("0x%08x") % code));
		NGI_LOG_ERROR("Exception address : " + boost::str(boost::format("0x%08x") % data->ExceptionRecord->ExceptionAddress));
		if (!desc.empty())
		{
			NGI_LOG_ERROR("Description       : " + desc);
		}

		#if NGI_DEBUG_MODE
		__debugbreak();
		#endif

		throw std::runtime_error("Aborting");
	}

	void SetFPException(unsigned int newFPState)
	{
		errno_t error;

		// Get current floating-point control word
		unsigned int currentFPState;
		if ((error = _controlfp_s(&currentFPState, 0, 0)) != 0)
		{
			NGI_LOG_ERROR("_controlfp_s failed : " + std::string(strerror(error)));
			return;
		}

		// Set a new control word
		if ((error = _controlfp_s(&currentFPState, newFPState, _MCW_EM)) != 0)
		{
			NGI_LOG_ERROR("_controlfp_s failed : " + std::string(strerror(error)));
			return;
		}
	}

	void EnableFPException()
	{
		#if 0
		SetFPException((unsigned int)(~(_EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW)));
		#else
		SetFPException((unsigned int)(~(_EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE)));
		#endif
	}

	void DisableFPException()
	{
		SetFPException((unsigned int)(_EM_INVALID | _EM_DENORMAL | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT));
	}

}

#define NGI_ENABLE_FP_EXCEPTION() EnableFPException()
#define NGI_DISABLE_FP_EXCEPTION() DisableFPException()

#else

#define NGI_ENABLE_FP_EXCEPTION()
#define NGI_DISABLE_FP_EXCEPTION()

#endif

#pragma endregion

#pragma region Random number generator

class Random
{
public:

	void SetSeed(unsigned int seed) { engine.seed(seed); distDouble.reset(); distUInt.reset(); }
	double Next() { return distDouble(engine); }
	glm::dvec2 Next2D() { return glm::dvec2(Next(), Next()); }
	unsigned int NextUInt() { return distUInt(engine); }

private:

	std::mt19937 engine;
	std::uniform_real_distribution<double> distDouble;
	std::uniform_int_distribution<unsigned int> distUInt;

};

#pragma endregion

#pragma region Discrete distribution

class Distribution1D
{
public:

	Distribution1D() { Clear(); }

public:

	void Add(double v)
	{
		cdf.push_back(cdf.back() + v);
	}

	void Normalize()
	{
		const double sum = cdf.back();
		const double invSum = 1.0 / sum;
		for (auto& v : cdf)
		{
			v *= invSum;
		}
	}

	int Sample(double u) const
	{
		int v = static_cast<int>(std::upper_bound(cdf.begin(), cdf.end(), u) - cdf.begin()) - 1;
		return glm::clamp<int>(v, 0, static_cast<int>(cdf.size()) - 2);
	}

	int SampleReuse(double u, double& u2) const
	{
		int v = static_cast<int>(std::upper_bound(cdf.begin(), cdf.end(), u) - cdf.begin()) - 1;
		int i = glm::clamp<int>(v, 0, static_cast<int>(cdf.size()) - 2);
		u2 = (u - cdf[i]) / (cdf[i + 1] - cdf[i]);
		return i;
	}

	double EvaluatePDF(int i) const
	{
		return (i < 0 || i + 1 >= static_cast<int>(cdf.size())) ? 0 : cdf[i + 1] - cdf[i];
	}

	void Clear()
	{
		cdf.clear();
		cdf.push_back(0);
	}

	bool Empty() const
	{
		return cdf.size() == 1;
	}

private:

	std::vector<double> cdf;

};

#pragma endregion

#pragma region Save image

namespace
{

	bool SaveImage(const std::string& path, const std::vector<glm::dvec3>& film, int width, int height)
	{
		#pragma region Check & create output directory

		{
			const auto parent = boost::filesystem::path(path).parent_path();
			if (!boost::filesystem::exists(parent) && parent != "")
			{
				NGI_LOG_INFO("Creating directory : " + parent.string());
				if (!boost::filesystem::create_directories(parent))
				{
					NGI_LOG_WARN("Failed to create output directory : " + parent.string());
					return false;
				}
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Save image

		{
			boost::filesystem::path fsPath(path);
			if (fsPath.extension() == ".hdr")
			{
				#pragma region HDR

				FIBITMAP* fibitmap = FreeImage_AllocateT(FIT_RGBF, width, height);
				if (!fibitmap)
				{
					NGI_LOG_ERROR("Failed to allocate bitmap");
					return false;
				}

				for (int y = 0; y < height; y++)
				{
					FIRGBF* bits = (FIRGBF*)FreeImage_GetScanLine(fibitmap, y);
					for (int x = 0; x < width; x++)
					{
						const int i = y * width + x;
						bits[x].red   = (float)(film[i].r);
						bits[x].green = (float)(film[i].g);
						bits[x].blue  = (float)(film[i].b);
					}
				}

				if (!FreeImage_Save(FIF_HDR, fibitmap, path.c_str(), HDR_DEFAULT))
				{
					NGI_LOG_ERROR("Failed to save image : " + path);
					FreeImage_Unload(fibitmap);
					return false;
				}

				NGI_LOG_INFO("Successfully saved to " + path);
				FreeImage_Unload(fibitmap);

				#pragma endregion
			}
			else if (fsPath.extension() == ".exr")
			{
				#pragma region EXR

				EXRImage image;
				InitEXRImage(&image);

				image.num_channels = 3;

				const char* channel_names[] = {"B", "G", "R"}; // "B", "G", "R", "A" for RGBA image

				std::vector<float> images[3];
				images[0].resize(width * height);
				images[1].resize(width * height);
				images[2].resize(width * height);

				// Flip y
				for (int y = 0; y < height; y++) {
					for (int x = 0; x < width; x++) {
						images[0][(height-y-1)*width+x] = (float)film[y*width+x].r;
						images[1][(height-y-1)*width+x] = (float)film[y*width+x].g;
						images[2][(height-y-1)*width+x] = (float)film[y*width+x].b;
					}
				}

				float* image_ptr[3];
				image_ptr[0] = &(images[2].at(0)); // B
				image_ptr[1] = &(images[1].at(0)); // G
				image_ptr[2] = &(images[0].at(0)); // R

				image.channel_names = channel_names;
				image.images = (unsigned char**)image_ptr;
				image.width = width;
				image.height = height;
				image.compression = TINYEXR_COMPRESSIONTYPE_ZIP;

				image.pixel_types = (int *)malloc(sizeof(int) * image.num_channels);
				image.requested_pixel_types = (int *)malloc(sizeof(int) * image.num_channels);
				for (int i = 0; i < image.num_channels; i++) {
					image.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of input image
					image.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of output image to be stored in .EXR
				}

				const char* err;
				int ret = SaveMultiChannelEXRToFile(&image, path.c_str(), &err);
				if (ret != 0) {
					NGI_LOG_ERROR("Failed to save image : " + path);
					return false;
				}

				NGI_LOG_INFO("Successfully saved to " + path);
				free(image.pixel_types);
				free(image.requested_pixel_types);

				#pragma endregion
			}
			else if (fsPath.extension() == ".png")
			{
				#pragma region PNG

				FIBITMAP* tonemappedBitmap = FreeImage_Allocate(width, height, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
				if (!tonemappedBitmap)
				{
					NGI_LOG_ERROR("Failed to allocate bitmap");
					return false;
				}

				const double Exp = 1.0 / 2.2;
				const int Bytespp = 3;
				for (int y = 0; y < height; y++)
				{
					BYTE* bits = FreeImage_GetScanLine(tonemappedBitmap, y);
					for (int x = 0; x < width; x++)
					{
						int idx = y * width + x;
						bits[FI_RGBA_RED]   = (BYTE)(glm::clamp((int)(glm::pow((double)(film[idx].r), Exp) * 255.0), 0, 255));
						bits[FI_RGBA_GREEN] = (BYTE)(glm::clamp((int)(glm::pow((double)(film[idx].g), Exp) * 255.0), 0, 255));
						bits[FI_RGBA_BLUE]  = (BYTE)(glm::clamp((int)(glm::pow((double)(film[idx].b), Exp) * 255.0), 0, 255));
						bits += Bytespp;
					}
				}

				if (!FreeImage_Save(FIF_PNG, tonemappedBitmap, path.c_str(), PNG_DEFAULT))
				{
					NGI_LOG_ERROR("Failed to save image : " + path);
					FreeImage_Unload(tonemappedBitmap);
					return false;
				}

				NGI_LOG_INFO("Successfully saved to " + path);
				FreeImage_Unload(tonemappedBitmap);

				#pragma endregion
			}
			else
			{
				NGI_LOG_ERROR("Invalid extension: " + fsPath.extension().string());
				return false;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		return true;
	}

}

#pragma endregion

NGI_NAMESPACE_END

#endif // NANOGI_BASIC_H
