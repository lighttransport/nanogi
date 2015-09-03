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
#ifndef NANOGI_BDPT_H
#define NANOGI_BDPT_H

#include <nanogi/rt.hpp>

NGI_NAMESPACE_BEGIN

struct PathVertex
{
	int type = PrimitiveType::None;
	SurfaceGeometry geom;
	const Primitive* primitive = nullptr;
};

struct Path
{

	std::vector<PathVertex> vertices;

public:

	#pragma region BDPT path initialization

	void SampleSubpath(const Scene& scene, Random& rng, TransportDirection transDir, int maxPathVertices)
	{
		PathVertex v;
		vertices.clear();
		for (int step = 0; maxPathVertices == -1 || step < maxPathVertices; step++)
		{
			if (step == 0)
			{
				#pragma region Sample initial vertex

				// Sample an emitter
				const auto type = transDir == TransportDirection::LE ? PrimitiveType::L : PrimitiveType::E;
				const auto* emitter = scene.SampleEmitter(type, rng.Next());
				v.primitive = emitter;
				v.type = type;

				// Sample a position on the emitter
				emitter->SamplePosition(rng.Next2D(), v.geom);

				// Create a vertex
				vertices.push_back(v);

				#pragma endregion
			}
			else
			{
				#pragma region Sample intermediate vertex

				// Previous & two before vertex
				const auto* pv = &vertices.back();
				const auto* ppv = vertices.size() > 1 ? &vertices[vertices.size() - 2] : nullptr;

				// Sample a next direction
				glm::dvec3 wo;
				const auto wi = ppv ? glm::normalize(ppv->geom.p - pv->geom.p) : glm::dvec3();
				pv->primitive->SampleDirection(rng.Next2D(), rng.Next(), pv->type, pv->geom, wi, wo);
				const auto f = pv->primitive->EvaluateDirection(pv->geom, pv->type, wi, wo, transDir, true);
				if (f == glm::dvec3())
				{
					break;
				}

				// Intersection query
				Ray ray = { pv->geom.p, wo };
				Intersection isect;
				if (!scene.Intersect(ray, isect))
				{
					break;
				}

				// Set vertex information
				v.geom = isect.geom;
				v.primitive = isect.Prim;
				v.type = isect.Prim->Type & ~PrimitiveType::Emitter;

				// Path termination
				const double rrProb = 0.5;
				if (rng.Next() > rrProb)
				{
					vertices.push_back(v);
					break;
				}

				// Add a vertex
				vertices.push_back(v);

				#pragma endregion
			}
		}
	}

	bool Connect(const Scene& scene, int s, int t, const Path& subpathL, const Path& subpathE)
	{
		assert(s > 0 || t > 0);

		vertices.clear();

		if (s == 0 && t > 0)
		{
			if ((subpathE.vertices[t - 1].primitive->Type & PrimitiveType::L) == 0)
			{
				return false;
			}
			for (int i = t - 1; i >= 0; i--)
			{
				vertices.push_back(subpathE.vertices[i]);
			}
			vertices.front().type = PrimitiveType::L;
		}
		else if (s > 0 && t == 0)
		{
			if ((subpathL.vertices[s - 1].primitive->Type & PrimitiveType::E) == 0)
			{
				return false;
			}
			for (int i = 0; i < s; i++)
			{
				vertices.push_back(subpathL.vertices[i]);
			}
			vertices.back().type = PrimitiveType::E;
		}
		else
		{
			assert(s > 0 && t > 0);
			if (!scene.Visible(subpathL.vertices[s - 1].geom.p, subpathE.vertices[t - 1].geom.p))
			{
				return false;
			}
			for (int i = 0; i < s; i++)
			{
				vertices.push_back(subpathL.vertices[i]);
			}
			for (int i = t - 1; i >= 0; i--)
			{
				vertices.push_back(subpathE.vertices[i]);
			}
		}

		return true;
	}

	#pragma endregion

public:

	#pragma region BDPT path evaluation

	glm::dvec3 EvaluateContribution(const Scene& scene, int s) const
	{
		const auto Cstar = EvaluateUnweightContribution(scene, s);
		return Cstar == glm::dvec3() ? glm::dvec3() : Cstar * EvaluatePowerHeuristicsMISWeightOpt(scene, s);
	}

	double SelectionProb(int s) const
	{
		const double rrProb = 0.5;
		const int n = (int)(vertices.size());
		const int t = n - s;
		double selectionProb = 1;
		for (int i = 1; i < s - 1; i++)
		{
			selectionProb *= rrProb;
		}
		for (int i = t - 2; i >= 1; i--)
		{
			selectionProb *= rrProb;
		}
		return selectionProb;
	}

	glm::dvec2 RasterPosition() const
	{
		const auto& v = vertices[vertices.size() - 1];
		const auto& vPrev = vertices[vertices.size() - 2];
		glm::dvec2 rasterPos;
		v.primitive->RasterPosition(glm::normalize(vPrev.geom.p - v.geom.p), v.geom, rasterPos);
		return rasterPos;
	}

	glm::dvec3 EvaluateCst(int s) const
	{
		const int n = (int)(vertices.size());
		const int t = n - s;
		glm::dvec3 cst;

		if (s == 0 && t > 0)
		{
			const auto& v = vertices[0];
			const auto& vNext = vertices[1];
			cst = v.primitive->EvaluatePosition(v.geom, false) * v.primitive->EvaluateDirection(v.geom, v.type, glm::dvec3(), glm::normalize(vNext.geom.p - v.geom.p), TransportDirection::EL, false);
		}
		else if (s > 0 && t == 0)
		{
			const auto& v = vertices[n - 1];
			const auto& vPrev = vertices[n - 2];
			cst = v.primitive->EvaluatePosition(v.geom, false) * v.primitive->EvaluateDirection(v.geom, v.type, glm::dvec3(), glm::normalize(vPrev.geom.p - v.geom.p), TransportDirection::LE, false);
		}
		else if (s > 0 && t > 0)
		{
			const auto* vL = &vertices[s - 1];
			const auto* vE = &vertices[s];
			const auto* vLPrev = s - 2 >= 0 ? &vertices[s - 2] : nullptr;
			const auto* vENext = s + 1 < n ? &vertices[s + 1] : nullptr;
			const auto fsL = vL->primitive->EvaluateDirection(vL->geom, vL->type, vLPrev ? glm::normalize(vLPrev->geom.p - vL->geom.p) : glm::dvec3(), glm::normalize(vE->geom.p - vL->geom.p), TransportDirection::LE, false);
			const auto fsE = vE->primitive->EvaluateDirection(vE->geom, vE->type, vENext ? glm::normalize(vENext->geom.p - vE->geom.p) : glm::dvec3(), glm::normalize(vL->geom.p - vE->geom.p), TransportDirection::EL, false);
			const double G = GeometryTerm(vL->geom, vE->geom);
			cst = fsL * G * fsE;
		}

		return cst;
	}

	glm::dvec3 EvaluateUnweightContribution(const Scene& scene, int s) const
	{
		const int n = (int)(vertices.size());
		const int t = n - s;

		// --------------------------------------------------------------------------------

		#pragma region Function to compute local contribution

		const auto LocalContrb = [](const glm::dvec3& f, double p) -> glm::dvec3
		{
			assert(p != 0 || (p == 0 && f == glm::dvec3()));
			if (f == glm::dvec3()) return glm::dvec3();
			return f / p;
		};

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Compute alphaL

		glm::dvec3 alphaL;

		if (s == 0)
		{
			alphaL = glm::dvec3(1);
		}
		else
		{
			const auto& v = vertices[0];
			alphaL = LocalContrb(v.primitive->EvaluatePosition(v.geom, true), v.primitive->EvaluatePositionPDF(v.geom, true) * scene.EvaluateEmitterPDF(v.primitive));
			for (int i = 0; i < s - 1; i++)
			{
				const auto* v     = &vertices[i];
				const auto* vPrev = i >= 1 ? &vertices[i - 1] : nullptr;
				const auto* vNext = &vertices[i + 1];
				const auto wi = vPrev ? glm::normalize(vPrev->geom.p - v->geom.p) : glm::dvec3();
				const auto wo = glm::normalize(vNext->geom.p - v->geom.p);
				alphaL *= LocalContrb(v->primitive->EvaluateDirection(v->geom, v->type, wi, wo, TransportDirection::LE, true), v->primitive->EvaluateDirectionPDF(v->geom, v->type, wi, wo, true));
			}
		}

		if (alphaL == glm::dvec3())
		{
			return glm::dvec3();
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Compute alphaE

		glm::dvec3 alphaE;

		if (t == 0)
		{
			alphaE = glm::dvec3(1);
		}
		else
		{
			const auto& v = vertices[n - 1];
			alphaE = LocalContrb(v.primitive->EvaluatePosition(v.geom, true), v.primitive->EvaluatePositionPDF(v.geom, true) * scene.EvaluateEmitterPDF(v.primitive));
			for (int i = n - 1; i > s; i--)
			{
				const auto* v = &vertices[i];
				const auto* vPrev = &vertices[i - 1];
				const auto* vNext = i < n - 1 ? &vertices[i + 1] : nullptr;
				const auto wi = vNext ? glm::normalize(vNext->geom.p - v->geom.p) : glm::dvec3();
				const auto wo = glm::normalize(vPrev->geom.p - v->geom.p);
				alphaE *= LocalContrb(v->primitive->EvaluateDirection(v->geom, v->type, wi, wo, TransportDirection::EL, true), v->primitive->EvaluateDirectionPDF(v->geom, v->type, wi, wo, true));
			}
		}

		if (alphaE == glm::dvec3())
		{
			return glm::dvec3();
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Compute Cst

		const auto cst = EvaluateCst(s);
		if (cst == glm::dvec3())
		{
			return glm::dvec3();
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		return alphaL * cst * alphaE;
	}

	double EvaluateSimpleMISWeight(const Scene& scene, int s) const
	{
		const int n = (int)(vertices.size());
		int nonzero = 0;

		for (int i = 0; i <= n; i++)
		{
			if (EvaluatePDF(scene, i) > 0)
			{
				nonzero++;
			}
		}

		assert(nonzero != 0);
		return 1.0 / nonzero;
	}

	double EvaluatePowerHeuristicsMISWeightOpt(const Scene& scene, int s) const
	{
		double invWeight = 0;
		const int n = static_cast<int>(vertices.size());
		const double ps = EvaluatePDF(scene, s);
		assert(ps > 0);

		for (int i = 0; i <= n; i++)
		{
			const auto pi = EvaluatePDF(scene, i);
			if (pi > 0)
			{
				const auto r = pi / ps;
				invWeight += r * r;
			}
		}

		return 1.0 / invWeight;
	}

	double EvaluateMISWeight(const Scene& scene, int s) const
	{
		double piDivps;
		bool prevPDFIsZero;
		double invWeight = 1;
		const int n = static_cast<int>(vertices.size());

		const double ps = EvaluatePDF(scene, s);
		assert(ps > 0);

		piDivps = 1;
		prevPDFIsZero = false;
		for (int i = s - 1; i >= 0; i--)
		{
			if (EvaluateCst(i) == glm::dvec3())
			{
				prevPDFIsZero = true;
				continue;
			}

			if (prevPDFIsZero)
			{
				piDivps = EvaluatePDF(scene, i) / ps;
				prevPDFIsZero = false;
			}
			else
			{
				const double ratio = EvaluatePDFRatio(scene, i);
				if (ratio == 0)
				{
					break;
				}
				piDivps *= 1.0 / ratio;
			}

			invWeight += piDivps * piDivps;
		}

		piDivps = 1;
		prevPDFIsZero = false;
		for (int i = s; i < n; i++)
		{
			if (EvaluateCst(i+1) == glm::dvec3())
			{
				prevPDFIsZero = true;
				continue;
			}

			if (prevPDFIsZero)
			{
				piDivps = EvaluatePDF(scene, i) / ps;
				prevPDFIsZero = false;
			}
			else
			{
				const double ratio = EvaluatePDFRatio(scene, i);
				if (ratio == 0)
				{
					break;
				}
				piDivps *= ratio;
			}

			invWeight += piDivps * piDivps;
		}

		return 1.0 / invWeight;
	}

	double EvaluatePDFRatio(const Scene& scene, int i) const
	{
		const int n = static_cast<int>(vertices.size());

		if (i == 0)
		{
			const auto* x0 = &vertices[0];
			const auto* x1 = &vertices[1];
			const auto* x2 = n > 2 ? &vertices[2] : nullptr;
			const double G = GeometryTerm(x0->geom, x1->geom);
			const double pAx0 = x0->primitive->EvaluatePositionPDF(x0->geom, true) * scene.EvaluateEmitterPDF(x0->primitive);
			const double pDx1x0 = x1->primitive->EvaluateDirectionPDF(x1->geom, x1->type, x2 ? glm::normalize(x2->geom.p - x1->geom.p) : glm::dvec3(), glm::normalize(x0->geom.p - x1->geom.p), true);
			return pAx0 / pDx1x0 / G;
		}

		if (i == n - 1)
		{
			const auto* xnp = &vertices[n - 1];
			const auto* xnp2 = &vertices[n - 2];
			const auto* xnp3 = n > 2 ? &vertices[n - 3] : nullptr;
			const double G = GeometryTerm(xnp->geom, xnp2->geom);
			const double pAxnp = xnp->primitive->EvaluatePositionPDF(xnp->geom, true) * scene.EvaluateEmitterPDF(xnp->primitive);
			const double pDxnp2xnp = xnp2->primitive->EvaluateDirectionPDF(xnp2->geom, xnp2->type, xnp3 ? glm::normalize(xnp3->geom.p - xnp2->geom.p) : glm::dvec3(), glm::normalize(xnp->geom.p - xnp2->geom.p), true);
			return pDxnp2xnp * G / pAxnp;
		}

		{
			const auto* xi = &vertices[i];
			const auto* xin = &vertices[i + 1];
			const auto* xip = &vertices[i - 1];
			const auto* xin2 = i + 2 < n ? &vertices[i + 2] : nullptr;
			const auto* xip2 = i - 2 >= 0 ? &vertices[i - 2] : nullptr;
			const double Gxipxi = GeometryTerm(xip->geom, xi->geom);
			const double Gxinxi = GeometryTerm(xin->geom, xi->geom);
			const double pDxipxi = xip->primitive->EvaluateDirectionPDF(xip->geom, xip->type, xip2 ? glm::normalize(xip2->geom.p - xip->geom.p) : glm::dvec3(), glm::normalize(xi->geom.p - xip->geom.p), true);
			const double pDxinxi = xin->primitive->EvaluateDirectionPDF(xin->geom, xin->type, xin2 ? glm::normalize(xin2->geom.p - xin->geom.p) : glm::dvec3(), glm::normalize(xi->geom.p - xin->geom.p), true);
			return pDxipxi * Gxipxi / pDxinxi / Gxinxi;
		}
	}

	double EvaluatePDF(const Scene& scene, int s) const
	{
		// Cases with p_{s,t}(x) = 0
		// i.e. the strategy (s,t) cannot generate the path
		// This condition is equivalent to c_{s,t}(x) = 0
		if (EvaluateCst(s) == glm::dvec3())
		{
			return 0;
		}

		// Otherwise the path can be generated with the given strategy (s,t)
		// so p_{s,t} can be safely evaluated.
		double pdf = 1;
		const int n = (int)(vertices.size());
		const int t = n - s;
		if (s > 0)
		{
			pdf *= vertices[0].primitive->EvaluatePositionPDF(vertices[0].geom, true) * scene.EvaluateEmitterPDF(vertices[0].primitive);
			for (int i = 0; i < s - 1; i++)
			{
				const auto* vi = &vertices[i];
				const auto* vip = i - 1 >= 0 ? &vertices[i - 1] : nullptr;
				const auto* vin = &vertices[i + 1];
				pdf *= vi->primitive->EvaluateDirectionPDF(vi->geom, vi->type, vip ? glm::normalize(vip->geom.p - vi->geom.p) : glm::dvec3(), glm::normalize(vin->geom.p - vi->geom.p), true);
				pdf *= GeometryTerm(vi->geom, vin->geom);
			}
		}
		if (t > 0)
		{
			pdf *= vertices[n - 1].primitive->EvaluatePositionPDF(vertices[n - 1].geom, true) * scene.EvaluateEmitterPDF(vertices[n - 1].primitive);
			for (int i = n - 1; i >= s + 1; i--)
			{
				const auto* vi = &vertices[i];
				const auto* vip = &vertices[i - 1];
				const auto* vin = i + 1 < n ? &vertices[i + 1] : nullptr;
				pdf *= vi->primitive->EvaluateDirectionPDF(vi->geom, vi->type, vin ? glm::normalize(vin->geom.p - vi->geom.p) : glm::dvec3(), glm::normalize(vip->geom.p - vi->geom.p), true);
				pdf *= GeometryTerm(vi->geom, vip->geom);
			}
		}

		return pdf;
	}

	#pragma endregion

};

NGI_NAMESPACE_END

#endif // NANOGI_BDPT_H