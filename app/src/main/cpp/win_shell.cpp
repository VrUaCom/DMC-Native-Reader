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
#include <optional>
#include <string>
#include <vector>

#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/model_placement_actions.h"
#include "dmcresource/spider/session_actions.h"
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
    kBtnAddPart = 102,
    kBtnOpen = 110,
    kBtnResetExport = 111,
    kBtnWireframe = 112,
    kBtnHierarchy = 113,
    kBtnUv = 114,
    kBtnInfo = 115,
};

enum MenuId : int {
    kMenuRegisterFileTypes = 900,
    kMenuExportAll = 901,
    kMenuQualityLow = 910,
    kMenuQualityMedium = 911,
    kMenuQualityHigh = 912,
};

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
    for (const wchar_t* want : {L".mod", L".scm", L".dds", L".ptx", L".evt"}) {
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

    // compose_mod_sessions() only accepts raw, single-part MOD sessions as
    // input -- a composite Session's own top-level RenderScene is never
    // populated (only composite_parts[i].scene is), so has_geometry() on it
    // is always false and feeding a composite back in as a "part" is
    // rejected. There is no core primitive for incremental N-ary append, so
    // adding a 3rd+ part means re-opening every original source file and
    // recomposing all of them together from scratch. This tracks those
    // original paths across the session's lifetime for that purpose; it is
    // cleared whenever a fresh top-level resource is opened.
    std::vector<std::wstring> composite_source_paths;
    // Parallel to composite_source_paths (same indices). A recompose builds a
    // brand-new Session from scratch, so any texture attached to an earlier
    // part is otherwise silently lost the moment a further part is added.
    // Mirrors Android's MainActivity.modelPartPtxUris / reattachSavedPtxToComposite
    // pattern (confirmed present in the same v33 merge): remember which
    // texture file went on which part and reapply all of them right after
    // every recompose.
    std::vector<std::wstring> composite_part_textures;
    // Parallel to composite_source_paths. -1 = not placed (renders at the
    // host's origin); otherwise the host-joint index a recompose should
    // silently replay placement onto, same reasoning as
    // composite_part_textures above.
    std::vector<int> composite_part_joints;
    std::wstring current_path;

    RgbaImage rgba;   // Last rendered/previewed frame, RGBA (for PNG export).
    RgbaImage bgra;   // Same frame, byte-swapped for StretchDIBits.
    bool static_image = false;

    float yaw = 0.65f;
    float pitch = -0.45f;
    float zoom = 1.0f;
    std::uint32_t render_flags = 0;
    bool hierarchy_available = false;

    // Rebuilt alongside g_state.bgra every RenderMesh() call, in the same
    // image-pixel space as the rendered frame (see RenderMesh), so a
    // WM_MOUSEMOVE hit-test lands on the exact marker render_view drew --
    // not a second, possibly-drifted reimplementation of the camera math.
    std::vector<std::wstring> hierarchy_point_labels;  // "id: name", index-aligned.
    std::vector<dmcresource::HierarchyScreenPoint> hierarchy_screen_points;
    int hierarchy_hot = -1;
    POINT hierarchy_hover_pt{};

    bool dragging = false;
    POINT drag_start{};
    float drag_start_yaw = 0.0f;
    float drag_start_pitch = 0.0f;
    DWORD last_render_tick = 0;

    bool child_browser_open = false;
    int child_hot = -1;
    // Built once per ActivateSession, not per paint: session_child_preview()
    // fully re-renders a UV map from scratch on every call (render_uv_map),
    // so generating it inline in PaintChildBrowser would redo that render on
    // every WM_PAINT -- including ones triggered by nothing more than a
    // hover-highlight change.
    std::vector<RgbaImage> gallery_thumbnails;  // BGRA, index-aligned with children.

    // Folder sibling navigation (Left/Right arrow keys), independent of the
    // parent/child resource stack above.
    std::vector<std::wstring> siblings;
    int sibling_index = -1;

    bool fullscreen = false;
    WINDOWPLACEMENT saved_placement{sizeof(WINDOWPLACEMENT)};

    // Render resolution cap: lower trades sharpness for CPU rasterization
    // speed on weaker machines; 1024 is both the default and the core's own
    // hard ceiling (see ComputeRenderSize).
    int render_quality = 1024;

    std::vector<ToolButton> header_buttons;
    std::vector<ToolButton> tool_buttons;

    HWND main_window = nullptr;
    HFONT ui_font = nullptr;
    HFONT small_font = nullptr;
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
    bool can_add_model_part = false;
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
    c.can_add_model_part = has_state(bits, StateFlag::CanAddModelPart);
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
        // No PostQuitMessage here: this is a modal child window, not the main
        // window. WM_QUIT is thread-wide -- posting it from a dialog's
        // WM_DESTROY would terminate the whole app's message loop the moment
        // any dialog (Info, part/joint picker) closed, not just the dialog.
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

struct ListPickerState {
    HWND list = nullptr;
    int result = -1;
    bool done = false;
};
ListPickerState g_list_picker;

LRESULT CALLBACK ListPickerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kText);
            SetBkColor(hdc, RGB(20, 20, 24));
            static HBRUSH brush = CreateSolidBrush(RGB(20, 20, 24));
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == 2) {  // OK
                const int sel =
                    static_cast<int>(SendMessageW(g_list_picker.list, LB_GETCURSEL, 0, 0));
                g_list_picker.result = sel;
                g_list_picker.done = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wparam) == 3) {  // Cancel
                g_list_picker.done = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            g_list_picker.done = true;
            DestroyWindow(hwnd);
            return 0;
        // Same reason as InfoDlgProc above: no PostQuitMessage in a modal
        // child's WM_DESTROY.
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

// Generic dark listbox modal shared by the composite-part and skeleton-joint
// pickers. Returns the chosen index, or -1 if cancelled.
int PickFromList(HWND owner, const std::wstring& title, const std::wstring& ok_label,
                 const std::vector<std::wstring>& labels, int default_index) {
    const wchar_t* kClass = L"DmcListPicker";
    WNDCLASSW wc{};
    wc.lpfnWndProc = ListPickerProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(owner, GWLP_HINSTANCE));
    wc.lpszClassName = kClass;
    wc.hbrBackground = CreateSolidBrush(RGB(20, 20, 24));
    RegisterClassW(&wc);

    g_list_picker = ListPickerState{};
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                               380, 340, owner, nullptr, wc.hInstance, nullptr);
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 8, 8, 348, 250,
                                dlg, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
    g_list_picker.list = list;
    for (const auto& label : labels) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>((std::max)(0, default_index)), 0);
    CreateWindowExW(0, L"BUTTON", ok_label.c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 188,
                   268, 80, 28, dlg, reinterpret_cast<HMENU>(2), wc.hInstance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 276, 268, 80,
                   28, dlg, reinterpret_cast<HMENU>(3), wc.hInstance, nullptr);

    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG msg;
    while (!g_list_picker.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return g_list_picker.result;
}

// Composite sessions can attach a texture companion to any one part; a single
// -part/non-composite session has nothing to disambiguate. Returns the chosen
// part index, or -1 if the picker was cancelled.
int PickCompositePart(HWND owner, const Session& session) {
    const auto count = dmcresource::session_composite_part_count(&session);
    if (count == 0) return 0;  // Non-composite: attach targets the whole session.

    std::vector<std::wstring> labels;
    labels.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = dmcresource::session_composite_part_name(&session, static_cast<int>(i));
        labels.push_back(L"[" + std::to_wstring(i) + L"] " + Utf8ToWide(name) +
                         (i == 0 ? L" (primary host)" : L""));
    }
    return PickFromList(owner, L"Attach texture to which part?", L"Attach", labels, 0);
}

// Host joints come from the primary host's own RenderScene::nodes -- the same
// named/indexed hierarchy the "H" inspection button already reads (see
// session_inspection.cpp's append_hierarchy). default_attachment_selector is
// the format's own evidence-backed suggested joint (e.g. Header::
// default_joint_index() for MOD) and is pre-selected when present; it does
// not by itself authorize placement, the user still confirms it here.
int PickHostJoint(HWND owner, const std::vector<dmcresource::RenderNode>& nodes,
                  std::optional<std::uint32_t> default_selector) {
    if (nodes.empty()) return -1;
    std::vector<std::wstring> labels;
    labels.reserve(nodes.size());
    int default_index = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::wstring name =
            nodes[i].name.empty() ? L"Node " + std::to_wstring(i) : Utf8ToWide(nodes[i].name);
        std::wstring label = L"[" + std::to_wstring(i) + L"] " + name;
        if (default_selector && *default_selector == i) {
            label += L" (suggested)";
            default_index = static_cast<int>(i);
        }
        labels.push_back(std::move(label));
    }
    return PickFromList(owner, L"Attach new part to which host joint?", L"Attach", labels,
                        default_index);
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
    const auto part_count = dmcresource::session_composite_part_count(g_state.session.get());
    if (part_count > 0) {
        text += "\r\n\r\nCOMPOSITE PARTS (" + std::to_string(part_count) + ")\r\n";
        for (std::size_t i = 0; i < part_count; ++i) {
            const auto name =
                dmcresource::session_composite_part_name(g_state.session.get(), static_cast<int>(i));
            text += "  [" + std::to_string(i) + "] " + (i == 0 ? name + " (primary host)" : name);
            const bool attached = i < g_state.session->composite_parts.size() &&
                g_state.session->composite_parts[i].texture_companion_attached;
            text += attached ? "  [textured]\r\n" : "\r\n";
        }
    }
    return text;
}

// render_session clamps each axis to [64, 1024] independently (see
// resource_session.cpp) -- 1024 is a hard core ceiling this shell cannot
// exceed no matter how high g_state.render_quality is set; the quality
// setting only lets a weaker machine ask for *less* than that, trading
// visible sharpness for rasterization speed, not exceed the core's own cap.
// Requesting the viewport's raw width/height verbatim lets a wide desktop
// window clamp only its (wider) width, returning an image whose aspect ratio
// no longer matches the viewport -- then blitting that mismatched image to
// fill the viewport distorts the model. Scale both axes down together
// first, the same way DmcRenderView.renderWidth()/Height() cap to a
// performance budget on Android, so the returned image's aspect always
// matches the viewport it will be stretched into.
void ComputeRenderSize(int viewport_w, int viewport_h, int* out_w, int* out_h) {
    const int kMaxDim = g_state.render_quality;
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

// Inverse of the StretchDIBits blit in PaintViewport's non-static-image
// branch, which stretches g_state.bgra to fill the whole viewport rect.
// ComputeRenderSize keeps the rendered image's aspect ratio equal to the
// viewport's, so this is a uniform scale, not a letterboxed one.
bool ViewportPointToImage(const RECT& viewport, int image_w, int image_h, POINT pt,
                          float* out_x, float* out_y) {
    if (image_w <= 0 || image_h <= 0 || !PtInRect(&viewport, pt)) return false;
    const int vw = viewport.right - viewport.left;
    const int vh = viewport.bottom - viewport.top;
    if (vw <= 0 || vh <= 0) return false;
    *out_x = static_cast<float>(pt.x - viewport.left) * image_w / vw;
    *out_y = static_cast<float>(pt.y - viewport.top) * image_h / vh;
    return true;
}

POINT ImagePointToViewport(const RECT& viewport, int image_w, int image_h, float ix, float iy) {
    if (image_w <= 0 || image_h <= 0) return POINT{viewport.left, viewport.top};
    const int vw = viewport.right - viewport.left;
    const int vh = viewport.bottom - viewport.top;
    return POINT{
        viewport.left + static_cast<int>(std::lround(ix * vw / image_w)),
        viewport.top + static_cast<int>(std::lround(iy * vh / image_h))};
}

int HierarchyHitTest(float image_x, float image_y) {
    constexpr float kHitRadius = 10.0f;
    int best = -1;
    float best_dist_sq = kHitRadius * kHitRadius;
    for (std::size_t i = 0; i < g_state.hierarchy_screen_points.size(); ++i) {
        const auto& p = g_state.hierarchy_screen_points[i];
        const float dx = p.x - image_x;
        const float dy = p.y - image_y;
        const float dist_sq = dx * dx + dy * dy;
        if (dist_sq <= best_dist_sq) {
            best_dist_sq = dist_sq;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// render_session() has a special-case branch that renders a UV map straight
// from Session::uv_gallery/uv_map_index -- it runs before the function's own
// `if (!renderable) return {};` guard, so it works even though a UV-leaf
// session (one specific map opened out of a gallery) is never marked
// renderable itself. Gating the shell's own render call on `renderable`
// alone skipped that branch entirely and left the viewport blank.
bool IsUvLeaf(const Session& session) {
    return static_cast<bool>(session.uv_gallery) && session.uv_map_index.has_value();
}

void RefreshHierarchyHitTestCache(int width, int height) {
    g_state.hierarchy_point_labels.clear();
    g_state.hierarchy_screen_points.clear();
    g_state.hierarchy_hot = -1;
    if (!g_state.session) return;
    const bool showing_hierarchy = dmcresource::has_render_flag(
        g_state.render_flags, RenderFlag::Hierarchy);
    const auto& overlay = g_state.session->hierarchy_overlay;
    if (!showing_hierarchy || !overlay.available()) return;

    dmcresource::ViewState view;
    view.yaw_radians = g_state.yaw;
    view.pitch_radians = (std::max)(-1.55f, (std::min)(1.55f, g_state.pitch));
    view.zoom = (std::max)(0.15f, (std::min)(8.0f, g_state.zoom));
    // render_session() clamps its own width/height to [64, 1024] before it
    // ever reaches render_view -- matching that here keeps this cache in the
    // exact image-pixel space the marker was actually drawn in.
    const int clamped_w = (std::max)(64, (std::min)(1024, width));
    const int clamped_h = (std::max)(64, (std::min)(1024, height));
    g_state.hierarchy_screen_points = dmcresource::project_hierarchy_points(
        g_state.session->render_mesh, overlay, clamped_w, clamped_h, view);

    const auto& nodes = g_state.session->scene.nodes;
    g_state.hierarchy_point_labels.reserve(g_state.hierarchy_screen_points.size());
    for (std::size_t i = 0; i < g_state.hierarchy_screen_points.size(); ++i) {
        std::wstring label = L"#" + std::to_wstring(i);
        if (i < nodes.size() && !nodes[i].name.empty()) {
            label += L"  " + Utf8ToWide(nodes[i].name);
        }
        g_state.hierarchy_point_labels.push_back(std::move(label));
    }
}

void RenderMesh(HWND hwnd) {
    if (!g_state.session || (!g_state.session->renderable && !IsUvLeaf(*g_state.session))) return;
    const RECT view = ViewportRect(hwnd);
    int width = 0, height = 0;
    ComputeRenderSize(view.right - view.left, view.bottom - view.top, &width, &height);
    g_state.rgba = dmcresource::render_session(g_state.session.get(), width, height,
                                               g_state.yaw, g_state.pitch, g_state.zoom,
                                               g_state.render_flags);
    g_state.bgra = ToBgra(g_state.rgba);
    g_state.static_image = false;
    g_state.last_render_tick = GetTickCount();
    RefreshHierarchyHitTestCache(width, height);
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
    g_state.hierarchy_point_labels.clear();
    g_state.hierarchy_screen_points.clear();
    g_state.hierarchy_hot = -1;
    g_state.static_image = false;
    g_state.child_browser_open = false;
    g_state.gallery_thumbnails.clear();
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
        const auto count = dmcresource::session_child_count(g_state.session.get());
        g_state.gallery_thumbnails.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            dmcresource::ImagePreview scratch;
            const auto* preview = dmcresource::session_child_preview(
                g_state.session.get(), static_cast<int>(i), &scratch);
            if (preview == nullptr || !preview->available()) continue;
            RgbaImage img;
            img.width = static_cast<int>(preview->width);
            img.height = static_cast<int>(preview->height);
            img.pixels = preview->rgba8;
            g_state.gallery_thumbnails[i] = ToBgra(img);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
    } else if (caps.can_preview_image) {
        LoadStaticPreview(hwnd);
    } else if (g_state.session->renderable || IsUvLeaf(*g_state.session)) {
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
    g_state.composite_source_paths.clear();
    g_state.composite_part_textures.clear();
    g_state.composite_part_joints.clear();
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
        g_state.current_path = path;
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

// OFN_ALLOWMULTISELECT packs the result as "<dir>\0name1\0name2\0...\0\0"
// when 2+ files are picked in the same folder, or just "<full path>\0\0"
// for exactly one -- matches Android's multi-select MOD-part picker
// (MainActivity.selectedUris / REQUEST_ADD_MOD_PARTS) so adding several
// parts at once doesn't need repeated "+MOD" clicks.
std::vector<std::wstring> PickMultipleFiles(HWND owner, const wchar_t* filter) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return {};

    std::vector<std::wstring> result;
    const wchar_t* p = buffer.data();
    const std::wstring first(p);
    p += first.size() + 1;
    if (*p == L'\0') {
        // Single selection: lpstrFile is the whole path already.
        result.push_back(first);
        return result;
    }
    const std::wstring dir = first;
    while (*p != L'\0') {
        const std::wstring name(p);
        result.push_back(dir + L"\\" + name);
        p += name.size() + 1;
    }
    return result;
}

void OpenFileDialog(HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"DMC resources (*.mod;*.scm;*.dds;*.ptx;*.evt)\0*.mod;*.scm;*.dds;*.ptx;*.evt\0"
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

    const int part_index = PickCompositePart(hwnd, *g_state.session);
    if (part_index < 0) return;  // Picker cancelled.
    const auto part_count = dmcresource::session_composite_part_count(g_state.session.get());

    const auto bytes = ReadFileBytes(path);
    const std::string name = WideToUtf8(std::wstring(path));
    const bool attached = part_count == 0
        ? dmcresource::spider::actions::attach_ptx(g_state.session.get(), name, bytes.data(),
                                                   bytes.size())
        : dmcresource::spider::actions::attach_ptx_to_part(
              g_state.session.get(), part_index, name, bytes.data(), bytes.size());
    // attach_part() (behind attach_ptx_to_part) writes the rejection/success
    // detail to the session-level field regardless of which part was
    // targeted -- CompositePart::texture_attachment_detail is never set by
    // it. Reading the per-part field here always produced an empty string on
    // a per-part rejection, hiding the real reason (e.g. "model requests
    // texture slot 1 but PTX does not expose that slot") behind a generic
    // "not accepted" message.
    const std::wstring detail = Utf8ToWide(g_state.session->texture_attachment_detail);
    MessageBoxW(hwnd, detail.empty() ? (attached ? L"Texture companion attached."
                                                 : L"Texture companion was not accepted.")
                                     : detail.c_str(),
               L"DMC Native Reader", MB_OK | (attached ? MB_ICONINFORMATION : MB_ICONWARNING));

    // Remember it so a later "+MOD" recompose (which rebuilds the whole
    // Session from scratch) can reapply it automatically instead of quietly
    // dropping it -- see composite_part_textures' declaration.
    if (attached && part_count > 0 &&
        static_cast<std::size_t>(part_index) < g_state.composite_part_textures.size()) {
        g_state.composite_part_textures[static_cast<std::size_t>(part_index)] = path;
    }

    if (g_state.session->renderable) RenderMesh(hwnd);
}

void AddModPartDialog(HWND hwnd) {
    if (!g_state.session) return;
    // Multi-select: matches Android's REQUEST_ADD_MOD_PARTS picker, which
    // takes several URIs in one action rather than needing "+MOD" clicked
    // once per part.
    const auto picked =
        PickMultipleFiles(hwnd, L"MOD model (*.mod)\0*.mod\0All files\0*.*\0");
    if (picked.empty()) return;

    // compose_mod_sessions() rejects a composite Session used as a source
    // (see the AppState::composite_source_paths comment), so 3+ parts means
    // recomposing every original source from scratch rather than folding new
    // ones into the existing composite in place.
    if (g_state.composite_source_paths.empty()) {
        if (g_state.current_path.empty()) {
            MessageBoxW(hwnd, L"Open a MOD file first (not a freshly-composed session).",
                       L"DMC Native Reader", MB_OK | MB_ICONWARNING);
            return;
        }
        g_state.composite_source_paths.push_back(g_state.current_path);
        g_state.composite_part_textures.push_back(L"");
        g_state.composite_part_joints.push_back(-1);
    }
    const std::size_t first_new_index = g_state.composite_source_paths.size();
    for (const auto& p : picked) {
        bool duplicate = false;
        for (const auto& existing : g_state.composite_source_paths) {
            if (_wcsicmp(existing.c_str(), p.c_str()) == 0) { duplicate = true; break; }
        }
        if (duplicate) continue;
        g_state.composite_source_paths.push_back(p);
        g_state.composite_part_textures.push_back(L"");
        g_state.composite_part_joints.push_back(-1);
    }
    if (g_state.composite_source_paths.size() == first_new_index) {
        MessageBoxW(hwnd, L"Those MOD files are already part of this composite.",
                   L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::vector<std::unique_ptr<Session>> opened;
    std::vector<const Session*> parts;
    std::vector<std::string> names;
    opened.reserve(g_state.composite_source_paths.size());
    parts.reserve(g_state.composite_source_paths.size());
    names.reserve(g_state.composite_source_paths.size());
    for (const auto& source_path : g_state.composite_source_paths) {
        const auto bytes = ReadFileBytes(source_path);
        std::unique_ptr<Session> part;
        try {
            part = dmcresource::open_session(WideToUtf8(source_path), bytes.data(), bytes.size());
        } catch (...) {
            part.reset();
        }
        if (!part) {
            MessageBoxW(hwnd,
                       (L"Could not reopen an earlier composite source: " +
                        std::wstring(PathFindFileNameW(source_path.c_str())))
                           .c_str(),
                       L"DMC Native Reader", MB_OK | MB_ICONWARNING);
            g_state.composite_source_paths.resize(first_new_index);
            g_state.composite_part_textures.resize(first_new_index);
            g_state.composite_part_joints.resize(first_new_index);
            return;
        }
        names.push_back(WideToUtf8(std::wstring(PathFindFileNameW(source_path.c_str()))));
        parts.push_back(part.get());
        opened.push_back(std::move(part));
    }

    auto composite = dmcresource::spider::actions::compose_mod_sessions(parts, names);
    if (!composite) {
        MessageBoxW(hwnd, L"Could not compose these MOD parts together.", L"DMC Native Reader",
                   MB_OK | MB_ICONWARNING);
        g_state.composite_source_paths.resize(first_new_index);
        g_state.composite_part_textures.resize(first_new_index);
        g_state.composite_part_joints.resize(first_new_index);
        return;
    }
    std::wstring title;
    for (const auto& source_path : g_state.composite_source_paths) {
        if (!title.empty()) title += L" + ";
        title += PathFindFileNameW(source_path.c_str());
    }

    // Reapply every previously-attached per-part texture -- a recompose
    // rebuilds the Session from scratch, so without this an earlier "+MOD"
    // would silently undo any DDS/PTX already attached to an existing part.
    for (std::size_t i = 0;
        i < composite->composite_parts.size() && i < g_state.composite_part_textures.size();
        ++i) {
        if (g_state.composite_part_textures[i].empty()) continue;
        const auto tex_bytes = ReadFileBytes(g_state.composite_part_textures[i]);
        // Best-effort: a failed reattach just leaves that part textureless
        // again, same as if the user hadn't attached one yet.
        const auto reattach = dmcresource::spider::actions::attach_ptx_to_part(
            composite.get(), static_cast<int>(i), WideToUtf8(g_state.composite_part_textures[i]),
            tex_bytes.data(), tex_bytes.size());
        (void)reattach;
    }

    // Placement: replay every part's previously-successful joint (same
    // reason as textures above), and auto-place every newly-added part using
    // that CHILD PART's OWN default joint (MOD header +0x13,
    // default_joint_index(), exposed per-scene as
    // RenderScene::default_attachment_selector -- see
    // dmc3-mod-cross-model-default-joint-2026-09-15.md) -- not the host's.
    // Each source MOD publishes its own selector (e.g. em028_004 -> joint 0,
    // em028_005 -> joint 1), so reading it from composite_parts[i] rather
    // than composite_parts.front() (the host) is what makes every part land
    // on ITS correct socket instead of every new part piling onto whichever
    // joint the host itself happens to prefer.
    const auto host_nodes = composite->composite_parts.empty()
        ? std::vector<dmcresource::RenderNode>{}
        : composite->composite_parts.front().scene.nodes;
    for (std::size_t i = 1;
        i < composite->composite_parts.size() && i < g_state.composite_part_joints.size(); ++i) {
        int joint = g_state.composite_part_joints[i];
        const bool is_new = i >= first_new_index;
        if (joint < 0 && is_new) {
            const auto own_selector = composite->composite_parts[i].scene.default_attachment_selector;
            if (own_selector.has_value()) joint = static_cast<int>(*own_selector);
        }
        if (joint < 0 || host_nodes.empty() || joint >= static_cast<int>(host_nodes.size())) {
            continue;
        }
        const auto result = dmcresource::spider::actions::attach_mod_part_to_host_joint(
            composite.get(), 0, i, static_cast<std::uint32_t>(joint));
        if (result.ok()) {
            g_state.composite_part_joints[i] = joint;
        }
    }

    ActivateSession(hwnd, std::move(composite), title, false);
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

std::wstring SanitizeFilename(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (iswalnum(c) || c == L'-' || c == L'_') {
            out += c;
        } else if (!out.empty() && out.back() != L'_') {
            out += L'_';
        }
    }
    while (!out.empty() && out.back() == L'_') out.pop_back();
    return out.empty() ? L"image" : out;
}

std::wstring PickFolder(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Choose a folder for exported PNGs";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl == nullptr) return L"";
    wchar_t path[MAX_PATH];
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}

// Android's MainActivity.exportGallery() equivalent: every child resource
// (PTX bundle textures, UV maps) gets its own PNG, named
// "<root>__<child title>.png" so a batch export never collides across
// children and stays traceable back to its source without a manifest file.
void ExportAllChildrenDialog(HWND hwnd) {
    if (!g_state.session) return;
    const auto count = dmcresource::session_child_count(g_state.session.get());
    if (count == 0) {
        MessageBoxW(hwnd, L"This resource has no gallery images to export.",
                   L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring folder = PickFolder(hwnd);
    if (folder.empty()) return;

    const std::wstring root_name = SanitizeFilename(g_state.title);
    int saved = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto title =
            dmcresource::session_child_title(g_state.session.get(), static_cast<int>(i));
        RgbaImage image;
        dmcresource::ImagePreview scratch;
        const auto* preview = dmcresource::session_child_preview(
            g_state.session.get(), static_cast<int>(i), &scratch);
        if (preview != nullptr && preview->available()) {
            image.width = static_cast<int>(preview->width);
            image.height = static_cast<int>(preview->height);
            image.pixels = preview->rgba8;
        } else {
            auto child =
                dmcresource::open_session_child(g_state.session.get(), static_cast<int>(i));
            if (!child || !child->renderable) continue;
            image = dmcresource::render_session(child.get(), 1024, 1024, 0.65f, -0.45f, 1.0f, 0);
        }
        if (image.width <= 0 || image.height <= 0) continue;
        const std::wstring filename =
            folder + L"\\" + root_name + L"__" + SanitizeFilename(Utf8ToWide(title)) + L".png";
        if (SavePng(filename, image)) ++saved;
    }
    const std::wstring msg =
        L"Exported " + std::to_wstring(saved) + L" / " + std::to_wstring(count) + L" images.";
    MessageBoxW(hwnd, msg.c_str(), L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
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
    x -= kHeaderHeight;
    ToolButton add_part{kBtnAddPart, L"+MOD", L"Add another MOD part to this composite"};
    add_part.rect = {x, 0, x + kHeaderHeight, kHeaderHeight};
    g_state.header_buttons.push_back(add_part);

    g_state.tool_buttons.clear();
    struct Def {
        int id;
        const wchar_t* glyph;
        const wchar_t* tip;
    };
    // Short readable words rather than single-letter/symbol glyphs (W, H, i,
    // ...) that read as cryptic -- plain ASCII also sidesteps GDI's spotty
    // classic-DrawText fallback for pictographic Unicode/emoji glyphs, which
    // Segoe UI itself doesn't carry and would otherwise risk showing tofu
    // boxes instead of an icon.
    const Def defs[] = {
        {kBtnOpen, L"OPEN", L"Open MOD / SCM / DDS / PTX"},
        {kBtnResetExport, L"RESET", L"Reset view / export PNG"},
        {kBtnWireframe, L"WIRE", L"Wireframe"},
        {kBtnHierarchy, L"BONES", L"Bones / hierarchy"},
        {kBtnUv, L"UV", L"UV layout"},
        {kBtnInfo, L"INFO", L"Resource information"},
    };
    const int count = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
    const int btn_width = 68;
    const int total_width = count * btn_width;
    int bx = (client.right - total_width) / 2;
    const int by = client.bottom - kToolbarHeight + (kToolbarHeight - kBtnSize) / 2;
    for (const auto& d : defs) {
        ToolButton b{d.id, d.glyph, d.tip};
        b.rect = {bx, by, bx + btn_width, by + kBtnSize};
        g_state.tool_buttons.push_back(b);
        bx += btn_width;
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
        } else if (b.id == kBtnAddPart) {
            b.visible = caps.can_add_model_part;
            b.enabled = b.visible;
        }
    }
    for (auto& b : g_state.tool_buttons) {
        switch (b.id) {
            case kBtnOpen:
                b.enabled = true;
                break;
            case kBtnResetExport:
                b.glyph = caps.can_export_png ? L"SAVE" : L"RESET";
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
    SelectObject(hdc, g_state.small_font);
    for (const auto& b : g_state.tool_buttons) PaintButton(hdc, b);
}

constexpr int kThumbTile = 152;
constexpr int kThumbImage = 128;
constexpr int kThumbPad = 14;

// Returns the tile rect for gallery index i, given the same left-to-right,
// top-to-bottom flow the paint and hit-test code must agree on.
RECT GalleryTileRect(const RECT& viewport, std::size_t i) {
    const int usable_w =
        (std::max)(kThumbTile, static_cast<int>(viewport.right - viewport.left) - kThumbPad);
    const int cols = (std::max)(1, usable_w / (kThumbTile + kThumbPad));
    const int col = static_cast<int>(i) % cols;
    const int row = static_cast<int>(i) / cols;
    const int left = viewport.left + kThumbPad + col * (kThumbTile + kThumbPad);
    const int top = viewport.top + kThumbPad + row * (kThumbTile + kThumbPad);
    return {left, top, left + kThumbTile, top + kThumbTile};
}

int GalleryHitTest(const RECT& viewport, POINT pt, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const RECT tile = GalleryTileRect(viewport, i);
        if (tile.top > viewport.bottom) break;
        if (PtInRect(&tile, pt)) return static_cast<int>(i);
    }
    return -1;
}

void PaintChildBrowser(HDC hdc, const RECT& viewport) {
    HBRUSH bg = CreateSolidBrush(kViewportBg);
    FillRect(hdc, &viewport, bg);
    DeleteObject(bg);
    if (!g_state.session) return;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_state.small_font);
    const auto count = dmcresource::session_child_count(g_state.session.get());
    for (std::size_t i = 0; i < count; ++i) {
        const RECT tile = GalleryTileRect(viewport, i);
        if (tile.top > viewport.bottom) break;

        if (static_cast<int>(i) == g_state.child_hot) {
            HBRUSH hot = CreateSolidBrush(kBtnHover);
            FillRect(hdc, &tile, hot);
            DeleteObject(hot);
        }

        RECT image_rect = {tile.left + (kThumbTile - kThumbImage) / 2, tile.top + 6,
                          tile.left + (kThumbTile - kThumbImage) / 2 + kThumbImage,
                          tile.top + 6 + kThumbImage};
        const RgbaImage* thumb =
            i < g_state.gallery_thumbnails.size() ? &g_state.gallery_thumbnails[i] : nullptr;
        if (thumb != nullptr && thumb->width > 0 && thumb->height > 0) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = thumb->width;
            bmi.bmiHeader.biHeight = -thumb->height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(hdc, image_rect.left, image_rect.top,
                         image_rect.right - image_rect.left, image_rect.bottom - image_rect.top,
                         0, 0, thumb->width, thumb->height, thumb->pixels.data(), &bmi,
                         DIB_RGB_COLORS, SRCCOPY);
        } else {
            HBRUSH placeholder = CreateSolidBrush(RGB(30, 30, 36));
            FillRect(hdc, &image_rect, placeholder);
            DeleteObject(placeholder);
        }

        SetTextColor(hdc, kText);
        RECT label_rect = {tile.left + 4, image_rect.bottom + 4, tile.right - 4, tile.bottom - 2};
        const auto title =
            dmcresource::session_child_title(g_state.session.get(), static_cast<int>(i));
        const std::wstring wtitle = Utf8ToWide(title);
        DrawTextW(hdc, wtitle.c_str(), -1, &label_rect,
                 DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
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

        if (g_state.hierarchy_hot >= 0 &&
            static_cast<std::size_t>(g_state.hierarchy_hot) < g_state.hierarchy_point_labels.size()) {
            const std::wstring& label = g_state.hierarchy_point_labels[
                static_cast<std::size_t>(g_state.hierarchy_hot)];
            SetBkMode(hdc, OPAQUE);
            SetTextColor(hdc, kText);
            SetBkColor(hdc, RGB(28, 28, 34));
            SelectObject(hdc, g_state.small_font);
            RECT text_rect{};
            DrawTextW(hdc, label.c_str(), -1, &text_rect, DT_CALCRECT | DT_SINGLELINE);
            const int pad = 5;
            const int lx = (std::min)(g_state.hierarchy_hover_pt.x + 14,
                                      viewport.right - (text_rect.right - text_rect.left) - 2 * pad);
            const int ly = (std::max)(viewport.top,
                                      g_state.hierarchy_hover_pt.y - (text_rect.bottom - text_rect.top) - 14);
            RECT box{lx, ly, lx + (text_rect.right - text_rect.left) + 2 * pad,
                    ly + (text_rect.bottom - text_rect.top) + 2 * pad};
            HBRUSH tip_bg = CreateSolidBrush(RGB(28, 28, 34));
            FillRect(hdc, &box, tip_bg);
            DeleteObject(tip_bg);
            RECT text_box{box.left + pad, box.top + pad, box.right - pad, box.bottom - pad};
            DrawTextW(hdc, label.c_str(), -1, &text_box, DT_LEFT | DT_TOP | DT_SINGLELINE);
            SetBkMode(hdc, TRANSPARENT);
        }
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
        case kBtnAddPart:
            AddModPartDialog(hwnd);
            break;
        case kBtnOpen:
            OpenFileDialog(hwnd);
            break;
        case kBtnResetExport: {
            const auto caps = CurrentCapabilities();
            if (caps.can_export_png) {
                ExportPngDialog(hwnd);
            } else if (g_state.session && (g_state.session->renderable || IsUvLeaf(*g_state.session))) {
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
            AppendMenuW(file_menu, MF_STRING, kMenuExportAll, L"&Export all gallery images...");
            AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(file_menu, MF_STRING, kMenuRegisterFileTypes,
                       L"&Register .scm/.ptx and add to \"Open with\"");
            AppendMenuW(menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"&File");

            HMENU view_menu = CreatePopupMenu();
            AppendMenuW(view_menu, MF_STRING, kMenuQualityLow,
                       L"Render quality: &Low (512px, fastest)");
            AppendMenuW(view_menu, MF_STRING, kMenuQualityMedium,
                       L"Render quality: &Medium (768px)");
            AppendMenuW(view_menu, MF_STRING, kMenuQualityHigh,
                       L"Render quality: &High (1024px, sharpest)");
            CheckMenuRadioItem(view_menu, kMenuQualityLow, kMenuQualityHigh, kMenuQualityHigh,
                              MF_BYCOMMAND);
            AppendMenuW(menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view_menu), L"&View");
            SetMenu(hwnd, menu_bar);

            g_state.ui_font =
                CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
            g_state.small_font =
                CreateFontW(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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
                const auto count = g_state.session
                    ? dmcresource::session_child_count(g_state.session.get())
                    : 0;
                const int hit = GalleryHitTest(viewport, pt, count);
                if (hit >= 0) OpenChildByIndex(hwnd, hit);
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
            // Needed so a hover highlight/label doesn't stay stuck on screen
            // once the cursor leaves the window -- WM_MOUSEMOVE stops firing
            // then, so nothing else would ever clear it.
            TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            if (g_state.child_browser_open) {
                const RECT viewport = ViewportRect(hwnd);
                const auto count = g_state.session
                    ? dmcresource::session_child_count(g_state.session.get())
                    : 0;
                const int hot = GalleryHitTest(viewport, pt, count);
                if (hot != g_state.child_hot) {
                    g_state.child_hot = hot;
                    InvalidateRect(hwnd, &viewport, FALSE);
                }
                return 0;
            }
            if (g_state.dragging) {
                const int dx = pt.x - g_state.drag_start.x;
                const int dy = pt.y - g_state.drag_start.y;
                // Inverted relative to Android's touch convention (drag
                // right there increases yaw) on purpose: a mouse-drag orbit
                // on desktop reads as "grab and turn the model", so dragging
                // right should turn the model's near side to the right, the
                // opposite sign from a touch-swipe pan.
                g_state.yaw = g_state.drag_start_yaw - dx * 0.008f;
                g_state.pitch = g_state.drag_start_pitch - dy * 0.008f;
                g_state.pitch = (std::max)(-1.55f, (std::min)(1.55f, g_state.pitch));
                RerenderThrottled(hwnd, false);
            } else if (!g_state.hierarchy_screen_points.empty()) {
                const RECT viewport = ViewportRect(hwnd);
                float ix = 0.0f, iy = 0.0f;
                const int hot = ViewportPointToImage(viewport, g_state.bgra.width,
                                                     g_state.bgra.height, pt, &ix, &iy)
                    ? HierarchyHitTest(ix, iy)
                    : -1;
                const bool moved_while_hot =
                    hot >= 0 && (pt.x != g_state.hierarchy_hover_pt.x ||
                                 pt.y != g_state.hierarchy_hover_pt.y);
                if (hot != g_state.hierarchy_hot || moved_while_hot) {
                    g_state.hierarchy_hot = hot;
                    g_state.hierarchy_hover_pt = pt;
                    InvalidateRect(hwnd, &viewport, FALSE);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            const RECT viewport = ViewportRect(hwnd);
            bool changed = false;
            if (g_state.child_hot != -1) { g_state.child_hot = -1; changed = true; }
            if (g_state.hierarchy_hot != -1) { g_state.hierarchy_hot = -1; changed = true; }
            if (changed) InvalidateRect(hwnd, &viewport, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (g_state.child_browser_open || !g_state.session ||
                (!g_state.session->renderable && !IsUvLeaf(*g_state.session)))
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
            if (LOWORD(wparam) == kMenuExportAll) {
                ExportAllChildrenDialog(hwnd);
            } else if (LOWORD(wparam) == kMenuRegisterFileTypes) {
                RegisterFileAssociations();
                MessageBoxW(hwnd,
                           L"Registered. .scm and .ptx now open with this reader by default; "
                           L".mod and .dds were added to \"Open with\" without changing your "
                           L"current default.",
                           L"DMC Native Reader", MB_OK | MB_ICONINFORMATION);
            } else if (LOWORD(wparam) == kMenuQualityLow ||
                      LOWORD(wparam) == kMenuQualityMedium ||
                      LOWORD(wparam) == kMenuQualityHigh) {
                g_state.render_quality = LOWORD(wparam) == kMenuQualityLow    ? 512
                                        : LOWORD(wparam) == kMenuQualityMedium ? 768
                                                                               : 1024;
                CheckMenuRadioItem(GetSubMenu(GetMenu(hwnd), 1), kMenuQualityLow,
                                  kMenuQualityHigh, LOWORD(wparam), MF_BYCOMMAND);
                RerenderThrottled(hwnd, true);
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
