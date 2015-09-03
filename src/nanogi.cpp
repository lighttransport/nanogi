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

// --------------------------------------------------------------------------------

#include <nanogi/macros.hpp>
#include <nanogi/basic.hpp>
#include <nanogi/rt.hpp>
#include <nanogi/bpt.hpp>

#include <boost/program_options.hpp>

#pragma warning(push)
#pragma warning(disable:4267)
#include <ctemplate/template.h>
#pragma warning(pop)
#include <eigen3/Eigen/Dense>

using namespace nanogi;

// --------------------------------------------------------------------------------

#pragma region Renderer

enum class RendererType
{
	PT,
	PTDirect,
	LT,
	LTDirect,
	BPT,
	PTMNEE,
};

const std::string RendererType_String[] =
{
	"pt",
	"ptdirect",
	"lt",
	"ltdirect",
	"bpt",
	"ptmnee",
};

NGI_ENUM_TYPE_MAP(RendererType);

struct Renderer
{

	RendererType Type;
	int NumThreads;
	long long GrainSize;
	long long ProgressUpdateInterval;
	double ProgressImageInterval;
	double ProgressImageUpdateInterval;
	std::string ProgressImageUpdateFormat;
	tbb::task_scheduler_init init{tbb::task_scheduler_init::deferred};

	struct
	{
		long long NumSamples;
		double RenderTime;
		int MaxNumVertices;
		int Width;
		int Height;

		struct
		{
			std::string SubpathImageDir;
		} BPTStrategy;
	} Params;

public:

	struct Context
	{
		int id = -1;						// Thread ID
		Random rng;							// Thread-specific RNG
		std::vector<glm::dvec3> film;		// Thread specific film
		long long processedSamples = 0;		// Temp for counting # of processed samples

		struct
		{
			Path subpathL, subpathE;		// BPT subpaths
			Path path;						// BPT fullpath
		} BPT;
	};

public:

	bool Load(const boost::program_options::variables_map& vm)
	{
		try
		{
			#pragma region Load parameters

			Type = NGI_STRING_TO_ENUM(RendererType, vm["renderer"].as<std::string>());
			Params.NumSamples = vm["num-samples"].as<long long>();
			Params.RenderTime = vm["render-time"].as<double>();
			Params.MaxNumVertices = vm["max-num-vertices"].as<int>();
			Params.Width = vm["width"].as<int>();
			Params.Height = vm["height"].as<int>();

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Renderer independent parameters

			if (vm.count("num-threads") > 0)
			{
				NumThreads = vm["num-threads"].as<int>();
			}
			else
			{
				#if NGI_DEBUG_MODE
				NumThreads = 1;
				#else
				NumThreads = 0;
				#endif
			}

			if (NumThreads <= 0)
			{
				NumThreads = static_cast<int>(std::thread::hardware_concurrency()) + NumThreads;
			}

			init.initialize(NumThreads);
			NGI_LOG_INFO("Number of threads: " + std::to_string(NumThreads));

			GrainSize = vm["grain-size"].as<long long>();
			NGI_LOG_INFO("Grain size: " + std::to_string(GrainSize));

			ProgressUpdateInterval = vm["progress-update-interval"].as<long long>();
			NGI_LOG_INFO("Progress update interval: " + std::to_string(ProgressUpdateInterval));

			ProgressImageUpdateInterval = vm["progress-image-update-interval"].as<double>();
			if (ProgressImageUpdateInterval > 0)
			{
				ProgressImageUpdateFormat = vm["progress-image-update-format"].as<std::string>();
				NGI_LOG_INFO("Progress image update interval: " + std::to_string(ProgressImageUpdateInterval));
				NGI_LOG_INFO("Progress image update format: " + ProgressImageUpdateFormat);
			}

			#pragma endregion
		}
		catch (boost::program_options::error& e)
		{
			std::cerr << "ERROR : " << e.what() << std::endl;
			return false;
		}

		return true;
	}

	void Render(const Scene& scene, std::vector<glm::dvec3>& film) const
	{
		#pragma region Random number generator

		Random initRng;
		#if NGI_DEBUG_MODE
		initRng.SetSeed(1008556906);
		#else
		initRng.SetSeed(static_cast<unsigned int>(std::time(nullptr)));
		#endif

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Rendering

		{
			NGI_ENABLE_FP_EXCEPTION();
			const auto start = std::chrono::high_resolution_clock::now();

			switch (Type)
			{
				case RendererType::PT:			{ RenderProcess(scene, initRng, film, std::bind(&Renderer::ProcessSample_PT,          this, std::placeholders::_1, std::placeholders::_2)); break; }
				case RendererType::PTDirect:	{ RenderProcess(scene, initRng, film, std::bind(&Renderer::ProcessSample_PTDirect,    this, std::placeholders::_1, std::placeholders::_2)); break; }
				case RendererType::LT:			{ RenderProcess(scene, initRng, film, std::bind(&Renderer::ProcessSample_LT,          this, std::placeholders::_1, std::placeholders::_2)); break; }
				case RendererType::LTDirect:	{ RenderProcess(scene, initRng, film, std::bind(&Renderer::ProcessSample_LTDirect,    this, std::placeholders::_1, std::placeholders::_2)); break; }
				case RendererType::BPT:			{ RenderProcess(scene, initRng, film, std::bind(&Renderer::ProcessSample_BPT,         this, std::placeholders::_1, std::placeholders::_2)); break; }
				case RendererType::PTMNEE:		{ RenderProcess(scene, initRng, film, std::bind(&Renderer::ProcessSample_PTMNEE,      this, std::placeholders::_1, std::placeholders::_2)); break; }
				default:						{ break; }
			};

			const auto end = std::chrono::high_resolution_clock::now();
			const double elapsed = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()) / 1000.0;
			NGI_LOG_INFO("Elapesed time: " + std::to_string(elapsed));
			NGI_DISABLE_FP_EXCEPTION();
		}

		#pragma endregion
	}

	using ProcessSampleFuncType = std::function<void(const Scene&, Context&)>;

	void RenderProcess(const Scene& scene, Random& initRng, std::vector<glm::dvec3>& film, const ProcessSampleFuncType& processSampleFunc) const
	{
		#pragma region Thread local storage

		tbb::enumerable_thread_specific<Context> contexts;
		std::mutex contextInitMutex;
		int currentThreadID = 0;

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Render loop

		std::atomic<long long> processedSamples(0);
		long long progressImageCount = 0;
		const auto renderStartTime = std::chrono::high_resolution_clock::now();
		auto prevImageUpdateTime = renderStartTime;
		const long long NumSamples = Params.RenderTime < 0 ? Params.NumSamples : GrainSize * 1000;

		while (true)
		{
			#pragma region Helper function

			const auto ProcessProgress = [&](Context& ctx) -> void
			{
				processedSamples += ctx.processedSamples;
				ctx.processedSamples = 0;

				if (Params.RenderTime < 0)
				{
					if (ctx.id == 0)
					{
						const double progress = (double)(processedSamples) / Params.NumSamples * 100.0;
						NGI_LOG_INPLACE(boost::str(boost::format("Progress: %.1f%%") % progress));
					}
				}
				else
				{
					if (ctx.id == 0)
					{
						const auto currentTime = std::chrono::high_resolution_clock::now();
						const double elapsed = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - renderStartTime).count()) / 1000.0;
						const double progress = elapsed / Params.RenderTime * 100.0;
						NGI_LOG_INPLACE(boost::str(boost::format("Progress: %.1f%% (%.1fs / %.1fs)") % progress % elapsed % Params.RenderTime));
					}
				}
			};

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Parallel loop

			std::atomic<bool> done(false);
			tbb::parallel_for(tbb::blocked_range<long long>(0, NumSamples, GrainSize), [&](const tbb::blocked_range<long long>& range) -> void
			{
				if (done)
				{
					return;
				}

				// --------------------------------------------------------------------------------

				#pragma region Thread local storage

				auto& ctx = contexts.local();
				if (ctx.id < 0)
				{
					std::unique_lock<std::mutex> lock(contextInitMutex);
					ctx.id = currentThreadID++;
					ctx.rng.SetSeed(initRng.NextUInt());
					ctx.film.assign(Params.Width * Params.Height, glm::dvec3());
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Sample loop

				for (long long sample = range.begin(); sample != range.end(); sample++)
				{
					// Process sample
					processSampleFunc(scene, ctx);

					// Report progress
					ctx.processedSamples++;
					if (ctx.processedSamples > ProgressUpdateInterval)
					{
						ProcessProgress(ctx);
					}
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Check termination

				if (Params.RenderTime > 0)
				{
					const auto currentTime = std::chrono::high_resolution_clock::now();
					const double elapsed = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - renderStartTime).count()) / 1000.0;
					if (elapsed > Params.RenderTime)
					{
						done = true;
					}
				}

				#pragma endregion
			});

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Add remaining processed samples

			for (auto& ctx : contexts)
			{
				ProcessProgress(ctx);
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Progress update of intermediate image

			if (ProgressImageUpdateInterval > 0)
			{
				const auto currentTime = std::chrono::high_resolution_clock::now();
				const double elapsed = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - prevImageUpdateTime).count()) / 1000.0;
				if (elapsed > ProgressImageUpdateInterval)
				{
					// Gather film data
					film.assign(Params.Width * Params.Height, glm::dvec3());
					contexts.combine_each([&](const Context& ctx)
					{
						std::transform(film.begin(), film.end(), ctx.film.begin(), film.begin(), std::plus<glm::dvec3>());
					});
					for (auto& v : film)
					{
						v *= (double)(Params.Width * Params.Height) / processedSamples;
					}

					// Output path
					progressImageCount++;
					std::string path;
					{
						namespace ct = ctemplate;
						ct::TemplateDictionary dict("dict");
						dict["count"] = boost::str(boost::format("%010d") % progressImageCount);

						std::string output;
						auto* tpl = ct::Template::StringToTemplate(ProgressImageUpdateFormat, ct::DO_NOT_STRIP);
						if (!tpl->Expand(&output, &dict))
						{
							NGI_LOG_ERROR("Failed to expand template");
							path = ProgressImageUpdateFormat;
						}
						else
						{
							path = output;
						}
					}

					// Save image
					{
						NGI_LOG_INFO("Saving progress: ");
						NGI_LOG_INDENTER();
						SaveImage(path, film, Params.Width, Params.Height);
					}

					// Update time
					prevImageUpdateTime = currentTime;
				}
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Exit condition

			if (Params.RenderTime < 0 || done)
			{
				break;
			}

			#pragma endregion
		}

		NGI_LOG_INFO("Progress: 100.0%");
		NGI_LOG_INFO(boost::str(boost::format("# of samples: %d") % processedSamples));

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Gather film data

		film.assign(Params.Width * Params.Height, glm::dvec3());
		contexts.combine_each([&](const Context& ctx)
		{
			std::transform(film.begin(), film.end(), ctx.film.begin(), film.begin(), std::plus<glm::dvec3>());
		});
		for (auto& v : film)
		{
			v *= (double)(Params.Width * Params.Height) / processedSamples;
		}

		#pragma endregion
	}

private:

	#pragma region Process sample

	void ProcessSample_PT(const Scene& scene, Context& ctx) const
	{
		#pragma region Sample a sensor

		const auto* E = scene.SampleEmitter(PrimitiveType::E, ctx.rng.Next());
		const double pdfE = scene.EvaluateEmitterPDF(E);
		assert(pdfE > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Sample a position on the sensor

		SurfaceGeometry geomE;
		E->SamplePosition(ctx.rng.Next2D(), geomE);
		const double pdfPE = E->EvaluatePositionPDF(geomE, true);
		assert(pdfPE > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Temporary variables

		auto throughput = E->EvaluatePosition(geomE, true) / pdfPE / pdfE;
		const auto* prim = E;
		int type = PrimitiveType::E;
		auto geom = geomE;
		glm::dvec3 wi;
		int pixelIndex = -1;
		int numVertices = 1;

		#pragma endregion

		// --------------------------------------------------------------------------------

		while (true)
		{
			if (Params.MaxNumVertices != -1 && numVertices >= Params.MaxNumVertices)
			{
				break;
			}

			// --------------------------------------------------------------------------------

			#pragma region Sample direction

			glm::dvec3 wo;
			prim->SampleDirection(ctx.rng.Next2D(), ctx.rng.Next(), type, geom, wi, wo);
			const double pdfD = prim->EvaluateDirectionPDF(geom, type, wi, wo, true);

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Calculate pixel index for initial vertex

			if (type == PrimitiveType::E)
			{
				#pragma region Calculate raster position

				glm::dvec2 rasterPos;
				if (!prim->RasterPosition(wo, geom, rasterPos))
				{
					break;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Pixel position

				pixelIndex = PixelIndex(rasterPos, Params.Width, Params.Height);

				#pragma endregion
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Evaluate direction

			const auto fs = prim->EvaluateDirection(geom, type, wi, wo, TransportDirection::EL, true);
			if (fs == glm::dvec3())
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update throughput

			assert(pdfD > 0);
			throughput *= fs / pdfD;

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Intersection

			// Setup next ray
			Ray ray = { geom.p, wo };

			// Intersection query
			Intersection isect;
			if (!scene.Intersect(ray, isect))
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Handle hit with light source

			if ((isect.Prim->Type & PrimitiveType::L) > 0)
			{
				// Accumulate to film
				ctx.film[pixelIndex] +=
					throughput
					* isect.Prim->EvaluateDirection(isect.geom, PrimitiveType::L, glm::dvec3(), -ray.d, TransportDirection::EL, false)
					* isect.Prim->EvaluatePosition(isect.geom, false);
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Path termination

			double rrProb = 0.5;
			if (ctx.rng.Next() > rrProb)
			{
				break;
			}
			else
			{
				throughput /= rrProb;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update information

			geom = isect.geom;
			prim = isect.Prim;
			type = isect.Prim->Type & ~PrimitiveType::Emitter;
			wi = -ray.d;
			numVertices++;

			#pragma endregion
		}
	}

	void ProcessSample_PTDirect(const Scene& scene, Context& ctx) const
	{
		#pragma region Sample a sensor

		const auto* E = scene.SampleEmitter(PrimitiveType::E, ctx.rng.Next());
		const double pdfE = scene.EvaluateEmitterPDF(E);
		assert(pdfE > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Sample a position on the sensor

		SurfaceGeometry geomE;
		E->SamplePosition(ctx.rng.Next2D(), geomE);
		const double pdfPE = E->EvaluatePositionPDF(geomE, true);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Temporary variables

		auto throughput = E->EvaluatePosition(geomE, true) / pdfPE / pdfE;
		const auto* prim = E;
		int type = PrimitiveType::E;
		auto geom = geomE;
		glm::dvec3 wi;
		int pixelIndex = -1;
		int numVertices = 1;

		#pragma endregion

		// --------------------------------------------------------------------------------

		while (true)
		{
			if (Params.MaxNumVertices != -1 && numVertices >= Params.MaxNumVertices)
			{
				break;
			}

			// --------------------------------------------------------------------------------

			#pragma region Direct light sampling

			{
				#pragma region Sample a light

				const auto* L = scene.SampleEmitter(PrimitiveType::L, ctx.rng.Next());
				const double pdfL = scene.EvaluateEmitterPDF(L);
				assert(pdfL > 0);

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Sample a position on the light

				SurfaceGeometry geomL;
				L->SamplePosition(ctx.rng.Next2D(), geomL);
				const double pdfPL = L->EvaluatePositionPDF(geomL, true);
				assert(pdfPL > 0);

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Evaluate contribution

				const auto ppL = glm::normalize(geomL.p - geom.p);
				const auto fsE = prim->EvaluateDirection(geom, type, wi, ppL, TransportDirection::EL, false);
				const auto fsL = L->EvaluateDirection(geomL, PrimitiveType::L, glm::dvec3(), -ppL, TransportDirection::LE, false);
				const auto G   = GeometryTerm(geom, geomL);
				const auto V   = scene.Visible(geom.p, geomL.p) ? 1.0 : 0.0;
				const auto LeP = L->EvaluatePosition(geomL, true);
				const auto C   = throughput * fsE * G * V * fsL * LeP / pdfL / pdfPL;

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Record to film

				if (C != glm::dvec3())
				{
					// Recompute pixel index if necessary
					int index = pixelIndex;
					if (type == PrimitiveType::E)
					{
						glm::dvec2 rasterPos;
						prim->RasterPosition(ppL, geom, rasterPos);
						index = PixelIndex(rasterPos, Params.Width, Params.Height);
					}

					// Accumulate to film
					ctx.film[index] += C;
				}

				#pragma endregion
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Sample next direction

			glm::dvec3 wo;
			prim->SampleDirection(ctx.rng.Next2D(), ctx.rng.Next(), type, geom, wi, wo);
			const double pdfD = prim->EvaluateDirectionPDF(geom, type, wi, wo, true);

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Calculate pixel index for initial vertex

			if (type == PrimitiveType::E)
			{
				glm::dvec2 rasterPos;
				if (!prim->RasterPosition(wo, geom, rasterPos)) { break; }
				pixelIndex = PixelIndex(rasterPos, Params.Width, Params.Height);
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Evaluate direction

			const auto fs = prim->EvaluateDirection(geom, type, wi, wo, TransportDirection::EL, true);
			if (fs == glm::dvec3())
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update throughput

			assert(pdfD > 0);
			throughput *= fs / pdfD;

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Intersection

			// Setup next ray
			Ray ray = { geom.p, wo };

			// Intersection query
			Intersection isect;
			if (!scene.Intersect(ray, isect))
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Path termination

			double rrProb = 0.5;
			if (ctx.rng.Next() > rrProb)
			{
				break;
			}
			else
			{
				throughput /= rrProb;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update information

			geom = isect.geom;
			prim = isect.Prim;
			type = isect.Prim->Type & ~PrimitiveType::Emitter;
			wi = -ray.d;
			numVertices++;

			#pragma endregion
		}
	}

	void ProcessSample_LT(const Scene& scene, Context& ctx) const
	{
		#pragma region Sample a light

		const auto* L = scene.SampleEmitter(PrimitiveType::L, ctx.rng.Next());
		const double pdfL = scene.EvaluateEmitterPDF(L);
		assert(pdfL > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Sample a position on the light

		SurfaceGeometry geomL;
		L->SamplePosition(ctx.rng.Next2D(), geomL);
		const double pdfPL = L->EvaluatePositionPDF(geomL, true);
		assert(pdfPL > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Temporary variables

		auto throughput = L->EvaluatePosition(geomL, true) / pdfPL / pdfL;
		const auto* prim = L;
		int type = PrimitiveType::L;
		auto geom = geomL;
		glm::dvec3 wi;
		int numVertices = 1;

		#pragma endregion

		// --------------------------------------------------------------------------------

		while (true)
		{
			if (Params.MaxNumVertices != -1 && numVertices >= Params.MaxNumVertices)
			{
				break;
			}

			// --------------------------------------------------------------------------------

			#pragma region Sample direction

			glm::dvec3 wo;
			prim->SampleDirection(ctx.rng.Next2D(), ctx.rng.Next(), type, geom, wi, wo);
			const double pdfD = prim->EvaluateDirectionPDF(geom, type, wi, wo, true);

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Evaluate direction

			const auto fs = prim->EvaluateDirection(geom, type, wi, wo, TransportDirection::LE, true);
			if (fs == glm::dvec3())
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update throughput

			assert(pdfD > 0);
			throughput *= fs / pdfD;

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Intersection

			// Setup next ray
			Ray ray = { geom.p, wo };

			// Intersection query
			Intersection isect;
			if (!scene.Intersect(ray, isect))
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Handle hit with sensor

			if ((isect.Prim->Type & PrimitiveType::E) > 0)
			{
				#pragma region Calculate raster position

				glm::dvec2 rasterPos;
				if (!isect.Prim->RasterPosition(-wo, isect.geom, rasterPos))
				{
					break;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Accumulate to film

				const int pixelIndex = PixelIndex(rasterPos, Params.Width, Params.Height);
				ctx.film[pixelIndex] +=
					throughput
					* isect.Prim->EvaluateDirection(isect.geom, PrimitiveType::E, glm::dvec3(), -ray.d, TransportDirection::LE, false)
					* isect.Prim->EvaluatePosition(isect.geom, false);

				#pragma endregion
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Path termination

			double rrProb = 0.5;
			if (ctx.rng.Next() > rrProb)
			{
				break;
			}
			else
			{
				throughput /= rrProb;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update information

			geom = isect.geom;
			prim = isect.Prim;
			type = isect.Prim->Type & ~PrimitiveType::Emitter;
			wi = -ray.d;
			numVertices++;

			#pragma endregion
		}
	}

	void ProcessSample_LTDirect(const Scene& scene, Context& ctx) const
	{
		#pragma region Sample a light

		const auto* L = scene.SampleEmitter(PrimitiveType::L, ctx.rng.Next());
		const double pdfL = scene.EvaluateEmitterPDF(L);
		assert(pdfL > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Sample a position on the light

		SurfaceGeometry geomL;
		L->SamplePosition(ctx.rng.Next2D(), geomL);
		const double pdfPL = L->EvaluatePositionPDF(geomL, true);
		assert(pdfPL > 0);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Temporary variables

		auto throughput = L->EvaluatePosition(geomL, true) / pdfPL / pdfL;
		const auto* prim = L;
		int type = PrimitiveType::L;
		auto geom = geomL;
		glm::dvec3 wi;
		int numVertices = 1;

		#pragma endregion

		// --------------------------------------------------------------------------------

		while (true)
		{
			if (Params.MaxNumVertices != -1 && numVertices >= Params.MaxNumVertices)
			{
				break;
			}

			// --------------------------------------------------------------------------------

			#pragma region Direct sensor sampling

			{
				#pragma region Sample a sensor

				const auto* E = scene.SampleEmitter(PrimitiveType::E, ctx.rng.Next());
				const double pdfE = scene.EvaluateEmitterPDF(E);
				assert(pdfE > 0);

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Sample a position on the sensor

				SurfaceGeometry geomE;
				E->SamplePosition(ctx.rng.Next2D(), geomE);
				const double pdfPE = L->EvaluatePositionPDF(geomE, true);
				assert(pdfPE > 0);

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Evaluate contribution

				const auto ppE = glm::normalize(geomE.p - geom.p);
				const auto fsL = prim->EvaluateDirection(geom, type, wi, ppE, TransportDirection::LE, false);
				const auto fsE = E->EvaluateDirection(geomE, PrimitiveType::E, glm::dvec3(), -ppE, TransportDirection::EL, false);
				const auto G   = GeometryTerm(geom, geomE);
				const auto V   = scene.Visible(geom.p, geomE.p) ? 1.0 : 0.0;
				const auto LeP = L->EvaluatePosition(geomE, true);
				const auto C   = throughput * fsL * G * V * fsE * LeP / pdfE / pdfPE;

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Record to film

				if (C != glm::dvec3())
				{
					// Pixel index
					glm::dvec2 rasterPos;
					E->RasterPosition(-ppE, geomE, rasterPos);
					int index = PixelIndex(rasterPos, Params.Width, Params.Height);

					// Accumulate to film
					ctx.film[index] += C;
				}

				#pragma endregion
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Sample next direction

			glm::dvec3 wo;
			prim->SampleDirection(ctx.rng.Next2D(), ctx.rng.Next(), type, geom, wi, wo);
			const double pdfD = prim->EvaluateDirectionPDF(geom, type, wi, wo, true);

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Evaluate direction

			const auto fs = prim->EvaluateDirection(geom, type, wi, wo, TransportDirection::LE, true);
			if (fs == glm::dvec3())
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update throughput

			assert(pdfD > 0);
			throughput *= fs / pdfD;

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Intersection

			// Setup next ray
			Ray ray = { geom.p, wo };

			// Intersection query
			Intersection isect;
			if (!scene.Intersect(ray, isect))
			{
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Path termination

			double rrProb = 0.5;
			if (ctx.rng.Next() > rrProb)
			{
				break;
			}
			else
			{
				throughput /= rrProb;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update information

			geom = isect.geom;
			prim = isect.Prim;
			type = isect.Prim->Type & ~PrimitiveType::Emitter;
			wi = -ray.d;
			numVertices++;

			#pragma endregion
		}
	}

	void ProcessSample_BPT(const Scene& scene, Context& ctx) const
	{
		#pragma region Sample subpaths

		ctx.BPT.subpathL.SampleSubpath(scene, ctx.rng, TransportDirection::LE, Params.MaxNumVertices);
		ctx.BPT.subpathE.SampleSubpath(scene, ctx.rng, TransportDirection::EL, Params.MaxNumVertices);

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Evaluate path combinations

		const int nL = static_cast<int>(ctx.BPT.subpathL.vertices.size());
		const int nE = static_cast<int>(ctx.BPT.subpathE.vertices.size());
		for (int n = 2; n <= nE + nL; n++)
		{
			if (Params.MaxNumVertices != -1 && n > Params.MaxNumVertices)
			{
				continue;
			}

			// --------------------------------------------------------------------------------

			const int minS = glm::max(0, n - nE);
			const int maxS = glm::min(nL, n);
			for (int s = minS; s <= maxS; s++)
			{
				#pragma region Connect subpaths & create fullpath

				const int t = n - s;
				if (!ctx.BPT.path.Connect(scene, s, t, ctx.BPT.subpathL, ctx.BPT.subpathE))
				{
					continue;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Evaluate contribution

				const auto C = ctx.BPT.path.EvaluateContribution(scene, s) / ctx.BPT.path.SelectionProb(s);
				if (C == glm::dvec3())
				{
					continue;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				#pragma region Accumulate to film

				ctx.film[PixelIndex(ctx.BPT.path.RasterPosition(), Params.Width, Params.Height)] += C;

				#pragma endregion
			}
		}

		#pragma endregion
	}

	void ProcessSample_PTMNEE(const Scene& scene, Context& ctx) const
	{
		Path path;

		for (int step = 0; Params.MaxNumVertices == -1 || step < Params.MaxNumVertices - 1; step++)
		{
			if (step == 0)
			{
				#pragma region Sample initial vertex

				PathVertex v;

				// Sample an emitter
				const auto* emitter = scene.SampleEmitter(PrimitiveType::E, ctx.rng.Next());
				v.primitive = emitter;
				v.type = PrimitiveType::E;

				// Sample a position on the emitter
				emitter->SamplePosition(ctx.rng.Next2D(), v.geom);

				// Create a vertex
				path.vertices.push_back(v);

				#pragma endregion
			}
			else
			{
				#pragma region Sample intermediate vertex

				// Previous & two before vertex
				const auto* pv = &path.vertices.back();
				const auto* ppv = path.vertices.size() > 1 ? &path.vertices[path.vertices.size() - 2] : nullptr;

				// Sample a next direction
				glm::dvec3 wo;
				const auto wi = ppv ? glm::normalize(ppv->geom.p - pv->geom.p) : glm::dvec3();
				pv->primitive->SampleDirection(ctx.rng.Next2D(), ctx.rng.Next(), pv->type, pv->geom, wi, wo);

				// Intersection query
				Ray ray = { pv->geom.p, wo };
				Intersection isect;
				if (!scene.Intersect(ray, isect))
				{
					break;
				}

				// Set vertex information
				PathVertex v;
				v.geom = isect.geom;
				v.primitive = isect.Prim;
				v.type = isect.Prim->Type & ~PrimitiveType::Emitter;
				path.vertices.push_back(v);

				#pragma endregion
			}

			// --------------------------------------------------------------------------------

			#pragma region Apply NEE or MNEE
				
			if ((path.vertices.back().type & (PrimitiveType::D | PrimitiveType::E)) > 0)
			{
				#pragma region Sample a seed path
					
				const auto SampleSeedPath = [&scene, &ctx](const Path& path, Path& seedPath) -> bool
				{
					seedPath.vertices.clear();

					// --------------------------------------------------------------------------------

					#pragma region Sample a light

					PathVertex vL;
					{
						// Sample a light
						const auto* L = scene.SampleEmitter(PrimitiveType::L, ctx.rng.Next());

						// Sample a position on the light (x_c in the paper)
						SurfaceGeometry geomL;
						L->SamplePosition(ctx.rng.Next2D(), geomL);

						vL.geom = geomL;
						vL.primitive = L;
						vL.type = PrimitiveType::L;
					}

					#pragma endregion

					// --------------------------------------------------------------------------------

					#pragma region Count the number of specular surfaces between x_b and x_c

					int countS = 0;
					{
						auto currP = path.vertices.back().geom.p;	
						while (true)
						{
							// Intersection query
							Ray ray = { currP, glm::normalize(vL.geom.p - currP) };
							Intersection isect;
							if (!scene.Intersect(ray, isect, EpsF, (1.0f - EpsF) * (float)(glm::length(vL.geom.p - currP))))
							{
								break;
							}

							// --------------------------------------------------------------------------------

							// If a vertex with non-specular surface, stop the chain
							if ((isect.Prim->Type & PrimitiveType::S) == 0)
							{
								return false;
							}

							// --------------------------------------------------------------------------------

							// Update information
							countS++;
							currP = isect.geom.p;
						}
					}

					// If countS is zero, it is the case with NEE
					if (countS == 0)
					{
						seedPath.vertices.push_back(vL);
						return true;
					}

					#pragma endregion

					// --------------------------------------------------------------------------------

					#pragma region Projection to specular manifold

					seedPath.vertices.push_back(vL);
					for (int i = 0; i < countS + 1; i++)
					{
						// Previous & two before vertex
						const auto* pv  = &seedPath.vertices.back();
						const auto* ppv = seedPath.vertices.size() > 1 ? &seedPath.vertices[seedPath.vertices.size() - 2] : nullptr;

						// --------------------------------------------------------------------------------

						// Next direction
						glm::dvec3 wo;
						if (ppv)
						{
							assert(pv->type == PrimitiveType::S);
							const auto wi = glm::normalize(ppv->geom.p - pv->geom.p);
							pv->primitive->SampleDirection(glm::dvec2(), 0, pv->type, pv->geom, wi, wo);
						}
						else
						{
							// Initial direction is fixed to x_c to x_b
							wo = glm::normalize(path.vertices.back().geom.p - vL.geom.p);
						}
			
						// --------------------------------------------------------------------------------

						// Intersection query
						Ray ray = { pv->geom.p, wo };
						Intersection isect;
						if (!scene.Intersect(ray, isect))
						{
							return false;
						}

						// --------------------------------------------------------------------------------

						if (i == countS)
						{
							// Failed if the last vertex is not 'D'
							if ((isect.Prim->Type & PrimitiveType::D) == 0) { return false; }
						}
						else
						{
							// Failed if 'S' is not found
							if ((isect.Prim->Type & PrimitiveType::S) == 0) { return false; }
						}

						// --------------------------------------------------------------------------------

						// Add a vertex
						PathVertex v;
						v.geom = isect.geom;
						v.primitive = isect.Prim;
						v.type = isect.Prim->Type & ~PrimitiveType::Emitter;
						seedPath.vertices.push_back(v);
					}

					// Number of vertices must be countS + 2
					assert(seedPath.vertices.size() == countS + 2);

					#pragma endregion

					// --------------------------------------------------------------------------------

					return true;
				};

				Path seedPath;
				if (!SampleSeedPath(path, seedPath))
				{
					continue;
				}

				assert(seedPath.vertices.size() >= 1);

				if (seedPath.vertices.size() > 1 && (int)(path.vertices.size() + seedPath.vertices.size() - 1) > Params.MaxNumVertices)
				{
					continue;
				}

				#pragma endregion

				// --------------------------------------------------------------------------------

				if (seedPath.vertices.size() == 1)
				{
					#pragma region Case with NEE

					Path evalPath = path;
					evalPath.vertices.push_back(seedPath.vertices[0]);
					std::reverse(evalPath.vertices.begin(), evalPath.vertices.end());
					ctx.film[PixelIndex(evalPath.RasterPosition(), Params.Width, Params.Height)] += evalPath.EvaluateUnweightContribution(scene, 1);

					#pragma endregion
				}
				else
				{
					#pragma region Case with MNEE

					{
						#pragma region Manifold walk

						Path optPath;
						if (!WalkManifold(scene, seedPath, path.vertices.back().geom.p, optPath))
						{
							continue;
						}

						Path revOptPath;
						if (!WalkManifold(scene, optPath, seedPath.vertices.back().geom.p, revOptPath))
						{
							continue;
						}

						#pragma endregion

						// --------------------------------------------------------------------------------

						#pragma region Evaluate contribution

						{
							#pragma region Compute throughput

							glm::dvec3 throughputE;
							{
								const auto LocalContrb = [](const glm::dvec3& f, double p) -> glm::dvec3
								{
									assert(p != 0 || (p == 0 && f == glm::dvec3()));
									if (f == glm::dvec3()) return glm::dvec3();
									return f / p;
								};

								const auto& v = path.vertices[0];
								throughputE = LocalContrb(v.primitive->EvaluatePosition(v.geom, true), v.primitive->EvaluatePositionPDF(v.geom, true) * scene.EvaluateEmitterPDF(v.primitive));
								for (size_t i = 0; i < path.vertices.size() - 1; i++)
								{
									const auto* v = &path.vertices[i];
									const auto* vPrev = i >= 1 ? &path.vertices[i - 1] : nullptr;
									const auto* vNext = &path.vertices[i + 1];
									const auto wi = vPrev ? glm::normalize(vPrev->geom.p - v->geom.p) : glm::dvec3();
									const auto wo = glm::normalize(vNext->geom.p - v->geom.p);
									throughputE *= LocalContrb(v->primitive->EvaluateDirection(v->geom, v->type, wi, wo, TransportDirection::EL, true), v->primitive->EvaluateDirectionPDF(v->geom, v->type, wi, wo, true));
								}
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Compute Fs, fsE, fsL, LeP

							glm::dvec3 Fs(1);
							{
								const int n = (int)(optPath.vertices.size());
								for (int i = n-2; i >= 1; i--)
								{
									const auto& v  = &optPath.vertices[i];
									const auto& vp = &optPath.vertices[i + 1];
									const auto& vn = &optPath.vertices[i - 1];
									Fs *= v->primitive->EvaluateDirection(v->geom, v->type, glm::normalize(vp->geom.p - v->geom.p), glm::normalize(vn->geom.p - v->geom.p), TransportDirection::EL, true);
								}
							}

							glm::dvec3 fsE;
							{
								const auto& vE  = path.vertices[path.vertices.size()-1];
								const auto& vEp = path.vertices[path.vertices.size()-2];
								const auto& vEn = optPath.vertices[optPath.vertices.size()-2];
								fsE = vE.primitive->EvaluateDirection(vE.geom, vE.type, glm::normalize(vEp.geom.p - vE.geom.p), glm::normalize(vEn.geom.p - vE.geom.p), TransportDirection::EL, true);
							}

							glm::dvec3 fsL;
							{
								const auto& vL = optPath.vertices[0];
								const auto& vLn = optPath.vertices[1];
								fsL = vL.primitive->EvaluateDirection(vL.geom, vL.type, glm::dvec3(), glm::normalize(vLn.geom.p - vL.geom.p), TransportDirection::LE, true);
							}

							glm::dvec3 LeP;
							{
								const auto& vL = optPath.vertices[0];
								LeP = vL.primitive->EvaluatePosition(vL.geom, true);
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Compute Jacobian

							double J = 1;
							{
								const int n = (int)(optPath.vertices.size());
								
								ConstraintJacobian nablaC(n - 2);
								ComputeConstraintJacobian(optPath, nablaC);
								const double Det = ComputeConstraintJacobianDeterminant(nablaC);
								J *= Det;

								const double G = GeometryTerm(optPath.vertices[0].geom, optPath.vertices[1].geom);
								J *= G;
							}

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Compute PDFs

							double pdfL;
							double pdfPL;
							{
								const auto& vL = optPath.vertices[0];
								pdfL = scene.EvaluateEmitterPDF(vL.primitive);
								pdfPL = vL.primitive->EvaluatePositionPDF(vL.geom, true);
							}

							assert(pdfL > 0);
							assert(pdfPL > 0);

							#pragma endregion

							// --------------------------------------------------------------------------------

							#pragma region Compute contribution & Accumulate to film

							// Contribution
							const auto C = throughputE * fsE * Fs * fsL * LeP * J / pdfL / pdfPL;

							// Pixel index
							int index;
							{
								glm::dvec2 rasterPos;
								const auto& vE  = path.vertices[0];
								const auto& vEn = path.vertices[1];
								vE.primitive->RasterPosition(glm::normalize(vEn.geom.p - vE.geom.p), vE.geom, rasterPos);
								index = PixelIndex(rasterPos, Params.Width, Params.Height);
							}

							// Accumulate to film
							ctx.film[index] += C;

							#pragma endregion
						}

						#pragma endregion
					}

					#pragma endregion
				}
			}

			#pragma endregion
		}
		
	}

	#pragma endregion

private:

	#pragma region MNEE specific functions

	struct VertexConstraintJacobian
	{
		glm::dmat2 A;
		glm::dmat2 B;
		glm::dmat2 C;
	};

	typedef std::vector<VertexConstraintJacobian> ConstraintJacobian;

	void ComputeConstraintJacobian(const Path& path, ConstraintJacobian& nablaC) const
	{
		const int n = (int)(path.vertices.size());
		for (int i = 1; i < n - 1; i++)
		{
			#pragma region Some precomputation

			const auto& x  = path.vertices[i].geom;
			const auto& xp = path.vertices[i-1].geom;
			const auto& xn = path.vertices[i+1].geom;
			
			const auto wi = glm::normalize(xp.p - x.p);
			const auto wo = glm::normalize(xn.p - x.p);
			const auto H  = glm::normalize(wi + wo);

			const double inv_wiL = 1.0 / glm::length(xp.p - x.p);
			const double inv_woL = 1.0 / glm::length(xn.p - x.p);
			const double inv_HL  = 1.0 / glm::length(wi + wo);
			
			const double dot_H_n    = glm::dot(x.sn, H);
			const double dot_H_dndu = glm::dot(x.dndu, H);
			const double dot_H_dndv = glm::dot(x.dndv, H);
			const double dot_u_n    = glm::dot(x.dpdu, x.sn);
			const double dot_v_n    = glm::dot(x.dpdv, x.sn);

			const auto s = x.dpdu - dot_u_n * x.sn;
			const auto t = x.dpdv - dot_v_n * x.sn;

			const double div_inv_wiL_HL = inv_wiL * inv_HL;
			const double div_inv_woL_HL = inv_woL * inv_HL;

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Compute $A_i$ (derivative w.r.t. $x_{i-1}$)
			
			{
				const auto tu = (xp.dpdu - wi * glm::dot(wi, xp.dpdu)) * div_inv_wiL_HL;
				const auto tv = (xp.dpdv - wi * glm::dot(wi, xp.dpdv)) * div_inv_wiL_HL;
				const auto dHdu = tu - H * glm::dot(tu, H);
				const auto dHdv = tv - H * glm::dot(tv, H);
				nablaC[i-1].A = glm::dmat2(
					glm::dot(dHdu, s), glm::dot(dHdu, t),
					glm::dot(dHdv, s), glm::dot(dHdv, t));
			}

			#pragma endregion
			
			// --------------------------------------------------------------------------------

			#pragma region Compute $B_i$ (derivative w.r.t. $x_i$)

			{
				const auto tu = -x.dpdu * (div_inv_wiL_HL + div_inv_woL_HL) + wi * (glm::dot(wi, x.dpdu) * div_inv_wiL_HL) + wo * (glm::dot(wo, x.dpdu) * div_inv_woL_HL);
				const auto tv = -x.dpdv * (div_inv_wiL_HL + div_inv_woL_HL) + wi * (glm::dot(wi, x.dpdv) * div_inv_wiL_HL) + wo * (glm::dot(wo, x.dpdv) * div_inv_woL_HL);
				const auto dHdu = tu - H * glm::dot(tu, H);
				const auto dHdv = tv - H * glm::dot(tv, H);
				nablaC[i-1].B = glm::dmat2(
					glm::dot(dHdu, s) - glm::dot(x.dpdu, x.dndu) * dot_H_n - dot_u_n * dot_H_dndu,
					glm::dot(dHdu, t) - glm::dot(x.dpdv, x.dndu) * dot_H_n - dot_v_n * dot_H_dndu,
					glm::dot(dHdv, s) - glm::dot(x.dpdu, x.dndv) * dot_H_n - dot_u_n * dot_H_dndv,
					glm::dot(dHdv, t) - glm::dot(x.dpdv, x.dndv) * dot_H_n - dot_v_n * dot_H_dndv);
			}

			#pragma endregion
			
			// --------------------------------------------------------------------------------

			#pragma region Compute $C_i$ (derivative w.r.t. $x_{i+1}$)

			{
				const auto tu = (xn.dpdu - wo * glm::dot(wo, xn.dpdu)) * div_inv_woL_HL;
				const auto tv = (xn.dpdv - wo * glm::dot(wo, xn.dpdv)) * div_inv_woL_HL;
				const auto dHdu = tu - H * glm::dot(tu, H);
				const auto dHdv = tv - H * glm::dot(tv, H);
				nablaC[i - 1].C = glm::dmat2(
					glm::dot(dHdu, s), glm::dot(dHdu, t),
					glm::dot(dHdv, s), glm::dot(dHdv, t));
			}

			#pragma endregion
		}
	}

	double ComputeConstraintJacobianDeterminant(const ConstraintJacobian& nablaC) const
	{
		const int n = (int)(nablaC.size());

		// $A$
		Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> A(2*n, 2*n);
		A.setZero();
		for (int i = 0; i < n; i++)
		{
			if (i > 0)
			{
				A(2*i+0, 2*(i-1)+0) = nablaC[i].A[0][0];
				A(2*i+0, 2*(i-1)+1) = nablaC[i].A[1][0];
				A(2*i+1, 2*(i-1)+0) = nablaC[i].A[0][1];
				A(2*i+1, 2*(i-1)+1) = nablaC[i].A[1][1];
			}

			A(2*i+0, 2*i+0) = nablaC[i].B[0][0];
			A(2*i+0, 2*i+1) = nablaC[i].B[1][0];
			A(2*i+1, 2*i+0) = nablaC[i].B[0][1];
			A(2*i+1, 2*i+1) = nablaC[i].B[1][1];

			if (i < n - 1)
			{
				A(2*i+0, 2*(i+1)+0) = nablaC[i+1].C[0][0];
				A(2*i+0, 2*(i+1)+1) = nablaC[i+1].C[1][0];
				A(2*i+1, 2*(i+1)+0) = nablaC[i+1].C[0][1];
				A(2*i+1, 2*(i+1)+1) = nablaC[i+1].C[1][1];
			}
		}

		// $A^-1$
		const decltype(A) invA = A.inverse();
		
		// $P_2 A^-1 B_{n}$
		const auto Bn_n1p = nablaC[n - 1].C;
		glm::dmat2 invA_0_n1p;
		invA_0_n1p[0][0] = invA(0, 2*n-2);
		invA_0_n1p[0][1] = invA(1, 2*n-2);
		invA_0_n1p[1][0] = invA(0, 2*n-1);
		invA_0_n1p[1][1] = invA(1, 2*n-1);
		
		return glm::determinant(invA_0_n1p * Bn_n1p);
	}

	void SolveBlockLinearEq(const ConstraintJacobian& nablaC, const std::vector<glm::dvec2>& V, std::vector<glm::dvec2>& W) const
	{
		const int n = (int)(nablaC.size());
		assert(V.size() == nablaC.size());
		
		// --------------------------------------------------------------------------------

		#pragma region LU decomposition

		// A'_{0,n-1} = B_{0,n-1}
		// B'_{0,n-2} = C_{0,n-2}
		// C'_{0,n-2} = A_{1,n-1}

		std::vector<glm::dmat2> L(n);
		std::vector<glm::dmat2> U(n);
		{
			// $U_1 = A'_1$
			U[0] = nablaC[0].B;
			for (int i = 1; i < n; i++)
			{
				L[i] = nablaC[i].A * glm::inverse(U[i-1]);		// $L_i = C'_i U_{i-1}^-1$
				U[i] = nablaC[i].B - L[i] * nablaC[i-1].C;		// $U_i = A'_i - L_i * B'_{i-1}$
			}
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Forward substitution
 
		// Solve $L V' = V$
		std::vector<glm::dvec2> Vp(n);
		Vp[0] = V[0];
		for (int i = 1; i < n; i++)
		{
			// V'_i = V_i - L_i V'_{i-1}
			Vp[i] = V[i] - L[i] * Vp[i - 1];
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Backward substitution

		W.assign(n, glm::dvec2());

		// Solve $U_n W_n = V'_n$
		W[n - 1] = glm::inverse(U[n - 1]) * Vp[n - 1];

		for (int i = n - 2; i >= 0; i--)
		{
			// Solve $U_i W_i = V'_i - V_i W_{i+1}$
			W[i] = glm::inverse(U[i]) * (Vp[i] - V[i] * W[i + 1]);
		}

		#pragma endregion
	}

	bool WalkManifold(const Scene& scene, const Path& seedPath, const glm::dvec3& target, Path& outPath) const
	{
		#pragma region Preprocess

		// Number of path vertices
		const int n = (int)(seedPath.vertices.size());

		// Initial path
		Path currPath = seedPath;

		// Compute $\nabla C$
		ConstraintJacobian nablaC(n - 2);
		ComputeConstraintJacobian(currPath, nablaC);

		// Compute $L$
		double L = 0;
		for (const auto& x : currPath.vertices)
		{
			L = glm::max(L, glm::length(x.geom.p));
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Optimization loop

		int iter = 0;
		const double MaxBeta = 100.0;
		double beta = MaxBeta;
		const double Eps = 10e-5;
		const int MaxIter = 30;
		bool converged = false;

		while (true)
		{
			#pragma region Stop condition

			if (iter++ >= MaxIter)
			{
				break;
			}

			if (glm::length(currPath.vertices[n - 1].geom.p - target) < Eps * L)
			{
				converged = true;
				break;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Compute movement in tangement plane

			// New position of initial specular vertex
			glm::dvec3 p;
			{
				// $x_n$, $x'_n$
				const auto& xn = currPath.vertices[n - 1].geom.p;
				const auto& xnp = target;

				// $T(x_n)^T$
				const glm::dmat3x2 TxnT = glm::transpose(glm::dmat2x3(currPath.vertices[n - 1].geom.dpdu, currPath.vertices[n - 1].geom.dpdv));

				// $V \equiv B_n T(x_n)^T (x'_n - x)$
				const auto Bn_n2p = nablaC[n - 3].C;
				const auto V_n2p = Bn_n2p * TxnT * (xnp - xn);

				// Solve $AW = V$
				std::vector<glm::dvec2> V(n - 2);
				std::vector<glm::dvec2> W(n - 2);
				for (int i = 0; i < n - 2; i++) { V[i] = i == n - 3 ? V_n2p : glm::dvec2(); }
				SolveBlockLinearEq(nablaC, V, W);

				// $x_2$, $T(x_2)$
				const auto& x2 = currPath.vertices[1].geom.p;
				const glm::dmat2x3 Tx2(currPath.vertices[1].geom.dpdu, currPath.vertices[1].geom.dpdv);

				// $W_{n-2} = P_2 W$
				const auto Wn2p = W[n - 3];

				// $p = x_2 - \beta T(x_2) P_2 W_{n-2}$
				p = x2 - beta * Tx2 * Wn2p;
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Propagate light path to $p - x1$

			bool fail = false;

			Path nextPath;

			// Initial vertex
			nextPath.vertices.push_back(currPath.vertices[0]);

			for (int i = 0; i < n - 1; i++)
			{
				// Current vertex & previous vertex
				const auto* v  = &nextPath.vertices[i];
				const auto* vp = i > 0 ? &nextPath.vertices[i-1] : nullptr;

				// Next ray direction
				glm::dvec3 wo;
				if (i == 0)
				{
					wo = glm::normalize(p - currPath.vertices[0].geom.p);
				}
				else
				{
					v->primitive->SampleDirection(glm::dvec2(), 0, v->type, v->geom, glm::normalize(vp->geom.p - v->geom.p), wo);
				}

				// Intersection query
				Ray ray = { v->geom.p, wo };
				Intersection isect;
				if (!scene.Intersect(ray, isect))
				{
					fail = true;
					break;
				}

				// Fails if not intersected with specular vertex
				if (i < n - 2 && (isect.Prim->Type & PrimitiveType::S) == 0)
				{
					fail = true;
					break;
				}

				// Create a new vertex
				PathVertex vn;
				vn.geom = isect.geom;
				vn.type = isect.Prim->Type;
				vn.primitive = isect.Prim;
				nextPath.vertices.push_back(vn);
			}
			
			if (!fail)
			{
				if (nextPath.vertices.size() != currPath.vertices.size())
				{
					// # of vertices is different
					fail = true;
				}
				else if ((nextPath.vertices.back().type & PrimitiveType::D) == 0)
				{
					// Last vertex type is not D
					fail = true;
				}
				else
				{
					// Larger difference
					const auto d  = glm::length2(currPath.vertices.back().geom.p - target);
					const auto dn = glm::length2(nextPath.vertices.back().geom.p - target);
					if (dn >= d)
					{
						fail = true;
					}
				}
			}

			#pragma endregion

			// --------------------------------------------------------------------------------

			#pragma region Update beta

			if (fail)
			{
				beta *= 0.5;
			}
			else
			{
				beta = glm::min(MaxBeta, beta * 1.7);
				//beta = glm::min(MaxBeta, beta * 2.0);
				currPath = nextPath;
			}

			#pragma endregion
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		outPath = currPath;
		assert(seedPath.vertices.size() == outPath.vertices.size());

		return converged;
	}

	#pragma endregion

};

#pragma endregion

// --------------------------------------------------------------------------------

bool Run(int argc, char** argv)
{
	#pragma region Parse arguments

	namespace po = boost::program_options;

	// Define options
	po::options_description opt("Allowed options");
	opt.add_options()
		("help", "Display help message")
		("scene,i", po::value<std::string>(), "Scene file")
		("result,o", po::value<std::string>()->default_value("render.hdr"), "Rendered result")
		("renderer,r", po::value<std::string>()->required(), "Rendering technique")
		("num-samples,n", po::value<long long>()->default_value(10000000L), "Number of samples")
		("max-num-vertices,m", po::value<int>()->default_value(-1), "Maximum number of vertices")
		("width,w", po::value<int>()->default_value(1280), "Width of the rendered image")
		("height,h", po::value<int>()->default_value(720), "Height of the rendered image")
		("num-threads,j", po::value<int>(), "Number of threads")
		#if NGI_DEBUG_MODE
		("grain-size", po::value<long long>()->default_value(10), "Grain size")
		#else
		("grain-size", po::value<long long>()->default_value(10000), "Grain size")
		#endif
		("progress-update-interval", po::value<long long>()->default_value(100000), "Progress update interval")
		("render-time,t", po::value<double>()->default_value(-1), "Render time in seconds (-1 to use # of samples)")
		("progress-image-update-interval", po::value<double>()->default_value(-1), "Progress image update interval (-1: disable)")
		("progress-image-update-format", po::value<std::string>()->default_value("progress/{{count}}.png"), "Progress image update format string \n - {{count}}: image count");

	// positional arguments
	po::positional_options_description p;
	p.add("renderer", 1).add("scene", 1).add("result", 1).add("width", 1).add("height", 1);

	// Parse options
	po::variables_map vm;
	try
	{
		po::store(po::command_line_parser(argc, argv).options(opt).positional(p).run(), vm);
		if (vm.count("help") || argc == 1)
		{
			std::cout << "Usage: nanogi [options] <renderer> <scene> <result> <width> <height>" << std::endl;
			std::cout << opt << std::endl;
			return 1;
		}

		po::notify(vm);
	}
	catch (po::required_option& e)
	{
		std::cerr << "ERROR : " << e.what() << std::endl;
		return false;
	}
	catch (po::error& e)
	{
		std::cerr << "ERROR : " << e.what() << std::endl;
		return false;
	}

	#pragma endregion

	// --------------------------------------------------------------------------------

	#pragma region Initial message

	NGI_LOG_INFO("nanogi");
	NGI_LOG_INFO("Copyright (c) 2015 Light Transport Entertainment Inc.");

	#pragma endregion

	// --------------------------------------------------------------------------------

	#pragma region Load scene

	Scene scene;
	{
		NGI_LOG_INFO("Loading scene");
		NGI_LOG_INDENTER();
		if (!scene.Load(vm["scene"].as<std::string>(), (double)(vm["width"].as<int>()) / vm["height"].as<int>()))
		{
			return false;
		}
	}

	#pragma endregion

	// --------------------------------------------------------------------------------

	#pragma region Initialize renderer

	Renderer renderer;
	{
		NGI_LOG_INFO("Initializing renderer");
		NGI_LOG_INDENTER();
		if (!renderer.Load(vm))
		{
			return false;
		}
	}

	#pragma endregion

	// --------------------------------------------------------------------------------

	#pragma region Rendering

	std::vector<glm::dvec3> film;
	{
		NGI_LOG_INFO("Rendering");
		NGI_LOG_INDENTER();
		renderer.Render(scene, film);
	}

	#pragma endregion

	// --------------------------------------------------------------------------------

	#pragma region Save rendered image

	{
		NGI_LOG_INFO("Saving rendered image");
		NGI_LOG_INDENTER();
		SaveImage(vm["result"].as<std::string>(), film, vm["width"].as<int>(), vm["height"].as<int>());
	}

	#pragma endregion

	// --------------------------------------------------------------------------------

	return true;
}

int main(int argc, char** argv)
{
	NGI_LOG_RUN();

	int result = EXIT_SUCCESS;
	try
	{
		#if NGI_PLATFORM_WINDOWS
		_set_se_translator(SETransFunc);
		#endif
		if (!Run(argc, argv))
		{
			result = EXIT_FAILURE;
		}
	}
	catch (const std::exception& e)
	{
		NGI_LOG_ERROR("EXCEPTION | " + std::string(e.what()));
		result = EXIT_FAILURE;
	}

	#if NGI_DEBUG_MODE
	std::cerr << "Press any key to exit ...";
	std::cin.get();
	#endif

	NGI_LOG_STOP();
	return result;
}

