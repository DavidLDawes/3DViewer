## About this fork

[![CI](https://github.com/DavidLDawes/3DViewer/actions/workflows/ci.yml/badge.svg)](https://github.com/DavidLDawes/3DViewer/actions/workflows/ci.yml)

This is `DavidLDawes/3DViewer`, a fork of [Revopoint/3DViewer](https://github.com/Revopoint/3DViewer).
It serves as the device-side reference for `mhs2revo`, an MCP server (in the
[Controller](https://github.com/DavidLDawes/Controller) project) that lets AI agents
drive a Revopoint camera. The application itself is unchanged. This fork adds:

- **Hardware-free tests** (`src/tests/`, Qt Test + CTest, enabled with `-DBUILD_TESTS=ON`):
  depth-to-point-cloud math and PLY export, the 16-bit depth PNG round trip, and a smoke
  test that the prebuilt 3DCamera SDK loads and enumerates cleanly with no camera attached.
- **GitHub Actions CI** (`.github/workflows/ci.yml`): builds the app and runs the tests on
  Ubuntu 22.04 and Windows (VS 2022). Both use the qt.io build of Qt 5.15.2: the prebuilt
  Linux quazip library doesn't link against Ubuntu's `qtbase5-dev`. macOS is not built
  because the bundled mac binaries are x86_64 only.
- **`revo-bridge`** (`src/csbridge/`): a console helper that drives the camera through
  the SDK and speaks JSON lines on stdin/stdout, for mhs2revo. See
  [`src/csbridge/README.md`](src/csbridge/README.md). It is built by default
  (`-DBUILD_BRIDGE=OFF` to skip) and has not yet been run against a camera.
- `CLAUDE.md`: notes on the code layout and the SDK surface, for working with Claude.

Build and test (Windows, VS 2022, Qt 5.15.2; Linux is the same with `-G Ninja`):

```
cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTS=ON -DQt5_DIR=C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Notes: configuring regenerates `src/csviewer/translations/*.ts/.qm` (upstream behavior), so
don't commit those by accident. To run `build/bin/3DViewer.exe`, put Qt's `bin` directory
and the `thirdparty/*/windows/...` library directories on `PATH`. The build does not copy
the DLLs. The upstream instructions follow.

## 3DViewer Introduction

3DViewer is an open-source application control software used with Revopoint 3D cameras. It mainly includes the functions of obtaining a camera list, connecting a specified camera, setting various parameters of the camera, and viewing 2D images and point cloud images obtained in different states.

## Support platform

- Windows 10 (64 bit) or later
- Ubuntu Linux 18.04 (64 bit) or higher
- MacOS 10.15 (64 bit) or higher

## Compile 3DViewer

Dependent environment:

- Qt5 (Qt 5.10.1 or higher),  Qt download address is shown in: [https://download.qt.io/](https://download.qt.io/)
- Windows
  - Visual Studio 2015 or later
- MacOS
  - Latest Xcode
- Linux
  - g++ 7.5.0 or higher
- Dependent libraries:
  - 3DCamera
  - OpenSceneGraph 3.6.5 or higher
  - OpenMP

### Compile under Windows platform

1. Installation [Qt5](https://download.qt.io/)、[Visual Studio 2015 or later (Community Edition)](https://visualstudio.microsoft.com)、[CMake](https://cmake.org/download/)、[Git](https://git-scm.com/downloads) (Add the running path to the windows system environment variable when installing CMake and Git)

2. Use Git to download code

   - Create a new code storage folder, for example: ***D:\3DViewer-git***
   - Use the Git clone command to download the code, press the ***windows + R*** shortcut key to open a command prompt window, enter ***cmd.exe***，and then enter the following command:

       ```
       cd /D D:\3DViewer-git
       git clone https://github.com/Revopoint/3DViewer.git
       ```

3. Create a new Visual Studio project by CMake (take QT 5.10.1 + Visual Studio 2015 as an example).

   - Create a new build folder in the root directory of downloaded code, open a command prompt, and enter the build directory:

     ```
     cd D:\3DViewer-git\3DViewer\build
     ```

   - To execute CMake, you need to modify ***-DQt5_DIR =‘……’*** which points to ***Qt5Config.cmake*** storage location, and ***-G "Visual Studio 14 2015 Win64"*** indicates to generate a Visual Studio 2015 project.

     ```
     cmake -DQt5_DIR=c:\Qt5.10.1\5.10.1\msvc2015_64\lib\cmake\Qt5 -G"Visual Studio 14 2015 Win64" ..\src\
     ```

4. Use Visual Studio 2015 to open the 3DViewer.sln solution file generated in the build directory.

5. After opening the project, press ***Ctrl+Shift+B*** to compile, or press ***F5*** to run the program directly.

### Compile under MacOS platform

1. Installation [Qt5](https://download.qt.io/)、[CMake](https://cmake.org/download/)、[Git](https://git-scm.com/downloads)、Xcode.

2. Open Terminal and run the following command to install OpenMP:

   ```
   brew install libomp
   ```

3. Open the terminal, create a new code storage folder, for example: ***~/3DViewer-git***. Then execute git clone to download the code.

   ```
   mkdir ~/3DViewer-git
   cd ~/3DViewer-git
   git clone https://github.com/Revopoint/3DViewer.git
   ```

4. Create a new build directory in the code root directory, enter the directory and execute CMake command.  ***-DQt5_ DIR =‘……’*** points to Qt5Config.cmake storage location.

   ```
   mkdir 3DViewer/build
   cd 3DViewer/build
   cmake -DQt5_DIR=~/Qt5.10.1/5.10.1/clang_64/lib/cmake/Qt5 ../src/
   ```

5. Execute make in the build directory to complete the project compilation.

   ```
   make
   ```

6. After compilation, double-click ***bin/3DViewer.app*** to run the program.

   (When connecting the camera via USB, you need to run the ***scripts/cs_rpc_install.sh*** script under the code root directory first.)


### Compile on Linux（Ubuntu） platform

1. Installation  [Qt5](https://download.qt.io/)、[CMake](https://cmake.org/download/)、[Git](https://git-scm.com/downloads)、g++ (take Qt 5.10.1 as an example).

   ```
   sudo apt -y install cmake g++ git 
   ```

   - Install Qt5 (take Qt 5.10.1 as an example)

     ```
     wget https://download.qt.io/new_archive/qt/5.10/5.10.1/qt-opensource-linux-x64-5.10.1.run
     chmod +x qt-opensource-linux-x64-5.10.1.run
     ./qt-opensource-linux-x64-5.10.1.run
     ```
     
   - Install OpenGL dependencies
   
     ```
     sudo apt install mesa-common-dev
     ```
   
2. Open the terminal, create a new code storage folder, for example: ***~/3DViewer-git***. Then execute git clone to download the code.

   ```
   mkdir ~/3DViewer-git
   cd ~/3DViewer-git
   git clone https://github.com/Revopoint/3DViewer.git
   ```

3. Create a new build directory in the code root directory, enter the directory and execute CMake command.  ***-DQt5_ DIR =‘……’*** points to the storage location of ***Qt5config.cmake***.

   ```
   mkdir 3DViewer/build
   cd 3DViewer/build
   cmake -DQt5_DIR=~/Qt5.10.1/5.10.1/gcc_64/lib/cmake/Qt5 ../src/
   ```

4. Execute make in the build directory to complete the project compilation.

   ```
   make
   ```

5. After compilation, execute the ***./bin/3DViewer*** to run the program.

   ```
   ./bin/3DViewer 
   ```

   (When connecting the camera via USB, you need to run the ***scripts/cs_uvc_config. sh*** script in the code root directory first.)
