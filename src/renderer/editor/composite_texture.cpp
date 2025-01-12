#include "composite_texture.h"
#include "engine/file_system.h"
#include "engine/lua_wrapper.h"
#include "engine/os.h"
#include "engine/stream.h"
#include "engine/string.h"

namespace Lumix {
	
static CompositeTexture& getThis(lua_State* L) {
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	CompositeTexture* tc = (CompositeTexture*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	ASSERT(tc);
	return *tc;
}

static CompositeTexture::ChannelSource toChannelSource(lua_State* L, int idx) {
	CompositeTexture::ChannelSource res;
	if (!lua_istable(L, idx)) {
		luaL_argerror(L, idx, "unexpected form");
	}
	char path[LUMIX_MAX_PATH];
	if (!LuaWrapper::checkStringField(L, idx, "path", Span(path))) {
		luaL_argerror(L, 1, "unexpected form");
	}
	res.path = path;

	if (!LuaWrapper::checkField(L, idx, "channel", &res.src_channel)) {
		luaL_argerror(L, 1, "unexpected form");
	}
	LuaWrapper::getOptionalField(L, idx, "invert", &res.invert);

	return res;
}

static int LUA_cubemap(lua_State* L) {
	LuaWrapper::DebugGuard guard(L);
	CompositeTexture& that = getThis(L);
	that.cubemap = LuaWrapper::checkArg<bool>(L, 1);
	return 0;
}

static int LUA_layer(lua_State* L) {
	LuaWrapper::DebugGuard guard(L);
	LuaWrapper::checkTableArg(L, 1);
	CompositeTexture& that = getThis(L);

	CompositeTexture::Layer& layer = that.layers.emplace();

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		const char* key = LuaWrapper::toType<const char*>(L, -2);
		if (equalIStrings(key, "rgb")) {
			layer.red = toChannelSource(L, -1);
			layer.green = layer.red;
			layer.blue = layer.red;
			layer.red.src_channel = 0;
			layer.green.src_channel = 1;
			layer.blue.src_channel = 2;
		}
		else if (equalIStrings(key, "alpha")) {
			layer.alpha = toChannelSource(L, -1);
		}
		else if (equalIStrings(key, "red")) {
			layer.red = toChannelSource(L, -1);
		}
		else if (equalIStrings(key, "green")) {
			layer.green = toChannelSource(L, -1);
		}
		else if (equalIStrings(key, "blue")) {
			layer.blue = toChannelSource(L, -1);
		}
		else {
			luaL_argerror(L, 1, StaticString<128>("unknown key ", key));
		}
		lua_pop(L, 1);
	}
	return 0;
}

static int LUA_output(lua_State* L) {
	const char* type = LuaWrapper::checkArg<const char*>(L, 1);
			
	CompositeTexture& that = getThis(L);
	if (equalIStrings(type, "bc1")) {
		that.output = CompositeTexture::Output::BC1;
	}
	else if (equalIStrings(type, "bc3")) {
		that.output = CompositeTexture::Output::BC3;
	}
	else {
		luaL_argerror(L, 1, "unknown value");
	}
	return 0;
}

CompositeTexture::CompositeTexture(IAllocator& allocator)
	: allocator(allocator)
	, layers(allocator)
{}

bool CompositeTexture::loadSync(FileSystem& fs, const Path& path) {
	OutputMemoryStream data(allocator);
	if (!fs.getContentSync(path, data)) return false;

	if (!init(Span(data.data(), (u32)data.size()), path.c_str())) return false;
	return true;
}

bool CompositeTexture::save(FileSystem& fs, const Path& path) {
	os::OutputFile file;
	if (fs.open(path.c_str(), file)) {
		file << "cubemap(" << (cubemap ? "true" : "false") << ")\n";
		for (CompositeTexture::Layer& layer : layers) {
			file << "layer {\n";
			file << "\tred = { path = \"" << layer.red.path.c_str() << "\", channel = " << layer.red.src_channel << ", invert = " << (layer.red.invert ? "true" : "false") << " },\n";
			file << "\tgreen = { path = \"" << layer.green.path.c_str() << "\", channel = " << layer.green.src_channel << ", invert = " << (layer.green.invert ? "true" : "false") << " },\n";
			file << "\tblue = { path = \"" << layer.blue.path.c_str() << "\", channel = " << layer.blue.src_channel << ", invert = " << (layer.blue.invert ? "true" : "false") << " },\n";
			file << "\talpha = { path = \"" << layer.alpha.path.c_str() << "\", channel = " << layer.alpha.src_channel << ", invert = " << (layer.alpha.invert ? "true" : "false") << " },\n";
			file << "}\n";
		}
		file.close();
		return true;
	}
	return false;
}

bool CompositeTexture::init(Span<const u8> data, const char* src_path) {
	layers.clear();
	output = Output::BC1;

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
		
	#define DEFINE_LUA_FUNC(func) \
		lua_pushcfunction(L, LUA_##func); \
		lua_setfield(L, LUA_GLOBALSINDEX, #func); 

	DEFINE_LUA_FUNC(layer)
	DEFINE_LUA_FUNC(cubemap)
	DEFINE_LUA_FUNC(output)
		
	#undef DEFINE_LUA_FUNC

	bool success = LuaWrapper::execute(L, Span((const char*)data.begin(), (const char*)data.end()), src_path, 0);
	lua_close(L);
	return success;
}

} // namespace Lumix
