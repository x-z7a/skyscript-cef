#ifndef APPSTATE_H
#define APPSTATE_H

#include <string>
#include <vector>
#include <XPLMDisplay.h>
#include "button.h"
#include "browser.h"
#include "notification.h"

struct WindowViewport {
    int x;
    int y;
    unsigned short width;
    unsigned short height;
    unsigned short textureWidth;
    unsigned short textureHeight;
    unsigned short browserWidth;
    unsigned short browserHeight;
    static constexpr unsigned char bytesPerPixel = 4;
};

struct AppConfiguration {
    std::string homepage;
    bool audio_muted;
    unsigned short minimum_width;
    unsigned char scroll_speed;
    std::string forced_language;
    std::string user_agent;
    bool hide_addressbar;
    unsigned char framerate;
#if DEBUG
    float debug_value_1;
    float debug_value_2;
    float debug_value_3;
#endif
};

typedef std::function<void()> CallbackFunc;

struct DelayedTask {
    CallbackFunc func;
    float executeAfterElapsedSeconds;
};

class AppState {
private:
    AppState();
    ~AppState();
    static AppState* instance;
    std::string remoteVersion;
    std::vector<DelayedTask> tasks;
    std::vector<Button *> buttons;
    Notification *notification;
    bool fileExists(std::string filename);
    bool setViewport(int x, int y, unsigned short width, unsigned short height);

public:
    static constexpr unsigned short defaultWindowWidth = 1024;
    static constexpr unsigned short defaultWindowHeight = 768;
    static constexpr float browserTopRatio = 1.0f;
    static constexpr float toolbarY = 0.985f;

    XPLMWindowID mainWindow;
    float brightness;
    WindowViewport viewport;
    AppConfiguration config;
    bool pluginInitialized = false;
    bool browserVisible = false;
    Browser *browser;
    CursorType activeCursor;
    
    static AppState* getInstance();
    bool initialize();
    void deinitialize();
    void checkLatestVersion();
    
    void update();
    void draw();
    bool syncWindowGeometry(bool resizeBrowser = true);
    bool normalizeWindowPoint(int x, int y, float *normalizedX, float *normalizedY);
    void applyWindowMode();
    
    bool updateButtons(float normalizedX, float normalizedY, ButtonState state);
    void registerButton(Button *button);
    void unregisterButton(Button *button);
    
    void showBrowser(std::string url = "");
    void hideBrowser();
    void showNotification(Notification *notification);
    void executeDelayed(CallbackFunc func, float delaySeconds);
    bool loadConfig(bool isReloading = true);
};

#endif
