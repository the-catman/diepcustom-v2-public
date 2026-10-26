#include "TankDefs.h"

#include <cstdlib>
#include <emscripten/emscripten.h>

EM_JS(void, tankdefs_fetch_js, (), {
    if (!Module.api) {
        const server = new URLSearchParams(location.search).get("server");
        const base = server ? location.protocol + "//" + server : "";
        Module.tankNames = Module.tankNames || {};
        const apply = {
            tanks: defs => {
                for (const tank of defs) {
                    if (tank) Module.tankNames[tank.id] = tank.name;
                }
                Module.tankDefs = defs;
            },
            servers: list => { Module.gameServers = list; },
            commands: list => { Module.serverCommands = list; },
            colors: colors => { Module.pendingColors = colors; }
        };
        // reload*Interval: -2 refetches on every connect, -1 never, anything else every that
        // many ms.
        const api = Module.api = { interval: { tanks: -2, servers: -2, commands: -2, colors: -2 }, timer: {}, failed: {} };
        api.reload = (name, tries) => {
            const get = url => fetch(url).then(r => r.json()).then(apply[name]);
            get(base + "/api/" + name).catch(e => {
                if (name === "tanks") console.error("tank defs fetch failed (" + (base || location.origin) + "/api/tanks):", e);
                const fallback = base ? get("/api/" + name) : Promise.reject(e);
                fallback.catch(() => {
                    if (tries > 0) setTimeout(() => api.reload(name, tries - 1), 1000);
                    else api.failed[name] = true;
                });
            });
        };
        api.setInterval = (name, ms) => {
            api.interval[name] = ms;
            clearTimeout(api.timer[name]);
            if (ms < 0) return;
            const tick = () => { api.timer[name] = setTimeout(() => { tick(); api.reload(name, 0); }, ms); };
            tick();
        };
    }
    for (const name of Object.keys(Module.api.interval)) {
        if (Module.api.interval[name] === -2) Module.api.reload(name, 5);
    }
});

// util_reload_*: no argument reloads once; "never", "connect" or a number of ms sets the
// automatic reload.
EM_JS(void, tankdefs_reload_js, (const char* name, const char* arg), {
    const api = Module.api;
    if (!api) return;
    const key = UTF8ToString(name);
    if (!arg) return api.reload(key, 0);
    const value = UTF8ToString(arg);
    const num = parseInt(value);
    if (isNaN(num) && value === "never") return api.setInterval(key, -1);
    if (isNaN(num) && value === "connect") return api.setInterval(key, -2);
    api.setInterval(key, num);
});

EM_JS(int, tankdefs_take_colors_js, (int* out, int maxCount), {
    const colors = Module.pendingColors;
    if (!colors) return 0;
    Module.pendingColors = null;
    let n = 0;
    for (const id of Object.keys(colors)) {
        if (n >= maxCount) break;
        setValue(out + n * 8, parseInt(id), "i32");
        setValue(out + n * 8 + 4, colors[id] | 0, "i32");
        n++;
    }
    return n;
});

EM_JS(char*, tankdefs_name_js, (int id), {
    const name = Module.tankNames ? Module.tankNames[id] : null;
    if (!name) return 0;
    return stringToNewUTF8(name);
});

EM_JS(int, tankdefs_count_js, (), {
    if (!Module.tankDefs) return 0;
    return Module.tankDefs.filter(function(t) { return !!t; }).length;
});

EM_JS(int, tankdefs_levelreq_js, (int id), {
    const t = Module.tankDefs ? Module.tankDefs.find(function(d) { return d && d.id === id; }) : null;
    return t ? (t.levelRequirement || 0) : 0;
});

EM_JS(int, tankdefs_sides_js, (int id), {
    const t = Module.tankDefs ? Module.tankDefs.find(function(d) { return d && d.id === id; }) : null;
    return t && t.sides ? t.sides : 1;
});

EM_JS(int, tankdefs_barrels_js, (int id, float* out, int maxCount), {
    const t = Module.tankDefs ? Module.tankDefs.find(function(d) { return d && d.id === id; }) : null;
    if (!t || !t.barrels) return 0;
    const n = Math.min(t.barrels.length, maxCount);
    for (let i = 0; i < n; i++) {
        const b = t.barrels[i];
        const p = out + i * 32;
        setValue(p, b.angle, "float");
        setValue(p + 4, b.size, "float");
        setValue(p + 8, b.width / 42, "float");
        setValue(p + 12, b.offset, "float");
        setValue(p + 16, b.isTrapezoid ? 1 : 0, "float");
        setValue(p + 20, b.trapezoidDirection || 0, "float");
        setValue(p + 24, b.distance || 0, "float");
        setValue(p + 28, b.addon === "trapLauncher" ? 1 : 0, "float");
    }
    return n;
});

EM_JS(char*, tankdefs_addon_js, (int id, int post), {
    const t = Module.tankDefs ? Module.tankDefs.find(function(d) { return d && d.id === id; }) : null;
    const a = t ? (post ? t.postAddon : t.preAddon) : null;
    if (!a) return 0;
    return stringToNewUTF8(a);
});

EM_JS(int, tankdefs_servers_js, (), {
    return Module.gameServers ? Module.gameServers.length : 0;
});

EM_JS(char*, tankdefs_server_gamemode_js, (int index), {
    const s = Module.gameServers ? Module.gameServers[index] : null;
    if (!s || !s.gamemode) return 0;
    return stringToNewUTF8(s.gamemode);
});

// A party link's server id: the list entry's 4-letter id, or its gamemode name.
EM_JS(char*, tankdefs_server_by_id_js, (const char* id), {
    const key = UTF8ToString(id);
    const s = (Module.gameServers || []).find(s => s && (s.id === key || s.gamemode === key));
    return s && s.gamemode ? stringToNewUTF8(s.gamemode) : 0;
});

EM_JS(char*, tankdefs_server_id_js, (const char* mode), {
    const key = UTF8ToString(mode);
    const s = (Module.gameServers || []).find(s => s && s.gamemode === key);
    return s && s.id ? stringToNewUTF8(s.id) : 0;
});

EM_JS(int, tankdefs_servers_settled_js, (), {
    return Module.gameServers || (Module.api && Module.api.failed.servers) ? 1 : 0;
});

EM_JS(char*, tankdefs_server_name_js, (int index), {
    const s = Module.gameServers ? Module.gameServers[index] : null;
    if (!s || !s.name) return 0;
    return stringToNewUTF8(s.name);
});

EM_JS(int, tankdefs_command_count_js, (), {
    return Module.serverCommands ? Module.serverCommands.length : 0;
});

EM_JS(char*, tankdefs_command_field_js, (int index, int field), {
    const c = Module.serverCommands ? Module.serverCommands[index] : null;
    if (!c) return 0;
    const v = field === 0 ? c.id : field === 1 ? c.usage : c.description;
    if (!v) return 0;
    return stringToNewUTF8(v);
});

EM_JS(int, tankdefs_command_allowed_js, (int index, int level), {
    const c = Module.serverCommands ? Module.serverCommands[index] : null;
    return c && !(c.permissionLevel > level) ? 1 : 0;
});

EM_JS(int, tankdefs_upgrades_js, (int id, int* out, int maxCount), {
    const t = Module.tankDefs ? Module.tankDefs.find(function(d) { return d && d.id === id; }) : null;
    if (!t || !t.upgrades) return 0;
    const n = Math.min(t.upgrades.length, maxCount);
    for (let i = 0; i < n; i++) setValue(out + i * 4, t.upgrades[i], "i32");
    return n;
});

namespace tankdefs {

void fetch() { tankdefs_fetch_js(); }
void reload(const char* name, const char* interval) { tankdefs_reload_js(name, interval); }

std::string name(int tankId) {
    char* p = tankdefs_name_js(tankId);
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}

int count() { return tankdefs_count_js(); }
int levelRequirement(int tankId) { return tankdefs_levelreq_js(tankId); }
int bodySides(int tankId) { return tankdefs_sides_js(tankId); }
int barrels(int tankId, float* out, int maxCount) { return tankdefs_barrels_js(tankId, out, maxCount); }

static std::string addon(int tankId, int post) {
    char* p = tankdefs_addon_js(tankId, post);
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}

std::string preAddon(int tankId) { return addon(tankId, 0); }
std::string postAddon(int tankId) { return addon(tankId, 1); }
int upgrades(int tankId, int* out, int maxCount) { return tankdefs_upgrades_js(tankId, out, maxCount); }

int serverCount() { return tankdefs_servers_js(); }
bool serversSettled() { return tankdefs_servers_settled_js() != 0; }

static std::string takeString(char* p) {
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}

std::string serverGamemodeById(const std::string& id) { return takeString(tankdefs_server_by_id_js(id.c_str())); }
std::string serverId(const std::string& gamemode) { return takeString(tankdefs_server_id_js(gamemode.c_str())); }
int takeColors(int* out, int maxCount) { return tankdefs_take_colors_js(out, maxCount); }

int commandCount() { return tankdefs_command_count_js(); }

static std::string commandField(int index, int field) {
    char* p = tankdefs_command_field_js(index, field);
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}

std::string commandId(int index) { return commandField(index, 0); }
std::string commandUsage(int index) { return commandField(index, 1); }
std::string commandDescription(int index) { return commandField(index, 2); }
bool commandAllowed(int index, int level) { return tankdefs_command_allowed_js(index, level) != 0; }

std::string serverGamemode(int index) {
    char* p = tankdefs_server_gamemode_js(index);
    if (!p) return "";
    std::string s(p);
    free(p);
    return s;
}

std::string serverName(int index) {
    char* p = tankdefs_server_name_js(index);
    if (!p) return gamemodeName(serverGamemode(index));
    std::string s(p);
    free(p);
    return s;
}

// Fallback display names for when /api/servers has no name field.
std::string gamemodeName(const std::string& id) {
    if (id == "ffa") return "FFA";
    if (id == "teams") return "2 Teams";
    if (id == "4teams") return "4 Teams";
    if (id == "dom") return "Domination";
    if (id == "tag") return "Tag";
    if (id == "sandbox") return "Sandbox";
    if (id == "maze") return "Maze";
    return id;
}

}
