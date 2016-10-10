// DirectX 12 renderer.
// ---------------------------------------------------------------------------
// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#include <DX12Renderer/Renderer.h>

#define NOMINMAX
#include <D3D12.h>
#include <dxgi1_4.h>
#include "d3dx12.h"
#undef RGB

#include <Cogwheel/Core/Window.h>
#include <Cogwheel/Scene/Camera.h>
#include <Cogwheel/Scene/SceneRoot.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Cogwheel::Math;
using namespace Cogwheel::Scene;

namespace DX12Renderer {

template<typename ResourcePtr>
void safe_release(ResourcePtr* resource_ptr) {
    if (*resource_ptr) {
        (*resource_ptr)->Release();
        *resource_ptr = nullptr;
    }
}

//----------------------------------------------------------------------------
// DirectX 12 renderer implementation.
//----------------------------------------------------------------------------
struct Renderer::Implementation {
    ID3D12Device* m_device;

    ID3D12CommandQueue* render_queue;
    IDXGISwapChain3* m_swap_chain;

    ID3D12DescriptorHeap* m_backbuffer_descriptors;
    struct Backbuffer {
        ID3D12Resource* resource; // Can this be typed more explicitly?
        ID3D12CommandAllocator* command_allocator;
        ID3D12Fence* fence;
        UINT64 fence_value;
    };
    std::vector<Backbuffer> m_backbuffers;
    int m_active_backbuffer_index;
    Backbuffer& active_backbuffer() { return m_backbuffers[m_active_backbuffer_index]; }

    HANDLE m_frame_rendered_event; // A handle to an event that occurs when our fence is unlocked/passed by the gpu.

    ID3D12GraphicsCommandList* m_command_list;

    struct {
        unsigned int CBV_SRV_UAV_descriptor;
        unsigned int sampler_descriptor;
        unsigned int RTV_descriptor;
    } size_of;

    bool is_valid() { return m_device != nullptr; }

    Implementation(HWND& hwnd, const Cogwheel::Core::Window& window) {

        IDXGIFactory4* dxgi_factory; // TODO Release at the bottom?
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgi_factory));
        if (FAILED(hr))
            return;

        { // Find the best performing device (apparently the one with the most memory) and initialize that.
            m_device = nullptr;
            IDXGIAdapter1* adapter = nullptr;

            struct WeightedAdapter {
                int index, dedicated_memory;

                inline bool operator<(WeightedAdapter rhs) const {
                    return rhs.dedicated_memory < dedicated_memory;
                }
            };

            std::vector<WeightedAdapter> sorted_adapters;
            for (int adapter_index = 0; dxgi_factory->EnumAdapters1(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapter_index) {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                // Ignore software rendering adapters.
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                WeightedAdapter e = { adapter_index, int(desc.DedicatedVideoMemory >> 20) };
                sorted_adapters.push_back(e);
            }

            std::sort(sorted_adapters.begin(), sorted_adapters.end());

            for (WeightedAdapter a : sorted_adapters) {
                dxgi_factory->EnumAdapters1(a.index, &adapter);

                // We need a device that is compatible with direct3d 12 (feature level 11 or higher)
                hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
                if (SUCCEEDED(hr)) {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);
                    printf("DX12Renderer using device %u: '%S' with feature level 11.0.\n", a.index, desc.Description);
                    break;
                }
            }
        }

        if (m_device == nullptr)
            return;

        // We have a valid device. Time to initialize the renderer!

        const UINT backbuffer_count = 2;
        m_backbuffers.resize(backbuffer_count);

        size_of.CBV_SRV_UAV_descriptor = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        size_of.sampler_descriptor = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        size_of.RTV_descriptor = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        { // Create the rendering command queue.
            D3D12_COMMAND_QUEUE_DESC description = {};
            description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; // Can be used to disable TDR.

            hr = m_device->CreateCommandQueue(&description, IID_PPV_ARGS(&render_queue));
            if (FAILED(hr)) {
                release_state();
                return;
            }
        }

        { // Swap chain.
            DXGI_SWAP_CHAIN_DESC description = {};
            description.BufferDesc.Width = window.get_width();
            description.BufferDesc.Height = window.get_height();;
            description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.BufferDesc.RefreshRate = { 1, 60 };
            description.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
            description.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
            description.SampleDesc.Count = 1; // No multi sampling. TODO Create enum and add multisample support.
            description.SampleDesc.Quality = 0; // No quality? :)
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = backbuffer_count;
            description.OutputWindow = hwnd;
            description.Windowed = TRUE;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // TODO MiniEngine uses FLIP_SEQUENTIALLY.
            description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

            IDXGISwapChain* swap_chain_interface;
            hr = dxgi_factory->CreateSwapChain(render_queue, &description, &swap_chain_interface);
            if (FAILED(hr)) {
                release_state();
                return;
            }
            // Downcast the IDXGISwapChain to a IDXGISwapChain3. NOTE MiniEngine is perfectly happy with the swapchain1. TODO And copy their initialization code as it�s cleaner.
            hr = swap_chain_interface->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_swap_chain);
            if (FAILED(hr)) {
                release_state();
                return;
            }

            m_active_backbuffer_index = m_swap_chain->GetCurrentBackBufferIndex();
        }

        { // Create the backbuffer's render target views and their descriptor heap.

            D3D12_DESCRIPTOR_HEAP_DESC description = {};
            description.NumDescriptors = backbuffer_count;
            description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            hr = m_device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&m_backbuffer_descriptors));
            if (FAILED(hr)) {
                release_state();
                return;
            }

            // Create a RTV for each backbuffer.
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_backbuffer_descriptors->GetCPUDescriptorHandleForHeapStart();
            for (int i = 0; i < backbuffer_count; ++i) {
                hr = m_swap_chain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i].resource));
                if (FAILED(hr)) {
                    release_state();
                    return;
                }

                m_device->CreateRenderTargetView(m_backbuffers[i].resource, nullptr, rtv_handle);

                rtv_handle.ptr += size_of.RTV_descriptor;
            }
        }

        { // Create the command allocators pr backbuffer.
            for (int i = 0; i < backbuffer_count; ++i) {
                hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_backbuffers[i].command_allocator));
                if (FAILED(hr)) {
                    release_state();
                    return;
                }
            }
        }

        { // Create the command list.
            const UINT device_0 = 0;
            ID3D12PipelineState* initial_pipeline = nullptr;
            hr = m_device->CreateCommandList(device_0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_backbuffers[0].command_allocator, // TODO Why do we only need one command list, but multiple allocators?
                initial_pipeline, IID_PPV_ARGS(&m_command_list));
            if (FAILED(hr)) {
                release_state();
                return;
            }
            m_command_list->Close(); // Close the command list, as we do not want to start recording yet.
        }

        { // Setup the fences for the backbuffers.

            // Create the fences and set their initial value.
            for (int i = 0; i < backbuffer_count; ++i) {
                const unsigned int initial_value = 0;
                hr = m_device->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_backbuffers[i].fence));
                if (FAILED(hr)) {
                    release_state();
                    return;
                }
                m_backbuffers[i].fence_value = initial_value;
            }

            // Create handle to a event that occurs when a frame has been rendered.
            m_frame_rendered_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (m_frame_rendered_event == nullptr) {
                release_state();
                return;
            }
        }

        dxgi_factory->Release();
    }

    void release_state() {
        if (m_device == nullptr)
            return;

        // Wait for the gpu to finish all frames
        // TODO Only do this if the GPU has actually started rendering.
        for (int m_active_backbuffer_index = 0; m_active_backbuffer_index < m_backbuffers.size(); ++m_active_backbuffer_index) {
            wait_for_previous_frame();
        }

        // Get swapchain out of full screen before exiting.
        BOOL is_fullscreen_on = false;
        m_swap_chain->GetFullscreenState(&is_fullscreen_on, NULL);
        if (is_fullscreen_on)
            m_swap_chain->SetFullscreenState(false, NULL);

        safe_release(&m_device);
        safe_release(&m_swap_chain);
        safe_release(&render_queue);
        safe_release(&m_backbuffer_descriptors);
        safe_release(&m_command_list);

        for (int i = 0; i < m_backbuffers.size(); ++i) {
            safe_release(&m_backbuffers[i].resource);
            safe_release(&m_backbuffers[i].command_allocator);
            safe_release(&m_backbuffers[i].fence);
        }
    }

    void render() {
        if (Cameras::begin() == Cameras::end())
            return;

        wait_for_previous_frame();

        if (m_device == nullptr)
            return;

        handle_updates();

        // We can only reset an allocator once the gpu is done with it.
        // Resetting an allocator frees the memory that the command list was stored in.
        ID3D12CommandAllocator* command_allocator = active_backbuffer().command_allocator;
        HRESULT hr = command_allocator->Reset();
        if (FAILED(hr))
            return release_state();

        // Reset the command list. Incidentally also sets it to record.
        hr = m_command_list->Reset(command_allocator, NULL);
        if (FAILED(hr))
            return release_state();

        { // Record commands.
            ID3D12Resource* backbuffer_resource = active_backbuffer().resource;

            // Transition the 'm_active_backbuffer' render target from the present state to the render target state so the command list draws to it starting from here.
            m_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backbuffer_resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

            // Here we get the handle to our current render target view so we can set it as the render target in the output merger stage of the pipeline.
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_backbuffer_descriptors->GetCPUDescriptorHandleForHeapStart(), m_active_backbuffer_index, size_of.RTV_descriptor);

            // Set the render target for the output merger stage (the output of the pipeline).
            m_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

            // Clear the render target to the background color.
            Cameras::UID camera_ID = *Cameras::begin();
            SceneRoot scene = Cameras::get_scene_ID(camera_ID);
            RGB env_tint = scene.get_environment_tint();
            float environment_tint[] = { env_tint.r, env_tint.g, env_tint.b, 1.0f };
            m_command_list->ClearRenderTargetView(rtv_handle, environment_tint, 0, nullptr);

            // Transition the m_active_backbuffer_index'th render target from the render target state to the present state. If the debug layer is enabled, you will receive a
            // warning if present is called on the render target when it's not in the present state.
            m_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backbuffer_resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

            hr = m_command_list->Close();
            if (FAILED(hr))
                return release_state();
        }

        { // Render, i.e. playback command list.

            // Create an array of command lists and execute.
            ID3D12CommandList* command_lists[] = { m_command_list };
            render_queue->ExecuteCommandLists(1, command_lists);

            // Signal the fence with the next fence value, so we can check if the fence has been reached.
            hr = render_queue->Signal(active_backbuffer().fence, active_backbuffer().fence_value);
            if (FAILED(hr))
                return release_state();

            // Present the current backbuffer.
            hr = m_swap_chain->Present(0, 0);
            if (FAILED(hr))
                return release_state();
        }
    }

    void wait_for_previous_frame() {
        // Swap the current backbuffer index so we draw on the correct buffer.
        m_active_backbuffer_index = m_swap_chain->GetCurrentBackBufferIndex();

        // If the current fence value is still less than 'fence_value', then we know the GPU has not finished executing
        // the command queue since it has not reached the 'commandQueue->Signal(fence, fenceValue)' command.
        if (active_backbuffer().fence->GetCompletedValue() < active_backbuffer().fence_value)
        {
            // We have the fence create an event which is signaled once the fence's current value is 'fence_value'.
            HRESULT hr = active_backbuffer().fence->SetEventOnCompletion(active_backbuffer().fence_value, m_frame_rendered_event);
            if (FAILED(hr))
                return release_state();

            // We will wait until the fence has triggered the event that it's current value has reached "fenceValue". once it's value
            // has reached 'fence_value', we know the command queue has finished executing.
            WaitForSingleObject(m_frame_rendered_event, INFINITE);
        }

        // increment fence value for next frame.
        active_backbuffer().fence_value++;
    }

    void handle_updates() {

    }
};

//----------------------------------------------------------------------------
// DirectX 12 renderer.
//----------------------------------------------------------------------------
Renderer* Renderer::initialize(HWND& hwnd, const Cogwheel::Core::Window& window) {
    Renderer* r = new Renderer(hwnd, window);
    if (r->m_impl->is_valid())
        return r;
    else {
        delete r;
        return nullptr;
    }
}

Renderer::Renderer(HWND& hwnd, const Cogwheel::Core::Window& window) {
    m_impl = new Implementation(hwnd, window);
}

Renderer::~Renderer() {
    m_impl->release_state();
}

void Renderer::render() {
    m_impl->render();
}

} // NS DX12Renderer