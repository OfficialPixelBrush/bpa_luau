extern "C" {
#include "addon_api.h"
}

// Luau's headers already declare their API with C linkage, so they must not
// be wrapped in extern "C" here.
#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace bpa {
namespace {

constexpr const char* scriptsDir = "luau";

const bp_api* g_api = nullptr;
bp_world* g_world = nullptr; // Best-effort fallback world, kept fresh by any event/call that hands us one.

class LuaState {
public:
    LuaState() = default;
    explicit LuaState(lua_State* L) : L_(L) {}
    ~LuaState() { close(); }

    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;

    LuaState(LuaState&& other) noexcept : L_(other.L_) { other.L_ = nullptr; }
    LuaState& operator=(LuaState&& other) noexcept {
        if (this != &other) {
            close();
            L_ = other.L_;
            other.L_ = nullptr;
        }
        return *this;
    }

    lua_State* get() const { return L_; }
    explicit operator bool() const { return L_ != nullptr; }

private:
    void close() {
        if (L_) {
            lua_close(L_);
            L_ = nullptr;
        }
    }

    lua_State* L_ = nullptr;
};

// Each plugin owns an isolated Luau VM
struct Plugin {
    LuaState state;            // main thread, owns the VM
    lua_State* script = nullptr; // sandboxed thread that scripts run on
    int scriptRef = LUA_NOREF; // keeps `script` alive across GC
    std::string name;
};

std::vector<Plugin> g_plugins;

void* checkHandle(lua_State* L, int idx, const char* what) {
    luaL_argcheck(L, lua_islightuserdata(L, idx), idx, what);
    return lua_touserdata(L, idx);
}

bool getGlobalFunction(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool callWithReport(lua_State* L, int nargs, int nresults) {
    if (lua_pcall(L, nargs, nresults, 0) != LUA_OK) {
        g_api->log.error(g_api, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void applyCancelReturn(lua_State* L, bool& cancel) {
    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
        cancel = true;
    }
    lua_pop(L, 1);
}

void pushVec3(lua_State* L, bp_vec3 v) {
    lua_newtable(L);
    lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
}

void pushBlock(lua_State* L, bp_block block) {
    lua_newtable(L);
    lua_pushinteger(L, block.id);   lua_setfield(L, -2, "id");
    lua_pushinteger(L, block.meta); lua_setfield(L, -2, "meta");
}

void pushItemStack(lua_State* L, bp_item_stack item) {
    lua_newtable(L);
    lua_pushinteger(L, item.id);    lua_setfield(L, -2, "id");
    lua_pushinteger(L, item.count); lua_setfield(L, -2, "count");
    lua_pushinteger(L, item.data);  lua_setfield(L, -2, "data");
}

// Lua-callable bindings
extern "C" {

// log.*
int lua_log_info(lua_State* L) {
    g_api->log.info(g_api, luaL_checkstring(L, 1));
    return 0;
}
int lua_log_warning(lua_State* L) {
    g_api->log.warning(g_api, luaL_checkstring(L, 1));
    return 0;
}
int lua_log_error(lua_State* L) {
    g_api->log.error(g_api, luaL_checkstring(L, 1));
    return 0;
}

// server.*
int lua_server_getPlayerCount(lua_State* L) {
    lua_pushinteger(L, g_api->server.getPlayerCount(g_api));
    return 1;
}
int lua_server_getPlayerAt(lua_State* L) {
    int index = static_cast<int>(luaL_checkinteger(L, 1));
    bp_player* player = g_api->server.getPlayerAt(g_api, index);
    if (!player) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, player);
    return 1;
}

// player.*
int lua_player_sendMessage(lua_State* L) {
    bp_player* player = static_cast<bp_player*>(checkHandle(L, 1, "Expected a player handle"));
    const char* message = luaL_checkstring(L, 2);
    g_api->player.sendMessage(player, message);
    return 0;
}
int lua_player_kick(lua_State* L) {
    bp_player* player = static_cast<bp_player*>(checkHandle(L, 1, "Expected a player handle"));
    const char* reason = luaL_optstring(L, 2, "Kicked");
    g_api->player.kick(g_api, player, reason);
    return 0;
}
int lua_player_getUsername(lua_State* L) {
    bp_player* player = static_cast<bp_player*>(checkHandle(L, 1, "Expected a player handle"));
    lua_pushstring(L, g_api->player.getUsername(player));
    return 1;
}
int lua_player_getEntity(lua_State* L) {
    bp_player* player = static_cast<bp_player*>(checkHandle(L, 1, "Expected a player handle"));
    bp_entity* entity = g_api->player.getEntity(player);
    if (!entity) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, entity);
    return 1;
}

// entity.*
int lua_entity_getPosition(lua_State* L) {
    bp_entity* entity = static_cast<bp_entity*>(checkHandle(L, 1, "Expected an entity handle"));
    bp_vec3 pos = g_api->entity.getPosition(entity);
    pushVec3(L, pos);
    return 1;
}
int lua_entity_setPosition(lua_State* L) {
    bp_entity* entity = static_cast<bp_entity*>(checkHandle(L, 1, "Expected an entity handle"));
    bp_vec3 pos;
    pos.x = luaL_checknumber(L, 2);
    pos.y = luaL_checknumber(L, 3);
    pos.z = luaL_checknumber(L, 4);
    g_api->entity.setPosition(entity, pos);
    return 0;
}
int lua_entity_getWorld(lua_State* L) {
    bp_entity* entity = static_cast<bp_entity*>(checkHandle(L, 1, "Expected an entity handle"));
    bp_world* world = g_api->entity.getWorld(entity);
    g_world = world; // keep the fallback fresh
    if (!world) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, world);
    return 1;
}

// world.*
int lua_world_getBlock(lua_State* L) {
    bp_world* world = static_cast<bp_world*>(checkHandle(L, 1, "Expected a world handle"));
    bp_block_pos bpos;
    bpos.x = static_cast<int32_t>(luaL_checkinteger(L, 2));
    bpos.y = static_cast<int32_t>(luaL_checkinteger(L, 3));
    bpos.z = static_cast<int32_t>(luaL_checkinteger(L, 4));

    bp_block block = g_api->world.getBlock(world, bpos);
    pushBlock(L, block);
    return 1;
}
int lua_world_setBlock(lua_State* L) {
    bp_world* world = static_cast<bp_world*>(checkHandle(L, 1, "Expected a world handle"));
    bp_block_pos bpos;
    bpos.x = static_cast<int32_t>(luaL_checkinteger(L, 2));
    bpos.y = static_cast<int32_t>(luaL_checkinteger(L, 3));
    bpos.z = static_cast<int32_t>(luaL_checkinteger(L, 4));

    bp_block block;
    block.id = static_cast<int8_t>(luaL_checkinteger(L, 5));
    block.meta = static_cast<uint8_t>(luaL_checkinteger(L, 6));

    g_api->world.setBlock(world, bpos, block);
    return 0;
}
int lua_world_sendBlockUpdate(lua_State* L) {
    // Same signature as setBlock, but only resends the block to clients
    // without changing world state server-side.
    bp_world* world = static_cast<bp_world*>(checkHandle(L, 1, "Expected a world handle"));
    bp_block_pos bpos;
    bpos.x = static_cast<int32_t>(luaL_checkinteger(L, 2));
    bpos.y = static_cast<int32_t>(luaL_checkinteger(L, 3));
    bpos.z = static_cast<int32_t>(luaL_checkinteger(L, 4));

    bp_block block;
    block.id = static_cast<int8_t>(luaL_checkinteger(L, 5));
    block.meta = static_cast<uint8_t>(luaL_checkinteger(L, 6));

    g_api->world.sendBlockUpdate(world, bpos, block);
    return 0;
}
// Best-effort world handle for events that don't carry one directly
int lua_world_getCurrent(lua_State* L) {
    if (!g_world) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, g_world);
    return 1;
}

// data.*
int lua_data_setPlayer(lua_State* L) {
    bp_player* player = static_cast<bp_player*>(checkHandle(L, 1, "Expected a player handle"));
    luaL_checkany(L, 2);
    int ref = lua_ref(L, 2); // does not pop; nil yields LUA_REFNIL (0), which reads back as nil
    g_api->data.setPlayer(g_api, player, reinterpret_cast<void*>(static_cast<intptr_t>(ref)));
    return 0;
}
int lua_data_getPlayer(lua_State* L) {
    bp_player* player = static_cast<bp_player*>(checkHandle(L, 1, "Expected a player handle"));
    void* raw = g_api->data.getPlayer(g_api, player);
    if (!raw) { lua_pushnil(L); return 1; }
    lua_getref(L, static_cast<int>(reinterpret_cast<intptr_t>(raw)));
    return 1;
}

} // extern "C"

const luaL_Reg lua_log_fns[] = {
    {"info", lua_log_info},
    {"warning", lua_log_warning},
    {"error", lua_log_error},
    {nullptr, nullptr}
};
const luaL_Reg lua_server_fns[] = {
    {"getPlayerCount", lua_server_getPlayerCount},
    {"getPlayerAt", lua_server_getPlayerAt},
    {nullptr, nullptr}
};
const luaL_Reg lua_player_fns[] = {
    {"sendMessage", lua_player_sendMessage},
    {"kick", lua_player_kick},
    {"getUsername", lua_player_getUsername},
    {"getEntity", lua_player_getEntity},
    {nullptr, nullptr}
};
const luaL_Reg lua_entity_fns[] = {
    {"getPosition", lua_entity_getPosition},
    {"setPosition", lua_entity_setPosition},
    {"getWorld", lua_entity_getWorld},
    {nullptr, nullptr}
};
const luaL_Reg lua_world_fns[] = {
    {"getBlock", lua_world_getBlock},
    {"setBlock", lua_world_setBlock},
    {"sendBlockUpdate", lua_world_sendBlockUpdate},
    {"getCurrent", lua_world_getCurrent},
    {nullptr, nullptr}
};
const luaL_Reg lua_data_fns[] = {
    {"setPlayer", lua_data_setPlayer},
    {"getPlayer", lua_data_getPlayer},
    {nullptr, nullptr}
};

void registerLib(lua_State* L, const char* name, const luaL_Reg* fns) {
    lua_newtable(L);
    luaL_register(L, nullptr, fns); // libname == NULL: fill the table on top of the stack
    lua_setglobal(L, name);
}

void registerApi(lua_State* L) {
    registerLib(L, "log",    lua_log_fns);
    registerLib(L, "server", lua_server_fns);
    registerLib(L, "player", lua_player_fns);
    registerLib(L, "entity", lua_entity_fns);
    registerLib(L, "world",  lua_world_fns);
    registerLib(L, "data",   lua_data_fns);
}

bool readFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool loadAndRun(lua_State* L, const std::string& source, const std::string& chunkName) {
    lua_CompileOptions opts = {};
    opts.optimizationLevel = 1; // baseline optimizations that don't hinder debugging
    opts.debugLevel = 1;        // line info, so runtime errors report file:line

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source.data(), source.size(), &opts, &bytecodeSize);
    int status = luau_load(L, chunkName.c_str(), bytecode, bytecodeSize, 0);
    std::free(bytecode);
    if (status != 0) return false; // compile/load error message is on the stack

    return lua_pcall(L, 0, 0, 0) == LUA_OK;
}

void loadPluginFile(const std::filesystem::path& path) {
    lua_State* raw = luaL_newstate();
    if (!raw) {
        g_api->log.error(g_api, "Failed to create Luau state");
        return;
    }
    LuaState state(raw);

    luaL_openlibs(raw);
    registerApi(raw);

    // Lock down the standard library and our API tables
    luaL_sandbox(raw);
    lua_State* script = lua_newthread(raw);
    luaL_sandboxthread(script);
    int scriptRef = lua_ref(raw, -1); // anchor the thread so GC can't collect it
    lua_pop(raw, 1);

    std::string source;
    if (!readFile(path, source)) {
        std::string msg = "Failed to read '" + path.string() + "'";
        g_api->log.error(g_api, msg.c_str());
        return;
    }

    std::string name = path.filename().string();
    if (!loadAndRun(script, source, "@" + name)) {
        g_api->log.error(g_api, lua_tostring(script, -1));
        return; // `state` destructor closes the Luau VM
    }

    std::string msg = "Loaded plugin '" + name + "'";
    g_api->log.info(g_api, msg.c_str());

    Plugin plugin;
    plugin.state = std::move(state);
    plugin.script = script;
    plugin.scriptRef = scriptRef;
    plugin.name = std::move(name);
    g_plugins.push_back(std::move(plugin));
}

void loadAllPlugins() {
    std::error_code ec;
    if (!std::filesystem::is_directory(scriptsDir, ec)) {
        g_api->log.warning(g_api, "No 'luau' directory found, no plugins loaded");
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension();
        if (ext != ".luau" && ext != ".lua") continue;
        loadPluginFile(entry.path());
    }
}

void unloadAllPlugins() {
    for (auto& plugin : g_plugins) {
        if (getGlobalFunction(plugin.script, "OnUnload")) {
            callWithReport(plugin.script, 0, 0);
        }
    }
    g_plugins.clear(); // each Plugin's LuaState destructor closes its lua_State
}

// Engine events that're sent to the scripts
extern "C" {

void OnPlayerJoin(const bp_api* /*api*/, const bp_player_join_event* ev) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnPlayerJoin")) continue;

        lua_pushlightuserdata(L, ev->player);
        callWithReport(L, 1, 0);
    }
}

void OnPlayerLeave(const bp_api* /*api*/, const bp_player_leave_event* ev) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnPlayerLeave")) continue;

        lua_pushlightuserdata(L, ev->player);
        callWithReport(L, 1, 0);
    }
}

void OnPlayerMove(const bp_api* /*api*/, bp_player_move_event* ev) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnPlayerMove")) continue;

        lua_pushlightuserdata(L, ev->player);
        pushVec3(L, ev->from);
        pushVec3(L, ev->to);

        // A plugin can cancel the move by returning `false`.
        if (callWithReport(L, 3, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnItemUse(const bp_api* /*api*/, bp_item_use_event* ev) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnItemUse")) continue;

        lua_pushlightuserdata(L, ev->player);
        pushItemStack(L, ev->item);

        // A plugin can cancel the item use by returning `false`.
        if (callWithReport(L, 2, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnBlockUse(const bp_api* /*api*/, bp_block_use_event* ev) {
    g_world = ev->world; // fallback world context, kept fresh here too

    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnBlockUse")) continue;

        lua_pushlightuserdata(L, ev->player);
        lua_pushlightuserdata(L, ev->world);
        lua_pushinteger(L, ev->blockPos.x);
        lua_pushinteger(L, ev->blockPos.y);
        lua_pushinteger(L, ev->blockPos.z);

        // A plugin can cancel the block-use by returning `false`.
        if (callWithReport(L, 5, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnBlockBreak(const bp_api* /*api*/, bp_block_break_event* ev) {
    g_world = ev->world; // fallback world context, kept fresh here too

    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnBlockBreak")) continue;

        lua_pushlightuserdata(L, ev->player);
        lua_pushlightuserdata(L, ev->world);
        pushItemStack(L, ev->tool);
        lua_pushinteger(L, ev->blockPos.x);
        lua_pushinteger(L, ev->blockPos.y);
        lua_pushinteger(L, ev->blockPos.z);
        pushBlock(L, ev->block);

        // A plugin can cancel the break by returning `false`.
        if (callWithReport(L, 7, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnBlockPlace(const bp_api* /*api*/, bp_block_place_event* ev) {
    g_world = ev->world; // fallback world context, kept fresh here too

    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnBlockPlace")) continue;

        lua_pushlightuserdata(L, ev->player);
        lua_pushlightuserdata(L, ev->world);
        lua_pushinteger(L, ev->blockPos.x);
        lua_pushinteger(L, ev->blockPos.y);
        lua_pushinteger(L, ev->blockPos.z);
        lua_pushinteger(L, ev->blockId);

        // A plugin can cancel the placement by returning `false`.
        if (callWithReport(L, 6, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnEntityDamage(const bp_api* /*api*/, bp_entity_damage_event* ev) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnEntityDamage")) continue;

        lua_pushlightuserdata(L, ev->entity);
        lua_pushinteger(L, ev->amount);

        // A plugin can cancel the damage by returning `false`.
        if (callWithReport(L, 2, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnServerTick(const bp_api* /*api*/, const bp_server_tick_event* /*ev*/) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnServerTick")) continue;
        callWithReport(L, 0, 0);
    }
}

void OnPlayerChat(const bp_api* /*api*/, bp_player_chat_event* ev) {
    for (auto& plugin : g_plugins) {
        lua_State* L = plugin.script;
        if (!getGlobalFunction(L, "OnPlayerChat")) continue;

        lua_pushlightuserdata(L, ev->player);
        lua_pushstring(L, ev->message);

        // A plugin can cancel the chat message by returning `false`.
        if (callWithReport(L, 2, 1)) applyCancelReturn(L, ev->cancel);
    }
}

void OnLoad(const bp_api* api, const bp_addon_load* /*ev*/) {
    g_api = api;

    loadAllPlugins();

    std::string msg = "Luau initialized! (" +
        std::to_string(g_plugins.size()) + " plugin(s) loaded)";
    api->log.info(api, msg.c_str());
}

void OnUnload(const bp_api* api, const bp_addon_unload* /*ev*/) {
    unloadAllPlugins();
    api->log.info(api, "Luau uninitialized!");
}

} // extern "C"

} // namespace
} // namespace bpa

extern "C" bp_addon_info bp_addon(const bp_api* /*api*/) {
    return bp_addon_info{
        "bpa_lua",
        "Luau",
        "0.1.0",
        bp_addon_events{
            /* playerJoin   */ bpa::OnPlayerJoin,
            /* playerLeave  */ bpa::OnPlayerLeave,
            /* playerChat   */ bpa::OnPlayerChat,
            /* playerMove   */ bpa::OnPlayerMove,
            /* itemUse      */ bpa::OnItemUse,
            /* blockBreak   */ bpa::OnBlockBreak,
            /* blockPlace   */ bpa::OnBlockPlace,
            /* blockUse     */ bpa::OnBlockUse,
            /* entityDamage */ bpa::OnEntityDamage,
            /* serverTick   */ bpa::OnServerTick,
            /* addonLoad    */ bpa::OnLoad,
            /* addonUnload  */ bpa::OnUnload,
        },
    };
}
