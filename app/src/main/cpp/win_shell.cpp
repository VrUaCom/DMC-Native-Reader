// Windows shell for DMC Native Reader.
//
// This is a thin Win32 host, mirroring app_native.cpp's role for Android:
// it owns the window, file dialog, pixel blit and Explorer integration only.
// Parsing, inspection and rendering all come from DMCNativeReader::Core (the
// same portable core the Android app links), so Windows reads DMC resources
// through the exact same code path as the phone build.

#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/view_renderer.h"

void RegisterFileAssociations();

namespace {

using dmcresource::InspectionTopic;
using dmcresource::RenderFlag;
using dmcresource::RgbaImage;
using dmcresource::Session;

constexpr int kPanelWidth = 380;
constexpr int kChildListHeight = 140;
constexpr UINT_PTR kEditControlId = 1001;
constexpr UINT_PTR kChildListId = 1002;

enum MenuId : int {
    kMenuOpen = 1,
    kMenuAttachPtx = 2,
    kMenuRegisterFileTypes = 3,
    kMenuFlagWireframe = 10,
    kMenuFlagHierarchy = 11,
    kMenuFlagBounds = 12,
    kMenuFlagSkinDebug = 13,
    kMenuFlagNormals = 14,
    kMenuFlagUvLayout = 15,
    kMenuUvGallery = 20,
    kMenuTopicMeshes = 30,
    kMenuTopicUv = 31,
    kMenuTopicHierarchy = 32,
};

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    }
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                        nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr,
                            nullptr);
    }
    return out;
}

std::vector<std::uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

struct AppState {
    std::unique_ptr<Session> session;
    RgbaImage display;  // Currently blitted pixels (BGRA, top-down).

    float yaw = 0.65f;
    float pitch = -0.45f;
    float zoom = 1.0f;
    bool flag_wireframe = false;
    bool flag_hierarchy = false;
    bool flag_bounds = false;
    bool flag_skin_debug = false;
    bool flag_normals = false;
    bool flag_uv_layout = false;

    bool dragging = false;
    POINT drag_start{};
    float drag_start_yaw = 0.0f;
    float drag_start_pitch = 0.0f;

    HWND edit_control = nullptr;
    HWND child_list = nullptr;
    HMENU menu_bar = nullptr;
};

AppState g_state;

RgbaImage ToBgra(const RgbaImage& rgba) {
    RgbaImage out;
    out.width = rgba.width;
    out.height = rgba.height;
    out.pixels = rgba.pixels;
    for (std::size_t i = 0; i + 3 < out.pixels.size(); i += 4) {
        std::swap(out.pixels[i + 0], out.pixels[i + 2]);  // RGBA -> BGRA
    }
    return out;
}

std::uint32_t CurrentRenderFlags() {
    std::uint32_t flags = 0;
    if (g_state.flag_wireframe) flags |= dmcresource::render_flag(RenderFlag::Wireframe);
    if (g_state.flag_hierarchy) flags |= dmcresource::render_flag(RenderFlag::Hierarchy);
    if (g_state.flag_bounds) flags |= dmcresource::render_flag(RenderFlag::Bounds);
    if (g_state.flag_skin_debug) flags |= dmcresource::render_flag(RenderFlag::SkinDebug);
    if (g_state.flag_normals) flags |= dmcresource::render_flag(RenderFlag::Normals);
    if (g_state.flag_uv_layout) flags |= dmcresource::render_flag(RenderFlag::UvLayout);
    return flags;
}

void SetInspectionText(const std::string& text) {
    if (g_state.edit_control == nullptr) return;
    SetWindowTextW(g_state.edit_control, Utf8ToWide(text).c_str());
}

RECT ViewportRect(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT viewport = client;
    viewport.left = kPanelWidth;
    return viewport;
}

void RefreshChildList() {
    if (g_state.child_list == nullptr) return;
    SendMessageW(g_state.child_list, LB_RESETCONTENT, 0, 0);
    if (!g_state.session) return;
    const auto count = dmcresource::session_child_count(g_state.session.get());
    for (std::size_t i = 0; i < count; ++i) {
        const auto title = dmcresource::session_child_title(g_state.session.get(),
                                                             static_cast<int>(i));
        SendMessageW(g_state.child_list, LB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(Utf8ToWide(title).c_str()));
    }
}

std::string BuildInspectionText(const Session& session) {
    std::string text = dmcresource::describe_session(&session);
    text += "\r\n\r\n";
    text += dmcresource::format_inspection_tree(session.inspection);
    if (!session.texture_attachment_detail.empty()) {
        text += "\r\n\r\n[PTX] " + session.texture_attachment_detail;
    }
    const auto child_count = dmcresource::session_child_count(&session);
    if (child_count > 0) {
        text += "\r\n\r\n" + std::to_string(child_count) +
                " child resource(s) below -- double-click to open.";
    }
    if (session.renderable) {
        text += "\r\n\r\n[drag = rotate, wheel = zoom -- see View menu for overlays]";
    }
    return text;
}

void Rerender(HWND hwnd) {
    if (!g_state.session || !g_state.session->renderable) return;
    const RECT view = ViewportRect(hwnd);
    const int width = (std::max)(16L, view.right - view.left);
    const int height = (std::max)(16L, view.bottom - view.top);
    RgbaImage rendered = dmcresource::render_session(
        g_state.session.get(), width, height, g_state.yaw, g_state.pitch,
        g_state.zoom, CurrentRenderFlags());
    g_state.display = ToBgra(rendered);
    InvalidateRect(hwnd, &view, FALSE);
}

// Takes ownership of a freshly-opened Session (top-level file, child resource
// or UV gallery -- Architecture v2 treats them identically) and updates every
// panel: inspection text, child list, and the viewport/image display.
void ActivateSession(HWND hwnd, std::unique_ptr<Session> session) {
    g_state.session = std::move(session);
    g_state.yaw = 0.65f;
    g_state.pitch = -0.45f;
    g_state.zoom = 1.0f;

    if (!g_state.session) {
        SetInspectionText("This resource was not accepted by any native module.");
        g_state.display = RgbaImage{};
        RefreshChildList();
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }

    SetInspectionText(BuildInspectionText(*g_state.session));
    RefreshChildList();

    if (g_state.session->renderable) {
        Rerender(hwnd);
    } else if (g_state.session->image_preview.available()) {
        RgbaImage img;
        img.width = static_cast<int>(g_state.session->image_preview.width);
        img.height = static_cast<int>(g_state.session->image_preview.height);
        img.pixels = g_state.session->image_preview.rgba8;
        g_state.display = ToBgra(img);
        InvalidateRect(hwnd, nullptr, TRUE);
    } else {
        g_state.display = RgbaImage{};
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void LoadFile(HWND hwnd, const std::wstring& path) {
    const auto bytes = ReadFileBytes(path);
    if (bytes.empty() && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetInspectionText("Failed to open file.");
        return;
    }
    const std::string name = WideToUtf8(path);
    std::unique_ptr<Session> session;
    try {
        session = dmcresource::open_session(name, bytes.data(), bytes.size());
    } catch (...) {
        session.reset();
    }
    ActivateSession(hwnd, std::move(session));
}

void OpenFileDialog(HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"DMC resources (*.mod;*.scm;*.dds;*.ptx)\0*.mod;*.scm;*.dds;*.ptx\0"
        L"All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    LoadFile(hwnd, path);
}

void AttachPtxDialog(HWND hwnd) {
    if (!g_state.session) {
        MessageBoxW(hwnd, L"Open a MOD or SCM model first.", L"DMC Native Reader",
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PTX texture (*.ptx)\0*.ptx\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    const auto bytes = ReadFileBytes(path);
    const std::string name = WideToUtf8(std::wstring(path));
    const bool attached = dmcresource::attach_session_ptx(
        g_state.session.get(), name, bytes.data(), bytes.size());
    if (!attached) {
        MessageBoxW(hwnd, L"PTX companion was not accepted for this model.",
                   L"DMC Native Reader", MB_OK | MB_ICONWARNING);
    }
    SetInspectionText(BuildInspectionText(*g_state.session));
    Rerender(hwnd);
}

void OpenChild(HWND hwnd, int index) {
    if (!g_state.session) return;
    try {
        auto child = dmcresource::open_session_child(g_state.session.get(), index);
        ActivateSession(hwnd, std::move(child));
    } catch (...) {
        MessageBoxW(hwnd, L"Could not open child resource.", L"DMC Native Reader",
                   MB_OK | MB_ICONWARNING);
    }
}

void OpenUvGallery(HWND hwnd) {
    if (!g_state.session) return;
    try {
        auto gallery = dmcresource::open_uv_gallery(g_state.session.get());
        ActivateSession(hwnd, std::move(gallery));
    } catch (...) {
        MessageBoxW(hwnd, L"No UV gallery available for this resource.",
                   L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
    }
}

void ShowInspectionTopic(InspectionTopic topic) {
    if (!g_state.session) return;
    try {
        const auto doc = dmcresource::inspect_session(g_state.session.get(), topic);
        SetInspectionText(dmcresource::format_inspection_tree(doc));
    } catch (...) {
        SetInspectionText("Information unavailable for this topic.");
    }
}

void PaintViewport(HDC hdc, const RECT& viewport) {
    if (g_state.display.width <= 0 || g_state.display.height <= 0) {
        FillRect(hdc, &viewport, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(hdc, TRANSPARENT);
        const wchar_t* hint = L"File > Open a .mod / .scm / .dds / .ptx file";
        DrawTextW(hdc, hint, -1, const_cast<RECT*>(&viewport),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_state.display.width;
    bmi.bmiHeader.biHeight = -g_state.display.height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    FillRect(hdc, &viewport, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    StretchDIBits(hdc, viewport.left, viewport.top,
                  viewport.right - viewport.left, viewport.bottom - viewport.top,
                  0, 0, g_state.display.width, g_state.display.height,
                  g_state.display.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void CheckMenuFlag(HMENU menu, int id, bool checked) {
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            g_state.menu_bar = CreateMenu();
            HMENU file_menu = CreatePopupMenu();
            AppendMenuW(file_menu, MF_STRING, kMenuOpen, L"&Open...\tCtrl+O");
            AppendMenuW(file_menu, MF_STRING, kMenuAttachPtx,
                       L"&Attach PTX texture...");
            AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(file_menu, MF_STRING, kMenuRegisterFileTypes,
                       L"&Register .scm/.ptx and add to \"Open with\"");
            AppendMenuW(g_state.menu_bar, MF_POPUP,
                       reinterpret_cast<UINT_PTR>(file_menu), L"&File");

            HMENU view_menu = CreatePopupMenu();
            AppendMenuW(view_menu, MF_STRING, kMenuFlagWireframe, L"&Wireframe\tW");
            AppendMenuW(view_menu, MF_STRING, kMenuFlagHierarchy, L"&Hierarchy");
            AppendMenuW(view_menu, MF_STRING, kMenuFlagBounds, L"&Bounds");
            AppendMenuW(view_menu, MF_STRING, kMenuFlagSkinDebug, L"&Skin debug");
            AppendMenuW(view_menu, MF_STRING, kMenuFlagNormals, L"&Normals");
            AppendMenuW(view_menu, MF_STRING, kMenuFlagUvLayout, L"&UV layout");
            AppendMenuW(view_menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(view_menu, MF_STRING, kMenuUvGallery, L"UV &Gallery");
            AppendMenuW(g_state.menu_bar, MF_POPUP,
                       reinterpret_cast<UINT_PTR>(view_menu), L"&View");

            HMENU inspect_menu = CreatePopupMenu();
            AppendMenuW(inspect_menu, MF_STRING, kMenuTopicMeshes, L"&Meshes");
            AppendMenuW(inspect_menu, MF_STRING, kMenuTopicUv, L"&UV");
            AppendMenuW(inspect_menu, MF_STRING, kMenuTopicHierarchy, L"&Hierarchy");
            AppendMenuW(g_state.menu_bar, MF_POPUP,
                       reinterpret_cast<UINT_PTR>(inspect_menu), L"&Inspect");

            SetMenu(hwnd, g_state.menu_bar);

            g_state.edit_control = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                    ES_AUTOVSCROLL,
                0, 0, kPanelWidth, 100, hwnd,
                reinterpret_cast<HMENU>(kEditControlId),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwnd, GWLP_HINSTANCE)),
                nullptr);
            HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     FIXED_PITCH, L"Consolas");
            SendMessageW(g_state.edit_control, WM_SETFONT,
                        reinterpret_cast<WPARAM>(font), TRUE);

            g_state.child_list = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                0, 0, kPanelWidth, kChildListHeight, hwnd,
                reinterpret_cast<HMENU>(kChildListId),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwnd, GWLP_HINSTANCE)),
                nullptr);
            SendMessageW(g_state.child_list, WM_SETFONT,
                        reinterpret_cast<WPARAM>(font), TRUE);

            SetInspectionText(
                "DMC Native Reader -- Windows shell\r\n"
                "Same DMCNativeReader::Core as the Android app.\r\n\r\n"
                "File > Open to read a .mod / .scm / .dds / .ptx file,\r\n"
                "or double-click one in Explorer once file types are registered.");
            return 0;
        }
        case WM_SIZE: {
            RECT client{};
            GetClientRect(hwnd, &client);
            const int edit_height =
                (std::max)(0L, client.bottom - client.top - kChildListHeight);
            MoveWindow(g_state.edit_control, 0, 0, kPanelWidth, edit_height, TRUE);
            MoveWindow(g_state.child_list, 0, edit_height, kPanelWidth,
                      kChildListHeight, TRUE);
            Rerender(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (HIWORD(wparam) == LBN_DBLCLK &&
                reinterpret_cast<HWND>(lparam) == g_state.child_list) {
                const int sel = static_cast<int>(
                    SendMessageW(g_state.child_list, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR) OpenChild(hwnd, sel);
                return 0;
            }
            switch (id) {
                case kMenuOpen: OpenFileDialog(hwnd); break;
                case kMenuAttachPtx: AttachPtxDialog(hwnd); break;
                case kMenuRegisterFileTypes: {
                    RegisterFileAssociations();
                    MessageBoxW(hwnd,
                               L"Registered. .scm and .ptx now open with this reader "
                               L"by default; .mod and .dds were added to \"Open "
                               L"with\" without changing your current default.",
                               L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                case kMenuFlagWireframe:
                    g_state.flag_wireframe = !g_state.flag_wireframe;
                    CheckMenuFlag(g_state.menu_bar, id, g_state.flag_wireframe);
                    Rerender(hwnd);
                    break;
                case kMenuFlagHierarchy:
                    g_state.flag_hierarchy = !g_state.flag_hierarchy;
                    CheckMenuFlag(g_state.menu_bar, id, g_state.flag_hierarchy);
                    Rerender(hwnd);
                    break;
                case kMenuFlagBounds:
                    g_state.flag_bounds = !g_state.flag_bounds;
                    CheckMenuFlag(g_state.menu_bar, id, g_state.flag_bounds);
                    Rerender(hwnd);
                    break;
                case kMenuFlagSkinDebug:
                    g_state.flag_skin_debug = !g_state.flag_skin_debug;
                    CheckMenuFlag(g_state.menu_bar, id, g_state.flag_skin_debug);
                    Rerender(hwnd);
                    break;
                case kMenuFlagNormals:
                    g_state.flag_normals = !g_state.flag_normals;
                    CheckMenuFlag(g_state.menu_bar, id, g_state.flag_normals);
                    Rerender(hwnd);
                    break;
                case kMenuFlagUvLayout:
                    g_state.flag_uv_layout = !g_state.flag_uv_layout;
                    CheckMenuFlag(g_state.menu_bar, id, g_state.flag_uv_layout);
                    Rerender(hwnd);
                    break;
                case kMenuUvGallery: OpenUvGallery(hwnd); break;
                case kMenuTopicMeshes: ShowInspectionTopic(InspectionTopic::Meshes); break;
                case kMenuTopicUv: ShowInspectionTopic(InspectionTopic::Uv); break;
                case kMenuTopicHierarchy:
                    ShowInspectionTopic(InspectionTopic::Hierarchy);
                    break;
                default: break;
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wparam == 'W') {
                g_state.flag_wireframe = !g_state.flag_wireframe;
                CheckMenuFlag(g_state.menu_bar, kMenuFlagWireframe,
                             g_state.flag_wireframe);
                Rerender(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            g_state.dragging = true;
            g_state.drag_start.x = GET_X_LPARAM(lparam);
            g_state.drag_start.y = GET_Y_LPARAM(lparam);
            g_state.drag_start_yaw = g_state.yaw;
            g_state.drag_start_pitch = g_state.pitch;
            SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_state.dragging && g_state.session && g_state.session->renderable) {
                const int dx = GET_X_LPARAM(lparam) - g_state.drag_start.x;
                const int dy = GET_Y_LPARAM(lparam) - g_state.drag_start.y;
                g_state.yaw = g_state.drag_start_yaw + dx * 0.01f;
                g_state.pitch = g_state.drag_start_pitch + dy * 0.01f;
                Rerender(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            g_state.dragging = false;
            ReleaseCapture();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            g_state.zoom *= (delta > 0) ? 1.1f : (1.0f / 1.1f);
            g_state.zoom = (std::max)(0.05f, (std::min)(20.0f, g_state.zoom));
            Rerender(hwnd);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintViewport(hdc, ViewportRect(hwnd));
            EndPaint(hwnd, &ps);
            return 0;
        }
        // Explorer delivers "Open with" / drag-drop launches as WM_DROPFILES when
        // DragAcceptFiles is enabled, and as a plain command-line argument when
        // this .exe is the registered file-type handler (handled in wWinMain).
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wparam);
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) LoadFile(hwnd, path);
            DragFinish(drop);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

// Registers this reader per-user (HKCU, no admin required):
//   - .scm and .ptx as the default opener (both were unclaimed on this machine)
//   - .dds and .mod added to the Explorer "Open with" list only, since both
//     extensions already have an unrelated default (an image viewer for .dds,
//     Windows Media Player's MOD-camcorder handler for .mod) that must not be
//     silently overridden.
void RegisterFileAssociations() {
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    const std::wstring exe(exe_path);
    const std::wstring open_command = L"\"" + exe + L"\" \"%1\"";
    const std::wstring prog_id = L"DMCNativeReader.Resource";

    auto set_key = [](const std::wstring& path, const std::wstring& value) {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
                            KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
            return;
        }
        RegSetValueExW(key, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    };

    set_key(L"Software\\Classes\\" + prog_id, L"DMC Native Reader resource");
    set_key(L"Software\\Classes\\" + prog_id + L"\\DefaultIcon", exe + L",0");
    set_key(L"Software\\Classes\\" + prog_id + L"\\shell\\open\\command",
           open_command);

    for (const auto& ext : {L".scm", L".ptx"}) {
        set_key(L"Software\\Classes\\" + std::wstring(ext), prog_id);
    }

    const std::wstring app_key =
        L"Software\\Classes\\Applications\\DMCNativeReader.exe";
    set_key(app_key + L"\\shell\\open\\command", open_command);
    for (const auto& ext : {L".dds", L".mod", L".scm", L".ptx"}) {
        HKEY key;
        const std::wstring path =
            L"Software\\Classes\\" + std::wstring(ext) + L"\\OpenWithList\\DMCNativeReader.exe";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
                            KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            RegCloseKey(key);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
    const wchar_t* kClassName = L"DmcNativeReaderWinShell";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES, kClassName, L"DMC Native Reader", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720, nullptr, nullptr, instance,
        nullptr);
    if (hwnd == nullptr) return 0;

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    // Explorer double-click / "Open with" / drag-onto-icon all launch this exe
    // with the file path as argv[1] (registered via RegisterFileAssociations).
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        if (argc >= 2) LoadFile(hwnd, argv[1]);
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
