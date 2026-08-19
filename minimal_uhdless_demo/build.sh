#!/bin/bash
set -e
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

configure_log="$(mktemp)"
trap 'rm -f "$configure_log"' EXIT

if ! cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build" -DCMAKE_BUILD_TYPE=Release 2>&1 | tee "$configure_log"; then
    if grep -q "No CMAKE_CXX_COMPILER could be found" "$configure_log"; then
        os_id=""
        os_version=""
        if [ -r /etc/os-release ]; then
            os_id="$(. /etc/os-release && echo "$ID")"
            os_version="$(. /etc/os-release && echo "$VERSION_ID")"
        fi
        if { [ "$os_id" = "rhel" ] || [ "$os_id" = "ol" ]; } && [[ "$os_version" == 8* ]]; then
            cat >&2 <<'EOF'

No C++ compiler was found. This project also needs C++20, which RHEL/Oracle Linux
8's base gcc-c++ package (GCC 8.5) doesn't fully support -- install and enable
gcc-toolset-13 instead, then re-run this script:

    sudo dnf install gcc-toolset-13-gcc gcc-toolset-13-gcc-c++
    scl enable gcc-toolset-13 bash
    ./build.sh
EOF
        fi
    fi
    exit 1
fi

cmake --build "$SCRIPT_DIR/build" -j
