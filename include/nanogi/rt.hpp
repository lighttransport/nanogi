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
#ifndef NANOGI_RT_H
#define NANOGI_RT_H

#include <nanogi/macros.hpp>
#include <nanogi/basic.hpp>

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>

#include <yaml-cpp/yaml.h>
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>
#include <assimp/Importer.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

NGI_NAMESPACE_BEGIN

#pragma region Utility functions

namespace
{
	void OrthonormalBasis(const glm::dvec3& a, glm::dvec3& b, glm::dvec3& c)
	{
		c = glm::abs(a.x) > glm::abs(a.y) ? glm::normalize(glm::dvec3(a.z, 0, -a.x)) : glm::normalize(glm::dvec3(0, a.z, -a.y));
		b = glm::normalize(glm::cross(c, a));
	}

	glm::dvec3 ParseVec3(const YAML::Node& node)
	{
		return glm::dvec3(node[0].as<double>(), node[1].as<double>(), node[2].as<double>());
	}

	double LocalCos(const glm::dvec3& v)
	{
		return v.z;
	}

	double LocalTan(const glm::dvec3& v)
	{
		const double t = 1.0 - v.z * v.z;
		return t <= 0 ? 0 : glm::sqrt(t) / v.z;
	}

	glm::dvec3 LocalReflect(const glm::dvec3& wi)
	{
		return glm::dvec3(-wi.x, -wi.y, wi.z);
	}

	glm::dvec3 LocalRefract(const glm::dvec3& wi, double eta, double cosThetaT)
	{
		return glm::dvec3(-eta * wi.x, -eta * wi.y, cosThetaT);
	}

	glm::dvec2 UniformConcentricDiskSample(const glm::dvec2& u)
	{
		auto v = 2.0 * u - 1.0;
		if (v.x == 0 && v.y == 0) { return glm::dvec2(); }
		double r, theta;
		if (v.x > -v.y)
		{
			if (v.x > v.y) { r = v.x; theta = (Pi * 0.25) * v.y / v.x; }
			else           { r = v.y; theta = (Pi * 0.25) * (2.0 - v.x / v.y); }
		}
		else
		{
			if (v.x < v.y) { r = -v.x; theta = (Pi * 0.25) * (4.0 + v.y / v.x); }
			else           { r = -v.y; theta = (Pi * 0.25) * (6.0 - v.x / v.y); }
		}
		return glm::dvec2(r * glm::cos(theta), r * glm::sin(theta));
	}

	glm::dvec3 CosineSampleHemisphere(const glm::dvec2& u)
	{
		const auto s = UniformConcentricDiskSample(u);
		return glm::dvec3(s, glm::sqrt(glm::max(0.0, 1.0 - s.x*s.x - s.y*s.y)));
	}

	double CosineSampleHemispherePDFProjSA(const glm::dvec3& d)
	{
		return InvPi;
	}

	glm::dvec3 UniformSampleSphere(const glm::dvec2& u)
	{
		const double z = 1.0 - 2.0 * u[0];
		const double r = glm::sqrt(glm::max(0.0, 1.0 - z*z));
		const double phi = 2.0 * Pi * u[1];
		return glm::dvec3(r * glm::cos(phi), r * glm::sin(phi), z);
	}

	double UniformSampleSpherePDFSA(const glm::dvec3& d)
	{
		return InvPi * 0.25;
	}

	glm::dvec2 UniformSampleTriangle(const glm::dvec2& u)
	{
		const auto s = glm::sqrt(glm::max(0.0, u.x));
		return glm::dvec2(1.0 - s, u.y * s);
	}

	int PixelIndex(const glm::dvec2& rasterPos, int w, int h)
	{
		const int pX = glm::clamp((int)(rasterPos.x * w), 0, w - 1);
		const int pY = glm::clamp((int)(rasterPos.y * h), 0, h - 1);
		return pY * w + pX;
	}

}

#pragma endregion

#pragma region Mesh loading

struct Mesh
{
	std::vector<double> Positions;
	std::vector<double> Normals;
	std::vector<double> Texcoords;
	std::vector<unsigned int> Faces;
	void* UserData;
};

struct Texture
{

	std::string Path;
	std::vector<float> Data;
	int Width;
	int Height;
	void* UserData;

public:

	bool Load(const std::string& path)
	{
		Path = path;

		// Try to deduce the file format by the file signature
		auto format = FreeImage_GetFileType(Path.c_str(), 0);
		if (format == FIF_UNKNOWN)
		{
			// Try to deduce the file format by the extension
			format = FreeImage_GetFIFFromFilename(Path.c_str());
			if (format == FIF_UNKNOWN)
			{
				// Unknown image
				NGI_LOG_ERROR("Unknown image format");
				return false;
			}
		}

		// Check the plugin capability
		if (!FreeImage_FIFSupportsReading(format))
		{
			NGI_LOG_ERROR("Unsupported format");
			return false;
		}

		// Load image
		auto* fibitmap = FreeImage_Load(format, Path.c_str(), 0);
		if (!fibitmap)
		{
			NGI_LOG_ERROR("Failed to load an image " + Path);
			return false;
		}

		// Width and height
		Width  = FreeImage_GetWidth(fibitmap);
		Height = FreeImage_GetHeight(fibitmap);

		// Image type and bits per pixel (BPP)
		const auto type = FreeImage_GetImageType(fibitmap);
		const auto bpp = FreeImage_GetBPP(fibitmap);
		if (!(type == FIT_RGBF || type == FIT_RGBAF || (type == FIT_BITMAP && (bpp == 24 || bpp == 32))))
		{
			FreeImage_Unload(fibitmap);
			NGI_LOG_ERROR("Unsupportted format");
			return false;
		}

		// Flip the loaded image
		// Note that in FreeImage loaded image is flipped from the beginning,
		// i.e., y axis is originated from bottom-left point and grows upwards.
		FreeImage_FlipVertical(fibitmap);

		// Read image data
		Data.clear();
		for (int y = 0; y < Height; y++)
		{
			if (type == FIT_RGBF)
			{
				auto* bits = (FIRGBF*)FreeImage_GetScanLine(fibitmap, y);
				for (int x = 0; x < Width; x++)
				{
					Data.emplace_back(bits[x].red);
					Data.emplace_back(bits[x].green);
					Data.emplace_back(bits[x].blue);
				}
			}
			else if (type == FIT_RGBAF)
			{
				auto* bits = (FIRGBAF*)FreeImage_GetScanLine(fibitmap, y);
				for (int x = 0; x < Width; x++)
				{
					Data.emplace_back(bits[x].red);
					Data.emplace_back(bits[x].green);
					Data.emplace_back(bits[x].blue);
				}
			}
			else if (type == FIT_BITMAP)
			{
				BYTE* bits = (BYTE*)FreeImage_GetScanLine(fibitmap, y);
				for (int x = 0; x < Width; x++)
				{
					Data.push_back((float)(bits[FI_RGBA_RED]) / 255.0f);
					Data.push_back((float)(bits[FI_RGBA_GREEN]) / 255.0f);
					Data.push_back((float)(bits[FI_RGBA_BLUE]) / 255.0f);
					bits += bpp / 8;
				}
			}
		}

		FreeImage_Unload(fibitmap);

		return true;
	}

	glm::dvec3 Evaluate(const glm::dvec2& uv) const
	{
		const int x = glm::clamp((int)(glm::fract(uv.x) * Width), 0, Width - 1);
		const int y = glm::clamp((int)(glm::fract(uv.y) * Height), 0, Height - 1);
		const int i = Width * y + x;
		return glm::dvec3(Data[3*i], Data[3*i+1], Data[3*i+2]);
	}

};

#pragma endregion

#pragma region Ray & surface geometry

struct Ray
{
	glm::dvec3 o;
	glm::dvec3 d;
};

struct SurfaceGeometry
{

	bool degenerated;
	glm::dvec3 p;
	glm::dvec3 sn;
	glm::dvec3 gn;
	glm::dvec3 dpdu, dpdv;
	glm::dvec3 dndu, dndv;
	glm::dvec2 uv;
	glm::dmat3 ToLocal;
	glm::dmat3 ToWorld;

	void ComputeTangentSpace()
	{
		OrthonormalBasis(sn, dpdu, dpdv);
		ToWorld = glm::dmat3(dpdu, dpdv, sn);
		ToLocal = glm::transpose(ToWorld);
	}

};

#pragma endregion

#pragma region Bounding box & Sphere

struct AABB
{

	glm::dvec3 min{ Inf};
	glm::dvec3 max{-Inf};

public:

	static AABB Union(const AABB& a, const AABB& b)
	{
		AABB r;
		r.min = glm::min(a.min, b.min);
		r.max = glm::max(a.max, b.max);
		return r;
	}

	static AABB Union(const AABB& a, const glm::dvec3& p)
	{
		AABB r;
		r.min = glm::min(a.min, p);
		r.max = glm::max(a.max, p);
		return r;
	}

};

#pragma endregion

#pragma region Primitive definition

namespace PrimitiveType
{
	enum Type
	{
		D = 1 << 0,
		G = 1 << 1,
		S = 1 << 2,
		L = 1 << 3,
		E = 1 << 4,
		BSDF = D | G | S,
		Emitter = L | E,
		None = 0
	};
}

enum class LType
{
	Area,
	Point,
	Directional,
};

enum class EType
{
	Area,
	Pinhole,
};

enum class SType
{
	Reflection,
	Refraction,
	Fresnel,
};

enum class TransportDirection
{
	LE,
	EL
};

struct Primitive
{

	// Associated mesh ID
	const Mesh* MeshRef = nullptr;

	// Primitive type
	int Type = PrimitiveType::None;

	// Parameters associated with primitive
	struct
	{
		struct
		{
			LType Type;

			struct
			{
				glm::dvec3 Le;
				Distribution1D Dist;
				double InvArea;
			} Area;

			struct
			{
				glm::dvec3 Le;
				glm::dvec3 Position;
			} Point;

			struct
			{
				glm::dvec3 Le;
				glm::dvec3 Direction;
				double InvArea;
				glm::dvec3 Center;
				double Radius;
			} Directional;
		} L;

		struct
		{
			EType Type;

			struct
			{
				glm::dvec3 Position;
				glm::dvec3 Vx, Vy, Vz;
				double Fov;
				double Aspect;
				glm::dvec3 We;
			} Pinhole;

			struct
			{
				glm::dvec3 We;
				Distribution1D Dist;
				double InvArea;
			} Area;
		} E;

		struct
		{
			glm::dvec3 R;
			const Texture* TexR = nullptr;
		} D;

		struct
		{
			glm::dvec3 R;
			const Texture* TexR = nullptr;
			glm::dvec3 Eta;
			glm::dvec3 K;
			double Roughness;
		} G;

		struct
		{
			SType Type;

			struct
			{
				glm::dvec3 R;
			} Reflection;

			struct
			{
				glm::dvec3 R;
				double Eta1;
				double Eta2;
			} Refraction;

			struct
			{
				glm::dvec3 R;
				double Eta1;
				double Eta2;
			} Fresnel;
		} S;
	} Params;

public:

	#pragma region Sampling & evaluation

	void SamplePosition(const glm::dvec2& u, SurfaceGeometry& geom) const
	{
		#pragma region Utilities

		// Function to sample a position on the triangle mesh
		const auto SampleTriangleMesh = [](const glm::dvec2& u, const Mesh* mesh, const Distribution1D& dist, SurfaceGeometry& geom)
		{
			#pragma region Sample a triangle & a position on triangle

			auto u2 = u;
			const int i = dist.SampleReuse(u.x, u2.x);
			const auto b = UniformSampleTriangle(u2);

			#pragma endregion

			#pragma region Store surface geometry information

			unsigned int i1 = mesh->Faces[3 * i];
			unsigned int i2 = mesh->Faces[3 * i + 1];
			unsigned int i3 = mesh->Faces[3 * i + 2];

			// Position
			glm::dvec3 p1(mesh->Positions[3 * i1], mesh->Positions[3 * i1 + 1], mesh->Positions[3 * i1 + 2]);
			glm::dvec3 p2(mesh->Positions[3 * i2], mesh->Positions[3 * i2 + 1], mesh->Positions[3 * i2 + 2]);
			glm::dvec3 p3(mesh->Positions[3 * i3], mesh->Positions[3 * i3 + 1], mesh->Positions[3 * i3 + 2]);
			geom.p = p1 * (1.0 - b.x - b.y) + p2 * b.x + p3 * b.y;

			// UV
			if (!mesh->Texcoords.empty())
			{
				glm::dvec2 uv1(mesh->Texcoords[2 * i1], mesh->Texcoords[2 * i1 + 1]);
				glm::dvec2 uv2(mesh->Texcoords[2 * i2], mesh->Texcoords[2 * i2 + 1]);
				glm::dvec2 uv3(mesh->Texcoords[2 * i3], mesh->Texcoords[2 * i3 + 1]);
				geom.uv = uv1 * (1.0 - b.x - b.y) + uv2 * b.x + uv3 * b.y;
			}

			// Normal
			geom.degenerated = false;
			geom.gn = glm::normalize(glm::cross(p2 - p1, p3 - p1));
			geom.sn = geom.gn;
			geom.ComputeTangentSpace();

			#pragma endregion
		};

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type L

		if ((Type & PrimitiveType::L) > 0)
		{
			if (Params.L.Type == LType::Area)
			{
				SampleTriangleMesh(u, MeshRef, Params.L.Area.Dist, geom);
				return;
			}

			if (Params.L.Type == LType::Point)
			{
				geom.degenerated = true;
				geom.p = Params.L.Point.Position;
				return;
			}

			if (Params.L.Type == LType::Directional)
			{
				// Sample a point on the virtual disk
				const auto p = UniformConcentricDiskSample(u) * Params.L.Directional.Radius;

				// Position & Normals
				geom.degenerated = false;
				geom.gn = Params.L.Directional.Direction;
				geom.sn = geom.gn;
				geom.ComputeTangentSpace();
				geom.p = Params.L.Directional.Center - Params.L.Directional.Direction * Params.L.Directional.Radius + (geom.dpdu * p.x + geom.dpdv * p.y);

				return;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type E

		if ((Type & PrimitiveType::E) > 0)
		{
			if (Params.E.Type == EType::Area)
			{
				SampleTriangleMesh(u, MeshRef, Params.E.Area.Dist, geom);
				return;
			}

			if (Params.E.Type == EType::Pinhole)
			{
				geom.degenerated = true;
				geom.p = Params.E.Pinhole.Position;
				return;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
	}

	glm::dvec3 EvaluatePosition(const SurfaceGeometry& geom, bool forceDegenerated) const
	{
		#pragma region Type L

		if ((Type & PrimitiveType::L) > 0)
		{
			if (Params.L.Type == LType::Area)
			{
				return glm::dvec3(1);
			}

			if (Params.L.Type == LType::Point)
			{
				return forceDegenerated ? glm::dvec3(1) : glm::dvec3();
			}

			if (Params.L.Type == LType::Directional)
			{
				return glm::dvec3(1);
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type E

		if ((Type & PrimitiveType::E) > 0)
		{
			if (Params.E.Type == EType::Area)
			{
				return glm::dvec3(1);
			}

			if (Params.E.Type == EType::Pinhole)
			{
				return forceDegenerated ? glm::dvec3(1) : glm::dvec3();
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
		return glm::dvec3();
	}

	double EvaluatePositionPDF(const SurfaceGeometry& geom, bool forceDegenerated) const
	{
		#pragma region Type L

		if ((Type & PrimitiveType::L) > 0)
		{
			if (Params.L.Type == LType::Area)
			{
				return Params.L.Area.InvArea;
			}

			if (Params.L.Type == LType::Point)
			{
				return forceDegenerated ? 1 : 0;
			}

			if (Params.L.Type == LType::Directional)
			{
				return Params.L.Directional.InvArea;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type E

		if ((Type & PrimitiveType::E) > 0)
		{
			if (Params.E.Type == EType::Area)
			{
				return Params.E.Area.InvArea;
			}

			if (Params.E.Type == EType::Pinhole)
			{
				return forceDegenerated ? 1 : 0;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
		return 0;
	}

	void SampleDirection(const glm::dvec2& u, double uComp, int queryType, const SurfaceGeometry& geom, const glm::dvec3& wi, glm::dvec3& wo) const
	{
		#pragma region Type L

		if ((queryType & PrimitiveType::L) > 0)
		{
			if (Params.L.Type == LType::Area)
			{
				const auto localWo = CosineSampleHemisphere(u);
				wo = geom.ToWorld * localWo;
				return;
			}

			if (Params.L.Type == LType::Point)
			{
				wo = UniformSampleSphere(u);
				return;
			}

			if (Params.L.Type == LType::Directional)
			{
				wo = Params.L.Directional.Direction;
				return;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type E

		if ((queryType & PrimitiveType::E) > 0)
		{
			if (Params.E.Type == EType::Area)
			{
				const auto localWo = CosineSampleHemisphere(u);
				wo = geom.ToWorld * localWo;
				return;
			}

			if (Params.E.Type == EType::Pinhole)
			{
				const auto rasterPos = 2.0 * u - 1.0;
				const double tanFov = glm::tan(Params.E.Pinhole.Fov * 0.5);
				const auto woEye = glm::normalize(glm::dvec3(Params.E.Pinhole.Aspect * tanFov * rasterPos.x, tanFov * rasterPos.y, -1));
				wo = Params.E.Pinhole.Vx * woEye.x + Params.E.Pinhole.Vy * woEye.y + Params.E.Pinhole.Vz * woEye.z;
				return;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type D

		if ((queryType & PrimitiveType::D) > 0)
		{
			const auto localWi = geom.ToLocal * wi;
			if (LocalCos(localWi) <= 0)
			{
				return;
			}

			const auto localWo = CosineSampleHemisphere(u);
			wo = geom.ToWorld * localWo;

			return;
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type G

		if ((queryType & PrimitiveType::G) > 0)
		{
			const auto localWi = geom.ToLocal * wi;
			if (LocalCos(localWi) <= 0)
			{
				return;
			}

			const auto SampleBechmannDist = [this](const glm::dvec2& u) -> glm::dvec3
			{
				const double tanThetaHSqr = -Params.G.Roughness * Params.G.Roughness * std::log(1.0 - u[0]);
				const double cosThetaH    = 1.0 / std::sqrt(1.0 + tanThetaHSqr);
				const double cosThetaH2   = cosThetaH * cosThetaH;
				double sinThetaH = std::sqrt(std::max(0.0, 1.0 - cosThetaH2));
				double phiH = 2.0 * Pi * u[1];
				return glm::dvec3(sinThetaH * std::cos(phiH), sinThetaH * std::sin(phiH), cosThetaH);
			};

			const auto H = SampleBechmannDist(u);
			const auto localWo = -localWi - 2.0 * glm::dot(-localWi, H) * H;
			if (LocalCos(localWo) <= 0)
			{
				return;
			}

			wo = geom.ToWorld * localWo;
			return;
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type S

		if ((queryType & PrimitiveType::S) > 0)
		{
			#pragma region Reflection

			if (Params.S.Type == SType::Reflection)
			{
				const auto localWi = geom.ToLocal * wi;
				if (LocalCos(localWi) <= 0)
				{
					return;
				}

				const auto localWo = LocalReflect(localWi);
				wo = geom.ToWorld * localWo;

				return;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Refraction

			if (Params.S.Type == SType::Refraction)
			{
				const auto localWi = geom.ToLocal * wi;

				// IORs
				double etaI = Params.S.Refraction.Eta1;
				double etaT = Params.S.Refraction.Eta2;
				if (LocalCos(localWi) < 0)
				{
					std::swap(etaI, etaT);
				}

				// Compute wo & pdf
				const double wiDotN = LocalCos(localWi);
				const double eta = etaI / etaT;
				const double cosThetaTSq = 1.0 - eta * eta * (1.0 - wiDotN * wiDotN);
				if (cosThetaTSq <= 0)
				{
					// Total internal reflection
					const auto localWo = LocalReflect(localWi);
					wo = geom.ToWorld * localWo;
					return;
				}
				else
				{
					// Refraction
					const double cosThetaT = glm::sqrt(cosThetaTSq) * (wiDotN > 0 ? -1.0 : 1.0);
					const auto localWo = LocalRefract(localWi, eta, cosThetaT);
					wo = geom.ToWorld * localWo;
					return;
				}
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Fresnel

			if (Params.S.Type == SType::Fresnel)
			{
				const auto localWi = geom.ToLocal * wi;

				// IORs
				double etaI = Params.S.Fresnel.Eta1;
				double etaT = Params.S.Fresnel.Eta2;
				if (LocalCos(localWi) < 0)
				{
					std::swap(etaI, etaT);
				}

				// Fresnel term
				const double Fr = EvaluateFresnelTerm(localWi, etaI, etaT);
				if (uComp <= Fr)
				{
					// Reflection
					const auto localWo = LocalReflect(localWi);
					wo = geom.ToWorld * localWo;
				}
				else
				{
					// Refraction
					const double wiDotN = LocalCos(localWi);
					const double eta = etaI / etaT;
					const double cosThetaTSq = 1.0 - eta * eta * (1.0 - wiDotN * wiDotN);
					assert(cosThetaTSq >= 0);
					const double cosThetaT = glm::sqrt(cosThetaTSq) * (wiDotN > 0 ? -1.0 : 1.0);
					const auto localWo = LocalRefract(localWi, eta, cosThetaT);
					wo = geom.ToWorld * localWo;
				}

				return;
			}

			#pragma endregion
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
	}

	glm::dvec3 EvaluateDirection(const SurfaceGeometry& geom, int queryType, const glm::dvec3& wi, const glm::dvec3& wo, TransportDirection transDir, bool forceDegenerated) const
	{
		#pragma region Emitter type

		if ((queryType & PrimitiveType::Emitter) > 0)
		{
			#pragma region Type L

			if ((queryType & PrimitiveType::L) > 0)
			{
				if (Params.L.Type == LType::Area)
				{
					const auto localWo = geom.ToLocal * wo;
					if (LocalCos(localWo) <= 0) { return glm::dvec3(); }
					return Params.L.Area.Le;
				}

				if (Params.L.Type == LType::Point)
				{
					return Params.L.Point.Le;
				}

				if (Params.L.Type == LType::Directional)
				{
					return forceDegenerated ? Params.L.Directional.Le : glm::dvec3();
				}
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Type E

			if ((queryType & PrimitiveType::E) > 0)
			{
				if (Params.E.Type == EType::Area)
				{
					const auto localWo = geom.ToLocal * wo;
					if (LocalCos(localWo) <= 0) { return glm::dvec3(); }
					return Params.E.Area.We;
				}

				if (Params.E.Type == EType::Pinhole)
				{
					#pragma region Calculate raster position

					glm::dvec2 rasterPos;
					if (!RasterPosition(wo, geom, rasterPos))
					{
						return glm::dvec3();
					}

					#pragma endregion

					#pragma region Evaluate importance

					const auto V = glm::transpose(glm::dmat3(Params.E.Pinhole.Vx, Params.E.Pinhole.Vy, Params.E.Pinhole.Vz));
					const auto woEye = V * wo;
					const double tanFov = glm::tan(Params.E.Pinhole.Fov * 0.5);
					const double cosTheta = -LocalCos(woEye);
					const double invCosTheta = 1.0 / cosTheta;
					const double A = tanFov * tanFov * Params.E.Pinhole.Aspect * 4.0;
					return glm::dvec3(invCosTheta * invCosTheta * invCosTheta / A);

					#pragma endregion
				}
			}

			#pragma endregion
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region BSDF type

		if ((queryType & PrimitiveType::BSDF) > 0)
		{
			#pragma region Correction factor for shading normal

			const double shadingNormalCorrection = [&]() -> double
			{
				const auto localWi = geom.ToLocal * wi;
				const auto localWo = geom.ToLocal * wo;
				const double wiDotNg = glm::dot(wi, geom.gn);
				const double woDotNg = glm::dot(wo, geom.gn);
				const double wiDotNs = LocalCos(localWi);
				const double woDotNs = LocalCos(localWo);
				if (wiDotNg * wiDotNs <= 0 || woDotNg * woDotNs <= 0) { return 0; }
				if (transDir == TransportDirection::LE) { return wiDotNs * woDotNg / (woDotNs * wiDotNg); }
				return 1;
			}();

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Type D

			if ((queryType & PrimitiveType::D) > 0)
			{
				const auto localWi = geom.ToLocal * wi;
				const auto localWo = geom.ToLocal * wo;
				if (LocalCos(localWi) <= 0 || LocalCos(localWo) <= 0)
				{
					return glm::dvec3();
				}

				const auto R = Params.D.TexR ? Params.D.TexR->Evaluate(geom.uv) : Params.D.R;
				return R * InvPi * shadingNormalCorrection;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Type G

			if ((queryType & PrimitiveType::G) > 0)
			{
				const auto localWi = geom.ToLocal * wi;
				const auto localWo = geom.ToLocal * wo;
				if (LocalCos(localWi) <= 0 || LocalCos(localWo) <= 0)
				{
					return glm::dvec3();
				}

				const auto   H = glm::normalize(localWi + localWo);
				const double D = EvaluateBechmannDist(H);
				const double G = EvalauteShadowMaskingFunc(localWi, localWo, H);
				const auto   F = EvaluateFrConductor(glm::dot(localWi, H));
				const auto   R = Params.G.TexR ? Params.G.TexR->Evaluate(geom.uv) : Params.G.R;
				return R * D * G * F / (4.0 * LocalCos(localWi)) / LocalCos(localWo) * shadingNormalCorrection;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Type S

			if ((queryType & PrimitiveType::S) > 0)
			{
				if (!forceDegenerated)
				{
					return glm::dvec3();
				}

				// --------------------------------------------------------------------------------

				#pragma region Reflection

				if (Params.S.Type == SType::Reflection)
				{
					const auto localWi = geom.ToLocal * wi;
					const auto localWo = geom.ToLocal * wo;
					if (LocalCos(localWi) <= 0 || LocalCos(localWo) <= 0)
					{
						return glm::dvec3();
					}

					return Params.S.Reflection.R * shadingNormalCorrection;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Refraction

				if (Params.S.Type == SType::Refraction)
				{
					const auto localWi = geom.ToLocal * wi;

					double etaI = Params.S.Refraction.Eta1;
					double etaT = Params.S.Refraction.Eta2;
					if (LocalCos(localWi) < 0)
					{
						std::swap(etaI, etaT);
					}

					const double eta = etaI / etaT;
					const auto refrCorrection = transDir == TransportDirection::EL ? eta : 1.0;
					return Params.S.Refraction.R * shadingNormalCorrection * refrCorrection * refrCorrection;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Fresnel

				if (Params.S.Type == SType::Fresnel)
				{
					// Local directions
					const auto localWi = geom.ToLocal * wi;
					const auto localWo = geom.ToLocal * wo;

					// IORs
					double etaI = Params.S.Fresnel.Eta1;
					double etaT = Params.S.Fresnel.Eta2;
					if (LocalCos(localWi) < 0)
					{
						std::swap(etaI, etaT);
					}

					// Fresnel term
					const double Fr = EvaluateFresnelTerm(localWi, etaI, etaT);
					if (LocalCos(localWi) * LocalCos(localWo) >= 0)
					{
						// Reflection
						return Params.S.Fresnel.R * Fr * shadingNormalCorrection;
					}
					else
					{
						// Refraction
						const double eta = etaI / etaT;
						const auto refrCorrection = transDir == TransportDirection::EL ? eta : 1.0;
						return Params.S.Fresnel.R * (1.0 - Fr) * shadingNormalCorrection * refrCorrection * refrCorrection;
					}
				}

				#pragma endregion
			}

			#pragma endregion
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
		return glm::dvec3();
	}

	double EvaluateDirectionPDF(const SurfaceGeometry& geom, int queryType, const glm::dvec3& wi, const glm::dvec3& wo, bool forceDegenerated) const
	{
		#pragma region Type L

		if ((queryType & PrimitiveType::L) > 0)
		{
			if (Params.L.Type == LType::Area)
			{
				const auto localWo = geom.ToLocal * wo;
				if (LocalCos(localWo) <= 0) { return 0; }
				return CosineSampleHemispherePDFProjSA(localWo);
			}

			if (Params.L.Type == LType::Point)
			{
				return UniformSampleSpherePDFSA(wo);
			}

			if (Params.L.Type == LType::Directional)
			{
				return forceDegenerated ? 1 : 0;
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type E

		if ((queryType & PrimitiveType::E) > 0)
		{
			if (Params.E.Type == EType::Area)
			{
				const auto localWo = geom.ToLocal * wo;
				if (LocalCos(localWo) <= 0) { return 0; }
				return CosineSampleHemispherePDFProjSA(localWo);
			}

			if (Params.E.Type == EType::Pinhole)
			{
				#pragma region Calculate raster position

				glm::dvec2 rasterPos;
				if (!RasterPosition(wo, geom, rasterPos))
				{
					return 0;
				}

				#pragma endregion

				#pragma region Evaluate importance

				const auto V = glm::transpose(glm::dmat3(Params.E.Pinhole.Vx, Params.E.Pinhole.Vy, Params.E.Pinhole.Vz));
				const auto woEye = V * wo;
				const double tanFov = glm::tan(Params.E.Pinhole.Fov * 0.5);
				const double cosTheta = -LocalCos(woEye);
				const double invCosTheta = 1.0 / cosTheta;
				const double A = tanFov * tanFov * Params.E.Pinhole.Aspect * 4.0;
				return invCosTheta * invCosTheta * invCosTheta / A;

				#pragma endregion
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type D

		if ((queryType & PrimitiveType::D) > 0)
		{
			const auto localWi = geom.ToLocal * wi;
			const auto localWo = geom.ToLocal * wo;
			if (LocalCos(localWi) <= 0 || LocalCos(localWo) <= 0)
			{
				return 0;
			}

			return CosineSampleHemispherePDFProjSA(localWo);
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type G

		if ((queryType & PrimitiveType::G) > 0)
		{
			const auto localWi = geom.ToLocal * wi;
			const auto localWo = geom.ToLocal * wo;
			if (LocalCos(localWi) <= 0 || LocalCos(localWo) <= 0)
			{
				return 0;
			}

			const auto H = glm::normalize(localWi + localWo);
			const double D = EvaluateBechmannDist(H);
			return D * LocalCos(H) / (4.0 * glm::dot(localWo, H)) / LocalCos(localWo);
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Type S

		if ((queryType & PrimitiveType::S) > 0)
		{
			if (!forceDegenerated)
			{
				return 0;
			}

			// --------------------------------------------------------------------------------

			#pragma region Reflection

			if (Params.S.Type == SType::Reflection)
			{
				const auto localWi = geom.ToLocal * wi;
				const auto localWo = geom.ToLocal * wo;
				if (LocalCos(localWi) <= 0 || LocalCos(localWo) <= 0)
				{
					return 0;
				}

				return 1;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Refraction

			if (Params.S.Type == SType::Refraction)
			{
				return 1;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Fresnel

			if (Params.S.Type == SType::Fresnel)
			{
				// Local directions
				const auto localWi = geom.ToLocal * wi;
				const auto localWo = geom.ToLocal * wo;

				// IORs
				double etaI = Params.S.Fresnel.Eta1;
				double etaT = Params.S.Fresnel.Eta2;
				if (LocalCos(localWi) < 0)
				{
					std::swap(etaI, etaT);
				}

				// Fresnel term
				const double Fr = EvaluateFresnelTerm(localWi, etaI, etaT);
				if (LocalCos(localWi) * LocalCos(localWo) >= 0)
				{
					// Reflection
					return Fr;
				}
				else
				{
					// Refraction
					return 1.0 - Fr;
				}
			}

			#pragma endregion
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
		return 0;
	}

	#pragma endregion

public:

	#pragma region Type E specific functions

	bool RasterPosition(const glm::dvec3& wo, const SurfaceGeometry& geom, glm::dvec2& rasterPos) const
	{
		#pragma region Pinhole

		if (Params.E.Type == EType::Pinhole)
		{
			#pragma region Check if wo is coming from bind the camera

			const auto V = glm::transpose(glm::dmat3(Params.E.Pinhole.Vx, Params.E.Pinhole.Vy, Params.E.Pinhole.Vz));
			const auto woEye = V * wo;
			if (LocalCos(woEye) >= 0)
			{
				return false;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Calculate raster position

			// Check if #wo is outside of the screen
			const double tanFov = glm::tan(Params.E.Pinhole.Fov * 0.5);
			rasterPos = (glm::dvec2(-woEye.x / woEye.z / tanFov / Params.E.Pinhole.Aspect, -woEye.y / woEye.z / tanFov) + 1.0) * 0.5;
			if (rasterPos.x < 0 || rasterPos.x > 1 || rasterPos.y < 0 || rasterPos.y > 1)
			{
				return false;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			return true;
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Area

		if (Params.E.Type == EType::Area)
		{
			// Just return UV coordinates
			rasterPos = geom.uv;
			return true;
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		assert(0);
		return false;
	}

	#pragma endregion

private:

	#pragma region Type G specific functions

	double EvaluateBechmannDist(const glm::dvec3& H) const
	{
		if (LocalCos(H) <= 0) return 0.0;
		const double ex = LocalTan(H) / Params.G.Roughness;
		const double t1 = glm::exp(-(ex * ex));
		const double t2 = (Pi * Params.G.Roughness * Params.G.Roughness * glm::pow(LocalCos(H), 4.0));
		return t1 / t2;
	}

	double EvaluatePhongDist(const glm::dvec3& H) const
	{
		const double Coeff = std::tgamma((Params.G.Roughness + 3.0) * 0.5) / std::tgamma((Params.G.Roughness + 2.0) * 0.5) / std::sqrt(Pi);
		if (LocalCos(H) <= 0) return 0.0;
		return std::pow(LocalCos(H), Params.G.Roughness) * Coeff;
	}

	double EvalauteShadowMaskingFunc(const glm::dvec3& wi, const glm::dvec3& wo, const glm::dvec3& H) const
	{
		const double n_dot_H = LocalCos(H);
		const double n_dot_wo = LocalCos(wo);
		const double n_dot_wi = LocalCos(wi);
		const double wo_dot_H = std::abs(glm::dot(wo, H));
		const double wi_dot_H = std::abs(glm::dot(wo, H));
		return std::min(1.0, std::min(2.0 * n_dot_H * n_dot_wo / wo_dot_H, 2.0 * n_dot_H * n_dot_wi / wi_dot_H));
	}

	glm::dvec3 EvaluateFrConductor(double cosThetaI) const
	{
		const auto eta    = Params.G.Eta;
		const auto k      = Params.G.K;
		const auto tmp    = (eta*eta + k*k) * (cosThetaI * cosThetaI);
		const auto rParl2 = (tmp - (eta * (2.0 * cosThetaI)) + 1.0) / (tmp + (eta * (2.0 * cosThetaI)) + 1.0);
		const auto tmpF   = eta*eta + k*k;
		const auto rPerp2 = (tmpF - (eta * (2.0 * cosThetaI)) + cosThetaI*cosThetaI) / (tmpF + (eta * (2.0 * cosThetaI)) + cosThetaI*cosThetaI);
		return (rParl2 + rPerp2) * 0.5;
	}

	#pragma endregion

private:

	#pragma region Type S specific functions

	double EvaluateFresnelTerm(const glm::dvec3& localWi, double etaI, double etaT) const
	{
		const double wiDotN = LocalCos(localWi);
		const double eta = etaI / etaT;
		const double cosThetaTSq = 1.0 - eta * eta * (1.0f - wiDotN * wiDotN);
		if (cosThetaTSq <= 0)
		{
			return 1;
		}

		const double absCosThetaI = glm::abs(wiDotN);
		const double absCosThetaT = glm::sqrt(cosThetaTSq);
		const double rhoS = (etaI * absCosThetaI - etaT * absCosThetaT) / (etaI * absCosThetaI + etaT * absCosThetaT);
		const double rhoT = (etaI * absCosThetaT - etaT * absCosThetaI) / (etaI * absCosThetaT + etaT * absCosThetaI);

		return (rhoS * rhoS + rhoT * rhoT) * 0.5;
	}

	#pragma endregion

};

#pragma endregion

#pragma region Scene

struct Intersection
{
	SurfaceGeometry geom;
	const Primitive* Prim;
};

namespace
{
	const int AppConfigVersionMin = 3;
	const int AppConfigVersionMax = 5;
}

struct Scene
{

	RTCScene RtcScene = nullptr;
	std::unordered_map<unsigned int, size_t> RtcGeomIDToPrimitiveIndexMap;

	std::vector<std::unique_ptr<Mesh>> Meshes;
	std::vector<std::unique_ptr<Texture>> Textures;

	std::vector<std::unique_ptr<Primitive>> Primitives;
	size_t SensorPrimitiveIndex;
	std::vector<size_t> LightPrimitiveIndices;

public:

	Scene()
	{
		rtcInit(nullptr);
		rtcSetErrorFunction(EmbreeErrorHandler);
	}

	~Scene()
	{
		if (RtcScene) rtcDeleteScene(RtcScene);
		rtcExit();
	}

public:

	#pragma region Scene loading

	bool Load(const std::string& path, double aspect)
	{
		try
		{
			#pragma region Load configuration

			const auto scene = YAML::LoadFile(path.c_str());
			const auto sceneNode = scene["scene"];
			const auto basePath = boost::filesystem::path(path).parent_path();

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Check config version

			const int version = scene["version"].as<int>();
			if (version < AppConfigVersionMin || AppConfigVersionMax < version)
			{
				NGI_LOG_INFO("Invalid config version [Min " + std::to_string(AppConfigVersionMin) + ", Max " + std::to_string(AppConfigVersionMax) + ", Actual " + std::to_string(version) + "]");
				return false;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Assimp logger

			Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
			Assimp::DefaultLogger::get()->attachStream(new LogStream(LogType::Info), Assimp::Logger::Info);
			Assimp::DefaultLogger::get()->attachStream(new LogStream(LogType::Warn), Assimp::Logger::Warn);
			Assimp::DefaultLogger::get()->attachStream(new LogStream(LogType::Error), Assimp::Logger::Err);
			#if NGI_DEBUG_MODE
			Assimp::DefaultLogger::get()->attachStream(new LogStream(LogType::Debug), Assimp::Logger::Debugging);
			#endif

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Load primitives

			AABB SceneBound;

			{
				NGI_LOG_INFO("Load primitives");
				NGI_LOG_INDENTER();

				std::unordered_map<std::string, size_t> PathToTextureIndex;

				const auto primitivesNode = sceneNode["primitives"];
				for (size_t i = 0; i < primitivesNode.size(); i++)
				{
					NGI_LOG_INFO("Loading primitive");
					NGI_LOG_INDENTER();

					std::unique_ptr<Primitive> primitive(new Primitive);
					const auto& primitiveNode = primitivesNode[i];

					// --------------------------------------------------------------------------------

					#pragma region Load primitive type

					{
						NGI_LOG_INFO("Loading primitive type");
						NGI_LOG_INDENTER();

						const auto& typeNode = primitiveNode["type"];
						primitive->Type = PrimitiveType::None;

						for (size_t j = 0; j < typeNode.size(); j++)
						{
							auto s = typeNode[j].as<std::string>();
							if      (s == "D") { primitive->Type |= PrimitiveType::D; }
							else if (s == "G") { primitive->Type |= PrimitiveType::G; }
							else if (s == "S") { primitive->Type |= PrimitiveType::S; }
							else if (s == "L") { primitive->Type |= PrimitiveType::L; }
							else if (s == "E") { primitive->Type |= PrimitiveType::E; }
						}

						if (primitive->Type == PrimitiveType::None || ((primitive->Type & PrimitiveType::L) > 0 && (primitive->Type & PrimitiveType::E) > 0))
						{
							NGI_LOG_ERROR("Invalid primitive type");
							return false;
						}

						if ((primitive->Type & PrimitiveType::E) > 0)
						{
							// TODO: Check if a camera already exists
							SensorPrimitiveIndex = Primitives.size();
						}

						if ((primitive->Type & PrimitiveType::L) > 0)
						{
							LightPrimitiveIndices.push_back((int)(Primitives.size()));
						}
					}

					#pragma endregion

					// --------------------------------------------------------------------------------

					#pragma region Load mesh

					if (primitiveNode["mesh"])
					{
						NGI_LOG_INFO("Loading mesh");
						NGI_LOG_INDENTER();

						std::unique_ptr<Mesh> mesh(new Mesh);

						// --------------------------------------------------------------------------------

						#pragma region Load scene

						const auto meshNode = primitiveNode["mesh"];
						const auto postProcessNode = meshNode["postprocess"];
						const auto localPath = meshNode["path"].as<std::string>();
						const auto meshPath = basePath / localPath;

						Assimp::Importer importer;
						const aiScene* scene = importer.ReadFile(meshPath.string().c_str(), 0);

						if (!scene)
						{
							NGI_LOG_ERROR(importer.GetErrorString());
							return false;
						}

						if (scene->mNumMeshes == 0)
						{
							NGI_LOG_ERROR("No mesh is found in " + localPath);
							return false;
						}

						if (!scene->mMeshes[0]->HasNormals() && postProcessNode)
						{
							importer.ApplyPostProcessing(
								(postProcessNode["generate_normals"].as<bool>() ? aiProcess_GenNormals : 0) |
								(postProcessNode["generate_smooth_normals"].as<bool>() ? aiProcess_GenSmoothNormals : 0) |
								aiProcess_Triangulate |
								aiProcess_JoinIdenticalVertices |
								aiProcess_PreTransformVertices);
						}
						else
						{
							importer.ApplyPostProcessing(
								aiProcess_Triangulate |
								aiProcess_JoinIdenticalVertices |
								aiProcess_PreTransformVertices);
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						#pragma region Load triangle mesh

						{
							const auto* aimesh = scene->mMeshes[0];

							// --------------------------------------------------------------------------------

							#pragma region Positions and normals

							for (unsigned int i = 0; i < aimesh->mNumVertices; i++)
							{
								auto& p = aimesh->mVertices[i];
								auto& n = aimesh->mNormals[i];
								mesh->Positions.push_back(p.x);
								mesh->Positions.push_back(p.y);
								mesh->Positions.push_back(p.z);
								mesh->Normals.push_back(n.x);
								mesh->Normals.push_back(n.y);
								mesh->Normals.push_back(n.z);
								SceneBound = AABB::Union(SceneBound, glm::dvec3(p.x, p.y, p.z));
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Texture coordinates

							if (aimesh->HasTextureCoords(0))
							{
								for (unsigned int i = 0; i < aimesh->mNumVertices; i++)
								{
									auto& uv = aimesh->mTextureCoords[0][i];
									mesh->Texcoords.push_back(uv.x);
									mesh->Texcoords.push_back(uv.y);
								}
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Faces

							for (unsigned int i = 0; i < aimesh->mNumFaces; i++)
							{
								// The mesh is already triangulated
								auto& f = aimesh->mFaces[i];
								mesh->Faces.push_back(f.mIndices[0]);
								mesh->Faces.push_back(f.mIndices[1]);
								mesh->Faces.push_back(f.mIndices[2]);
							}

							#pragma endregion
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						primitive->MeshRef = mesh.get();
						Meshes.push_back(std::move(mesh));
					}

					#pragma endregion

					// --------------------------------------------------------------------------------

					#pragma region Load parameters

					// Function to create discrete distribution for sampling area light or raw sensor
					const auto CreateTriangleAreaDist = [](const Mesh* mesh, Distribution1D& dist, double& invArea)
					{
						double sumArea = 0;
						dist.Clear();
						for (size_t i = 0; i < mesh->Faces.size() / 3; i++)
						{
							unsigned int i1 = mesh->Faces[3 * i];
							unsigned int i2 = mesh->Faces[3 * i + 1];
							unsigned int i3 = mesh->Faces[3 * i + 2];
							glm::dvec3 p1(mesh->Positions[3 * i1], mesh->Positions[3 * i1 + 1], mesh->Positions[3 * i1 + 2]);
							glm::dvec3 p2(mesh->Positions[3 * i2], mesh->Positions[3 * i2 + 1], mesh->Positions[3 * i2 + 2]);
							glm::dvec3 p3(mesh->Positions[3 * i3], mesh->Positions[3 * i3 + 1], mesh->Positions[3 * i3 + 2]);
							const double area = glm::length(glm::cross(p2 - p1, p3 - p1)) * 0.5;
							dist.Add(area);
							sumArea += area;
						}
						dist.Normalize();
						invArea = 1.0 / sumArea;
					};

					{
						NGI_LOG_INFO("Loading parameters");
						NGI_LOG_INDENTER();

						const auto paramsNode = primitiveNode["params"];

						// --------------------------------------------------------------------------------

						const auto LoadTexture = [&](const std::string& texPath) -> const Texture*
						{
							size_t index;
							const auto it = PathToTextureIndex.find(texPath);
							if (it == PathToTextureIndex.end())
							{
								const size_t idx = Textures.size();
								PathToTextureIndex[texPath] = idx;
								index = idx;

								NGI_LOG_INFO("Loading texture : " + texPath);
								NGI_LOG_INDENTER();

								std::unique_ptr<Texture> texture(new Texture);
								if (!texture->Load(texPath))
								{
									return false;
								}

								Textures.push_back(std::move(texture));
							}
							else
							{
								index = (int)(it->second);
							}

							return Textures[index].get();
						};

						// --------------------------------------------------------------------------------

						#pragma region Type L

						if ((primitive->Type & PrimitiveType::L) > 0)
						{
							const auto LNode = paramsNode["L"];
							const auto type = LNode["type"].as<std::string>();

							// --------------------------------------------------------------------------------

							#pragma region Area light

							if (type == "area")
							{
								const auto areaNode = LNode["area"];

								primitive->Params.L.Type = LType::Area;
								primitive->Params.L.Area.Le = ParseVec3(areaNode["Le"]);

								// Check compatibility
								const auto* mesh = primitive->MeshRef;
								if (!mesh)
								{
									NGI_LOG_ERROR("Area light must be associated with mesh");
									return false;
								}

								// Create distribution according to triangle area
								CreateTriangleAreaDist(mesh, primitive->Params.L.Area.Dist, primitive->Params.L.Area.InvArea);
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Point light

							else if (type == "point")
							{
								const auto pointNode = LNode["point"];
								primitive->Params.L.Type = LType::Point;
								primitive->Params.L.Point.Le = ParseVec3(pointNode["Le"]);
								primitive->Params.L.Point.Position = ParseVec3(pointNode["position"]);
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Directional light

							else if (type == "directional")
							{
								const auto directionalNode = LNode["directional"];
								primitive->Params.L.Type = LType::Directional;
								primitive->Params.L.Directional.Le = ParseVec3(directionalNode["Le"]);
								primitive->Params.L.Directional.Direction = ParseVec3(directionalNode["direction"]);
							}

							#pragma endregion
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						#pragma region Type E

						if ((primitive->Type & PrimitiveType::E) > 0)
						{
							const auto ENode = paramsNode["E"];
							const auto type = ENode["type"].as<std::string>();

							// --------------------------------------------------------------------------------

							#pragma region Pinhole camera

							if (type == "pinhole")
							{
								const auto pinholeNode = ENode["pinhole"];
								const auto viewNode = pinholeNode["view"];
								const auto percpectiveNode = pinholeNode["perspective"];

								const auto Eye = ParseVec3(viewNode["eye"]);
								const auto Center = ParseVec3(viewNode["center"]);
								const auto Up = ParseVec3(viewNode["up"]);

								primitive->Params.E.Type = EType::Pinhole;
								auto& P = primitive->Params.E.Pinhole;

								P.We = ParseVec3(pinholeNode["We"]);
								P.Position = Eye;
								P.Fov = glm::radians(percpectiveNode["fov"].as<double>());
								P.Vz = glm::normalize(Eye - Center);
								P.Vx = glm::normalize(glm::cross(Up, P.Vz));
								P.Vy = glm::cross(P.Vz, P.Vx);
								P.Aspect = aspect;
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Area sensor

							else if (type == "area")
							{
								const auto areaNode = ENode["area"];

								primitive->Params.E.Type = EType::Area;
								auto& P = primitive->Params.E.Area;

								P.We = ParseVec3(areaNode["We"]);

								// Check compatibility
								const auto* mesh = primitive->MeshRef;
								if (!mesh || mesh->Texcoords.empty())
								{
									NGI_LOG_ERROR("Raw sensor must be associated with mesh with UV coordinates");
									return false;
								}

								// Create distribution according to triangle area
								CreateTriangleAreaDist(mesh, P.Dist, P.InvArea);
							}

							#pragma endregion
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						#pragma region Type D

						if ((primitive->Type & PrimitiveType::D) > 0)
						{
							const auto DNode = paramsNode["D"];
							if (DNode["R"])
							{
								primitive->Params.D.R = ParseVec3(DNode["R"]);
							}
							else if (DNode["TexR"])
							{
								const auto localTexPath = DNode["TexR"].as<std::string>();
								const auto texPath = (basePath / localTexPath).string();
								primitive->Params.D.TexR = LoadTexture(texPath);
							}
							else
							{
								return false;
							}
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						#pragma region Type G

						if ((primitive->Type & PrimitiveType::G) > 0)
						{
							const auto GNode = paramsNode["G"];
							primitive->Params.G.Eta       = ParseVec3(GNode["Eta"]);
							primitive->Params.G.K         = ParseVec3(GNode["K"]);
							primitive->Params.G.Roughness = GNode["Roughness"].as<double>();
							if (GNode["R"])
							{
								primitive->Params.G.R = ParseVec3(GNode["R"]);
							}
							else if (GNode["TexR"])
							{
								const auto localTexPath = GNode["TexR"].as<std::string>();
								const auto texPath = (basePath / localTexPath).string();
								primitive->Params.G.TexR = LoadTexture(texPath);
							}
							else
							{
								return false;
							}
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						#pragma region Type S

						if ((primitive->Type & PrimitiveType::S) > 0)
						{
							const auto SNode = paramsNode["S"];
							const auto type = SNode["type"].as<std::string>();

							// --------------------------------------------------------------------------------

							#pragma region Reflection

							if (type == "reflection")
							{
								const auto reflectionNode = SNode["reflection"];
								primitive->Params.S.Type = SType::Reflection;
								primitive->Params.S.Reflection.R = ParseVec3(reflectionNode["R"]);
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Refraction

							else if (type == "refraction")
							{
								const auto refractionNode = SNode["refraction"];
								primitive->Params.S.Type = SType::Refraction;
								primitive->Params.S.Refraction.R = ParseVec3(refractionNode["R"]);
								primitive->Params.S.Refraction.Eta1 = refractionNode["eta1"].as<double>();
								primitive->Params.S.Refraction.Eta2 = refractionNode["eta2"].as<double>();
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Fresnel

							else if (type == "fresnel")
							{
								const auto fresnelNode = SNode["fresnel"];
								primitive->Params.S.Type = SType::Fresnel;
								primitive->Params.S.Fresnel.R = ParseVec3(fresnelNode["R"]);
								primitive->Params.S.Fresnel.Eta1 = fresnelNode["eta1"].as<double>();
								primitive->Params.S.Fresnel.Eta2 = fresnelNode["eta2"].as<double>();
							}

							#pragma endregion
						}

						#pragma endregion
					}

					#pragma endregion

					// --------------------------------------------------------------------------------

					Primitives.push_back(std::move(primitive));
				}
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Post configure primitives

			for (const auto& primitive : Primitives)
			{
				#pragma region Type L

				if ((primitive->Type & PrimitiveType::L) > 0)
				{
					#pragma region Directional

					if (primitive->Params.L.Type == LType::Directional)
					{
						auto& P = primitive->Params.L.Directional;
						P.Center = (SceneBound.max + SceneBound.min) * 0.5;
						P.Radius = glm::length(primitive->Params.L.Directional.Center - SceneBound.max) * 1.01;
						P.InvArea = 1.0 / (2.0 * Pi * P.Radius * P.Radius);
					}

					#pragma endregion
				}

				#pragma endregion
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Build scene

			{
				NGI_LOG_INFO("Build scene");
				NGI_LOG_INDENTER();

				// Create scene
				RtcScene = rtcNewScene(RTC_SCENE_STATIC | RTC_SCENE_INCOHERENT, RTC_INTERSECT1);

				// Add meshes to the scene
				for (size_t i = 0; i < Primitives.size(); i++)
				{
					const auto& prim = Primitives[i];
					if (!prim->MeshRef)
					{
						continue;
					}

					const auto* mesh = prim->MeshRef;

					// Create a triangle mesh
					unsigned int geomID = rtcNewTriangleMesh(RtcScene, RTC_GEOMETRY_STATIC, mesh->Faces.size() / 3, mesh->Faces.size());
					RtcGeomIDToPrimitiveIndexMap[geomID] = i;

					// Copy vertices & faces
					auto* mappedPositions = reinterpret_cast<float*>(rtcMapBuffer(RtcScene, geomID, RTC_VERTEX_BUFFER));
					auto* mappedFaces = reinterpret_cast<int*>(rtcMapBuffer(RtcScene, geomID, RTC_INDEX_BUFFER));

					for (size_t j = 0; j < mesh->Faces.size() / 3; j++)
					{
						// Positions
						unsigned int i1 = mesh->Faces[3 * j];
						unsigned int i2 = mesh->Faces[3 * j + 1];
						unsigned int i3 = mesh->Faces[3 * j + 2];
						glm::vec3 p1(mesh->Positions[3 * i1], mesh->Positions[3 * i1 + 1], mesh->Positions[3 * i1 + 2]);
						glm::vec3 p2(mesh->Positions[3 * i2], mesh->Positions[3 * i2 + 1], mesh->Positions[3 * i2 + 2]);
						glm::vec3 p3(mesh->Positions[3 * i3], mesh->Positions[3 * i3 + 1], mesh->Positions[3 * i3 + 2]);

						// Store into mapped buffers
						int mi1 = 3 * (int)(j);
						int mi2 = 3 * (int)(j)+1;
						int mi3 = 3 * (int)(j)+2;
						mappedFaces[mi1] = mi1;
						mappedFaces[mi2] = mi2;
						mappedFaces[mi3] = mi3;
						for (int k = 0; k < 3; k++)
						{
							mappedPositions[4 * mi1 + k] = p1[k];
							mappedPositions[4 * mi2 + k] = p2[k];
							mappedPositions[4 * mi3 + k] = p3[k];
						}
					}

					rtcUnmapBuffer(RtcScene, geomID, RTC_VERTEX_BUFFER);
					rtcUnmapBuffer(RtcScene, geomID, RTC_INDEX_BUFFER);
				}

				rtcCommit(RtcScene);
			}

			#pragma endregion
		}
		catch (const YAML::Exception& e)
		{
			NGI_LOG_ERROR("YAML exception: " + std::string(e.what()));
			return false;
		}

		return true;
	}

	#pragma endregion

public:

	#pragma region Intersection

	bool Intersect(const Ray& ray, Intersection& isect, float minT, float maxT) const
	{
		// Create RTCRay
		RTCRay rtcRay;
		rtcRay.org[0] = (float)(ray.o[0]);
		rtcRay.org[1] = (float)(ray.o[1]);
		rtcRay.org[2] = (float)(ray.o[2]);
		rtcRay.dir[0] = (float)(ray.d[0]);
		rtcRay.dir[1] = (float)(ray.d[1]);
		rtcRay.dir[2] = (float)(ray.d[2]);
		rtcRay.tnear  = minT;
		rtcRay.tfar   = maxT;
		rtcRay.geomID = RTC_INVALID_GEOMETRY_ID;
		rtcRay.primID = RTC_INVALID_GEOMETRY_ID;
		rtcRay.instID = RTC_INVALID_GEOMETRY_ID;
		rtcRay.mask = 0xFFFFFFFF;
		rtcRay.time = 0;

		// Intersection query
		NGI_DISABLE_FP_EXCEPTION();
		rtcIntersect(RtcScene, rtcRay);
		NGI_ENABLE_FP_EXCEPTION();
		if ((unsigned int)(rtcRay.geomID) == RTC_INVALID_GEOMETRY_ID)
		{
			return false;
		}

		// Store information into #isect
		const size_t primIndex = RtcGeomIDToPrimitiveIndexMap.at(rtcRay.geomID);
		const int faceIndex = rtcRay.primID;
		const auto* prim = Primitives.at(primIndex).get();
		const auto* mesh = prim->MeshRef;
		isect.Prim = prim;

		// Intersection point
		isect.geom.p = ray.o + ray.d * (double)(rtcRay.tfar);

		// Geometry normal
		int v1 = mesh->Faces[3 * faceIndex];
		int v2 = mesh->Faces[3 * faceIndex + 1];
		int v3 = mesh->Faces[3 * faceIndex + 2];
		glm::dvec3 p1(mesh->Positions[3 * v1], mesh->Positions[3 * v1 + 1], mesh->Positions[3 * v1 + 2]);
		glm::dvec3 p2(mesh->Positions[3 * v2], mesh->Positions[3 * v2 + 1], mesh->Positions[3 * v2 + 2]);
		glm::dvec3 p3(mesh->Positions[3 * v3], mesh->Positions[3 * v3 + 1], mesh->Positions[3 * v3 + 2]);
		isect.geom.gn = glm::normalize(glm::cross(p2 - p1, p3 - p1));

		// Shading normal
		glm::dvec3 n1(mesh->Normals[3 * v1], mesh->Normals[3 * v1 + 1], mesh->Normals[3 * v1 + 2]);
		glm::dvec3 n2(mesh->Normals[3 * v2], mesh->Normals[3 * v2 + 1], mesh->Normals[3 * v2 + 2]);
		glm::dvec3 n3(mesh->Normals[3 * v3], mesh->Normals[3 * v3 + 1], mesh->Normals[3 * v3 + 2]);
		isect.geom.sn = glm::normalize(n1 * (double)(1.0f - rtcRay.u - rtcRay.v) + n2 * (double)(rtcRay.u) + n3 * (double)(rtcRay.v));
		if (std::isnan(isect.geom.sn.x) || std::isnan(isect.geom.sn.y) || std::isnan(isect.geom.sn.z))
		{
			// There is a case with one of n1 ~ n3 generates NaN
			// possibly a bug of mesh loader?
			isect.geom.sn = isect.geom.gn;
		}

		// Texture coordinates
		if (!mesh->Texcoords.empty())
		{
			glm::dvec2 uv1(mesh->Texcoords[2 * v1], mesh->Texcoords[2 * v1 + 1]);
			glm::dvec2 uv2(mesh->Texcoords[2 * v2], mesh->Texcoords[2 * v2 + 1]);
			glm::dvec2 uv3(mesh->Texcoords[2 * v3], mesh->Texcoords[2 * v3 + 1]);
			isect.geom.uv = uv1 * (double)(1.0f - rtcRay.u - rtcRay.v) + uv2 * (double)(rtcRay.u) + uv3 * (double)(rtcRay.v);
		}

		// Scene surface is not degenerated
		isect.geom.degenerated = false;

		// Compute tangent space
		isect.geom.ComputeTangentSpace();

		// Compute normal derivative
		const auto N = n1 * (double)(1.0f - rtcRay.u - rtcRay.v) + n2 * (double)(rtcRay.u) + n3 * (double)(rtcRay.v);
		const double NLen = glm::length(N);
		const auto dNdu = (n2 - n1) / NLen;
		const auto dNdv = (n3 - n2) / NLen;
		isect.geom.dndu = dNdu - isect.geom.sn * glm::dot(dNdu, isect.geom.sn);
		isect.geom.dndv = dNdv - isect.geom.sn * glm::dot(dNdv, isect.geom.sn);

		return true;
	}

	bool Intersect(const Ray& ray, Intersection& isect) const
	{
		return Intersect(ray, isect, EpsF, InfF);
	}

	bool Visible(const glm::dvec3& p1, const glm::dvec3& p2) const
	{
		Ray shadowRay;
		const auto p1p2  = p2 - p1;
		const auto p1p2L = glm::length(p1p2);
		shadowRay.d = p1p2 / p1p2L;
		shadowRay.o = p1;

		Intersection _;
		return !Intersect(shadowRay, _, EpsF, (float)(p1p2L) * (1.0f - EpsF));
	}

	#pragma endregion

public:

	#pragma region Error handlers

	static void EmbreeErrorHandler(const RTCError code, const char* str)
	{
		std::string error = "";
		switch (code)
		{
			case RTC_UNKNOWN_ERROR:		{ error = "RTC_UNKNOWN_ERROR";		break; }
			case RTC_INVALID_ARGUMENT:	{ error = "RTC_INVALID_ARGUMENT";	break; }
			case RTC_INVALID_OPERATION:	{ error = "RTC_INVALID_OPERATION";	break; }
			case RTC_OUT_OF_MEMORY:		{ error = "RTC_OUT_OF_MEMORY";		break; }
			case RTC_UNSUPPORTED_CPU:	{ error = "RTC_UNSUPPORTED_CPU";	break; }
			default:					{ error = "Invalid error code";		break; }
		}
		NGI_LOG_ERROR("Embree error : " + error);
	}

	class LogStream final : public Assimp::LogStream
	{
	public:

		LogStream(LogType type) : Type(type) {}

		virtual void write(const char* message) override
		{
			// Remove new line
			std::string str(message);
			str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());

			// Remove initial string
			boost::regex re("[a-zA-Z]+, +T[0-9]+: (.*)");
			str = "Assimp : " + boost::regex_replace(str, re, "$1");

			switch (Type)
			{
				case LogType::Error: { NGI_LOG_ERROR(str); break; }
				case LogType::Warn:  { NGI_LOG_WARN(str); break; }
				case LogType::Info:  { NGI_LOG_INFO(str); break; }
				case LogType::Debug: { NGI_LOG_DEBUG(str); break; }
			}
		}

	private:

		LogType Type;

	};

	#pragma endregion

public:

	#pragma region Emitter sampling function

	const Primitive* SampleEmitter(int type, double u) const
	{
		if ((type & PrimitiveType::L) > 0)
		{
			int n = static_cast<int>(LightPrimitiveIndices.size());
			int i = glm::clamp(static_cast<int>(u * n), 0, n - 1);
			return Primitives.at(LightPrimitiveIndices[i]).get();
		}

		if ((type & PrimitiveType::E) > 0)
		{
			return Primitives.at(SensorPrimitiveIndex).get();
		}

		return nullptr;
	}

	double EvaluateEmitterPDF(const Primitive* primitive) const
	{
		if ((primitive->Type & PrimitiveType::L) > 0)
		{
			int n = static_cast<int>(LightPrimitiveIndices.size());
			return 1.0 / n;
		}

		if ((primitive->Type & PrimitiveType::E) > 0)
		{
			return 1;
		}

		return 0;
	}

	#pragma endregion

};

#pragma endregion

#pragma region Utility functions for rendering

namespace
{
	double GeometryTerm(const SurfaceGeometry& geom1, const SurfaceGeometry& geom2)
	{
		auto p1p2 = geom2.p - geom1.p;
		const auto p1p2L2 = glm::dot(p1p2, p1p2);
		const auto p1p2L  = glm::sqrt(p1p2L2);
		p1p2 /= p1p2L;
		double t = 1.0;
		if (!geom1.degenerated) { t *= glm::abs(glm::dot(geom1.sn, p1p2));  }
		if (!geom2.degenerated) { t *= glm::abs(glm::dot(geom2.sn, -p1p2)); }
		return t / p1p2L2;
	}
}

#pragma endregion

NGI_NAMESPACE_END

#endif // NANOGI_RT_H