#!/bin/bash

echo "Checking build dependencies for Typio..."

# Check for Rime
echo -n "Checking for Rime (librime)... "
if pkg-config --exists rime; then
    echo "OK (via pkg-config)"
else
    # Check for headers manually
    if [ -f "/usr/include/rime_api.h" ] || [ -f "/usr/local/include/rime_api.h" ]; then
        echo "OK (Headers found, but pkg-config missing)"
        echo "  Warning: You might need to set RIME_INCLUDE_DIR manually if build fails."
    else
        echo "MISSING"
        echo "  Rime development headers not found."
        echo "  Libraries found: $(find /usr/lib /usr/local/lib -name "librime.so*" 2>/dev/null | head -n 1)"
        
        echo "  To fix, please install the development package:"
        if command -v apt-get &> /dev/null; then
            echo "    sudo apt-get install librime-dev"
        elif command -v dnf &> /dev/null; then
            echo "    sudo dnf install librime-devel"
        elif command -v pacman &> /dev/null; then
            echo "    sudo pacman -S librime"
        else
            echo "    Install librime-dev or equivalent for your distro."
        fi
    fi
fi

# Check for Whisper
echo -n "Checking for Whisper (whisper.cpp)... "
# We now fetch it automatically, so this is just informational
echo "Will be fetched automatically if missing."

echo ""
echo "Done."
