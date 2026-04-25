#include <clap/clap.h>
#include "video_player_plugin.h"
#include "plugin_info.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// CLAP plugin factory
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t factory_get_plugin_count(const clap_plugin_factory_t* /*factory*/)
{
    return 1;
}

static const clap_plugin_descriptor_t* factory_get_plugin_descriptor(
    const clap_plugin_factory_t* /*factory*/, uint32_t index)
{
    if (index == 0)
        return VideoPlayerPlugin::descriptor();
    return nullptr;
}

static const clap_plugin_t* factory_create_plugin(
    const clap_plugin_factory_t* /*factory*/,
    const clap_host_t*           host,
    const char*                  plugin_id)
{
    if (strcmp(plugin_id, PLUGIN_ID) != 0)
        return nullptr;

    if (!clap_version_is_compatible(host->clap_version))
        return nullptr;

    auto* plugin = new VideoPlayerPlugin(host);
    return plugin->clapPlugin();
}

static const clap_plugin_factory_t s_plugin_factory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin,
};

// ─────────────────────────────────────────────────────────────────────────────
// CLAP entry point (exported symbol)
// ─────────────────────────────────────────────────────────────────────────────
static bool entry_init(const char* plugin_path)
{
#ifdef _WIN32
    // FFmpeg DLLs live next to the .clap file, but Windows won't search the
    // plugin folder automatically — it searches the host (DAW) folder and PATH.
    // SetDllDirectoryA adds our folder so the delay-loaded FFmpeg imports resolve.
    if (plugin_path && *plugin_path) {
        char dir[MAX_PATH] = {};
        strncpy_s(dir, plugin_path, _TRUNCATE);
        char* sep = strrchr(dir, '\\');
        if (!sep) sep = strrchr(dir, '/');
        if (sep) { *sep = '\0'; SetDllDirectoryA(dir); }
    }
#endif
    return true;
}

static void entry_deinit() {}

static const void* entry_get_factory(const char* factory_id)
{
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &s_plugin_factory;
    return nullptr;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entry_init,
    entry_deinit,
    entry_get_factory,
};
