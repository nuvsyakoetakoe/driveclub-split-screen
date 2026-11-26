// Split Screen additions, 2026-11-26. Copyright (C) 2026 nuvsyakoetakoe.
// SPDX-License-Identifier: GPL-2.0-only
// Keep the port's launcher selection and startup behavior. Its children
// inherit the Vulkan layer for the local game installation.
#define wWinMain OriginalBootstrapMain
#include "../../../upstream/driveclub-042/launcher/setup/Bootstrap.cpp"
#undef wWinMain

static void AppendEnvironment(const wchar_t* key, const std::wstring& value) {
    std::wstring existing = Bootstrap::Environment(key);
    if (!existing.empty()) existing += L';';
    existing += value;
    SetEnvironmentVariableW(key, existing.c_str());
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prior, PWSTR command, int show) {
    wchar_t file[32768];
    const DWORD length = GetModuleFileNameW(nullptr, file, 32768);
    if (length && length < 32768) {
        std::wstring root(file, length);
        root.resize(root.find_last_of(L"\\/"));
        const auto addon = Bootstrap::Join(root, L"mods\\split-screen");
        if (Bootstrap::File(Bootstrap::Join(addon, L"driveclub_split.json")) &&
            Bootstrap::File(Bootstrap::Join(addon, L"driveclub_split.dll"))) {
            SetEnvironmentVariableW(L"DC_NATIVE_ROOT", root.c_str());
            AppendEnvironment(L"VK_ADD_LAYER_PATH", addon);
            AppendEnvironment(L"VK_INSTANCE_LAYERS", L"VK_LAYER_DC_split_screen");
            for (const auto* key : {L"DC_SPLIT_CAMERA", L"DC_VIEW_VISIBILITY", L"DC_VIEW_STENCIL",
                                    L"DC_SPLIT_MIRRORS", L"DC_SPLIT_TRACK_MAP"}) {
                SetEnvironmentVariableW(key, L"1");
            }
        }
    }
    return OriginalBootstrapMain(instance, prior, command, show);
}
