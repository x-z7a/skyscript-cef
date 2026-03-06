#include "appstate.h"

#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <regex>

#include <curl/curl.h>

#include "INIReader.h"
#include "config.h"
#include "dataref.h"
#include "json.hpp"
#include "path.h"

namespace {
unsigned short nextPowerOfTwo(unsigned int value) {
    unsigned int result = 1;
    while (result < value) {
        result <<= 1;
    }

    return static_cast<unsigned short>(std::min(result, static_cast<unsigned int>(std::numeric_limits<unsigned short>::max())));
}
}

AppState* AppState::instance = nullptr;

AppState::AppState() {
    remoteVersion = "";
    notification = nullptr;
    mainWindow = nullptr;
    pluginInitialized = false;
    browserVisible = false;
    browser = nullptr;
    activeCursor = CursorDefault;
    brightness = 1.0f;
    viewport = {0, 0, defaultWindowWidth, defaultWindowHeight, 0, 0, 0, 0};
}

AppState::~AppState() {
    instance = nullptr;
}

AppState* AppState::getInstance() {
    if (instance == nullptr) {
        instance = new AppState();
    }

    return instance;
}

bool AppState::initialize() {
    if (pluginInitialized) {
        return false;
    }

    browserVisible = false;
    brightness = 1.0f;
    activeCursor = CursorDefault;

    Path::getInstance()->reloadPaths();
    if (Path::getInstance()->pluginDirectory.empty()) {
        return false;
    }

    if (!loadConfig(false)) {
        return false;
    }

    setViewport(0, 0, defaultWindowWidth, defaultWindowHeight);

    if (!browser) {
        browser = new Browser();
    }

    browser->initialize();

    Dataref::getInstance()->createDataref<bool>("skyscript/visible", &browserVisible);
    Dataref::getInstance()->createCommand("skyscript/toggle", "Show or hide the browser window", [this](XPLMCommandPhase inPhase) {
        if (inPhase != xplm_CommandBegin) {
            return;
        }

        if (browserVisible) {
            hideBrowser();
        }
        else {
            showBrowser();
        }
    });

    pluginInitialized = true;
    return true;
}

void AppState::deinitialize() {
    if (!pluginInitialized) {
        return;
    }

    Dataref::getInstance()->destroyAllBindings();

    if (notification) {
        notification->destroy();
        notification = nullptr;
    }

    if (browser) {
        browser->visibilityWillChange(false);
        browser->destroy();
        browser = nullptr;
    }

    tasks.clear();
    buttons.clear();
    browserVisible = false;
    pluginInitialized = false;
    activeCursor = CursorDefault;
    brightness = 1.0f;
}

void AppState::checkLatestVersion() {
    if (!remoteVersion.empty()) {
        return;
    }

    std::string response;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, VERSION_CHECK_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    CURLcode status = curl_easy_perform(curl);
    if (status != CURLE_OK) {
        debug("Version fetch failed: %s\n", curl_easy_strerror(status));
    }
    curl_easy_cleanup(curl);

    try {
        std::string tag = nlohmann::json::parse(response)[0]["tag_name"];
        if (tag.starts_with("v")) {
            tag = tag.substr(1);
        }

        remoteVersion = tag;
        std::string cleanedRemote = std::regex_replace(tag, std::regex("[^0-9]"), "");
        std::string cleanedLocal = std::regex_replace(VERSION, std::regex("[^0-9]"), "");
        int remoteVersionNumber = std::stoi(cleanedRemote);
        int localVersionNumber = std::stoi(cleanedLocal);
        if (remoteVersionNumber > localVersionNumber) {
            debug("There is a newer version of the plugin available. Current: %s, latest: %s\n", VERSION, tag.c_str());
            std::string description = "There is an update available for the " + std::string(FRIENDLY_NAME) + " plugin.\n\nVersion " + tag + ".\n";
            showNotification(new Notification("Update available", description));
        }
    }
    catch (const std::exception& e) {
        debug("Could not fetch latest version information from GitHub. Reason: %s\n", e.what());
        remoteVersion = VERSION;
    }
}

void AppState::update() {
    if (!browser) {
        return;
    }

    brightness = 1.0f;

    if (browserVisible) {
        syncWindowGeometry();
    }

    browser->update();

    if (notification) {
        notification->update();
    }

    tasks.erase(
        std::remove_if(tasks.begin(), tasks.end(), [&](const DelayedTask& task) {
            if (XPLMGetElapsedTime() > task.executeAfterElapsedSeconds) {
                task.func();
                return true;
            }

            return false;
        }),
        tasks.end()
    );
}

void AppState::draw() {
    if (!pluginInitialized || !browserVisible) {
        return;
    }

    syncWindowGeometry();

    browser->draw();
    if (notification) {
        notification->draw();
    }
}

bool AppState::syncWindowGeometry(bool resizeBrowser) {
    if (!mainWindow) {
        return false;
    }

    int left, top, right, bottom;
    XPLMGetWindowGeometry(mainWindow, &left, &top, &right, &bottom);

    unsigned short width = static_cast<unsigned short>(std::max(1, right - left));
    unsigned short height = static_cast<unsigned short>(std::max(1, top - bottom));
    bool changed = setViewport(left, bottom, width, height);
    if (changed && resizeBrowser && browser) {
        browser->resize();
    }

    return changed;
}

bool AppState::normalizeWindowPoint(int x, int y, float *normalizedX, float *normalizedY) {
    syncWindowGeometry(false);

    if (viewport.width == 0 || viewport.height == 0) {
        return false;
    }

    *normalizedX = static_cast<float>(x - viewport.x) / viewport.width;
    *normalizedY = static_cast<float>(y - viewport.y) / viewport.height;

    return !(*normalizedX < -0.1f || *normalizedX > 1.1f || *normalizedY < -0.1f || *normalizedY > 1.1f);
}

void AppState::applyWindowMode() {
    if (!mainWindow) {
        return;
    }

    bool isVrEnabled = Dataref::getInstance()->get<bool>("sim/graphics/VR/enabled");
    XPLMSetWindowPositioningMode(mainWindow, isVrEnabled ? xplm_WindowVR : xplm_WindowPositionFree, -1);

    if (browserVisible) {
        XPLMBringWindowToFront(mainWindow);
    }
}

bool AppState::updateButtons(float normalizedX, float normalizedY, ButtonState state) {
    bool didAct = false;
    for (const auto& button : buttons) {
        didAct = didAct || button->handleState(normalizedX, normalizedY, state);
    }

    return didAct;
}

void AppState::registerButton(Button *button) {
    buttons.push_back(button);
}

void AppState::unregisterButton(Button *button) {
    auto it = std::find(buttons.begin(), buttons.end(), button);
    if (it != buttons.end()) {
        buttons.erase(it);
    }
}

void AppState::showBrowser(std::string url) {
    if (!browser || !mainWindow) {
        return;
    }

    if (!url.empty()) {
        browser->loadUrl(url);
    }

    if (!browserVisible) {
        browser->visibilityWillChange(true);
        browserVisible = true;
        XPLMSetWindowIsVisible(mainWindow, 1);
        applyWindowMode();
        syncWindowGeometry();
        checkLatestVersion();
    }

    XPLMBringWindowToFront(mainWindow);
}

void AppState::hideBrowser() {
    if (!browserVisible) {
        return;
    }

    browser->visibilityWillChange(false);
    browserVisible = false;
    browser->setFocus(false);
    XPLMTakeKeyboardFocus(0);

    if (mainWindow) {
        XPLMSetWindowIsVisible(mainWindow, 0);
    }
}

void AppState::showNotification(Notification *aNotification) {
    if (notification && notification != aNotification) {
        notification->destroy();
    }

    notification = aNotification;
}

void AppState::executeDelayed(CallbackFunc func, float delaySeconds) {
    tasks.push_back({
        func,
        XPLMGetElapsedTime() + delaySeconds
    });
}

bool AppState::loadConfig(bool isReloading) {
    if (Path::getInstance()->pluginDirectory.empty()) {
        return false;
    }

    std::string filename = Path::getInstance()->pluginDirectory + "/config.ini";
    if (isReloading) {
        debug("Reloading configuration at %s...\n", filename.c_str());
    }

    if (!fileExists(filename)) {
        const char *defaultConfig = R"(# Browser window configuration file.
# If you're having trouble with this file or missing parameters, delete it and restart X-Plane.
# This file will then be recreated with default settings.
[browser]
homepage=https://www.google.com
audio_muted=false
# minimum_width: Ensures the browser width does not go below this value.
# The height is adjusted proportionally to maintain the aspect ratio.
# Leave empty to use the current window width.
minimum_width=
# scroll_speed: The speed/steps in which the browser scrolls.
# The default value is 5. Increase to scroll faster.
scroll_speed=
# forced_language: The language code for the application.
# Valid values: en-US, en-GB, nl-NL, fr-FR, etc.
# Leave empty for default language.
forced_language=
# user_agent: The User-Agent header for the browser
# Leave empty for the default Chrome UA.
user_agent=
# hide_addressbar: Whether the address bar should be hidden or not. Default is false.
hide_addressbar=
# framerate: The number of frames per second to render the browser. Saves CPU if set to a lower value.
# The browser will still sleep / idle when able or not visible.
# Leave empty for default framerate.
framerate=

)";

        std::ofstream fileOutputHandle(filename);
        if (fileOutputHandle.is_open()) {
            fileOutputHandle << defaultConfig;
            fileOutputHandle.close();
            debug("Default config file written to %s\n", filename.c_str());
        }
        else {
            debug("Failed to write default config file at %s\n", filename.c_str());
        }
    }

    INIReader reader(filename);
    if (reader.ParseError() != 0) {
        debug("Could not read config file at path %s, file is malformed.\n", filename.c_str());
        return false;
    }

    config.homepage = reader.Get("browser", "homepage", "https://www.google.com");
    config.audio_muted = reader.GetBoolean("browser", "audio_muted", false);
    config.minimum_width = reader.GetInteger("browser", "minimum_width", 0);
    config.scroll_speed = reader.GetInteger("browser", "scroll_speed", 5);
    config.forced_language = reader.Get("browser", "forced_language", "");
    config.user_agent = reader.GetString("browser", "user_agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.2.5.0 Safari/537.36");
    config.hide_addressbar = reader.GetBoolean("browser", "hide_addressbar", false);
    config.framerate = reader.GetInteger("browser", "framerate", 25);

#if DEBUG
    config.debug_value_1 = reader.GetReal("debug", "debug_value_1", 0.0f);
    config.debug_value_2 = reader.GetReal("debug", "debug_value_2", 0.0f);
    config.debug_value_3 = reader.GetReal("debug", "debug_value_3", 0.0f);
#endif

    unsigned short width = viewport.width > 0 ? viewport.width : defaultWindowWidth;
    unsigned short height = viewport.height > 0 ? viewport.height : defaultWindowHeight;
    setViewport(viewport.x, viewport.y, width, height);

    if (isReloading) {
        debug("Config file has been reloaded.\n");

        if (browser) {
            std::string url = browser->currentUrl;
            bool wasVisible = browserVisible;
            browser->visibilityWillChange(false);
            browser->destroy();
            browser->initialize();
            if (!url.empty()) {
                browser->loadUrl(url);
            }
            if (wasVisible) {
                browser->visibilityWillChange(true);
                browser->resize();
            }
        }
    }

    return true;
}

bool AppState::fileExists(std::string filename) {
    std::ifstream fileExistsHandle(filename);
    if (!fileExistsHandle.good()) {
        return false;
    }

    fileExistsHandle.close();
    return true;
}

bool AppState::setViewport(int x, int y, unsigned short width, unsigned short height) {
    unsigned short safeWidth = std::max<unsigned short>(1, width);
    unsigned short safeHeight = std::max<unsigned short>(1, height);
    float multiplier = safeWidth < config.minimum_width ? static_cast<float>(config.minimum_width) / safeWidth : 1.0f;
    unsigned short browserWidth = static_cast<unsigned short>(std::ceil(safeWidth * multiplier));
    unsigned short browserHeight = static_cast<unsigned short>(std::max(1.0f, std::ceil((safeHeight * browserTopRatio) * multiplier)));
    unsigned short textureWidth = nextPowerOfTwo(browserWidth);
    unsigned short textureHeight = nextPowerOfTwo(browserHeight);

    bool changed = viewport.x != x ||
        viewport.y != y ||
        viewport.width != safeWidth ||
        viewport.height != safeHeight ||
        viewport.textureWidth != textureWidth ||
        viewport.textureHeight != textureHeight ||
        viewport.browserWidth != browserWidth ||
        viewport.browserHeight != browserHeight;

    viewport = {
        x,
        y,
        safeWidth,
        safeHeight,
        textureWidth,
        textureHeight,
        browserWidth,
        browserHeight
    };

    return changed;
}
