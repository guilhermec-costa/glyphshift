#include <dwrite_3.h>
#include <windows.h>
#include <wrl/client.h>

#include <glyphshift/platform/fonts.hpp>
#include <stdexcept>
#include <vector>

#include "text.hpp"

// Font discovery, Windows: DirectWrite's system font collection, which also
// holds the fonts installed for the current user.
namespace glyphshift::platform {
namespace {
using Microsoft::WRL::ComPtr;
using windows::wide;

// The file of `family`'s bold face, only when the collection has that exact
// family; DirectWrite's family lookup never substitutes another one. A family
// without a real bold face (only semibold, or bold simulated from regular) is
// skipped: thin strokes vanish at cell scale, which is why these families are
// preferred. Variable fonts are skipped too: the atlas loads a file's default
// instance, which is not the bold one DirectWrite selected.
std::string match(IDWriteFontCollection* fonts, std::string_view family) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    ComPtr<IDWriteFontFamily> found;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFace> face;
    if (FAILED(fonts->FindFamilyName(wide(family).c_str(), &index, &exists)) || !exists ||
        FAILED(fonts->GetFontFamily(index, &found)) ||
        FAILED(found->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            &font)) ||
        font->GetWeight() < DWRITE_FONT_WEIGHT_BOLD ||
        font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE ||
        FAILED(font->CreateFontFace(&face)))
        return {};
    if (ComPtr<IDWriteFontFace5> variable;
        SUCCEEDED(face.As(&variable)) && variable->HasVariations())
        return {};
    UINT32 count = 0;
    if (FAILED(face->GetFiles(&count, nullptr)) || !count)
        return {};
    std::vector<IDWriteFontFile*> files(count);
    if (FAILED(face->GetFiles(&count, files.data())))
        return {};
    ComPtr<IDWriteFontFile> file;
    file.Attach(files[0]);
    for (UINT32 i = 1; i < count; ++i)
        files[i]->Release();
    const void* key = nullptr;
    UINT32 key_size = 0;
    ComPtr<IDWriteFontFileLoader> loader;
    ComPtr<IDWriteLocalFontFileLoader> local;
    UINT32 length = 0;
    if (FAILED(file->GetReferenceKey(&key, &key_size)) || FAILED(file->GetLoader(&loader)) ||
        FAILED(loader.As(&local)) ||
        FAILED(local->GetFilePathLengthFromKey(key, key_size, &length)))
        return {};
    std::wstring path(length + 1, L'\0');
    if (FAILED(local->GetFilePathFromKey(key, key_size, path.data(), length + 1)))
        return {};
    path.resize(length);
    return windows::utf8(path);
}
}  // namespace

std::string bold_monospace_font(std::span<const std::string_view> families) {
    ComPtr<IDWriteFactory> factory;
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) ||
        FAILED(factory->GetSystemFontCollection(&fonts, FALSE)))
        throw std::runtime_error("cannot initialize font discovery");
    for (const auto family : families)
        if (auto path = match(fonts.Get(), family); !path.empty())
            return path;
    // The system's monospace font, as the console and Notepad use it.
    for (const std::string_view fallback : {"Consolas", "Courier New"})
        if (auto path = match(fonts.Get(), fallback); !path.empty())
            return path;
    throw std::runtime_error("no monospace font found");
}
}  // namespace glyphshift::platform
