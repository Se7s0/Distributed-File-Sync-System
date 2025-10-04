# Boost.Asio Setup Guide for Windows (Visual Studio)

This guide shows you how to install Boost and integrate it with your CMake project in Visual Studio.

## Option 1: Using vcpkg (Recommended)

### Step 1: Install vcpkg

Open **Command Prompt** or **PowerShell** as Administrator:

```powershell
# Navigate to C drive
cd C:\

# Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git

# Navigate to vcpkg directory
cd vcpkg

# Bootstrap vcpkg (builds the executable)
.\bootstrap-vcpkg.bat

# Integrate with Visual Studio (one-time setup)
.\vcpkg integrate install
```

**Expected output:**
```
Applied user-wide integration for this vcpkg root.
All MSBuild C++ projects can now #include any installed libraries.
CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

### Step 2: Install Boost.Asio

In the same Command Prompt:

```powershell
# Install Boost.Asio and Boost.System for 64-bit Windows
.\vcpkg install boost-asio:x64-windows boost-system:x64-windows
```

**This will take 5-10 minutes.** vcpkg downloads and builds Boost from source.

**Expected output:**
```
The following packages will be built and installed:
    boost-asio:x64-windows -> 1.84.0
    boost-system:x64-windows -> 1.84.0
    ...
Successfully installed boost-asio:x64-windows
Successfully installed boost-system:x64-windows
```

### Step 3: Configure Visual Studio to Use vcpkg

**Method A: Using CMake Toolchain (Automatic)**

Visual Studio will automatically find vcpkg if you set the toolchain file.

1. Open Visual Studio
2. Go to **Project** → **CMake Settings**
3. Find **CMake toolchain file** field
4. Set it to: `C:\vcpkg\scripts\buildsystems\vcpkg.cmake`
5. Click **Save**

**Method B: Using CMakeSettings.json (Manual)**

If Visual Studio doesn't auto-detect, create/edit `CMakeSettings.json` in your project root:

```json
{
  "configurations": [
    {
      "name": "x64-Debug",
      "generator": "Ninja",
      "configurationType": "Debug",
      "buildRoot": "${projectDir}\\out\\build\\${name}",
      "installRoot": "${projectDir}\\out\\install\\${name}",
      "cmakeCommandArgs": "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake",
      "buildCommandArgs": "",
      "ctestCommandArgs": "",
      "inheritEnvironments": [ "msvc_x64_x64" ]
    },
    {
      "name": "x64-Release",
      "generator": "Ninja",
      "configurationType": "Release",
      "buildRoot": "${projectDir}\\out\\build\\${name}",
      "installRoot": "${projectDir}\\out\\install\\${name}",
      "cmakeCommandArgs": "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake",
      "buildCommandArgs": "",
      "ctestCommandArgs": "",
      "inheritEnvironments": [ "msvc_x64_x64" ]
    }
  ]
}
```

### Step 4: Configure CMake in Visual Studio

1. Open your project in Visual Studio
2. Go to **Project** → **Configure Cache** (or **Project** → **Delete Cache and Reconfigure**)
3. Watch the **Output** window for CMake messages

**You should see:**
```
-- Boost found: 1.84.0
-- Boost include: C:/vcpkg/installed/x64-windows/include
-- Building with Boost.Asio support
```

If you see:
```
-- Boost not found - Asio server will not be built
-- To install: vcpkg install boost-asio:x64-windows
```

Then vcpkg isn't properly integrated. Go back to Step 3.

### Step 5: Verify Installation

Create a test file to verify Boost works:

**test_boost.cpp:**
```cpp
#include <boost/asio.hpp>
#include <iostream>

int main() {
    boost::asio::io_context io_context;
    std::cout << "Boost.Asio works! Version: "
              << BOOST_VERSION / 100000 << "."
              << BOOST_VERSION / 100 % 1000 << "."
              << BOOST_VERSION % 100 << std::endl;
    return 0;
}
```

Build and run. Expected output:
```
Boost.Asio works! Version: 1.84.0
```

---

## Option 2: Using CMake FetchContent (No vcpkg)

If you don't want to install vcpkg, you can let CMake download Boost automatically.

### Edit Root CMakeLists.txt

Replace the Boost section with:

```cmake
# Option: Download Boost via FetchContent (slower first build)
include(FetchContent)

FetchContent_Declare(
    Boost
    URL https://boostorg.jfrog.io/artifactory/main/release/1.84.0/source/boost_1_84_0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(BOOST_INCLUDE_LIBRARIES asio system)
set(BOOST_ENABLE_CMAKE ON)

FetchContent_MakeAvailable(Boost)
```

**Pros:**
- No separate installation needed
- Works on any platform

**Cons:**
- First build is VERY slow (~30-60 minutes)
- Downloads entire Boost library (~200MB)

---

## Option 3: Download Boost Manually

### Step 1: Download Boost

1. Go to https://www.boost.org/users/download/
2. Download **boost_1_84_0.zip** (or latest version)
3. Extract to `C:\boost_1_84_0`

### Step 2: Build Boost (Required on Windows)

Open **Developer Command Prompt for VS**:

```cmd
cd C:\boost_1_84_0
.\bootstrap.bat
.\b2 --with-system --with-thread variant=release link=static runtime-link=shared threading=multi address-model=64 stage
```

This builds only the libraries you need (~10 minutes).

### Step 3: Tell CMake Where Boost Is

Edit root `CMakeLists.txt`:

```cmake
set(BOOST_ROOT "C:/boost_1_84_0")
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(Boost 1.84 REQUIRED COMPONENTS system)
```

### Step 4: Configure Visual Studio

Set environment variable (one-time):

```powershell
# In PowerShell as Administrator
[System.Environment]::SetEnvironmentVariable("BOOST_ROOT", "C:\boost_1_84_0", "User")
```

Restart Visual Studio.

---

## Troubleshooting

### Problem: "Boost not found"

**Solution 1:** Check vcpkg integration
```powershell
C:\vcpkg\vcpkg integrate install
```

**Solution 2:** Explicitly set CMAKE_TOOLCHAIN_FILE
- Visual Studio: Project → CMake Settings → CMake toolchain file
- Set to: `C:\vcpkg\scripts\buildsystems\vcpkg.cmake`

**Solution 3:** Check installed packages
```powershell
C:\vcpkg\vcpkg list | findstr boost
```

Should show:
```
boost-asio:x64-windows
boost-system:x64-windows
```

### Problem: Wrong architecture (x86 vs x64)

If you installed for x86 but building x64:

```powershell
# Remove x86 version
C:\vcpkg\vcpkg remove boost-asio:x86-windows

# Install x64 version
C:\vcpkg\vcpkg install boost-asio:x64-windows boost-system:x64-windows
```

### Problem: CMake can't find Boost::system

Check your CMake output. If you see:

```
Could NOT find Boost (missing: Boost_INCLUDE_DIR system)
```

**Solution:** Verify `CMAKE_TOOLCHAIN_FILE` is set in Visual Studio.

### Problem: Linker errors with Boost

Add to your CMakeLists.txt:

```cmake
if(MSVC)
    add_compile_definitions(_WIN32_WINNT=0x0A00)  # Windows 10
endif()
```

Boost.Asio needs Windows version defined on MSVC.

---

## Quick Reference Commands

### Install Boost via vcpkg
```powershell
cd C:\vcpkg
.\vcpkg install boost-asio:x64-windows boost-system:x64-windows
```

### Integrate with Visual Studio
```powershell
cd C:\vcpkg
.\vcpkg integrate install
```

### List installed packages
```powershell
C:\vcpkg\vcpkg list
```

### Update vcpkg
```powershell
cd C:\vcpkg
git pull
.\bootstrap-vcpkg.bat
```

### Remove Boost
```powershell
C:\vcpkg\vcpkg remove boost-asio:x64-windows boost-system:x64-windows
```

---

## Next Steps

Once Boost is installed and CMake finds it:

1. ✅ CMake shows: "Boost found: 1.84.0"
2. ✅ CMake shows: "Building with Boost.Asio support"
3. ➡️ Create `include/dfs/network/http_server_asio.hpp`
4. ➡️ Create `src/network/http_server_asio.cpp`
5. ➡️ Build the project - new files will be compiled automatically

See `src/network/multi_threaded_impl.md` for implementation details.

---

## Recommended Approach Summary

**For this project:** Use **Option 1 (vcpkg)**

**Why:**
- Integrates seamlessly with Visual Studio
- Easy to update
- Works with CMake automatically
- Standard for Windows C++ development

**Installation time:**
- vcpkg setup: 2 minutes
- Boost download/build: 5-10 minutes
- **Total: ~15 minutes**

**Disk space:** ~500MB (vcpkg + Boost)
