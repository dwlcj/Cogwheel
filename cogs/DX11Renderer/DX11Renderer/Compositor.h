// DirectX 11 compositor.
//-------------------------------------------------------------------------------------------------
// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License.
// See LICENSE.txt for more detail.
//-------------------------------------------------------------------------------------------------

#ifndef _DX11RENDERER_COMPOSITOR_H_
#define _DX11RENDERER_COMPOSITOR_H_

#include <Cogwheel/Core/Renderer.h>
#include <Cogwheel/Core/Window.h>
#include <Cogwheel/Scene/Camera.h>

#include <memory>

//-------------------------------------------------------------------------------------------------
// Forward declarations.
//-------------------------------------------------------------------------------------------------
namespace Cogwheel {
namespace Core {
class Engine;
class Window;
}
}
struct HWND__;
typedef HWND__* HWND;
struct ID3D11Device1;
struct ID3D11DeviceContext1;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace DX11Renderer {
template <typename T> class OwnedResourcePtr;
using ODevice1 = OwnedResourcePtr<ID3D11Device1>;
using ODeviceContext1 = DX11Renderer::OwnedResourcePtr<ID3D11DeviceContext1>;
using ORenderTargetView = OwnedResourcePtr<ID3D11RenderTargetView>;
using OShaderResourceView = OwnedResourcePtr<ID3D11ShaderResourceView>;
}

#define D3D11_CREATE_DEVICE_NONE 0

namespace DX11Renderer {

//-------------------------------------------------------------------------------------------------
// Result of rendering a frame.
//-------------------------------------------------------------------------------------------------
struct RenderedFrame {
    OShaderResourceView& frame_SRV;
    Cogwheel::Math::Rect<int> viewport;
    unsigned int iteration_count;
};

//-------------------------------------------------------------------------------------------------
// Renderer interface.
// Future work:
// * Pass masked areas (rects) from overlapping (opaque) cameras in render, to help with masking and culling.
//-------------------------------------------------------------------------------------------------
class IRenderer {
public:
    virtual ~IRenderer() {}
    virtual Cogwheel::Core::Renderers::UID get_ID() const = 0;
    virtual void handle_updates() = 0;
    virtual RenderedFrame render(Cogwheel::Scene::Cameras::UID camera_ID, int width, int height) = 0;
};

typedef IRenderer*(*RendererCreator)(ID3D11Device1& device, int width_hint, int height_hint, const std::wstring& data_folder_path);

//-------------------------------------------------------------------------------------------------
// GUI renderer interface.
//-------------------------------------------------------------------------------------------------
class IGuiRenderer {
public:
    virtual ~IGuiRenderer() {}
    virtual void render(ODeviceContext1& context) = 0;
};

typedef IGuiRenderer*(*GuiRendererCreator)(ODevice1& device);

//-------------------------------------------------------------------------------------------------
// Utility function to create a 'performant' DX11 device.
//-------------------------------------------------------------------------------------------------
ODevice1 create_performant_device1(unsigned int create_device_flags = D3D11_CREATE_DEVICE_NONE);
ODevice1 create_performant_debug_device1();

//-------------------------------------------------------------------------------------------------
// DirectX 11 compositor.
// Composits the rendered images from various cameras attached to the window.
//-------------------------------------------------------------------------------------------------
class Compositor final {
public:

    static Compositor* initialize(HWND& hwnd, const Cogwheel::Core::Window& window, const std::wstring& data_folder_path);
    ~Compositor();

    // --------------------------------------------------------------------------------------------
    // Renderers
    // --------------------------------------------------------------------------------------------

    std::unique_ptr<IRenderer>& add_renderer(RendererCreator renderer_creator);
    std::unique_ptr<IGuiRenderer>& add_GUI_renderer(GuiRendererCreator renderer_creator);
    void render();

    // --------------------------------------------------------------------------------------------
    // Settings
    // --------------------------------------------------------------------------------------------
    bool uses_v_sync() const;
    void set_v_sync(bool use_v_sync);

private:

    Compositor(HWND& hwnd, const Cogwheel::Core::Window& window, const std::wstring& data_folder_path);

    // Delete copy constructors to avoid having multiple versions of the same Compositor.
    Compositor(Compositor& other) = delete;
    Compositor& operator=(const Compositor& rhs) = delete;

    // Pimpl the state to avoid exposing DirectX dependencies.
    class Implementation;
    Implementation* m_impl;
};

} // NS DX11Renderer

#endif // _DX11RENDERER_COMPOSITOR_H_