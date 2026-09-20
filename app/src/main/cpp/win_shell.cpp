// Windows shell for DMC Native Reader.
//
// This is a thin Win32 host, mirroring MainActivity/DmcRenderView's role on
// Android: it owns the window, file dialog, Explorer integration and pixel
// blit only. Parsing, inspection, rendering and UI-capability decisions
// (Spider Black Widow) all come from DMCNativeReader::Core -- the exact
// portable core the Android app links -- so this reads and gates the UI
// identically to the phone build, not a reimplementation of its logic.
//
// Visual design intentionally matches the Android app: near-black
// full-bleed viewport, a thin header (back + title), a thin icon toolbar,
// modal info instead of a permanent side panel.

#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <objbase.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/view_renderer.h"

#pragma comment(lib, "windowscodecs.lib")

void RegisterFileAssociations();

namespace {

using dmcresource::InspectionTopic;
using dmcresource::RenderFlag;
using dmcresource::RgbaImage;
using dmcresource::Session;
using dmcresource::spider::black_widow::has_state;
using dmcresource::spider::black_widow::StateFlag;

// ---- Palette (matches the Android app's near-black theme) ----------------
constexpr COLORREF kBg = RGB(11, 11, 14);
constexpr COLORREF kViewportBg = RGB(18, 18, 22);
constexpr COLORREF kBtnHover = RGB(26, 26, 32);
constexpr COLORREF kBtnActive = RGB(40, 44, 56);
constexpr COLORREF kText = RGB(235, 235, 238);
constexpr COLORREF kTextDim = RGB(96, 96, 102);
constexpr COLORREF kAccent = RGB(120, 170, 255);

constexpr int kHeaderHeight = 42;
constexpr int kToolbarHeight = 56;
constexpr int kBtnSize = 44;

enum ButtonId : int {
    kBtnBack = 100,
    kBtnPtx = 101,
    kBtnOpen = 110,
    kBtnResetExport = 111,
    kBtnWireframe = 112,
    kBtnHierarchy = 113,
    kBtnUv = 114,
    kBtnInfo = 115,
};

enum MenuId : int { kMenuRegisterFileTypes = 900 };

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::vector<std::uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

bool HasSupportedExtension(const std::wstring& name) {
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    for (const wchar_t* want : {L".mod", L".scm", L".dds", L".ptx"}) {
        if (_wcsicmp(ext, want) == 0) return true;
    }
    return false;
}

// ---- Toolbar/header button model ------------------------------------------

struct ToolButton {
    int id;
    std::wstring glyph;
    std::wstring tooltip;
    RECT rect{};
    bool visible = true;
    bool enabled = false;
    bool active = false;
};

struct NavEntry {
    std::unique_ptr<Session> session;
    std::wstring title;
};

struct AppState {
    std::unique_ptr<Session> session;
    std::vector<NavEntry> nav_stack;
    std::wstring title = L"DMC Native Reader";

    RgbaImage rgba;   // Last rendered/previewed frame, RGBA (for PNG export).
    RgbaImage bgra;   // Same frame, byte-swapped for StretchDIBits.
    bool static_image = false;

    float yaw = 0.65f;
    float pitch = -0.45f;
    float zoom = 1.0f;
    std::uint32_t render_flags = 0;
    bool hierarchy_available = false;

    bool dragging = false;
    POINT drag_start{};
    float drag_start_yaw = 0.0f;
    float drag_start_pitch = 0.0f;
    DWORD last_render_tick = 0;

    bool child_browser_open = false;
    int child_hot = -1;

    // Folder sibling navigation (Left/Right arrow keys), independent of the
    // parent/child resource stack above.
    std::vector<std::wstring> siblings;
    int sibling_index = -1;

    bool fullscreen = false;
    WINDOWPLACEMENT saved_placement{sizeof(WINDOWPLACEMENT)};

    std::vector<ToolButton> header_buttons;
    std::vector<ToolButton> tool_buttons;

    HWND main_window = nullptr;
    HFONT ui_font = nullptr;
    HFONT title_font = nullptr;
};

AppState g_state;

// ---- Black Widow-driven capability snapshot, mirroring BlackWidowState.java

struct Capabilities {
    bool can_render = false;
    bool can_wireframe = false;
    bool can_inspect = false;
    bool can_show_hierarchy = false;
    bool can_preview_image = false;
    bool can_show_uv = false;
    bool child_browser_mode = false;
    bool can_attach_texture_companion = false;
    bool texture_companion_attached = false;
    bool uv_map_view = false;
    bool can_export_png = false;
};

Capabilities CurrentCapabilities() {
    Capabilities c;
    if (!g_state.session) return c;
    const auto bits = dmcresource::black_widow_state(g_state.session.get());
    c.can_render = has_state(bits, StateFlag::CanRender);
    c.can_wireframe = has_state(bits, StateFlag::CanWireframe);
    c.can_inspect = has_state(bits, StateFlag::CanInspect);
    c.can_show_hierarchy = has_state(bits, StateFlag::CanShowHierarchy);
    c.can_preview_image = has_state(bits, StateFlag::CanPreviewImage);
    c.can_show_uv = has_state(bits, StateFlag::CanShowUv);
    c.child_browser_mode = has_state(bits, StateFlag::ChildBrowserMode);
    c.can_attach_texture_companion = has_state(bits, StateFlag::TextureCompanionAttachable);
    c.texture_companion_attached = has_state(bits, StateFlag::TextureCompanionAttached);
    c.uv_map_view = has_state(bits, StateFlag::UvMapView);
    c.can_export_png = has_state(bits, StateFlag::CanExportPng);
    return c;
}

RgbaImage ToBgra(const RgbaImage& rgba) {
    RgbaImage out;
    out.width = rgba.width;
    out.height = rgba.height;
    out.pixels = rgba.pixels;
    for (std::size_t i = 0; i + 3 < out.pixels.size(); i += 4) {
        std::swap(out.pixels[i + 0], out.pixels[i + 2]);
    }
    return out;
}

RECT ViewportRect(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT viewport = client;
    viewport.top = kHeaderHeight;
    viewport.bottom = (std::max)(viewport.top, client.bottom - kToolbarHeight);
    return viewport;
}

// ---- Modal info dialog (dark, monospace, read-only) ------------------------

LRESULT CALLBACK InfoDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kText);
            SetBkColor(hdc, RGB(20, 20, 24));
            static HBRUSH brush = CreateSolidBrush(RGB(20, 20, 24));
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_SIZE: {
            RECT client{};
            GetClientRect(hwnd, &client);
            MoveWindow(GetDlgItem(hwnd, 1), 8, 8, client.right - 16,
                      client.bottom - 52, TRUE);
            MoveWindow(GetDlgItem(hwnd, 2), client.right - 96, client.bottom - 36, 88,
                      28, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == 2) DestroyWindow(hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void ShowInfoDialog(HWND owner, const std::wstring& title, const std::string& text_utf8) {
    const wchar_t* kClass = L"DmcInfoDialog";
    WNDCLASSW wc{};
    wc.lpfnWndProc = InfoDlgProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(owner, GWLP_HINSTANCE));
    wc.lpszClassName = kClass;
    wc.hbrBackground = CreateSolidBrush(RGB(20, 20, 24));
    RegisterClassW(&wc);

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                               CW_USEDEFAULT, CW_USEDEFAULT, 640, 520, owner, nullptr,
                               wc.hInstance, nullptr);
    HWND edit = CreateWindowExW(
        0, L"EDIT", Utf8ToWide(text_utf8).c_str(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        8, 8, 600, 420, dlg, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
    HFONT font = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH, L"Consolas");
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    CreateWindowExW(0, L"BUTTON", L"Close",
                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 540, 470, 88, 28, dlg,
                   reinterpret_cast<HMENU>(2), wc.hInstance, nullptr);

    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg)) break;
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// ---- PNG export (Windows Imaging Component) --------------------------------

bool SavePng(const std::wstring& path, const RgbaImage& rgba) {
    if (rgba.width <= 0 || rgba.height <= 0) return false;
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IWICImagingFactory),
                                reinterpret_cast<void**>(&factory)))) {
        return false;
    }
    bool ok = false;
    IWICStream* stream = nullptr;
    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        IWICBitmapEncoder* encoder = nullptr;
        if (SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) {
            IWICBitmapFrameEncode* frame = nullptr;
            if (SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
                SUCCEEDED(frame->Initialize(nullptr))) {
                frame->SetSize(static_cast<UINT>(rgba.width), static_cast<UINT>(rgba.height));
                WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
                frame->SetPixelFormat(&format);
                const UINT stride = static_cast<UINT>(rgba.width) * 4U;
                if (SUCCEEDED(frame->WritePixels(
                        static_cast<UINT>(rgba.height), stride,
                        static_cast<UINT>(rgba.pixels.size()),
                        const_cast<BYTE*>(rgba.pixels.data())))) {
                    ok = SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
                }
                if (frame) frame->Release();
            }
            encoder->Release();
        }
        stream->Release();
    }
    factory->Release();
    return ok;
}

// ---- Forward declarations ---------------------------------------------------

void ActivateSession(HWND hwnd, std::unique_ptr<Session> session, const std::wstring& title,
                     bool push_current);
void LoadFile(HWND hwnd, const std::wstring& path, bool refresh_siblings);
void RefreshSiblings(const std::wstring& path);
void Rerender(HWND hwnd);

// -----------------------------------------------------------------------------

std::string BuildInspectionText() {
    if (!g_state.session) return "No resource loaded.";
    std::string text = dmcresource::describe_session(g_state.session.get());
    text += "\r\n\r\n";
    text += dmcresource::format_inspection_tree(g_state.session->inspection);
    if (!g_state.session->texture_attachment_detail.empty()) {
        text += "\r\n\r\n[PTX] " + g_state.session->texture_attachment_detail;
    }
    return text;
}

// render_session clamps each axis to [64, 1024] independently (see
// resource_session.cpp). Requesting the viewport's raw width/height verbatim
// lets a wide desktop window clamp only its (wider) width, returning an image
// whose aspect ratio no longer matches the viewport -- then blitting that
// mismatched image to fill the viewport distorts the model. Scale both axes
// down together first, the same way DmcRenderView.renderWidth()/Height() cap
// to a performance budget on Android, so the returned image's aspect always
// matches the viewport it will be stretched into.
void ComputeRenderSize(int viewport_w, int viewport_h, int* out_w, int* out_h) {
    constexpr int kMaxDim = 1024;
    const int w = (std::max)(16, viewport_w);
    const int h = (std::max)(16, viewport_h);
    if (w <= kMaxDim && h <= kMaxDim) {
        *out_w = w;
        *out_h = h;
        return;
    }
    const float scale =
        (std::min)(static_cast<float>(kMaxDim) / w, static_cast<float>(kMaxDim) / h);
    *out_w = (std::max)(16, static_cast<int>(w * scale));
    *out_h = (std::max)(16, static_cast<int>(h * scale));
}

void RenderMesh(HWND hwnd) {
    if (!g_state.session || !g_state.session->renderable) return;
    const RECT view = ViewportRect(hwnd);
    int width = 0, height = 0;
    ComputeRenderSize(view.right - view.left, view.bottom - view.top, &width, &height);
    g_state.rgba = dmcresource::render_session(g_state.session.get(), width, height,
                                               g_state.yaw, g_state.pitch, g_state.zoom,
                                               g_state.render_flags);
    g_state.bgra = ToBgra(g_state.rgba);
    g_state.static_image = false;
    g_state.last_render_tick = GetTickCount();
    InvalidateRect(hwnd, &view, FALSE);
}

void Rerender(HWND hwnd) { RenderMesh(hwnd); }

void RerenderThrottled(HWND hwnd, bool force) {
    const DWORD now = GetTickCount();
    if (force || now - g_state.last_render_tick >= 45) RenderMesh(hwnd);
}

void LoadStaticPreview(HWND hwnd) {
    if (!g_state.session || !g_state.session->image_preview.available()) return;
    g_state.rgba.width = static_cast<int>(g_state.session->image_preview.width);
    g_state.rgba.height = static_cast<int>(g_state.session->image_preview.height);
    g_state.rgba.pixels = g_state.session->image_preview.rgba8;
    g_state.bgra = ToBgra(g_state.rgba);
    g_state.static_image = true;
    InvalidateRect(hwnd, nullptr, TRUE);
}

void ActivateSession(HWND hwnd, std::unique_ptr<Session> session, const std::wstring& title,
                     bool push_current) {
    if (push_current && g_state.session) {
        g_state.nav_stack.push_back({std::move(g_state.session), g_state.title});
    }

    g_state.session = std::move(session);
    g_state.title = title;
    g_state.yaw = 0.65f;
    g_state.pitch = -0.45f;
    g_state.zoom = 1.0f;
    g_state.render_flags = 0;
    g_state.hierarchy_available = false;
    g_state.static_image = false;
    g_state.child_browser_open = false;
    g_state.rgba = RgbaImage{};
    g_state.bgra = RgbaImage{};

    if (!g_state.session) {
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }

    const auto caps = CurrentCapabilities();
    g_state.child_browser_open = caps.child_browser_mode;
    if (caps.uv_map_view) g_state.render_flags = dmcresource::render_flag(RenderFlag::UvLayout);

    if (caps.child_browser_mode) {
        InvalidateRect(hwnd, nullptr, TRUE);
    } else if (caps.can_preview_image) {
        LoadStaticPreview(hwnd);
    } else if (g_state.session->renderable) {
        RenderMesh(hwnd);
    } else {
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

void RefreshSiblings(const std::wstring& path) {
    g_state.siblings.clear();
    g_state.sibling_index = -1;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, path.c_str(), MAX_PATH - 1);
    PathRemoveFileSpecW(dir);

    WIN32_FIND_DATAW find{};
    const std::wstring pattern = std::wstring(dir) + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!HasSupportedExtension(find.cFileName)) continue;
        g_state.siblings.push_back(std::wstring(dir) + L"\\" + find.cFileName);
    } while (FindNextFileW(h, &find));
    FindClose(h);

    std::sort(g_state.siblings.begin(), g_state.siblings.end(),
             [](const std::wstring& a, const std::wstring& b) {
                 return _wcsicmp(a.c_str(), b.c_str()) < 0;
             });
    for (std::size_t i = 0; i < g_state.siblings.size(); ++i) {
        if (_wcsicmp(g_state.siblings[i].c_str(), path.c_str()) == 0) {
            g_state.sibling_index = static_cast<int>(i);
            break;
        }
    }
}

void LoadFile(HWND hwnd, const std::wstring& path, bool refresh_siblings) {
    g_state.nav_stack.clear();
    const auto bytes = ReadFileBytes(path);
    if (bytes.empty() && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd, L"Failed to open file.", L"DMC Native Reader", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::string name = WideToUtf8(path);
    std::unique_ptr<Session> session;
    try {
        session = dmcresource::open_session(name, bytes.data(), bytes.size());
    } catch (...) {
        session.reset();
    }

    const wchar_t* filename = PathFindFileNameW(path.c_str());
    if (!session) {
        ActivateSession(hwnd, nullptr, filename, false);
        MessageBoxW(hwnd, L"This resource was not accepted by any native module.",
                   L"DMC Native Reader", MB_OK | MB_ICONWARNING);
    } else {
        ActivateSession(hwnd, std::move(session), filename, false);
    }
    if (refresh_siblings) RefreshSiblings(path);
}

void NavigateSibling(HWND hwnd, int delta) {
    if (g_state.siblings.empty()) return;
    int index = g_state.sibling_index;
    if (index < 0) index = 0;
    index = (index + delta + static_cast<int>(g_state.siblings.size())) %
           static_cast<int>(g_state.siblings.size());
    const std::wstring path = g_state.siblings[index];
    LoadFile(hwnd, path, false);
    g_state.sibling_index = index;
}

void NavigateBack(HWND hwnd) {
    if (g_state.nav_stack.empty()) return;
    NavEntry entry = std::move(g_state.nav_stack.back());
    g_state.nav_stack.pop_back();
    ActivateSession(hwnd, std::move(entry.session), entry.title, false);
}

void OpenFileDialog(HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"DMC resources (*.mod;*.scm;*.dds;*.ptx)\0*.mod;*.scm;*.dds;*.ptx\0"
                      L"All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    LoadFile(hwnd, path, true);
}

void AttachTextureDialog(HWND hwnd) {
    if (!g_state.session) return;
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    // attach_session_ptx (core: texture_companion::attach_ptx) accepts both a
    // full PTX bundle and a lone standalone/wrapped DDS -- it tries PTX
    // framing first and falls back to single-slot DDS parsing.
    ofn.lpstrFilter = L"Texture companion (*.ptx;*.dds)\0*.ptx;*.dds\0"
                      L"PTX bundle (*.ptx)\0*.ptx\0DDS texture (*.dds)\0*.dds\0"
                      L"All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    const auto bytes = ReadFileBytes(path);
    const std::string name = WideToUtf8(std::wstring(path));
    const bool attached =
        dmcresource::attach_session_ptx(g_state.session.get(), name, bytes.data(), bytes.size());
    const std::wstring detail = Utf8ToWide(g_state.session->texture_attachment_detail);
    MessageBoxW(hwnd, detail.empty() ? (attached ? L"Texture companion attached."
                                                 : L"Texture companion was not accepted.")
                                     : detail.c_str(),
               L"DMC Native Reader", MB_OK | (attached ? MB_ICONINFORMATION : MB_ICONWARNING));
    if (g_state.session->renderable) RenderMesh(hwnd);
}

void ExportPngDialog(HWND hwnd) {
    if (g_state.rgba.width <= 0) return;
    wchar_t path[MAX_PATH] = L"";
    wcsncpy_s(path, (g_state.title + L".png").c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PNG image (*.png)\0*.png\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"png";
    if (!GetSaveFileNameW(&ofn)) return;
    const bool ok = SavePng(path, g_state.rgba);
    MessageBoxW(hwnd, ok ? L"PNG saved." : L"Could not export PNG.", L"DMC Native Reader",
               MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void OpenChildByIndex(HWND hwnd, int index) {
    if (!g_state.session) return;
    try {
        auto child = dmcresource::open_session_child(g_state.session.get(), index);
        if (!child) return;
        const auto title = dmcresource::session_child_title(g_state.session.get(), index);
        ActivateSession(hwnd, std::move(child), Utf8ToWide(title), true);
    } catch (...) {
    }
}

void OpenUvGallery(HWND hwnd) {
    if (!g_state.session) return;
    try {
        auto gallery = dmcresource::open_uv_gallery(g_state.session.get());
        if (!gallery) {
            MessageBoxW(hwnd, L"UV maps unavailable: incomplete bindings.", L"DMC Native Reader",
                       MB_OK | MB_ICONINFORMATION);
            return;
        }
        ActivateSession(hwnd, std::move(gallery), g_state.title + L" · UV", true);
    } catch (...) {
    }
}

void ShowResourceInfo(HWND hwnd) {
    ShowInfoDialog(hwnd, g_state.title, BuildInspectionText());
}

void ShowInspectionTopic(HWND hwnd, InspectionTopic topic) {
    if (!g_state.session) return;
    try {
        const auto doc = dmcresource::inspect_session(g_state.session.get(), topic);
        ShowInfoDialog(hwnd, g_state.title, dmcresource::format_inspection_tree(doc));
    } catch (...) {
        ShowInfoDialog(hwnd, g_state.title, "Information unavailable for this topic.");
    }
}

// ---- Layout + owner-draw painting ------------------------------------------

void LayoutButtons(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);

    g_state.header_buttons.clear();
    int x = 0;
    ToolButton back{kBtnBack, L"←", L"Back"};
    back.rect = {x, 0, x + kHeaderHeight, kHeaderHeight};
    g_state.header_buttons.push_back(back);
    x = client.right - kHeaderHeight;
    ToolButton ptx{kBtnPtx, L"TEX", L"Attach texture companion (PTX or DDS)"};
    ptx.rect = {x, 0, x + kHeaderHeight, kHeaderHeight};
    g_state.header_buttons.push_back(ptx);

    g_state.tool_buttons.clear();
    struct Def {
        int id;
        const wchar_t* glyph;
        const wchar_t* tip;
    };
    const Def defs[] = {
        {kBtnOpen, L"↑", L"Open MOD / SCM / DDS / PTX"},
        {kBtnResetExport, L"↻", L"Reset view / export PNG"},
        {kBtnWireframe, L"W", L"Wireframe"},
        {kBtnHierarchy, L"H", L"Bones / hierarchy"},
        {kBtnUv, L"UV", L"UV layout"},
        {kBtnInfo, L"i", L"Resource information"},
    };
    const int count = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
    const int total_width = count * kBtnSize;
    int bx = (client.right - total_width) / 2;
    const int by = client.bottom - kToolbarHeight + (kToolbarHeight - kBtnSize) / 2;
    for (const auto& d : defs) {
        ToolButton b{d.id, d.glyph, d.tip};
        b.rect = {bx, by, bx + kBtnSize, by + kBtnSize};
        g_state.tool_buttons.push_back(b);
        bx += kBtnSize;
    }
}

void UpdateButtonStates() {
    const auto caps = CurrentCapabilities();
    const bool has_session = static_cast<bool>(g_state.session);

    for (auto& b : g_state.header_buttons) {
        if (b.id == kBtnBack) {
            b.visible = !g_state.nav_stack.empty();
            b.enabled = b.visible;
        } else if (b.id == kBtnPtx) {
            b.visible = caps.can_attach_texture_companion;
            b.enabled = b.visible;
            b.active = caps.texture_companion_attached;
        }
    }
    for (auto& b : g_state.tool_buttons) {
        switch (b.id) {
            case kBtnOpen:
                b.enabled = true;
                break;
            case kBtnResetExport:
                b.glyph = caps.can_export_png ? L"↓" : L"↻";
                b.tooltip = caps.can_export_png ? L"Export PNG" : L"Reset view";
                b.enabled = caps.can_export_png || (has_session && caps.can_render);
                b.active = false;
                break;
            case kBtnWireframe:
                b.enabled = has_session && caps.can_wireframe && !caps.uv_map_view;
                b.active =
                    b.enabled && (g_state.render_flags & dmcresource::render_flag(
                                                             RenderFlag::Wireframe)) != 0;
                break;
            case kBtnHierarchy:
                b.enabled = has_session && caps.can_show_hierarchy && !caps.uv_map_view;
                b.active =
                    b.enabled && (g_state.render_flags & dmcresource::render_flag(
                                                             RenderFlag::Hierarchy)) != 0;
                break;
            case kBtnUv:
                b.enabled = has_session && caps.can_show_uv;
                b.active = caps.uv_map_view;
                break;
            case kBtnInfo:
                b.enabled = has_session && caps.can_inspect;
                break;
            default:
                break;
        }
    }
}

void PaintButton(HDC hdc, const ToolButton& b) {
    if (!b.visible) return;
    HBRUSH bg = CreateSolidBrush(b.active ? kBtnActive : kBg);
    FillRect(hdc, &b.rect, bg);
    DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, !b.enabled ? kTextDim : (b.active ? kAccent : kText));
    RECT r = b.rect;
    DrawTextW(hdc, b.glyph.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintChrome(HDC hdc, HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);

    RECT header = {0, 0, client.right, kHeaderHeight};
    HBRUSH bgBrush = CreateSolidBrush(kBg);
    FillRect(hdc, &header, bgBrush);
    RECT toolbar = {0, client.bottom - kToolbarHeight, client.right, client.bottom};
    FillRect(hdc, &toolbar, bgBrush);
    DeleteObject(bgBrush);

    RECT title_rect = {kHeaderHeight + 8, 0, client.right - kHeaderHeight - 8, kHeaderHeight};
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kText);
    SelectObject(hdc, g_state.title_font);
    DrawTextW(hdc, g_state.title.c_str(), -1, &title_rect,
             DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(hdc, g_state.ui_font);
    for (const auto& b : g_state.header_buttons) PaintButton(hdc, b);
    for (const auto& b : g_state.tool_buttons) PaintButton(hdc, b);
}

void PaintChildBrowser(HDC hdc, const RECT& viewport) {
    HBRUSH bg = CreateSolidBrush(kViewportBg);
    FillRect(hdc, &viewport, bg);
    DeleteObject(bg);
    if (!g_state.session) return;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_state.ui_font);
    const auto count = dmcresource::session_child_count(g_state.session.get());
    const int row_h = 30;
    for (std::size_t i = 0; i < count; ++i) {
        RECT row = {viewport.left + 12,
                   viewport.top + 12 + static_cast<int>(i) * row_h, viewport.right - 12,
                   viewport.top + 12 + static_cast<int>(i + 1) * row_h};
        if (row.top > viewport.bottom) break;
        if (static_cast<int>(i) == g_state.child_hot) {
            HBRUSH hot = CreateSolidBrush(kBtnHover);
            FillRect(hdc, &row, hot);
            DeleteObject(hot);
        }
        SetTextColor(hdc, kText);
        const auto title =
            dmcresource::session_child_title(g_state.session.get(), static_cast<int>(i));
        std::wstring wtitle = Utf8ToWide(title);
        RECT text_rect = row;
        text_rect.left += 8;
        DrawTextW(hdc, wtitle.c_str(), -1, &text_rect, DT_VCENTER | DT_SINGLELINE);
    }
}

void PaintViewport(HDC hdc, const RECT& viewport) {
    if (g_state.child_browser_open) {
        PaintChildBrowser(hdc, viewport);
        return;
    }
    HBRUSH bg = CreateSolidBrush(kViewportBg);
    FillRect(hdc, &viewport, bg);
    DeleteObject(bg);

    if (g_state.bgra.width <= 0 || g_state.bgra.height <= 0) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kTextDim);
        SelectObject(hdc, g_state.ui_font);
        const wchar_t* hint = L"Open a .mod / .scm / .dds / .ptx file";
        RECT r = viewport;
        DrawTextW(hdc, hint, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_state.bgra.width;
    bmi.bmiHeader.biHeight = -g_state.bgra.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (g_state.static_image) {
        const float sx = static_cast<float>(viewport.right - viewport.left) / g_state.bgra.width;
        const float sy = static_cast<float>(viewport.bottom - viewport.top) / g_state.bgra.height;
        const float scale = (std::min)(sx, sy);
        const int dw = static_cast<int>(g_state.bgra.width * scale);
        const int dh = static_cast<int>(g_state.bgra.height * scale);
        const int dx = viewport.left + ((viewport.right - viewport.left) - dw) / 2;
        const int dy = viewport.top + ((viewport.bottom - viewport.top) - dh) / 2;
        StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, g_state.bgra.width, g_state.bgra.height,
                     g_state.bgra.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        StretchDIBits(hdc, viewport.left, viewport.top, viewport.right - viewport.left,
                     viewport.bottom - viewport.top, 0, 0, g_state.bgra.width,
                     g_state.bgra.height, g_state.bgra.pixels.data(), &bmi, DIB_RGB_COLORS,
                     SRCCOPY);
    }
}

int HitTestButtons(const std::vector<ToolButton>& buttons, POINT pt) {
    for (const auto& b : buttons) {
        if (!b.visible || !b.enabled) continue;
        if (PtInRect(&b.rect, pt)) return b.id;
    }
    return -1;
}

void HandleButtonClick(HWND hwnd, int id) {
    switch (id) {
        case kBtnBack:
            NavigateBack(hwnd);
            break;
        case kBtnPtx:
            AttachTextureDialog(hwnd);
            break;
        case kBtnOpen:
            OpenFileDialog(hwnd);
            break;
        case kBtnResetExport: {
            const auto caps = CurrentCapabilities();
            if (caps.can_export_png) {
                ExportPngDialog(hwnd);
            } else if (g_state.session && g_state.session->renderable) {
                g_state.yaw = 0.65f;
                g_state.pitch = -0.45f;
                g_state.zoom = 1.0f;
                RenderMesh(hwnd);
            }
            break;
        }
        case kBtnWireframe:
            g_state.render_flags ^= dmcresource::render_flag(RenderFlag::Wireframe);
            RenderMesh(hwnd);
            break;
        case kBtnHierarchy:
            g_state.render_flags ^= dmcresource::render_flag(RenderFlag::Hierarchy);
            RenderMesh(hwnd);
            break;
        case kBtnUv:
            OpenUvGallery(hwnd);
            break;
        case kBtnInfo:
            ShowResourceInfo(hwnd);
            break;
        default:
            break;
    }
    UpdateButtonStates();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void HandleButtonRightClick(HWND hwnd, int id) {
    // Right-click is the desktop analog of Android's long-press-for-info.
    switch (id) {
        case kBtnUv:
            ShowInspectionTopic(hwnd, InspectionTopic::Uv);
            break;
        case kBtnWireframe:
            ShowInspectionTopic(hwnd, InspectionTopic::Meshes);
            break;
        case kBtnHierarchy:
            ShowInspectionTopic(hwnd, InspectionTopic::Hierarchy);
            break;
        default:
            break;
    }
}

void ToggleFullscreen(HWND hwnd) {
    const DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!g_state.fullscreen) {
        MONITORINFO mi{sizeof(MONITORINFO)};
        if (GetWindowPlacement(hwnd, &g_state.saved_placement) &&
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                        mi.rcMonitor.right - mi.rcMonitor.left,
                        mi.rcMonitor.bottom - mi.rcMonitor.top,
                        SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_state.fullscreen = true;
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_state.saved_placement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                        SWP_FRAMECHANGED);
        g_state.fullscreen = false;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            g_state.main_window = hwnd;
            HMENU menu_bar = CreateMenu();
            HMENU file_menu = CreatePopupMenu();
            AppendMenuW(file_menu, MF_STRING, kMenuRegisterFileTypes,
                       L"&Register .scm/.ptx and add to \"Open with\"");
            AppendMenuW(menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"&File");
            SetMenu(hwnd, menu_bar);

            g_state.ui_font =
                CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
            g_state.title_font =
                CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
            LayoutButtons(hwnd);
            UpdateButtonStates();
            DragAcceptFiles(hwnd, TRUE);
            return 0;
        }
        case WM_SIZE:
            LayoutButtons(hwnd);
            RerenderThrottled(hwnd, true);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int id = HitTestButtons(g_state.header_buttons, pt);
            if (id < 0) id = HitTestButtons(g_state.tool_buttons, pt);
            if (id >= 0) {
                HandleButtonClick(hwnd, id);
                return 0;
            }
            if (g_state.child_browser_open) {
                const RECT viewport = ViewportRect(hwnd);
                if (PtInRect(&viewport, pt)) {
                    const int row = (pt.y - viewport.top - 12) / 30;
                    const auto count = static_cast<int>(
                        g_state.session ? dmcresource::session_child_count(g_state.session.get())
                                        : 0);
                    if (row >= 0 && row < count) OpenChildByIndex(hwnd, row);
                }
                return 0;
            }
            g_state.dragging = false;
            ReleaseCapture();
            RerenderThrottled(hwnd, true);
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int id = HitTestButtons(g_state.tool_buttons, pt);
            if (id >= 0) HandleButtonRightClick(hwnd, id);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const RECT viewport = ViewportRect(hwnd);
            if (!g_state.child_browser_open && PtInRect(&viewport, pt) && g_state.session &&
                g_state.session->renderable) {
                g_state.dragging = true;
                g_state.drag_start = pt;
                g_state.drag_start_yaw = g_state.yaw;
                g_state.drag_start_pitch = g_state.pitch;
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (g_state.child_browser_open) {
                const RECT viewport = ViewportRect(hwnd);
                int hot = -1;
                if (PtInRect(&viewport, pt)) {
                    const int row = (pt.y - viewport.top - 12) / 30;
                    const auto count = static_cast<int>(
                        g_state.session ? dmcresource::session_child_count(g_state.session.get())
                                        : 0);
                    if (row >= 0 && row < count) hot = row;
                }
                if (hot != g_state.child_hot) {
                    g_state.child_hot = hot;
                    InvalidateRect(hwnd, &viewport, FALSE);
                }
                return 0;
            }
            if (g_state.dragging) {
                const int dx = pt.x - g_state.drag_start.x;
                const int dy = pt.y - g_state.drag_start.y;
                g_state.yaw = g_state.drag_start_yaw + dx * 0.008f;
                g_state.pitch = g_state.drag_start_pitch + dy * 0.008f;
                g_state.pitch = (std::max)(-1.55f, (std::min)(1.55f, g_state.pitch));
                RerenderThrottled(hwnd, false);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (g_state.child_browser_open || !g_state.session || !g_state.session->renderable)
                return 0;
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            g_state.zoom *= (delta > 0) ? 1.1f : (1.0f / 1.1f);
            g_state.zoom = (std::max)(0.15f, (std::min)(8.0f, g_state.zoom));
            RerenderThrottled(hwnd, true);
            return 0;
        }
        case WM_KEYDOWN: {
            switch (wparam) {
                case VK_F11:
                    ToggleFullscreen(hwnd);
                    return 0;
                case VK_ESCAPE:
                    if (g_state.fullscreen) ToggleFullscreen(hwnd);
                    return 0;
                case VK_BACK:
                    NavigateBack(hwnd);
                    UpdateButtonStates();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case VK_LEFT:
                case VK_UP:
                    NavigateSibling(hwnd, -1);
                    UpdateButtonStates();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case VK_RIGHT:
                case VK_DOWN:
                    NavigateSibling(hwnd, 1);
                    UpdateButtonStates();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case 'W':
                    if (!g_state.child_browser_open) HandleButtonClick(hwnd, kBtnWireframe);
                    return 0;
                default:
                    return 0;
            }
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == kMenuRegisterFileTypes) {
                RegisterFileAssociations();
                MessageBoxW(hwnd,
                           L"Registered. .scm and .ptx now open with this reader by default; "
                           L".mod and .dds were added to \"Open with\" without changing your "
                           L"current default.",
                           L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintChrome(hdc, hwnd);
            PaintViewport(hdc, ViewportRect(hwnd));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wparam);
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) LoadFile(hwnd, path, true);
            DragFinish(drop);
            UpdateButtonStates();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // WM_PAINT repaints the whole client area every time.
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

void RegisterFileAssociations() {
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    const std::wstring exe(exe_path);
    const std::wstring open_command = L"\"" + exe + L"\" \"%1\"";
    const std::wstring prog_id = L"DMCNativeReader.Resource";

    auto set_key = [](const std::wstring& path, const std::wstring& value) {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                            &key, nullptr) != ERROR_SUCCESS) {
            return;
        }
        RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    };

    set_key(L"Software\\Classes\\" + prog_id, L"DMC Native Reader resource");
    set_key(L"Software\\Classes\\" + prog_id + L"\\DefaultIcon", exe + L",0");
    set_key(L"Software\\Classes\\" + prog_id + L"\\shell\\open\\command", open_command);

    for (const auto& ext : {L".scm", L".ptx"}) {
        set_key(L"Software\\Classes\\" + std::wstring(ext), prog_id);
    }

    set_key(L"Software\\Classes\\Applications\\DMCNativeReader.exe\\shell\\open\\command",
           open_command);
    for (const auto& ext : {L".dds", L".mod", L".scm", L".ptx"}) {
        HKEY key;
        const std::wstring path = L"Software\\Classes\\" + std::wstring(ext) +
                                  L"\\OpenWithList\\DMCNativeReader.exe";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                            &key, nullptr) == ERROR_SUCCESS) {
            RegCloseKey(key);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const wchar_t* kClassName = L"DmcNativeReaderWinShell";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // WM_ERASEBKGND is suppressed; WM_PAINT owns the surface.
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, L"DMC Native Reader",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                               nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) return 0;

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        if (argc >= 2) LoadFile(hwnd, argv[1], true);
        LocalFree(argv);
    }
    UpdateButtonStates();
    InvalidateRect(hwnd, nullptr, TRUE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
