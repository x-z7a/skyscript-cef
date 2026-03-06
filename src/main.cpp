#ifndef XPLM301
    #error This is made to be compiled against the X-Plane 4.2.0 SDK for XP11 and XP12
#endif

#include "config.h"
#include "appstate.h"
#include "cursor.h"
#include "dataref.h"
#include "path.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <XPLMDisplay.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>

#if IBM
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}
#endif

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void* params);
float update(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
int mouseClicked(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus status, void* inRefcon);
int mouseWheel(XPLMWindowID inWindowID, int x, int y, int wheel, int clicks, void* inRefcon);
int mouseCursor(XPLMWindowID inWindowID, int x, int y, void* inRefcon);
void keyPressed(XPLMWindowID inWindowID, char key, XPLMKeyFlags flags, char virtualKey, void* inRefcon, int losingFocus);
void menuAction(void* mRef, void* iRef);
void registerWindow();
void captureVrChanges();

unsigned char pressedKeyCode = 0;
unsigned char pressedVirtualKeyCode = 0;
double pressedKeyTime = 0;

PLUGIN_API int XPluginStart(char * name, char * sig, char * desc)
{
    strcpy(name, FRIENDLY_NAME);
    strcpy(sig, BUNDLE_ID);
    strcpy(desc, "Standalone browser window for X-Plane");
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);

    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), FRIENDLY_NAME, nullptr, 1);
    XPLMMenuID id = XPLMCreateMenu(FRIENDLY_NAME, XPLMFindPluginsMenu(), item, menuAction, nullptr);
    XPLMAppendMenuItem(id, "Toggle browser window", (void *)"ActionToggleBrowser", 0);
    XPLMAppendMenuItem(id, "Reload configuration", (void *)"ActionReloadConfig", 0);
    XPLMAppendMenuItem(id, "About", (void *)"ActionAbout", 0);

    XPLMRegisterFlightLoopCallback(update, REFRESH_INTERVAL_SECONDS_SLOW, nullptr);

    XPluginReceiveMessage(0, XPLM_MSG_PLANE_LOADED, nullptr);

    captureVrChanges();
    initializeCursor();

    debug("Plugin started (version %s)\n", VERSION);

#if DEBUG
    Dataref::getInstance()->createCommand("avitab_browser/debug/window_to_foreground", "Bring window to front", [](XPLMCommandPhase inPhase) {
        if (inPhase != xplm_CommandBegin) {
            return;
        }

        if (AppState::getInstance()->mainWindow) {
            XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
        }
    });
#endif

    return 1;
}

PLUGIN_API void XPluginStop(void) {
    XPLMUnregisterFlightLoopCallback(update, nullptr);
    if (AppState::getInstance()->mainWindow) {
        XPLMDestroyWindow(AppState::getInstance()->mainWindow);
        AppState::getInstance()->mainWindow = nullptr;
    }

    destroyCursor();
    AppState::getInstance()->deinitialize();
    debug("Plugin stopped\n");
}

PLUGIN_API int XPluginEnable(void) {
    Path::getInstance()->reloadPaths();

    if (AppState::getInstance()->mainWindow && AppState::getInstance()->browserVisible) {
        XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
    }

    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    debug("Disabling plugin...\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void* params) {
    switch (msg) {
        case XPLM_MSG_PLANE_LOADED:
            if ((intptr_t)params != 0) {
                return;
            }

            if (AppState::getInstance()->initialize()) {
                registerWindow();
            }
            break;

        case XPLM_MSG_PLANE_CRASHED:
            break;

        case XPLM_MSG_PLANE_UNLOADED:
            if ((intptr_t)params != 0) {
                return;
            }

            if (AppState::getInstance()->mainWindow) {
                XPLMDestroyWindow(AppState::getInstance()->mainWindow);
                AppState::getInstance()->mainWindow = nullptr;
            }
            AppState::getInstance()->deinitialize();
            break;

        default:
            break;
    }
}

void menuAction(void* mRef, void* iRef) {
    if (!strcmp((char *)iRef, "ActionToggleBrowser")) {
        if (AppState::getInstance()->browserVisible) {
            AppState::getInstance()->hideBrowser();
        }
        else {
            AppState::getInstance()->showBrowser();
        }
    }
    else if (!strcmp((char *)iRef, "ActionAbout")) {
        int winLeft, winTop, winRight, winBot;
        XPLMGetScreenBoundsGlobal(&winLeft, &winTop, &winRight, &winBot);
        XPLMCreateWindow_t params;
        float screenWidth = fabs(winLeft - winRight);
        float screenHeight = fabs(winTop - winBot);
        float width = 450.0f;
        float height = 180.0f;

        params.structSize = sizeof(params);
        params.left = (int)(winLeft + (screenWidth - width) / 2);
        params.right = params.left + width;
        params.top = (int)(winTop - (screenHeight - height) / 2);
        params.bottom = params.top - height;
        params.visible = 1;
        params.refcon = nullptr;
        params.drawWindowFunc = [](XPLMWindowID inWindowID, void *drawingRef) {
            XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);

            int left, top, right, bottom;
            XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
            float color[] = {1.0f, 1.0f, 1.0f};

            float x = left + 16.0f;
            float y = top - 16.0f;
            XPLMDrawString(color, x, y, FRIENDLY_NAME, nullptr, xplmFont_Proportional);
            y -= 16.0f;
            XPLMDrawString(color, x, y, "Version " VERSION, nullptr, xplmFont_Proportional);
            y -= 32.0f;
            XPLMDrawString(color, x, y, "Standalone browser window for X-Plane.", nullptr, xplmFont_Proportional);
            y -= 16.0f;
            XPLMDrawString(color, x, y, "This software is licensed under the GNU General Public License, GPL-3.0", nullptr, xplmFont_Proportional);
            y -= 32.0f;
            XPLMDrawString(color, x, y, "For updates, see x-plane.org or github.com/rswilem/avitab-browser.", nullptr, xplmFont_Proportional);
            y -= 16.0f;
            XPLMDrawString(color, x, y, "Existing avitab_browser commands/datarefs remain for compatibility.", nullptr, xplmFont_Proportional);
        };

        params.handleMouseClickFunc = nullptr;
        params.handleRightClickFunc = nullptr;
        params.handleMouseWheelFunc = nullptr;
        params.handleKeyFunc = nullptr;
        params.handleCursorFunc = nullptr;
        params.layer = xplm_WindowLayerFloatingWindows;
        params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
        XPLMWindowID aboutWindow = XPLMCreateWindowEx(&params);
        XPLMSetWindowTitle(aboutWindow, FRIENDLY_NAME);
        XPLMSetWindowPositioningMode(aboutWindow, Dataref::getInstance()->get<bool>("sim/graphics/VR/enabled") ? xplm_WindowVR : xplm_WindowPositionFree, -1);
        XPLMBringWindowToFront(aboutWindow);
    }
    else if (!strcmp((char *)iRef, "ActionReloadConfig")) {
        AppState::getInstance()->loadConfig();

        if (AppState::getInstance()->mainWindow && AppState::getInstance()->browserVisible) {
            XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
        }
    }
}

void keyPressed(XPLMWindowID inWindowID, char key, XPLMKeyFlags flags, char virtualKey, void* inRefcon, int losingFocus) {
    if (!AppState::getInstance()->browserVisible) {
        return;
    }

    if ((flags & xplm_DownFlag) == xplm_DownFlag) {
        pressedKeyCode = key;
        pressedVirtualKeyCode = virtualKey;
        pressedKeyTime = XPLMGetElapsedTime();
        AppState::getInstance()->browser->key(key, virtualKey, flags);
    }

    if ((flags & xplm_UpFlag) == xplm_UpFlag) {
        pressedKeyCode = 0;
        pressedVirtualKeyCode = 0;
        pressedKeyTime = 0;
        AppState::getInstance()->browser->key(key, virtualKey, flags);
    }

    if (losingFocus) {
        AppState::getInstance()->browser->setFocus(false);
    }
}

int mouseClicked(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus status, void* inRefcon) {
    if (!AppState::getInstance()->browserVisible) {
        return 0;
    }

    float mouseX, mouseY;
    if (!AppState::getInstance()->normalizeWindowPoint(x, y, &mouseX, &mouseY)) {
        if (AppState::getInstance()->browser->hasInputFocus()) {
            AppState::getInstance()->browser->setFocus(false);
        }
        return 0;
    }

    if (status == xplm_MouseDown && AppState::getInstance()->updateButtons(mouseX, mouseY, kButtonClick)) {
        return 1;
    }

    if (AppState::getInstance()->browser->click(status, mouseX, mouseY)) {
        return 1;
    }

    AppState::getInstance()->browser->setFocus(false);
    return 0;
}

int mouseWheel(XPLMWindowID inWindowID, int x, int y, int wheel, int clicks, void* inRefcon) {
    if (!AppState::getInstance()->browserVisible) {
        return 0;
    }

    float mouseX, mouseY;
    if (!AppState::getInstance()->normalizeWindowPoint(x, y, &mouseX, &mouseY)) {
        return 0;
    }

    bool horizontal = wheel == 1;
    AppState::getInstance()->browser->scroll(mouseX, mouseY, clicks * AppState::getInstance()->config.scroll_speed, horizontal);
    return 1;
}

int mouseCursor(XPLMWindowID inWindowID, int x, int y, void* inRefcon) {
    float mouseX, mouseY;
    if (!AppState::getInstance()->normalizeWindowPoint(x, y, &mouseX, &mouseY)) {
        AppState::getInstance()->activeCursor = CursorDefault;
        return xplm_CursorDefault;
    }

    AppState::getInstance()->browser->mouseMove(mouseX, mouseY);

    bool isVREnabled = Dataref::getInstance()->getCached<int>("sim/graphics/VR/enabled");
    if (isVREnabled) {
        return xplm_CursorDefault;
    }

    CursorType wantedCursor = CursorDefault;
    if (AppState::getInstance()->updateButtons(mouseX, mouseY, kButtonHover)) {
        wantedCursor = CursorHand;
    }
    else if (AppState::getInstance()->browserVisible && AppState::getInstance()->browser->cursor() != CursorDefault) {
        wantedCursor = AppState::getInstance()->browser->cursor();
    }

    if (wantedCursor == CursorDefault) {
        AppState::getInstance()->activeCursor = CursorDefault;
        return xplm_CursorDefault;
    }

    if (wantedCursor != AppState::getInstance()->activeCursor) {
        AppState::getInstance()->activeCursor = wantedCursor;
        setCursor(wantedCursor);
    }

    return xplm_CursorCustom;
}

float update(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (!AppState::getInstance()->pluginInitialized) {
        return REFRESH_INTERVAL_SECONDS_SLOW;
    }

    Dataref::getInstance()->update();
    AppState::getInstance()->update();

    if (!AppState::getInstance()->browserVisible) {
        return REFRESH_INTERVAL_SECONDS_SLOW;
    }

#ifndef DEBUG
    if (pressedKeyTime > 0 && XPLMGetElapsedTime() > pressedKeyTime + 0.3f) {
        AppState::getInstance()->browser->key(pressedKeyCode, pressedVirtualKeyCode);
    }
#endif

    if (AppState::getInstance()->browser->hasInputFocus() != XPLMHasKeyboardFocus(AppState::getInstance()->mainWindow)) {
        if (AppState::getInstance()->browser->hasInputFocus()) {
            AppState::getInstance()->browser->setFocus(true);
            XPLMBringWindowToFront(AppState::getInstance()->mainWindow);
            XPLMTakeKeyboardFocus(AppState::getInstance()->mainWindow);
        }
        else {
            AppState::getInstance()->browser->setFocus(false);
            XPLMTakeKeyboardFocus(0);
        }
    }

    return REFRESH_INTERVAL_SECONDS_FAST;
}

void registerWindow() {
    if (AppState::getInstance()->mainWindow) {
        XPLMDestroyWindow(AppState::getInstance()->mainWindow);
        AppState::getInstance()->mainWindow = nullptr;
    }

    int winLeft, winTop, winRight, winBot;
    XPLMGetScreenBoundsGlobal(&winLeft, &winTop, &winRight, &winBot);

    float screenWidth = fabs(winLeft - winRight);
    float screenHeight = fabs(winTop - winBot);
    int width = AppState::defaultWindowWidth;
    int height = AppState::defaultWindowHeight;

    XPLMCreateWindow_t params;
    params.structSize = sizeof(params);
    params.left = (int)(winLeft + (screenWidth - width) / 2);
    params.right = params.left + width;
    params.top = (int)(winTop - (screenHeight - height) / 2);
    params.bottom = params.top - height;
    params.visible = 0;
    params.refcon = nullptr;
    params.drawWindowFunc = [](XPLMWindowID, void *) {
        AppState::getInstance()->draw();
    };
    params.handleMouseClickFunc = mouseClicked;
    params.handleRightClickFunc = nullptr;
    params.handleMouseWheelFunc = mouseWheel;
    params.handleKeyFunc = keyPressed;
    params.handleCursorFunc = mouseCursor;
    params.layer = xplm_WindowLayerFloatingWindows;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;

    AppState::getInstance()->mainWindow = XPLMCreateWindowEx(&params);
    XPLMSetWindowTitle(AppState::getInstance()->mainWindow, FRIENDLY_NAME);
    XPLMSetWindowResizingLimits(AppState::getInstance()->mainWindow, 640, 480, 4096, 4096);
    AppState::getInstance()->syncWindowGeometry(false);
    AppState::getInstance()->applyWindowMode();
    XPLMSetWindowIsVisible(AppState::getInstance()->mainWindow, 0);
}

void captureVrChanges() {
    Dataref::getInstance()->monitorExistingDataref<bool>("sim/graphics/VR/enabled", [](bool isVrEnabled) {
        if (!AppState::getInstance()->mainWindow) {
            return;
        }

        AppState::getInstance()->applyWindowMode();
        debug("VR is now %s.\n", isVrEnabled ? "enabled" : "disabled");
    });
}
