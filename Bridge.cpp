// =============================================================================
// bridge/Bridge.cpp — MediaX IPC Dispatcher (Implementation)
// Author: Ankit Kimothi
//
// IPC FLOW OVERVIEW:
//
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │  JavaScript (WebView2)                                              │
//   │    window.chrome.webview.postMessage('{"type":"PLAY","id":"…"}')    │
//   └──────────────────────────┬──────────────────────────────────────────┘
//                              │  WM_WEBMESSAGERECEIVED (Win32 event)
//                              ▼
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  main.cpp: ICoreWebView2_add_WebMessageReceived callback           │
//   │    Bridge::instance().handleFromJS(utf8String)                     │
//   └──────────────────────────┬─────────────────────────────────────────┘
//                              │
//                              ▼
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  Bridge::handleFromJS(json)                                        │
//   │    Command cmd = parseCommand(json)                                │
//   │    Engine::instance().handleCommand(cmd)                           │
//   └──────────────────────────┬─────────────────────────────────────────┘
//                              │
//                              ▼
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  Engine processes command, may respond with an event:              │
//   │    Event e = { "TICK", stateJson };                                │
//   │    Bridge::instance().pushEventToJS(e)                             │
//   └──────────────────────────┬─────────────────────────────────────────┘
//                              │
//                              ▼
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  Bridge::pushEventToJS(e)                                          │
//   │    script = "window.mediax && window.mediax.onEvent(<json>)"       │
//   │    executor_(script)                                               │
//   └──────────────────────────┬─────────────────────────────────────────┘
//                              │  ExecuteScript (WebView2 COM call)
//                              ▼
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  JavaScript: window.mediax.onEvent({ type:"TICK", payload:{…} })  │
//   └────────────────────────────────────────────────────────────────────┘
// =============================================================================

#include "Bridge.h"
#include "Commands.h"

// Forward-declare the Engine singleton so we can call it without a circular
// include.  Engine.h includes Bridge.h, so we use a forward declaration here.
// The full Engine header is included in Engine.cpp where the implementation lives.
namespace mediax {
    class Engine;
}

// We need the full Engine definition to call Engine::instance().handleCommand().
// If the Engine layer has not yet been implemented, comment out this include
// and the handleCommand call in Bridge::handleFromJS() — the Bridge will then
// parse commands but silently discard them (useful during early development).
#include "../engine/Engine.h"

#include <stdexcept>
#include <string>

namespace mediax {

// ===========================================================================
// Bridge::instance — Meyers' singleton
// ===========================================================================
/**
 * @brief Returns the single Bridge instance.
 *
 * The static local variable is initialised on first call and destroyed when
 * the program exits.  C++11 guarantees thread-safe initialisation of function-
 * local statics, so this is safe to call from multiple threads concurrently
 * (though subsequent method calls are NOT thread-safe — see Bridge.h).
 */
Bridge& Bridge::instance()
{
    static Bridge s_instance;
    return s_instance;
}

// ===========================================================================
// Bridge::setScriptExecutor
// ===========================================================================
/**
 * @brief Stores the WebView2 ExecuteScript callable.
 *
 * Called once from the WebView2 controller-ready callback in main.cpp.
 * After this call, pushEventToJS() will work correctly.
 *
 * @param fn  Callable(std::string) that runs the given JS in WebView2.
 */
void Bridge::setScriptExecutor(std::function<void(const std::string&)> fn)
{
    // Simply store the functor.  std::function's move-assignment is cheap
    // when moving from an rvalue lambda.
    executor_ = std::move(fn);
}

// ===========================================================================
// Bridge::handleFromJS
// ===========================================================================
/**
 * @brief Entry point for messages arriving from the JavaScript front-end.
 *
 * Steps:
 *   1. Parse the raw JSON string into a typed Command struct.
 *   2. Forward the Command to the Engine for processing.
 *
 * Unknown command types (CommandType::UNKNOWN) are still forwarded to the
 * Engine so it can log them; the Engine is responsible for the ignore policy.
 *
 * @param jsonMessage  UTF-8 JSON string, e.g. {"type":"SEEK","numValue":42.5}
 */
void Bridge::handleFromJS(const std::string& jsonMessage)
{
    // Step 1 — Parse the wire-format JSON into a Command struct.
    //   parseCommand() is defined in Commands.h (inline, no external deps).
    Command cmd = parseCommand(jsonMessage);

    // Step 2 — Dispatch to the Engine.
    //   Engine is also a singleton; it processes the command synchronously on
    //   the same thread (the Win32 UI thread / STA thread).
    Engine::instance().handleCommand(cmd);
}

// ===========================================================================
// Bridge::pushEventToJS
// ===========================================================================
/**
 * @brief Serialises @p e and executes the resulting JavaScript in WebView2.
 *
 * Builds the idiomatic guard-call pattern:
 *
 *   window.mediax && window.mediax.onEvent(<json>)
 *
 * The @c && short-circuit prevents a ReferenceError if window.mediax is not
 * yet defined (e.g. during the initial page load before app.js has run).
 *
 * @param e  The event to send.  e.payload must be valid JSON (object, string,
 *           number, array, or literal null/true/false).
 */
void Bridge::pushEventToJS(const Event& e)
{
    // Guard: if the script executor has not been registered yet (e.g. the
    // WebView2 controller is still being created), drop the event silently.
    // The Engine should buffer critical early events if needed.
    if (!executor_) return;

    // Serialise the Event to its JSON wire format: {"type":"…","payload":…}
    std::string json = eventToJson(e);

    // Build the JavaScript expression that the front-end listens for.
    // Using the && guard means the call is a no-op if window.mediax is not yet
    // defined — avoids console errors during the brief window between page load
    // and app.js initialisation.
    std::string script =
        "window.mediax && window.mediax.onEvent(" + json + ")";

    // Invoke ExecuteScript via the stored executor functor.
    // This ultimately calls ICoreWebView2::ExecuteScript on the UI thread,
    // which queues the JS execution asynchronously in the renderer process.
    executor_(script);
}

} // namespace mediax
