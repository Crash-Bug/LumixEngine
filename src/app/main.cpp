#if 1 // set to 0 to build minimal lunex example

#include "engine/allocators.h"
#include "engine/atomic.h"
#include "engine/command_line_parser.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/thread.h"
#include "engine/universe.h"
#include "gui/gui_system.h"
#include "lua_script/lua_script_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"

#ifdef __linux__
	#define STB_IMAGE_IMPLEMENTATION
	#include "stb/stb_image.h"
#endif

using namespace Lumix;

static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");

struct GUIInterface : GUISystem::Interface {
	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0); }
	Vec2 getSize() const override { return size; }
	void setCursor(os::CursorType type) override { os::setCursor(type); }
	void enableCursor(bool enable) override { os::showCursor(enable); }

	Vec2 size;
	Pipeline* pipeline;
};


struct Runner final
{
	Runner() 
		: m_allocator(m_main_allocator) 
	{
		if (!jobs::init(os::getCPUsCount(), m_allocator)) {
			logError("Failed to initialize job system.");
		}
	}

	~Runner() {
		jobs::shutdown();
		ASSERT(!m_universe); 
	}

	void onResize() {
		if (!m_engine.get()) return;
		if (m_engine->getWindowHandle() == os::INVALID_WINDOW) return;

		const os::Rect r = os::getWindowClientRect(m_engine->getWindowHandle());
		m_viewport.w = r.width;
		m_viewport.h = r.height;
		m_gui_interface.size = Vec2((float)r.width, (float)r.height);
	}

	void initRenderPipeline() {
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.far = 10'000.f;
		m_viewport.is_ortho = false;
		m_viewport.near = 0.1f;
		m_viewport.pos = {0, 0, 0};
		m_viewport.rot = Quat::IDENTITY;

		m_renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = m_engine->getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*m_renderer, pres, "APP", m_engine->getAllocator());

		while (m_engine->getFileSystem().hasWork()) {
			os::sleep(100);
			m_engine->getFileSystem().processCallbacks();
		}

		m_pipeline->setUniverse(m_universe);
	}

	void initDemoScene() {
		const EntityRef env = m_universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_universe->createComponent(ENVIRONMENT_TYPE, env);
		m_universe->createComponent(LUA_SCRIPT_TYPE, env);
		
		RenderScene* render_scene = (RenderScene*)m_universe->getScene("renderer");
		Environment& environment = render_scene->getEnvironment(env);
		environment.direct_intensity = 3;
		
		Quat rot;
		rot.fromEuler(Vec3(degreesToRadians(45.f), 0, 0));
		m_universe->setRotation(env, rot);
		
		LuaScriptScene* lua_scene = (LuaScriptScene*)m_universe->getScene("lua_script");
		lua_scene->addScript(env, 0);
		lua_scene->setScriptPath(env, 0, Path("pipelines/atmo.lua"));
	}

	bool loadUniverse(const char* path, const char* universe_name) {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path(path), data)) return false;

		InputMemoryStream blob(data);
		EntityMap entity_map(m_allocator);

		UniverseHeader header;
		blob.read(header);
		if ((u32)header.version <= (u32)UniverseSerializedVersion::HASH64) {
			u32 dummy;
			blob.read(dummy);
			blob.read(dummy);
		}
		else {
			StableHash hash;
			blob.read(hash);
			const StableHash hash2((const u8*)blob.getData() + blob.getPosition(), u32(blob.size() - blob.getPosition()));
			if (hash != hash2) {
				logError("Corrupted file '", path, "'");
				return false;
			}
		}

		m_universe->setName(universe_name);
		if (!m_engine->deserialize(*m_universe, blob, entity_map)) {
			logError("Failed to deserialize ", path);
			return false;
		}
		return true;
	}

	static bool isWindowCommandLineOption() {
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-window")) return true;
		}
		return false;
	}

	void loadProject() {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path("lumix.prj"), data)) return;

		InputMemoryStream tmp(data);
		const DeserializeProjectResult res = m_engine->deserializeProject(tmp, Span(m_startup_universe));
		if (DeserializeProjectResult::SUCCESS != res) {
			logError("Failed to deserialize project file");
		}
	}

	void onInit() {
		Engine::InitArgs init_data;
		init_data.window_title = "On the hunt";

		if (os::fileExists("main.pak")) {
			init_data.file_system = FileSystem::createPacked("main.pak", m_allocator);
		}

		m_engine = Engine::create(static_cast<Engine::InitArgs&&>(init_data), m_allocator);
		
		if (!isWindowCommandLineOption()) {
			os::setFullscreen(m_engine->getWindowHandle());
			captureMouse(true);
		}

		m_universe = &m_engine->createUniverse(true);
		initRenderPipeline();
		
		auto* gui = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		m_gui_interface.pipeline = m_pipeline.get();
		gui->setInterface(&m_gui_interface);

		loadProject();

		const StaticString<LUMIX_MAX_PATH> unv_path("universes/", m_startup_universe, ".unv");
		if (!loadUniverse(unv_path, m_startup_universe)) {
			initDemoScene();
		}
		os::showCursor(false);
		while (m_engine->getFileSystem().hasWork()) {
			os::sleep(10);
			m_engine->getFileSystem().processCallbacks();
		}
		m_engine->getFileSystem().processCallbacks();

		os::showCursor(false);
		onResize();
		m_engine->startGame(*m_universe);
	}

	void shutdown() {
		m_engine->destroyUniverse(*m_universe);
		auto* gui = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		gui->setInterface(nullptr);
		m_pipeline.reset();
		m_engine.reset();
		m_universe = nullptr;
	}

	void captureMouse(bool capture) {
		if (m_focused) {
			os::grabMouse(m_engine->getWindowHandle());
			os::showCursor(false);
		}
		else {
			os::grabMouse(os::INVALID_WINDOW);
			os::showCursor(true);
		}
	}

	void onEvent(const os::Event& event) {
		if (m_engine.get()) {
			const bool is_mouse_up = event.type == os::Event::Type::MOUSE_BUTTON && !event.mouse_button.down;
			const bool is_key_up = event.type == os::Event::Type::KEY && !event.key.down;
			if (m_focused || is_mouse_up || is_key_up) {
				InputSystem& input = m_engine->getInputSystem();
				input.injectEvent(event, 0, 0);
			}
		}
		switch (event.type) {
			case os::Event::Type::FOCUS:
				m_focused = event.focus.gained;
				captureMouse(m_focused);
				break;
			case os::Event::Type::QUIT:
			case os::Event::Type::WINDOW_CLOSE: 
				m_finished = true;
				break;
			case os::Event::Type::WINDOW_MOVE:
			case os::Event::Type::WINDOW_SIZE:
				onResize();
				captureMouse(m_focused);
				break;
			default: break;
		}
	}

	void onIdle() {
		m_engine->update(*m_universe);

		EntityPtr camera = m_pipeline->getScene()->getActiveCamera();
		if (camera.isValid()) {
			int w = m_viewport.w;
			int h = m_viewport.h;
			m_viewport = m_pipeline->getScene()->getCameraViewport((EntityRef)camera);
			m_viewport.w = w;
			m_viewport.h = h;
		}

		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_renderer->frame();
	}

	DefaultAllocator m_main_allocator;
	debug::Allocator m_allocator;
	UniquePtr<Engine> m_engine;
	Renderer* m_renderer = nullptr;
	Universe* m_universe = nullptr;
	UniquePtr<Pipeline> m_pipeline;
	char m_startup_universe[96] = "main";

	Viewport m_viewport;
	bool m_finished = false;
	bool m_focused = true;
	GUIInterface m_gui_interface;
};

int main(int args, char* argv[])
{
	profiler::setThreadName("Main thread");
	struct Data {
		Data() : semaphore(0, 1) {}
		Runner app;
		Semaphore semaphore;
	} data;

	jobs::runEx(&data, [](void* ptr) {
		Data* data = (Data*)ptr;

		data->app.onInit();
		while(!data->app.m_finished) {
			os::Event e;
			while(os::getEvent(e)) {
				data->app.onEvent(e);
			}
			data->app.onIdle();
		}

		data->app.shutdown();

		data->semaphore.signal();
	}, nullptr, 0);
	
	PROFILE_BLOCK("sleeping");
	data.semaphore.wait();

	return 0;
}

#else

#include "engine/allocators.h"
#include "engine/os.h"
#include "renderer/gpu/gpu.h"

using namespace Lumix;

int main(int args, char* argv[]) {
	os::WindowHandle win = os::createWindow({});

	DefaultAllocator allocator;
	gpu::preinit(allocator, false);
	gpu::init(win, gpu::InitFlags::NONE);
	gpu::ProgramHandle shader = gpu::allocProgramHandle();

	const gpu::ShaderType types[] = {gpu::ShaderType::VERTEX, gpu::ShaderType::FRAGMENT};
	const char* srcs[] = {
		"void main() { gl_Position = vec4(gl_VertexID & 1, (gl_VertexID >> 1) & 1, 0, 1); }",
		"layout(location = 0) out vec4 color; void main() { color = vec4(1, 0, 1, 1); }",
	};
	gpu::createProgram(shader, {}, srcs, types, 2, nullptr, 0, "shader");

	bool finished = false;
	while (!finished) {
		os::Event e;
		while (os::getEvent(e)) {
			switch (e.type) {
				case os::Event::Type::WINDOW_CLOSE:
				case os::Event::Type::QUIT: finished = true; break;
			}
		}

		gpu::setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
		const float clear_col[] = {0, 0, 0, 1};
		gpu::clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH, clear_col, 0);
		gpu::useProgram(shader);
		gpu::setState(gpu::StateFlags::NONE);
		gpu::drawArrays(gpu::PrimitiveType::TRIANGLES, 0, 3);

		u32 frame = gpu::swapBuffers();
		gpu::waitFrame(frame);
	}

	gpu::shutdown();

	os::destroyWindow(win);
}

#endif