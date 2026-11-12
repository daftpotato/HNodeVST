// Ensures SpoutLibrary.dll (shipped beside the plugin) is discoverable when
// the host loads this VST3. Windows does not search the plugin's own directory
// by default, so without this the load fails with ERROR_MOD_NOT_FOUND (126).
#include <windows.h>
#include <string>

static void EnsureSpoutPath()
{
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCSTR)&EnsureSpoutPath, &hMod))
        return;
    char buf[MAX_PATH];
    if (GetModuleFileNameA(hMod, buf, MAX_PATH) == 0) return;
    std::string dir(buf);
    const size_t pos = dir.find_last_of("\/");
    if (pos != std::string::npos) dir.resize(pos);
    SetDllDirectoryA(dir.c_str());
    // Keep it resident for the lifetime of the plugin (matches smoketest).
    LoadLibraryA((dir + "\SpoutLibrary.dll").c_str());
}

struct SpoutLoader { SpoutLoader() { EnsureSpoutPath(); } } gSpoutLoader;
