#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/portable_session.h"

namespace {
constexpr std::uint64_t kMaxResourceBytes = 512ULL * 1024ULL * 1024ULL;
constexpr UINT kMenuOpen = 1001;
constexpr UINT kMenuExit = 1002;
constexpr UINT kMenuWireframe = 1101;
constexpr UINT kMenuReset = 1102;
constexpr UINT kMenuInspector = 1103;
constexpr UINT kMenuAbout = 1201;

std::unique_ptr<dmcresource::PortableSession> g_session;
std::wstring g_file_name;
std::wstring g_status = L"Open a MOD, SCM, DDS or PTX resource.";
dmcresource::ViewState g_view;
std::size_t g_child_index = 0U;
bool g_dragging = false;
POINT g_drag_start{};
float g_drag_yaw = 0.0F;
float g_drag_pitch = 0.0F;

std::string utf8_from_wide(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring wide_from_utf8(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) return L"";
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

std::wstring base_name(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1U);
}

void reset_view() {
    g_view = {};
}

bool load_resource(HWND hwnd, const std::wstring& path) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!input) {
        MessageBoxW(hwnd, L"Could not open the selected file.", L"DMC Native Reader",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    const std::streamsize signed_size = input.tellg();
    if (signed_size <= 0 || static_cast<std::uint64_t>(signed_size) > kMaxResourceBytes) {
        MessageBoxW(hwnd, L"Resource is empty or exceeds the 512 MiB safety cap.",
                    L"DMC Native Reader", MB_OK | MB_ICONERROR);
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(signed_size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), signed_size)) {
        MessageBoxW(hwnd, L"Could not read the complete file.", L"DMC Native Reader",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring name_w = base_name(path);
    const std::string name = utf8_from_wide(name_w);
    std::string error;
    auto decoded = dmcresource::PortableSession::decode(name, bytes.data(), bytes.size(), &error);
    if (!decoded) {
        const std::wstring message = wide_from_utf8(error.empty()
            ? std::string_view{"Architecture v2 rejected this resource"}
            : std::string_view{error});
        MessageBoxW(hwnd, message.c_str(), L"DMC Native Reader", MB_OK | MB_ICONWARNING);
        return false;
    }

    g_session = std::move(decoded);
    g_file_name = name_w;
    g_status = wide_from_utf8(g_session->summary());
    g_child_index = 0U;
    reset_view();
    SetWindowTextW(hwnd, (L"DMC Native Reader — " + g_file_name).c_str());
    InvalidateRect(hwnd, nullptr, TRUE);
    return true;
}

void choose_file(HWND hwnd) {
    wchar_t file[MAX_PATH]{};
    const wchar_t filter[] =
        L"DMC Native Reader resources (*.mod;*.scm;*.dds;*.ptx)\0*.mod;*.scm;*.dds;*.ptx\0"
        L"All files (*.*)\0*.*\0\0";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) load_resource(hwnd, file);
}

void draw_rgba(HDC hdc, const RECT& dest,
               const std::uint8_t* rgba, std::size_t bytes,
               int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) return;
    const std::size_t expected = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) * 4U;
    if (bytes != expected) return;

    std::vector<std::uint8_t> bgra(expected);
    for (std::size_t i = 0; i < expected; i += 4U) {
        bgra[i + 0U] = rgba[i + 2U];
        bgra[i + 1U] = rgba[i + 1U];
        bgra[i + 2U] = rgba[i + 0U];
        bgra[i + 3U] = 0xFFU;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    const int area_w = static_cast<int>(std::max<LONG>(1, dest.right - dest.left));
    const int area_h = static_cast<int>(std::max<LONG>(1, dest.bottom - dest.top));
    const double scale = std::min(static_cast<double>(area_w) / width,
                                  static_cast<double>(area_h) / height);
    const int out_w = std::max(1, static_cast<int>(width * scale));
    const int out_h = std::max(1, static_cast<int>(height * scale));
    const int x = dest.left + (area_w - out_w) / 2;
    const int y = dest.top + (area_h - out_h) / 2;

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, x, y, out_w, out_h,
                  0, 0, width, height,
                  bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

const dmcresource::ImagePreview* current_preview() {
    if (!g_session) return nullptr;
    const auto& root = g_session->result().image_preview;
    if (root.available()) return &root;
    if (const auto* child = g_session->child(g_child_index)) {
        if (child->image_preview.available()) return &child->image_preview;
    }
    return nullptr;
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH background = CreateSolidBrush(RGB(11, 11, 14));
    FillRect(hdc, &client, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(235, 235, 235));
    RECT title = client;
    title.left += 14;
    title.top += 10;
    title.right -= 14;
    title.bottom = title.top + 52;
    std::wstring header = g_file_name.empty() ? L"DMC Native Reader 1.0 — Windows preview"
                                              : g_file_name + L"\n" + g_status;
    DrawTextW(hdc, header.c_str(), -1, &title, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

    RECT content = client;
    content.left += 10;
    content.right -= 10;
    content.top = 70;
    content.bottom -= 34;

    if (g_session) {
        if (g_session->has_geometry()) {
            const int w = static_cast<int>(std::clamp<LONG>(content.right - content.left, 64, 1600));
            const int h = static_cast<int>(std::clamp<LONG>(content.bottom - content.top, 64, 1200));
            const auto image = g_session->render(w, h, g_view);
            draw_rgba(hdc, content, image.pixels.data(), image.pixels.size(), image.width, image.height);
        } else if (const auto* preview = current_preview()) {
            draw_rgba(hdc, content, preview->rgba8.data(), preview->rgba8.size(),
                      static_cast<int>(preview->width), static_cast<int>(preview->height));
        }
    } else {
        SetTextColor(hdc, RGB(165, 165, 175));
        RECT empty = content;
        DrawTextW(hdc,
                  L"Drop a file here or choose File > Open.\n\nStable v1 families: MOD · SCM · DDS · PTX",
                  -1, &empty, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
    }

    SetTextColor(hdc, RGB(145, 145, 155));
    RECT footer = client;
    footer.left += 14;
    footer.right -= 14;
    footer.top = client.bottom - 28;
    std::wstring help = L"Drag: rotate · Wheel: zoom · W: wireframe · R: reset · I: inspector";
    if (g_session && g_session->child_count() > 0U) {
        help += L" · ←/→: PTX child " + std::to_wstring(g_child_index + 1U) + L"/" +
                std::to_wstring(g_session->child_count());
    }
    DrawTextW(hdc, help.c_str(), -1, &footer, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    EndPaint(hwnd, &ps);
}

void show_inspector(HWND hwnd) {
    if (!g_session) return;
    std::string text = g_session->inspection_text(700U);
    if (text.size() > 28000U) text.resize(28000U);
    const std::wstring wide = wide_from_utf8(text);
    MessageBoxW(hwnd, wide.c_str(), L"DMC Native Reader — Inspector", MB_OK | MB_ICONINFORMATION);
}

HMENU create_menu() {
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kMenuOpen, L"&Open…\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kMenuExit, L"E&xit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, kMenuWireframe, L"&Wireframe\tW");
    AppendMenuW(view, MF_STRING, kMenuReset, L"&Reset view\tR");
    AppendMenuW(view, MF_STRING, kMenuInspector, L"&Inspector\tI");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, kMenuAbout, L"&About");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return menu;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kMenuOpen: choose_file(hwnd); return 0;
        case kMenuExit: DestroyWindow(hwnd); return 0;
        case kMenuWireframe:
            g_view.wireframe = !g_view.wireframe;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kMenuReset:
            reset_view();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kMenuInspector: show_inspector(hwnd); return 0;
        case kMenuAbout:
            MessageBoxW(hwnd,
                        L"DMC Native Reader 1.0 — Windows preview\n\n"
                        L"Native C++20 Architecture v2 shell for MOD, SCM, DDS and PTX.",
                        L"About", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH) > 0) load_resource(hwnd, path);
        DragFinish(drop);
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_dragging = true;
        g_drag_start = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        g_drag_yaw = g_view.yaw_radians;
        g_drag_pitch = g_view.pitch_radians;
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging && (wparam & MK_LBUTTON) != 0U && g_session && g_session->has_geometry()) {
            const int dx = GET_X_LPARAM(lparam) - g_drag_start.x;
            const int dy = GET_Y_LPARAM(lparam) - g_drag_start.y;
            g_view.yaw_radians = g_drag_yaw + static_cast<float>(dx) * 0.01F;
            g_view.pitch_radians = std::clamp(g_drag_pitch + static_cast<float>(dy) * 0.01F,
                                              -1.55F, 1.55F);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        g_dragging = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEWHEEL:
        if (g_session && g_session->has_geometry()) {
            const float step = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
            g_view.zoom = std::clamp(g_view.zoom * (1.0F + step * 0.1F), 0.15F, 8.0F);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && wparam == 'O') {
            choose_file(hwnd);
            return 0;
        }
        if (wparam == 'W') {
            g_view.wireframe = !g_view.wireframe;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wparam == 'R') {
            reset_view();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wparam == 'I') {
            show_inspector(hwnd);
            return 0;
        }
        if (g_session && g_session->child_count() > 0U) {
            if (wparam == VK_RIGHT) {
                g_child_index = (g_child_index + 1U) % g_session->child_count();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wparam == VK_LEFT) {
                g_child_index = g_child_index == 0U ? g_session->child_count() - 1U
                                                    : g_child_index - 1U;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    const wchar_t class_name[] = L"DMCNativeReaderWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, class_name, L"DMC Native Reader",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
                                nullptr, create_menu(), instance, nullptr);
    if (hwnd == nullptr) return 1;
    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        if (argc > 1) load_resource(hwnd, argv[1]);
        LocalFree(argv);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
