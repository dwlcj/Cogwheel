// Cogwheel Win32 main.
// ----------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ----------------------------------------------------------------------------

#ifndef _COGWHEEL_WIN32_DRIVER_H_
#define _COGWHEEL_WIN32_DRIVER_H_

//----------------------------------------------------------------------------
// Forward declerations
//----------------------------------------------------------------------------
namespace Cogwheel {
namespace Core {
class Engine;
class Window;
}
}
struct HWND__;
typedef HWND__* HWND;

namespace Win32Driver {

typedef int (*OnLaunchCallback)(Cogwheel::Core::Engine&);
typedef int (*OnWindowCreatedCallback)(Cogwheel::Core::Engine&, Cogwheel::Core::Window&, HWND&);

int run(OnLaunchCallback on_launch, OnWindowCreatedCallback on_window_created);

} // NS Win32Driver

#endif // _COGWHEEL_WIN32_DRIVER_H_