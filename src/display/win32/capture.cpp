#include <unknwn.h>
#include <windows.h>
// After windows.h:
#include <d3d11_4.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <string>

#include "capture.hpp"

namespace glyphshift::display::win32 {
namespace {
namespace capture = winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

void require(HRESULT result, const char* what) {
    if (FAILED(result))
        throw std::runtime_error(
            std::string(what) + " failed: " + std::to_string(static_cast<unsigned long>(result)));
}

// The client area's place inside the captured image, which spans the visible
// window frame (DWM's extended frame bounds).
RECT client_area(HWND window) {
    RECT frame{}, client{};
    POINT origin{0, 0};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof frame)) ||
        !GetClientRect(window, &client) || !ClientToScreen(window, &origin))
        return {};
    return {origin.x - frame.left,
        origin.y - frame.top,
        origin.x - frame.left + client.right,
        origin.y - frame.top + client.bottom};
}
}  // namespace

struct Capture::State {
    HWND target = nullptr;
    winrt::com_ptr<ID3D11Device5> device;
    winrt::com_ptr<ID3D11DeviceContext4> context;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrt_device{nullptr};
    capture::GraphicsCaptureItem item{nullptr};
    capture::Direct3D11CaptureFramePool pool{nullptr};
    capture::GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::SizeInt32 pool_size{};
    winrt::event_token arrived{}, closed_token{};
    HANDLE event = nullptr;
    std::atomic<bool> closed{false};
    winrt::com_ptr<ID3D11Texture2D> shared;
    HANDLE shared_handle = nullptr;
    unsigned width = 0, height = 0;
    uint64_t generation = 0;
    winrt::com_ptr<ID3D11Fence> fence;
    HANDLE fence_handle = nullptr;
    uint64_t value = 0;
    bool borderless = false;

    void share(unsigned new_width, unsigned new_height) {
        if (shared_handle)
            CloseHandle(shared_handle);
        shared_handle = nullptr;
        shared = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = new_width;
        desc.Height = new_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        require(device->CreateTexture2D(&desc, nullptr, shared.put()), "create shared texture");
        require(shared.as<IDXGIResource1>()->CreateSharedHandle(nullptr,
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    nullptr,
                    &shared_handle),
            "share texture");
        width = new_width;
        height = new_height;
        ++generation;
    }
};

Capture::Capture(HWND target, LUID adapter) : state_(std::make_unique<State>()) {
    try {
        start(target, adapter);
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("cannot capture the window: " + winrt::to_string(error.message()));
    }
}

void Capture::start(HWND target, LUID adapter) {
    auto& s = *state_;
    s.target = target;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
        // Already initialized in another apartment mode on this thread.
    }
    if (!capture::GraphicsCaptureSession::IsSupported())
        throw std::runtime_error("Windows.Graphics.Capture is not supported here");
    winrt::com_ptr<IDXGIFactory4> factory;
    require(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())), "create DXGI factory");
    winrt::com_ptr<IDXGIAdapter> dxgi_adapter;
    require(factory->EnumAdapterByLuid(adapter, IID_PPV_ARGS(dxgi_adapter.put())),
        "find the renderer's adapter");
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    require(D3D11CreateDevice(dxgi_adapter.get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.put(),
                nullptr,
                context.put()),
        "create D3D11 device");
    s.device = device.as<ID3D11Device5>();
    s.context = context.as<ID3D11DeviceContext4>();
    require(s.device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(s.fence.put())),
        "create shared fence");
    require(s.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &s.fence_handle),
        "share fence");
    winrt::com_ptr<::IInspectable> inspectable;
    require(
        CreateDirect3D11DeviceFromDXGIDevice(s.device.as<IDXGIDevice>().get(), inspectable.put()),
        "wrap the D3D11 device");
    s.winrt_device =
        inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    const auto interop =
        winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    require(interop->CreateForWindow(target,
                winrt::guid_of<capture::GraphicsCaptureItem>(),
                winrt::put_abi(s.item)),
        "capture the window");
    s.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s.pool_size = s.item.Size();
    s.pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(s.winrt_device,
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        s.pool_size);
    HANDLE event = s.event;
    s.arrived = s.pool.FrameArrived([event](auto&&, auto&&) {
        SetEvent(event);
    });
    auto* closed = &s.closed;
    s.closed_token = s.item.Closed([event, closed](auto&&, auto&&) {
        closed->store(true);
        SetEvent(event);
    });
    s.session = s.pool.CreateCaptureSession(s.item);
    s.session.IsCursorCaptureEnabled(false);
    using winrt::Windows::Foundation::Metadata::ApiInformation;
    if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession",
            L"IsBorderRequired")) {
        try {
            capture::GraphicsCaptureAccess::RequestAccessAsync(
                capture::GraphicsCaptureAccessKind::Borderless)
                .get();
            s.session.IsBorderRequired(false);
            s.borderless = !s.session.IsBorderRequired();
        } catch (const winrt::hresult_error&) {
            // The border stays; capture still works.
        }
    }
    s.session.StartCapture();
}

Capture::~Capture() {
    auto& s = *state_;
    if (s.session)
        s.session.Close();
    if (s.pool) {
        s.pool.FrameArrived(s.arrived);
        s.pool.Close();
    }
    if (s.item)
        s.item.Closed(s.closed_token);
    if (s.context) {
        // Nothing may still read or write the shared texture.
        s.context->Flush();
    }
    if (s.shared_handle)
        CloseHandle(s.shared_handle);
    if (s.fence_handle)
        CloseHandle(s.fence_handle);
    if (s.event)
        CloseHandle(s.event);
}

HANDLE Capture::event() const noexcept {
    return state_->event;
}

bool Capture::closed() const noexcept {
    return state_->closed.load() || !IsWindow(state_->target);
}

bool Capture::update() {
    try {
        return copy();
    } catch (const winrt::hresult_error&) {
        // The capture ended under us (the window closed).
        state_->closed = true;
        return false;
    }
}

bool Capture::copy() {
    auto& s = *state_;
    // Only the newest waiting frame matters.
    capture::Direct3D11CaptureFrame frame{nullptr};
    while (auto next = s.pool.TryGetNextFrame()) {
        if (frame)
            frame.Close();
        frame = next;
    }
    if (!frame)
        return false;
    const auto content = frame.ContentSize();
    if (content.Width != s.pool_size.Width || content.Height != s.pool_size.Height) {
        // The window changed size: later frames come at the new size.
        s.pool_size = content;
        s.pool.Recreate(s.winrt_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, content);
    }
    winrt::com_ptr<ID3D11Texture2D> texture;
    require(frame.Surface()
                .as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()
                ->GetInterface(IID_PPV_ARGS(texture.put())),
        "read the captured frame");
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    RECT area = client_area(s.target);
    // Right after a resize (or while minimized) the frame's texture can be
    // smaller than its content size: crop to what both hold.
    const LONG limit_x = std::min<LONG>(content.Width, static_cast<LONG>(desc.Width));
    const LONG limit_y = std::min<LONG>(content.Height, static_cast<LONG>(desc.Height));
    area.left = std::clamp<LONG>(area.left, 0, limit_x);
    area.top = std::clamp<LONG>(area.top, 0, limit_y);
    area.right = std::clamp<LONG>(area.right, area.left, limit_x);
    area.bottom = std::clamp<LONG>(area.bottom, area.top, limit_y);
    const auto w = static_cast<unsigned>(area.right - area.left);
    const auto h = static_cast<unsigned>(area.bottom - area.top);
    if (!w || !h) {
        frame.Close();
        return false;
    }
    if (w != s.width || h != s.height)
        s.share(w, h);
    const D3D11_BOX box{static_cast<UINT>(area.left),
        static_cast<UINT>(area.top),
        0,
        static_cast<UINT>(area.right),
        static_cast<UINT>(area.bottom),
        1};
    // After the renderer's last read; then hand the copy over.
    s.context->Wait(s.fence.get(), s.value);
    s.context->CopySubresourceRegion(s.shared.get(), 0, 0, 0, 0, texture.get(), 0, &box);
    s.context->Signal(s.fence.get(), ++s.value);
    s.context->Flush();
    frame.Close();
    return true;
}

HANDLE Capture::texture() const noexcept {
    return state_->shared_handle;
}

HANDLE Capture::fence() const noexcept {
    return state_->fence_handle;
}

unsigned Capture::width() const noexcept {
    return state_->width;
}

unsigned Capture::height() const noexcept {
    return state_->height;
}

uint64_t Capture::generation() const noexcept {
    return state_->generation;
}

uint64_t& Capture::fence_value() noexcept {
    return state_->value;
}

bool Capture::borderless() const noexcept {
    return state_->borderless;
}
}  // namespace glyphshift::display::win32
