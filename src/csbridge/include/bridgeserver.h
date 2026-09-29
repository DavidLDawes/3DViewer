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

#ifndef _CS_BRIDGESERVER_H
#define _CS_BRIDGESERVER_H

#include <functional>
#include <mutex>

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include "camerabackend.h"

namespace csbridge
{

// revo-bridge's request handling, independent of stdio and of the real SDK.
//
// Protocol (version 1): one compact JSON object per line.
//   request:  {"id": <any>, "cmd": "<name>", "args": {...}}
//   reply:    {"id": <same>, "ok": true,  "result": {...}}
//             {"id": <same>, "ok": false, "error": {"code": "...", "message": "...",
//                                                  "sdk_code": n, "sdk_error": "..."}}
//   event:    {"event": "<name>", ...}   (unsolicited; never has an "id")
// One request is handled at a time; each gets exactly one reply. Events can be
// written between replies, from SDK threads, through the same line writer.
// See src/csbridge/README.md for the command list.
class BridgeServer
{
public:
    static const int PROTOCOL_VERSION = 1;
    static const char* const BRIDGE_VERSION;

    // writes one line (without the trailing newline); must be thread safe
    typedef std::function<void(const QByteArray&)> LineWriter;

    BridgeServer(CameraBackend& backend, LineWriter writeLine);

    QJsonObject readyEvent();
    QByteArray handleLine(const QByteArray& line);
    bool exitRequested() const { return m_exitRequested; }

    // stops streams and disconnects; used on shutdown and at end of input
    void closeCamera();

private:
    struct Reply
    {
        bool ok = true;
        QJsonObject result;
        QJsonObject error;
    };

    Reply dispatch(const QString& cmd, const QJsonObject& args);

    Reply cmdHello();
    Reply cmdList();
    Reply cmdConnect(const QJsonObject& args);
    Reply cmdDisconnect();
    Reply cmdInfo();
    Reply cmdCapabilities();
    Reply cmdProperties();
    Reply cmdGet(const QJsonObject& args);
    Reply cmdSet(const QJsonObject& args);
    Reply cmdStartStream(const QJsonObject& args);
    Reply cmdStopStream();
    Reply cmdCapture(const QJsonObject& args);
    Reply cmdRestart();
    Reply cmdShutdown();

    Reply fail(const QString& code, const QString& message);
    Reply sdkFail(ERROR_CODE code, const QString& what);
    bool requireConnected(Reply& reply);

    QJsonObject cameraJson(const CameraInfo& info);
    bool chooseStream(STREAM_TYPE type, const QJsonObject& request, STREAM_FORMAT defaultFormat,
        int defaultWidth, int defaultHeight, StreamInfo& chosen, QString& error);
    void stopStreams();
    void resetConnectionState();
    void onBackendEvent(const QJsonObject& event);
    void applyPendingRemoval();

    CameraBackend& m_backend;
    LineWriter m_writeLine;
    bool m_exitRequested = false;

    bool m_connected = false;
    CameraInfo m_info;
    bool m_depthSupported = false;
    bool m_rgbSupported = false;
    bool m_depthStreaming = false;
    bool m_rgbStreaming = false;
    StreamInfo m_depthStream;
    StreamInfo m_rgbStream;
    float m_depthScale = 0.0f;
    Intrinsics m_depthIntrinsics;
    Intrinsics m_rgbIntrinsics;
    Extrinsics m_extrinsics;
    bool m_hasDepthIntrinsics = false;
    bool m_hasRgbCalibration = false;

    // camera_removed events arrive on an SDK thread; applied before the next command
    std::mutex m_eventMutex;
    QSet<QString> m_removedSerials;
};

} // namespace csbridge

#endif // _CS_BRIDGESERVER_H
