/*******************************************************************************
* This file is part of the 3DViewer                                            *
*                                                                              *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU General Public License as published by         *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
* This program is distributed in the hope that it will be useful,              *
* but WITHOUT ANY WARRANTY; without even the implied warranty of               *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                *
* GNU General Public License (http://www.gnu.org/licenses/gpl.txt)             *
* for more details.                                                            *
*                                                                              *
********************************************************************************/

// revo-bridge: drives a Revopoint camera through the 3DCamera SDK, speaking
// JSON lines on stdin/stdout (see bridgeserver.h and README.md). Meant to be
// spawned by a host process such as mhs2revo; diagnostics go to stderr.

#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <QCoreApplication>
#include <QJsonDocument>

#include <hpp/System.hpp>

#include "bridgeserver.h"
#include "sdkbackend.h"

using namespace csbridge;

// Keep the protocol channel private: move the real stdout to a new descriptor,
// then point descriptor 1 at stderr, so anything else that prints to stdout
// (the SDK, Qt, a stray printf) ends up on stderr instead of corrupting a reply.
static FILE* takeOverStdout()
{
    fflush(stdout);
#ifdef _WIN32
    const int fd = _dup(_fileno(stdout));
    if (fd < 0 || _dup2(_fileno(stderr), _fileno(stdout)) < 0)
    {
        return nullptr;
    }
    _setmode(fd, _O_BINARY);
    return _fdopen(fd, "wb");
#else
    const int fd = dup(fileno(stdout));
    if (fd < 0 || dup2(fileno(stderr), fileno(stdout)) < 0)
    {
        return nullptr;
    }
    return fdopen(fd, "wb");
#endif
}

static void printUsage()
{
    std::fprintf(stderr,
        "usage: revo-bridge [--selftest] [--sdk-log DIR] [--no-networking] [--version]\n"
        "\n"
        "Speaks JSON lines on stdin/stdout; see src/csbridge/README.md.\n"
        "  --selftest        print the ready event and the camera list, then exit\n"
        "                    (exit status 0 if the SDK loaded and enumerated)\n"
        "  --sdk-log DIR     enable the SDK's own log files, written to DIR\n"
        "  --no-networking   call setSdkEnableNetworking(false) before anything else\n"
        "                    (untested: whether USB cameras still work with it)\n"
        "  --version         print the bridge version and exit\n");
}

int main(int argc, char* argv[])
{
    bool selftest = false;
    bool noNetworking = false;
    const char* sdkLogDir = nullptr;

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--selftest") == 0)
        {
            selftest = true;
        }
        else if (std::strcmp(argv[i], "--no-networking") == 0)
        {
            noNetworking = true;
        }
        else if (std::strcmp(argv[i], "--sdk-log") == 0 && i + 1 < argc)
        {
            sdkLogDir = argv[++i];
        }
        else if (std::strcmp(argv[i], "--version") == 0)
        {
            std::printf("revo-bridge %s (protocol %d)\n", BridgeServer::BRIDGE_VERSION, BridgeServer::PROTOCOL_VERSION);
            return 0;
        }
        else
        {
            printUsage();
            return (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) ? 0 : 2;
        }
    }

    FILE* protocolOut = takeOverStdout();
    if (!protocolOut)
    {
        std::fprintf(stderr, "revo-bridge: could not set up the protocol channel on stdout\n");
        return 1;
    }

    // for Qt's image plugins (JPEG decoding for textured point clouds)
    QCoreApplication app(argc, argv);

    if (noNetworking)
    {
        cs::setSdkEnableNetworking(false);
    }
    if (sdkLogDir)
    {
        cs::setLogSavePath(sdkLogDir);
        cs::enableLoging(true);
    }
    else
    {
        cs::enableLoging(false);
    }

    std::mutex outMutex;
    auto writeLine = [&](const QByteArray& line)
    {
        std::lock_guard<std::mutex> lock(outMutex);
        std::fwrite(line.constData(), 1, (size_t)line.size(), protocolOut);
        std::fputc('\n', protocolOut);
        std::fflush(protocolOut);
    };

    SdkBackend backend;
    BridgeServer server(backend, writeLine);

    writeLine(QJsonDocument(server.readyEvent()).toJson(QJsonDocument::Compact));

    if (selftest)
    {
        const QByteArray reply = server.handleLine(R"({"id":"selftest","cmd":"list"})");
        writeLine(reply);
        const QJsonObject o = QJsonDocument::fromJson(reply).object();
        return o.value("ok").toBool() ? 0 : 1;
    }

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.find_first_not_of(" \t\r") == std::string::npos)
        {
            continue;
        }
        writeLine(server.handleLine(QByteArray::fromStdString(line)));
        if (server.exitRequested())
        {
            break;
        }
    }

    // stdin closed (host gone) or shutdown: leave the camera stopped and released
    server.closeCamera();
    return 0;
}
