// Cogwheel engine driver.
// ---------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#ifndef _COGWHEEL_CORE_ENGINE_H_
#define _COGWHEEL_CORE_ENGINE_H_

#include <Cogwheel/Core/Array.h>
#include <Cogwheel/Core/Time.h>
#include <Cogwheel/Core/Window.h>
#include <Cogwheel/Scene/SceneNode.h>

#include <vector>

namespace Cogwheel {
namespace Core {
    class IModule;
}
namespace Input {
    class Keyboard;
    class Mouse;
}
}

namespace Cogwheel {
namespace Core {

// ---------------------------------------------------------------------------
// Engine driver, responsible for invoking the modules and handling all engine
// 'tick' logic not related to the operating system.
// Future work
// * Add a 'mutation complete' (said in the Zerg voice) callback.
// * Add on_exit callback and deallocate the managers internal state.
// * Can I setup callbacks using lambdas?
// * Consider if anything is made easier by having the engine as a singleton.
// ---------------------------------------------------------------------------
class Engine final {
public:
    static inline Engine* get_instance() { return m_instance; }

    Engine();
    ~Engine();

    inline Time& get_time() { return m_time; }
    inline Window& get_window() { return m_window; }

    inline void request_quit() { m_quit = true; }
    inline bool is_quit_requested() const { return m_quit; }

    // -----------------------------------------------------------------------
    // Input
    // -----------------------------------------------------------------------
    void set_keyboard(const Input::Keyboard* const keyboard) { m_keyboard = keyboard; }
    const Input::Keyboard* const get_keyboard() const { return m_keyboard; } // So .... you're saying it's const?
    void set_mouse(const Input::Mouse* const mouse) { m_mouse = mouse; }
    const Input::Mouse* const get_mouse() const { return m_mouse; }

    // -----------------------------------------------------------------------
    // Scene root.
    // -----------------------------------------------------------------------
    inline void set_scene_root(Scene::SceneNodes::UID root_ID) { m_scene_root = root_ID; }
    inline Scene::SceneNodes::UID get_scene_root() const { return m_scene_root; }

    // -----------------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------------
    void add_mutating_callback(Core::IModule* callback);

    typedef void(*non_mutating_callback)(const Engine& engine, void* callback_state);
    void add_non_mutating_callback(non_mutating_callback callback, void* callback_state);

    typedef void(*tick_cleanup_callback)(void* callback_state);
    void add_tick_cleanup_callback(tick_cleanup_callback callback, void* callback_state);

    // -----------------------------------------------------------------------
    // Main loop
    // -----------------------------------------------------------------------
    void do_tick(double delta_time);

private:

    // Delete copy constructors.
    Engine(const Engine& rhs) = delete;
    Engine& operator=(Engine& rhs) = delete;

    static Engine* m_instance;

    Time m_time;
    Window m_window;
    Scene::SceneNodes::UID m_scene_root;
    bool m_quit;

    // A closure wrapping a callback function and its state.
    template <typename Function>
    struct Closure {
        Function callback;
        void* data;
    };

    // All engine callbacks.
    Core::Array<Core::IModule*> m_mutating_callbacks;
    std::vector<Closure<non_mutating_callback>> m_non_mutating_callbacks;
    std::vector<Closure<tick_cleanup_callback>> m_tick_cleanup_callbacks;

    // Input should only be updated by whoever created it and not by access via the engine.
    const Input::Keyboard* m_keyboard;
    const Input::Mouse* m_mouse;

};

} // NS Core
} // NS Cogwheel

#endif // _COGWHEEL_CORE_ENGINE_H_