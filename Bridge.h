// =============================================================================
// bridge/Bridge.h — MediaX IPC Dispatcher (Header)
// Author: Ankit Kimothi
//
// Bridge is the single point of contact between the WebView2 message layer
// and the C++ Engine.  It is a singleton so that both main.cpp (which owns
// the WebView2 controller) and the Engine can reach it without passing
// pointers around.
//
// RESPONSIBILITIES:
//   • Receive raw JSON strings from JavaScript (via WebView2 WebMessage event)
//     and dispatch them as typed Command objects to the Engine.
//   • Accept Event objects from the Engine and push them back to JavaScript
//     by calling ExecuteScript() through a stored executor functor.
//   • Decouple main.cpp (platform layer) from Engine (logic layer) — neither
//     needs to know about the other's internals.
//
// THREAD SAFETY:
//   WebView2 callbacks fire on the main UI thread (the same thread that
//   created the controller).  The Engine is also called on that thread.
//   No explicit synchronisation is needed as long as this single-threaded
//   contract is maintained.  If the Engine later moves work to background
//   threads, pushEventToJS() will need to marshal back to the UI thread
//   (e.g. via PostMessage + a custom WM_ message).
// =============================================================================

#pragma once

#include <functional>   // std::function
#include <string>

#include "Commands.h"   // Event, Command (for callers that include only Bridge.h)

namespace mediax {

// ===========================================================================
// Bridge — Singleton IPC dispatcher
// ===========================================================================
/**
 * @brief Thread-unsafe singleton that routes messages between WebView2 and Engine.
 *
 * Usage (main.cpp — wiring phase):
 * @code
 *   // Store the WebView2 ExecuteScript capability in the Bridge
 *   Bridge::instance().setScriptExecutor([](const std::string& js) {
 *       g_webview->ExecuteScript(widen(js).c_str(), nullptr);
 *   });
 * @endcode
 *
 * Usage (WebView2 message callback — JS → C++):
 * @code
 *   Bridge::instance().handleFromJS(utf8JsonString);
 * @endcode
 *
 * Usage (Engine — C++ → JS):
 * @code
 *   Event e{ "TICK", buildStateJson() };
 *   Bridge::instance().pushEventToJS(e);
 * @endcode
 */
class Bridge
{
public:
    // -----------------------------------------------------------------------
    // Singleton access
    // -----------------------------------------------------------------------
    /**
     * @brief Returns the single global Bridge instance.
     *
     * Constructed on first call (Meyers' singleton — thread-safe in C++11+
     * for the initialisation itself).
     */
    static Bridge& instance();

    // -----------------------------------------------------------------------
    // JS → C++ direction
    // -----------------------------------------------------------------------
    /**
     * @brief Called by the WebView2 WebMessageReceived event handler.
     *
     * Parses @p jsonMessage into a Command via parseCommand() and forwards
     * it to Engine::instance().handleCommand().
     *
     * @param jsonMessage  UTF-8 JSON string from window.chrome.webview.postMessage()
     */
    void handleFromJS(const std::string& jsonMessage);

    // -----------------------------------------------------------------------
    // C++ → JS direction
    // -----------------------------------------------------------------------
    /**
     * @brief Serialises @p e and executes it in the WebView2 via the stored executor.
     *
     * Builds the script:
     * @code
     *   window.mediax && window.mediax.onEvent({"type":"…","payload":…})
     * @endcode
     * and calls executor_(script).  The short-circuit @c && ensures the call
     * is silently skipped if the front-end has not yet registered window.mediax
     * (e.g. during initial page load).
     *
     * @param e  The event to push to JavaScript.
     *
     * @note If setScriptExecutor() has not been called yet (executor_ is empty),
     *       this method returns immediately without error.  Events pushed before
     *       WebView2 is ready are silently dropped.  The Engine should buffer
     *       early events if delivery is critical.
     */
    void pushEventToJS(const Event& e);

    /**
     * @brief Stores the functor that calls WebView2's ExecuteScript.
     *
     * Called once from main.cpp after the WebView2 controller is ready
     * (inside the CreateCoreWebView2Controller completion callback).
     *
     * @param fn  A callable that accepts a UTF-8 JavaScript string and
     *            executes it inside the hosted WebView2.
     */
    void setScriptExecutor(std::function<void(const std::string&)> fn);

    // -----------------------------------------------------------------------
    // Non-copyable, non-movable (singleton contract)
    // -----------------------------------------------------------------------
    Bridge(const Bridge&)            = delete;
    Bridge& operator=(const Bridge&) = delete;
    Bridge(Bridge&&)                 = delete;
    Bridge& operator=(Bridge&&)      = delete;

private:
    /**
     * @brief Private constructor — use instance() to access the singleton.
     */
    Bridge() = default;

    /**
     * @brief Functor set by setScriptExecutor(); calls WebView2 ExecuteScript.
     *
     * Empty (falsy) until setScriptExecutor() is called.  pushEventToJS()
     * checks this before invoking it.
     */
    std::function<void(const std::string&)> executor_;
};

} // namespace mediax
