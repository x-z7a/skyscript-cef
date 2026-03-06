#!/bin/sh

set -eu

PROJECT_NAME=$(grep "#define PRODUCT_NAME " src/include/config.h | cut -d '"' -f 2)
VERSION=$(grep "#define VERSION " src/include/config.h | cut -d " " -f 3 | tr -d '"')
XPLANE_SDK_ROOT="${XPLANE_SDK_ROOT:-/Volumes/storage/git/SkyScript/SDK}"
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "Building $PROJECT_NAME.xpl version $VERSION. Is this correct? (y/n):"
read CONFIRM

if [ -z "$CONFIRM" ]; then
    CONFIRM="y"
fi

if [ "$CONFIRM" != "y" ]; then
    echo "Please update the version number in config.h and try again."
    exit 1
fi

AVAILABLE_PLATFORMS="mac win lin"
echo "Which platforms would you like to build? ($AVAILABLE_PLATFORMS):"
read PLATFORMS

if [ -z "$PLATFORMS" ]; then
    PLATFORMS=$AVAILABLE_PLATFORMS
fi

for platform in $PLATFORMS; do
    if ! echo "$AVAILABLE_PLATFORMS" | grep -q "$platform"; then
        echo "Invalid platform: $platform. Exiting."
        exit 1
    fi
done

echo "Building for platforms: \033[1m$PLATFORMS\033[0m\n"

echo "Which X-Plane version do you want to build for? (12):"
read XPLANE_VERSION

if [ -z "$XPLANE_VERSION" ]; then
    XPLANE_VERSION=12
fi

if [ "$XPLANE_VERSION" != "12" ]; then
    echo "Only X-Plane 12 builds are supported."
    exit 1
fi

echo "Building with SDK version $XPLANE_VERSION\n"
echo "Clean build directory? (y/n):"
read CLEAN_BUILD

if [ -z "$CLEAN_BUILD" ]; then
    CLEAN_BUILD="n"
fi

echo "Create additional files for the plugin? (y/n):"
read EXTRA_FILES

if [ -z "$EXTRA_FILES" ]; then
    EXTRA_FILES="n"
fi

if [ "$CLEAN_BUILD" = "y" ]; then
    echo "Cleaning build directories..."
    if [ -d "build" ]; then
        rm -rf build
    fi
fi

if echo "$PLATFORMS" | grep -q "mac"; then
    [ -d "$XPLANE_SDK_ROOT/CHeaders/XPLM" ] || { echo "Missing X-Plane SDK headers at $XPLANE_SDK_ROOT"; exit 1; }
    [ -d "$XPLANE_SDK_ROOT/CHeaders/Widgets" ] || { echo "Missing X-Plane Widgets headers at $XPLANE_SDK_ROOT"; exit 1; }
    [ -d "$XPLANE_SDK_ROOT/CHeaders/Wrappers" ] || { echo "Missing X-Plane Wrappers headers at $XPLANE_SDK_ROOT"; exit 1; }
    [ -f "$XPLANE_SDK_ROOT/Libraries/Mac/XPLM.framework/XPLM" ] || { echo "Missing XPLM.framework at $XPLANE_SDK_ROOT"; exit 1; }
    [ -f "$XPLANE_SDK_ROOT/Libraries/Mac/XPWidgets.framework/XPWidgets" ] || { echo "Missing XPWidgets.framework at $XPLANE_SDK_ROOT"; exit 1; }
    [ -d "lib/mac_x64/cef/include" ] || { echo "Missing CEF headers under lib/mac_x64/cef"; exit 1; }
    [ -f "lib/mac_x64/cef/Release/Chromium Embedded Framework.framework/Chromium Embedded Framework" ] || { echo "Missing macOS CEF framework under lib/mac_x64/cef/Release"; exit 1; }
    [ -f "lib/mac_x64/cef/libcef_dll_wrapper.a" ] || { echo "Missing lib/mac_x64/cef/libcef_dll_wrapper.a"; exit 1; }
fi

for platform in $PLATFORMS; do
    echo "Building $platform..."
    if [ "$platform" = "lin" ]; then
        docker build -t gcc-cmake -f ./docker/Dockerfile.linux . && \
        docker run --user $(id -u):$(id -g) --rm -v $(pwd):/src -w /src gcc-cmake:latest bash -c "\
        cmake -DCMAKE_CXX_FLAGS='-march=x86-64' -DCMAKE_TOOLCHAIN_FILE=toolchain-$platform.cmake -DXPLANE_VERSION=$XPLANE_VERSION -DXPLANE_SDK_ROOT=$XPLANE_SDK_ROOT -Bbuild/$platform -H. && \
        cmake --build build/$platform --parallel \$(nproc)"
    else
        cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-$platform.cmake -DCMAKE_OSX_ARCHITECTURES=arm64 -DXPLANE_VERSION=$XPLANE_VERSION -DXPLANE_SDK_ROOT="$XPLANE_SDK_ROOT" -Bbuild/$platform -H.
        cmake --build build/$platform --parallel "$JOBS"
    fi

    echo "\n\n"
    echo "\033[1;32m$platform build succeeded.\033[0m\nProduct: build/$platform/${platform}_x64/${PROJECT_NAME}.xpl"
    file build/$platform/${platform}_x64/${PROJECT_NAME}.xpl
    sleep 1
done

echo "Building has finished."

echo "Creating distribution bundle..."
if [ -d "build/dist" ]; then
    rm -rf build/dist
fi

for platform in $AVAILABLE_PLATFORMS; do
    mkdir -p build/dist/${platform}_x64
    if [ -d "build/$platform/${platform}_x64" ]; then
        cp build/$platform/${platform}_x64/${PROJECT_NAME}.xpl build/dist/${platform}_x64/${PROJECT_NAME}.xpl
    fi

    if echo "$PLATFORMS" | grep -q "$platform" && [ -d "lib/${platform}_x64/dist_${XPLANE_VERSION}" ]; then
        cp -r lib/${platform}_x64/dist_${XPLANE_VERSION}/* build/dist/${platform}_x64
    fi

    if echo "$PLATFORMS" | grep -q "$platform" && [ -d "lib/${platform}_x64/dist_extra_${XPLANE_VERSION}" ] && [ "$EXTRA_FILES" = "y" ]; then
        mkdir -p build/extra_${platform}/${platform}_x64
        cp -r lib/${platform}_x64/dist_extra_${XPLANE_VERSION}/* build/extra_${platform}/${platform}_x64
    fi
done

cp -r assets build/dist

if [ "$XPLANE_VERSION" -ge 12 ]; then
    echo "module|https://github.com/x-z7a/skyscript-cef\nname|SkyScript\nversion|$VERSION\nlocked|false\ndisabled|false\nzone|custom" > build/dist/skunkcrafts_updater.cfg
fi

cd build
mv dist "$PROJECT_NAME"

VERSION=$VERSION-XP$XPLANE_VERSION

rm -f "$PROJECT_NAME-$VERSION.zip"
zip -rq "$PROJECT_NAME-$VERSION.zip" "$PROJECT_NAME" -x "*/.DS_Store" -x "*/__MACOSX/*"

if [ "$EXTRA_FILES" = "y" ]; then
    for platform in $PLATFORMS; do
        if [ -d "extra_${platform}" ]; then
            echo "The '${platform}_x64' folder contains additional files required by the $PROJECT_NAME plugin for XP$XPLANE_VERSION.\nUnzip and merge these files with the plugin folder in your X-Plane installation directory, usually located at 'Resources/plugins/$PROJECT_NAME/${platform}_x64'." > "extra_${platform}/README.txt"
            rm -f "XP$XPLANE_VERSION-$platform-additional-files.zip"
            zip -rq "XP$XPLANE_VERSION-$platform-additional-files.zip" "extra_${platform}" -x ".DS_Store" -x "__MACOSX"
        fi
    done
fi

mv "$PROJECT_NAME" dist
mv "$PROJECT_NAME-$VERSION.zip" dist/
cd ..

echo "Bundle created. Distribution: build/dist/$PROJECT_NAME-$VERSION.zip"
