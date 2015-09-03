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

// --------------------------------------------------------------------------------

#include <nanogi/macros.hpp>
#include <nanogi/basic.hpp>
#include <nanogi/rt.hpp>
#include <nanogi/gl.hpp>

#include <boost/program_options.hpp>

// Do not include QtWidgets header
// Because it conflicts with GLEW in qopenglfunctions.h
#include <QApplication>
#include <QMainWindow>
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QTime>
#include <QtGui/QWindow>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>
#include <QtGui/QPainter>

#define NGI_APP_NAME nanogi-viewer
#define NGI_QT_UI_HEADER NGI_TOKENPASTE2(<ui_, NGI_TOKENPASTE2(NGI_APP_NAME, .h>))
#include NGI_QT_UI_HEADER

using namespace nanogi;

// --------------------------------------------------------------------------------

struct DisplayCamera
{

	glm::vec3 Position;
	glm::quat Rotation;
	float Fov;

	glm::vec3 Forward() const					{ return glm::mat3_cast(Rotation) * glm::vec3(0.0f, 0.0f, -1.0f); }
	glm::vec3 Right() const						{ return glm::mat3_cast(Rotation) * glm::vec3(1.0f, 0.0f, 0.0f); }
	glm::mat4 ViewMatrix() const				{ return glm::translate(glm::transpose(glm::mat4_cast(Rotation)), -Position); }
	glm::mat4 InvViewMatrix() const				{ return glm::mat4_cast(Rotation) * glm::translate(glm::mat4(1.0f), Position); }
	glm::mat4 ProjMatrix(float aspect) const	{ return glm::perspective(Fov, aspect, 0.01f, 1000.0f); }
	glm::mat4 InvProjMatrix(float aspect) const	{ return glm::inverse(ProjMatrix(aspect)); }

};

struct GLMesh
{
	GLResource BufferP;
	GLResource BufferN;
	GLResource BufferUV;
	GLResource BufferF;
	GLResource VertexArray;

	void Load(const Mesh& mesh)
	{
		const int VertexAttributeP = 0;
		const int VertexAttributeN = 1;
		const int VertexAttributeUV = 2;

		VertexArray.Create(GLResourceType::VertexArray);

		BufferP.Create(GLResourceType::ArrayBuffer);
		BufferP.Allocate(mesh.Positions.size() * sizeof(double), &mesh.Positions[0], GL_STATIC_DRAW);
		VertexArray.AddVertexAttribute(BufferP, VertexAttributeP, 3, GL_DOUBLE, GL_FALSE, 0, nullptr);

		BufferN.Create(GLResourceType::ArrayBuffer);
		BufferN.Allocate(mesh.Normals.size() * sizeof(double), &mesh.Normals[0], GL_STATIC_DRAW);
		VertexArray.AddVertexAttribute(BufferN, VertexAttributeN, 3, GL_DOUBLE, GL_FALSE, 0, nullptr);

		if (!mesh.Texcoords.empty())
		{
			BufferUV.Create(GLResourceType::ArrayBuffer);
			BufferUV.Allocate(mesh.Texcoords.size() * sizeof(double), &mesh.Texcoords[0], GL_STATIC_DRAW);
			VertexArray.AddVertexAttribute(BufferUV, VertexAttributeUV, 2, GL_DOUBLE, GL_FALSE, 0, nullptr);
		}

		BufferF.Create(GLResourceType::ElementArrayBuffer);
		BufferF.Allocate(mesh.Faces.size() * sizeof(unsigned int), &mesh.Faces[0], GL_STATIC_DRAW);
	}
};

struct GLTexture
{
	GLResource Tex;

	void Load(const Texture& texture)
	{
		Tex.Create(GLResourceType::Texture2D);
		Tex.Bind();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.Width, texture.Height, 0, GL_RGB, GL_UNSIGNED_BYTE, &texture.Data[0]);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		Tex.Unbind();
	}
};

struct GLScene
{

	std::vector<std::unique_ptr<GLMesh>> meshes;
	std::vector<std::unique_ptr<GLTexture>> textures;
	GLResource Pipeline;
	GLResource ProgramV;
	GLResource ProgramF;

	bool Load(const Scene& scene)
	{
		#pragma region Load mesh & texture

		for (const auto& m : scene.Meshes)
		{
			std::unique_ptr<GLMesh> mesh(new GLMesh);
			mesh->Load(*m);
			m->UserData = mesh.get();
			meshes.push_back(std::move(mesh));
		}

		for (const auto& t : scene.Textures)
		{
			std::unique_ptr<GLTexture> texture(new GLTexture);
			texture->Load(*t);
			t->UserData = texture.get();
			textures.push_back(std::move(texture));
		}

		#pragma endregion

		// --------------------------------------------------------------------------------

		#pragma region Load shaders

		const std::string RenderVs = R"x(
			#version 400 core

			#define POSITION 0
			#define NORMAL   1
			#define TEXCOORD 2

			layout (location = POSITION) in vec3 position;
			layout (location = NORMAL) in vec3 normal;
			layout (location = TEXCOORD) in vec2 texcoord;

			out vec3 vNormal;
			out vec2 vTexcoord;
				
			uniform mat4 ModelMatrix;
			uniform mat4 ViewMatrix;
			uniform mat4 ProjectionMatrix;

			void main()
			{
				mat4 mvMatrix = ViewMatrix * ModelMatrix;
				mat4 mvpMatrix = ProjectionMatrix * mvMatrix;
				mat3 normalMatrix = mat3(transpose(inverse(mvMatrix)));
				vNormal = normalMatrix * normal;
				vTexcoord = texcoord;
				gl_Position = mvpMatrix * vec4(position, 1);
			}
		)x";

		const std::string RenderFs = R"x(
			#version 400 core

			in vec3 vNormal;
			in vec2 vTexcoord;

			out vec4 fragColor;

			uniform vec3 Diffuse;
			uniform sampler2D DiffuseTex;
			uniform int UseTexture;
			uniform vec3 Color;

			void main()
			{
				fragColor.rgb = Color;
				fragColor.a = 1;
			}
		)x";

		ProgramV.Create(GLResourceType::Program);
		ProgramF.Create(GLResourceType::Program);
		if (!ProgramV.CompileString(GL_VERTEX_SHADER, RenderVs)) return false;
		if (!ProgramF.CompileString(GL_FRAGMENT_SHADER, RenderFs)) return false;
		if (!ProgramV.Link()) return false;
		if (!ProgramF.Link()) return false;
		Pipeline.Create(GLResourceType::Pipeline);
		Pipeline.AddProgram(ProgramV);
		Pipeline.AddProgram(ProgramF);

		#pragma endregion

		return true;
	}

	void Draw(const Scene& scene, const DisplayCamera& displayCamera, float aspect)
	{
		const glm::mat4 Projection = displayCamera.ProjMatrix(aspect);
		const glm::mat4 Model(1);
		const glm::mat4 View = displayCamera.ViewMatrix();

		ProgramV.SetUniform("ModelMatrix", Model);
		ProgramV.SetUniform("ViewMatrix", View);
		ProgramV.SetUniform("ProjectionMatrix", Projection);

		Pipeline.Bind();
		
		for (const auto& primitive : scene.Primitives)
		{
			const auto* mesh = primitive->MeshRef;
			if (!mesh)
			{
				continue;
			}

			if ((primitive->Type & PrimitiveType::L) > 0)
			{
				ProgramF.SetUniform("Color", glm::vec3(1.0f, 1.0f, 0.0f));
			}
			else if ((primitive->Type & PrimitiveType::S) > 0)
			{
				ProgramF.SetUniform("Color", glm::vec3(1.0f, 0.0f, 0.0f));
			}
			else
			{
				ProgramF.SetUniform("Color", glm::vec3(1.0f));
			}

			// Draw mesh
			const auto* glmesh = reinterpret_cast<const GLMesh*>(mesh->UserData);
			glmesh->VertexArray.Draw(GL_TRIANGLES, glmesh->BufferF);
		}

		Pipeline.Unbind();
	}
};

// --------------------------------------------------------------------------------

class DisplayWindow final : public QWindow
{
	Q_OBJECT

public:

	DisplayWindow(QWindow* parent = 0)
		: QWindow(parent)
	{
		setSurfaceType(QWindow::OpenGLSurface);

		// Surface format
		QSurfaceFormat format;
		format.setMajorVersion(4);
		format.setMajorVersion(0);
		format.setSamples(4);
		format.setDepthBufferSize(24);
		format.setProfile(QSurfaceFormat::CompatibilityProfile);

		// Create surface
		setFormat(format);
		create();

		// OpenGL context
		context = new QOpenGLContext(this);
		context->setFormat(format);
		context->create();
		context->makeCurrent(this);

		// Render device
		device = new QOpenGLPaintDevice();

		// Initialize GLEW
		glewExperimental = GL_TRUE;
		if (glewInit() != GLEW_OK)
		{
			NGI_LOG_ERROR("Failed to initialize GLEW");
			return;
		}

		// GLEW causes GL_INVALID_ENUM on GL 3.2 core or higher profiles
		// because GLEW uses glGetString(GL_EXTENSIONS) internally.
		// cf. http://www.opengl.org/wiki/OpenGL_Loading_Library
		glGetError();

		// GL debug output
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
		glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_MEDIUM_ARB, 0, NULL, GL_FALSE);
		glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, GL_FALSE);
		glDebugMessageCallbackARB(&GLUtils::DebugOutput, NULL);
	}

public:

	bool Initialize(const std::string& path)
	{
		// Load resources
		// aspect ratio changes by window size, here fixed to 1
		if (!scene.Load(path, 1))
		{
			return false;
		}

		// Load GL resources
		if (!glscene.Load(scene))
		{
			return false;
		}

		// Initial view
		SetInitialView();

		return true;
	}

protected:

	virtual bool event(QEvent *event) override
	{
		switch (event->type())
		{
			case QEvent::UpdateRequest:
				pending = false;
				Update();
				Render();
				return true;
			default:
				return QWindow::event(event);
		}
	}

	virtual void exposeEvent(QExposeEvent* event) override
	{
		if (isExposed()) Render();
	}

	virtual void keyPressEvent(QKeyEvent* event) override
	{
		// Show current camera info
		if (event->key() == Qt::Key_C)
		{
			const auto* E = scene.Primitives[scene.SensorPrimitiveIndex].get();
			if (E->Params.E.Type == EType::Pinhole)
			{
				NGI_LOG_INFO("Current camera");
				NGI_LOG_INDENTER();
				NGI_LOG_INFO("eye: [ " + std::to_string(camera.Position.x) + ", " + std::to_string(camera.Position.y) + ", " + std::to_string(camera.Position.z) + " ]");

				const auto center = camera.Position + glm::mat3_cast(camera.Rotation) * glm::vec3(0.0f, 0.0f, -1.0f);
				NGI_LOG_INFO("center: [ " + std::to_string(center.x) + ", " + std::to_string(center.y) + ", " + std::to_string(center.z) + " ]");
			}
		}

		pressedKeys[event->key()] = true;
		if (event->isAutoRepeat())
		{
			event->ignore();
			return;
		}
	}

	virtual void keyReleaseEvent(QKeyEvent* event) override
	{
		pressedKeys[event->key()] = false;
	}

	virtual void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::MouseButton::RightButton)
		{
			cameraRotating = true;
		}
	}

	virtual void mouseReleaseEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::MouseButton::RightButton && cameraRotating)
		{
			cameraRotating = false;
		}
	}

	virtual void mouseMoveEvent(QMouseEvent* event) override
	{
		int x = event->x();
		int y = event->y();

		if (cameraRotating)
		{
			float ndx = (float)(lastCursorPos.x - x) / width();
			float ndy = (float)(lastCursorPos.y - y) / height();
			camera.Rotation =
				glm::rotate(glm::quat(), glm::radians(ndy * 50.0f), camera.Right()) *
				glm::rotate(glm::quat(), glm::radians(ndx * 50.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * camera.Rotation;
		}

		lastCursorPos = glm::ivec2(x, y);
	}

private:

	void Update()
	{
		// Move camera
		auto keyMod = QApplication::keyboardModifiers();
		float factor = keyMod.testFlag(Qt::KeyboardModifier::ShiftModifier) ? 1.0f : 0.1f;

		auto f = camera.Forward();
		auto r = camera.Right();

		if (pressedKeys['W']) { camera.Position += f * factor; }
		if (pressedKeys['S']) { camera.Position -= f * factor; }
		if (pressedKeys['A']) { camera.Position -= r * factor; }
		if (pressedKeys['D']) { camera.Position += r * factor; }
	}

	void Render()
	{
		if (!isExposed()) return;

		context->makeCurrent(this);
		device->setSize(size());
		QPainter painter(device);

		// ----------------------------------------------------------------------

		// Viewport
		glViewportIndexedfv(0, &glm::vec4(0, 0, size().width(), size().height())[0]);

		// Clear buffer
		float Depth(1.0f);
		glClearBufferfv(GL_DEPTH, 0, &Depth);
		glClearBufferfv(GL_COLOR, 0, &glm::vec4(0.0f)[0]);

		// State
		glEnable(GL_DEPTH_TEST);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		// ----------------------------------------------------------------------

		// Draw scene
		glscene.Draw(scene, camera, (float)(size().width()) / size().height());

		// ----------------------------------------------------------------------

		context->swapBuffers(this);
		if (!pending)
		{
			pending = true;
			QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest));
		}
	}

private:

	void SetInitialView()
	{
		const auto* E = scene.Primitives[scene.SensorPrimitiveIndex].get();
		if (E->Params.E.Type == EType::Pinhole)
		{
			const auto& p = E->Params.E.Pinhole;
			camera.Position = glm::vec3(p.Position);
			camera.Rotation = glm::quat_cast(glm::mat3(p.Vx, p.Vy, p.Vz));
			camera.Fov = p.Fov;
		}
		else
		{
			camera.Position = glm::vec3(0.0f, 0.0f, 1.0f);
			camera.Rotation = glm::quat();
			camera.Fov = glm::radians(45.0f);
		}
	}

private:

	QOpenGLContext* context;
	QOpenGLPaintDevice* device;
	bool pending = false;

private:

	DisplayCamera camera;
	std::unordered_map<int, bool> pressedKeys;
	bool cameraRotating = false;
	glm::ivec2 lastCursorPos;

private:

	Scene scene;
	GLScene glscene;

};

class MainWindow final : public QMainWindow
{
	Q_OBJECT

public:

	MainWindow(QWidget *parent = 0)
		: QMainWindow(parent)
	{
		ui.setupUi(this);
		displayWindow = new DisplayWindow;
		auto* container = QWidget::createWindowContainer(displayWindow, this);
		container->setMinimumSize(200, 200);
		ui.display->addWidget(container);
	}

public:

	bool Initialize(const std::string& path)
	{
		return displayWindow->Initialize(path);
	}

public:

	Ui::MainWindow ui;
	DisplayWindow* displayWindow;

};

// --------------------------------------------------------------------------------

int main(int argc, char** argv)
{
	#pragma region Parse arguments

	namespace po = boost::program_options;

	// Arguments
	std::string ScenePath;

	// Define options
	po::options_description opt("Allowed options");
	opt.add_options()
		("help", "Display help message")
		("scene,i", po::value<std::string>(&ScenePath), "Scene file");

	// positional arguments
	po::positional_options_description p;
	p.add("scene", 1);

	// Parse options
	po::variables_map vm;
	try
	{
		po::store(po::command_line_parser(argc, argv).options(opt).positional(p).run(), vm);
		if (vm.count("help") || argc == 1)
		{
			std::cout << "Usage: nanogi-viewer [options] <scene>" << std::endl;
			std::cout << opt << std::endl;
			return 1;
		}

		po::notify(vm);
	}
	catch (po::required_option& e)
	{
		std::cerr << "ERROR : " << e.what() << std::endl;
		return 1;
	}
	catch (po::error& e)
	{
		std::cerr << "ERROR : " << e.what() << std::endl;
		return 1;
	}

	#pragma endregion

	// --------------------------------------------------------------------------------

	#pragma region Initial message

	NGI_LOG_INFO("nanogi-viewer");
	NGI_LOG_INFO("Copyright (c) 2015 Light Transport Entertainment Inc.");

	#pragma endregion

	// --------------------------------------------------------------------------------

	NGI_LOG_RUN();

	int result;

	try
	{
		#if NGI_PLATFORM_WINDOWS
		_set_se_translator(SETransFunc);
		#endif

		QApplication a(argc, argv);
		
		MainWindow w;
		if (!w.Initialize(ScenePath))
		{
			result = EXIT_FAILURE;
		}
		else
		{
			w.show();
			result = a.exec();
		}
	}
	catch (const std::exception& e)
	{
		result = EXIT_FAILURE;
		NGI_LOG_ERROR(e.what());
	}
	
	NGI_LOG_STOP();

	// --------------------------------------------------------------------------------

	return result;
}

// --------------------------------------------------------------------------------

#define NGI_QT_MOC_CPP NGI_TOKENPASTE2(<src/moc_, NGI_TOKENPASTE2(NGI_APP_NAME, .cpp>))
#include NGI_QT_MOC_CPP
