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

#include "bridgeserver.h"

#include <cstring>

#include <QJsonArray>
#include <QJsonDocument>

#include "capturewriter.h"
#include "propertymap.h"

namespace csbridge
{

const char* const BridgeServer::BRIDGE_VERSION = "0.1.0";

// 3DViewer's default stream choices (CAMERA_DEFAULT_STREAM_TYPE in cscamera.cpp)
static const STREAM_FORMAT DEFAULT_DEPTH_FORMAT = STREAM_FORMAT_Z16;
static const int DEFAULT_DEPTH_WIDTH = 640;
static const int DEFAULT_DEPTH_HEIGHT = 400;
static const STREAM_FORMAT DEFAULT_RGB_FORMAT = STREAM_FORMAT_RGB8;
static const int DEFAULT_RGB_WIDTH = 1280;
static const int DEFAULT_RGB_HEIGHT = 800;

static const int DEFAULT_CAPTURE_TIMEOUT_MS = 5000;
static const int MAX_CAPTURE_TIMEOUT_MS = 60000;

static QString fixedString(const char* s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
    {
        n++;
    }
    return QString::fromUtf8(s, (int)n);
}

static QJsonObject streamInfoJson(const StreamInfo& info)
{
    return QJsonObject{ { "format", streamFormatName(info.format) }, { "width", info.width },
                        { "height", info.height }, { "fps", info.fps } };
}

static QJsonObject intrinsicsJson(const Intrinsics& intr)
{
    return QJsonObject{ { "width", intr.width }, { "height", intr.height },
                        { "fx", intr.fx }, { "fy", intr.fy }, { "cx", intr.cx }, { "cy", intr.cy } };
}

static QJsonObject extrinsicsJson(const Extrinsics& extr)
{
    QJsonArray rotation, translation;
    for (float r : extr.rotation)
    {
        rotation.append(r);
    }
    for (float t : extr.translation)
    {
        translation.append(t);
    }
    return QJsonObject{ { "rotation", rotation }, { "translation", translation } };
}

static QString statusName(CAMERA_STATUS status)
{
    switch (status)
    {
    case CS_IDLE:                return "idle";
    case CS_CONNECTED_BY_SDK:    return "connected_by_this_process";
    case CS_CONNECTED_BY_OTHER:  return "connected_by_other_process";
    default:                     return "unknown";
    }
}

BridgeServer::BridgeServer(CameraBackend& backend, LineWriter writeLine)
    : m_backend(backend)
    , m_writeLine(writeLine)
{
    std::memset(&m_info, 0, sizeof(m_info));
    std::memset(&m_depthStream, 0, sizeof(m_depthStream));
    std::memset(&m_rgbStream, 0, sizeof(m_rgbStream));
    std::memset(&m_depthIntrinsics, 0, sizeof(m_depthIntrinsics));
    std::memset(&m_rgbIntrinsics, 0, sizeof(m_rgbIntrinsics));
    std::memset(&m_extrinsics, 0, sizeof(m_extrinsics));

    m_backend.setEventHandler([this](const QJsonObject& event) { onBackendEvent(event); });
}

QJsonObject BridgeServer::readyEvent()
{
    return QJsonObject{ { "event", "ready" }, { "protocol", PROTOCOL_VERSION },
                        { "bridge_version", BRIDGE_VERSION },
                        { "sdk_version", QString::fromStdString(m_backend.sdkVersion()) } };
}

void BridgeServer::onBackendEvent(const QJsonObject& event)
{
    if (event.value("event").toString() == "camera_removed")
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_removedSerials.insert(event.value("serial").toString());
    }
    m_writeLine(QJsonDocument(event).toJson(QJsonDocument::Compact));
}

void BridgeServer::applyPendingRemoval()
{
    QSet<QString> removed;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        removed.swap(m_removedSerials);
    }

    if (m_connected && removed.contains(fixedString(m_info.serial, sizeof(m_info.serial))))
    {
        // the camera is gone: nothing to stop, just forget it
        resetConnectionState();
    }
}

QByteArray BridgeServer::handleLine(const QByteArray& line)
{
    applyPendingRemoval();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &parseError);

    QJsonValue id;
    Reply reply;

    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        reply = fail("BAD_REQUEST", QString("not a JSON object: %1").arg(parseError.errorString()));
    }
    else
    {
        const QJsonObject request = doc.object();
        id = request.value("id");

        const QJsonValue cmd = request.value("cmd");
        const QJsonValue args = request.value("args");
        if (!cmd.isString())
        {
            reply = fail("BAD_REQUEST", "\"cmd\" must be a string");
        }
        else if (!args.isUndefined() && !args.isNull() && !args.isObject())
        {
            reply = fail("BAD_REQUEST", "\"args\" must be an object");
        }
        else
        {
            reply = dispatch(cmd.toString(), args.toObject());
        }
    }

    QJsonObject out;
    out["id"] = id.isUndefined() ? QJsonValue() : id;
    out["ok"] = reply.ok;
    if (reply.ok)
    {
        out["result"] = reply.result;
    }
    else
    {
        out["error"] = reply.error;
    }
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

BridgeServer::Reply BridgeServer::dispatch(const QString& cmd, const QJsonObject& args)
{
    if (cmd == "hello")         return cmdHello();
    if (cmd == "list")          return cmdList();
    if (cmd == "connect")       return cmdConnect(args);
    if (cmd == "disconnect")    return cmdDisconnect();
    if (cmd == "info")          return cmdInfo();
    if (cmd == "capabilities")  return cmdCapabilities();
    if (cmd == "properties")    return cmdProperties();
    if (cmd == "get")           return cmdGet(args);
    if (cmd == "set")           return cmdSet(args);
    if (cmd == "start_stream")  return cmdStartStream(args);
    if (cmd == "stop_stream")   return cmdStopStream();
    if (cmd == "capture")       return cmdCapture(args);
    if (cmd == "restart")       return cmdRestart();
    if (cmd == "shutdown")      return cmdShutdown();

    return fail("UNKNOWN_COMMAND", QString("unknown command '%1'").arg(cmd));
}

BridgeServer::Reply BridgeServer::fail(const QString& code, const QString& message)
{
    Reply reply;
    reply.ok = false;
    reply.error = QJsonObject{ { "code", code }, { "message", message } };
    return reply;
}

BridgeServer::Reply BridgeServer::sdkFail(ERROR_CODE code, const QString& what)
{
    Reply reply = fail("SDK_ERROR",
        QString("%1: %2").arg(what).arg(QString::fromStdString(m_backend.errorString(code))));
    reply.error["sdk_code"] = (int)code;
    reply.error["sdk_error"] = errorCodeName(code);
    return reply;
}

bool BridgeServer::requireConnected(Reply& reply)
{
    if (!m_connected)
    {
        reply = fail("NOT_CONNECTED", "no camera connected; call connect first");
        return false;
    }
    return true;
}

QJsonObject BridgeServer::cameraJson(const CameraInfo& info)
{
    const QString uniqueId = fixedString(info.uniqueId, sizeof(info.uniqueId));
    return QJsonObject{
        { "name", fixedString(info.name, sizeof(info.name)) },
        { "serial", fixedString(info.serial, sizeof(info.serial)) },
        { "unique_id", uniqueId },
        { "firmware_version", fixedString(info.firmwareVersion, sizeof(info.firmwareVersion)) },
        { "algorithm_version", fixedString(info.algorithmVersion, sizeof(info.algorithmVersion)) },
        { "model", QString::fromStdString(m_backend.cameraTypeName(info.serial)) },
        // the same test 3DViewer uses (CSCamera::isNetworkConnect): network cameras are listed by IP
        { "connection", uniqueId.contains('.') ? "network" : "usb" },
        { "status", statusName(m_backend.cameraStatus(info.serial)) },
    };
}

// ---------------------------------------------------------------------- commands

BridgeServer::Reply BridgeServer::cmdHello()
{
    Reply reply;
    reply.result = QJsonObject{ { "protocol", PROTOCOL_VERSION }, { "bridge_version", BRIDGE_VERSION },
                                { "sdk_version", QString::fromStdString(m_backend.sdkVersion()) } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdList()
{
    std::vector<CameraInfo> cameras;
    ERROR_CODE ret = m_backend.queryCameras(cameras);
    if (ret != SUCCESS)
    {
        return sdkFail(ret, "querying cameras failed");
    }

    QJsonArray list;
    for (const auto& info : cameras)
    {
        list.append(cameraJson(info));
    }

    Reply reply;
    reply.result = QJsonObject{ { "cameras", list } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdConnect(const QJsonObject& args)
{
    const QJsonValue serialArg = args.value("serial");
    if (!serialArg.isUndefined() && !serialArg.isNull() && !serialArg.isString())
    {
        return fail("BAD_VALUE", "\"serial\" must be a string");
    }
    const QString serial = serialArg.toString();

    if (m_connected)
    {
        const QString current = fixedString(m_info.serial, sizeof(m_info.serial));
        if (serial.isEmpty() || serial == current)
        {
            Reply reply;
            reply.result = cameraJson(m_info);
            reply.result["already_connected"] = true;
            return reply;
        }
        return fail("ALREADY_CONNECTED", QString("connected to %1; disconnect first").arg(current));
    }

    std::vector<CameraInfo> cameras;
    ERROR_CODE ret = m_backend.queryCameras(cameras);
    if (ret != SUCCESS)
    {
        return sdkFail(ret, "querying cameras failed");
    }

    const CameraInfo* target = nullptr;
    if (serial.isEmpty())
    {
        if (cameras.size() == 1)
        {
            target = &cameras[0];
        }
        else if (cameras.empty())
        {
            return fail("CAMERA_NOT_FOUND", "no camera found");
        }
        else
        {
            return fail("AMBIGUOUS_CAMERA", QString("%1 cameras found; pass \"serial\"").arg(cameras.size()));
        }
    }
    else
    {
        for (const auto& info : cameras)
        {
            if (serial == fixedString(info.serial, sizeof(info.serial)))
            {
                target = &info;
            }
        }
        if (!target)
        {
            return fail("CAMERA_NOT_FOUND", QString("no camera with serial %1").arg(serial));
        }
    }

    if (m_backend.cameraStatus(target->serial) == CS_CONNECTED_BY_OTHER)
    {
        return fail("CAMERA_IN_USE", "another process (e.g. 3DViewer or Revo Scan) is connected to this camera");
    }

    ret = m_backend.connect(*target);
    if (ret != SUCCESS)
    {
        return sdkFail(ret, "connect failed");
    }

    m_connected = true;
    m_info = *target;

    QJsonArray warnings;
    auto warn = [&](const QString& what, ERROR_CODE code)
    {
        warnings.append(QString("%1: %2").arg(what).arg(errorCodeName(code)));
    };

    if ((ret = m_backend.isStreamSupport(STREAM_TYPE_DEPTH, m_depthSupported)) != SUCCESS)
    {
        warn("checking depth stream support failed", ret);
    }
    if ((ret = m_backend.isStreamSupport(STREAM_TYPE_RGB, m_rgbSupported)) != SUCCESS)
    {
        warn("checking RGB stream support failed", ret);
    }

    // calibration, read once per connection as 3DViewer does (CSCamera::initCameraInfo)
    if (m_depthSupported)
    {
        ret = m_backend.getIntrinsics(STREAM_TYPE_DEPTH, m_depthIntrinsics);
        m_hasDepthIntrinsics = (ret == SUCCESS);
        if (ret != SUCCESS)
        {
            warn("reading depth intrinsics failed", ret);
        }
    }
    if (m_rgbSupported)
    {
        ERROR_CODE r1 = m_backend.getIntrinsics(STREAM_TYPE_RGB, m_rgbIntrinsics);
        ERROR_CODE r2 = m_backend.getExtrinsics(m_extrinsics);
        m_hasRgbCalibration = (r1 == SUCCESS && r2 == SUCCESS);
        if (r1 != SUCCESS)
        {
            warn("reading RGB intrinsics failed", r1);
        }
        if (r2 != SUCCESS)
        {
            warn("reading extrinsics failed", r2);
        }

        // match depth and RGB frames by timestamp, as 3DViewer does
        PropertyExtension match;
        std::memset(&match, 0, sizeof(match));
        match.depthRgbMatchParam.iRgbOffset = 0;
        match.depthRgbMatchParam.iDifThreshold = 0;
        if ((ret = m_backend.setPropertyExtension(PROPERTY_EXT_DEPTH_RGB_MATCH_PARAM, match)) != SUCCESS)
        {
            warn("setting depth/RGB match parameters failed", ret);
        }
    }

    Reply reply;
    reply.result = cameraJson(m_info);
    reply.result["depth_supported"] = m_depthSupported;
    reply.result["rgb_supported"] = m_rgbSupported;
    if (!warnings.isEmpty())
    {
        reply.result["warnings"] = warnings;
    }
    return reply;
}

void BridgeServer::stopStreams()
{
    if (m_depthStreaming)
    {
        m_backend.stopStream(STREAM_TYPE_DEPTH);
        m_depthStreaming = false;
    }
    if (m_rgbStreaming)
    {
        m_backend.stopStream(STREAM_TYPE_RGB);
        m_rgbStreaming = false;
    }
}

void BridgeServer::resetConnectionState()
{
    m_connected = false;
    m_depthStreaming = false;
    m_rgbStreaming = false;
    m_depthSupported = false;
    m_rgbSupported = false;
    m_hasDepthIntrinsics = false;
    m_hasRgbCalibration = false;
    m_depthScale = 0.0f;
    std::memset(&m_info, 0, sizeof(m_info));
}

void BridgeServer::closeCamera()
{
    if (m_connected)
    {
        stopStreams();
        m_backend.disconnect();
    }
    resetConnectionState();
}

BridgeServer::Reply BridgeServer::cmdDisconnect()
{
    const bool wasConnected = m_connected;
    closeCamera();

    Reply reply;
    reply.result = QJsonObject{ { "was_connected", wasConnected } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdInfo()
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }

    reply.result = cameraJson(m_info);
    reply.result["sdk_version"] = QString::fromStdString(m_backend.sdkVersion());
    reply.result["bridge_version"] = BRIDGE_VERSION;
    reply.result["depth_supported"] = m_depthSupported;
    reply.result["rgb_supported"] = m_rgbSupported;
    reply.result["depth_stream"] = m_depthStreaming ? QJsonValue(streamInfoJson(m_depthStream)) : QJsonValue();
    reply.result["rgb_stream"] = m_rgbStreaming ? QJsonValue(streamInfoJson(m_rgbStream)) : QJsonValue();
    if (m_hasDepthIntrinsics)
    {
        reply.result["depth_intrinsics"] = intrinsicsJson(m_depthIntrinsics);
    }
    if (m_hasRgbCalibration)
    {
        reply.result["rgb_intrinsics"] = intrinsicsJson(m_rgbIntrinsics);
        reply.result["extrinsics"] = extrinsicsJson(m_extrinsics);
    }
    return reply;
}

BridgeServer::Reply BridgeServer::cmdCapabilities()
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }

    QJsonObject streams;
    const struct { STREAM_TYPE type; const char* name; bool supported; } kinds[] =
    {
        { STREAM_TYPE_DEPTH, "depth", m_depthSupported },
        { STREAM_TYPE_RGB, "rgb", m_rgbSupported },
    };
    for (const auto& kind : kinds)
    {
        QJsonObject stream{ { "supported", kind.supported } };
        if (kind.supported)
        {
            std::vector<StreamInfo> infos;
            ERROR_CODE ret = m_backend.getStreamInfos(kind.type, infos);
            if (ret == SUCCESS)
            {
                QJsonArray modes;
                for (const auto& info : infos)
                {
                    modes.append(streamInfoJson(info));
                }
                stream["modes"] = modes;
            }
            else
            {
                stream["modes_error"] = errorCodeName(ret);
            }
        }
        streams[kind.name] = stream;
    }

    QJsonArray properties;
    for (const auto& def : propertyTable())
    {
        properties.append(describeProperty(m_backend, def, true));
    }

    QJsonArray outputs;
    for (const QString& output : CaptureWriter::knownOutputs())
    {
        outputs.append(output);
    }

    reply.result = QJsonObject{ { "streams", streams }, { "properties", properties }, { "capture_outputs", outputs } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdProperties()
{
    QJsonArray properties;
    for (const auto& def : propertyTable())
    {
        properties.append(describeProperty(m_backend, def, false));
    }

    Reply reply;
    reply.result = QJsonObject{ { "properties", properties } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdGet(const QJsonObject& args)
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }

    const QString name = args.value("name").toString();
    const PropertyDef* def = findProperty(name);
    if (!def)
    {
        return fail("UNKNOWN_PROPERTY", QString("unknown property '%1'; see the properties command").arg(name));
    }

    QJsonValue value;
    const PropertyStatus status = readProperty(m_backend, *def, value);
    switch (status.kind)
    {
    case PropertyStatus::Ok:
        reply.result = QJsonObject{ { "name", name }, { "value", value } };
        return reply;
    case PropertyStatus::NotReadable:
        return fail("NOT_READABLE", status.message);
    case PropertyStatus::SdkError:
        return sdkFail(status.sdkCode, QString("reading %1 failed").arg(name));
    default:
        return fail("BAD_VALUE", status.message);
    }
}

BridgeServer::Reply BridgeServer::cmdSet(const QJsonObject& args)
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }

    const QString name = args.value("name").toString();
    const PropertyDef* def = findProperty(name);
    if (!def)
    {
        return fail("UNKNOWN_PROPERTY", QString("unknown property '%1'; see the properties command").arg(name));
    }
    if (!args.contains("value"))
    {
        return fail("BAD_REQUEST", "\"value\" is required");
    }

    const PropertyStatus status = writeProperty(m_backend, *def, args.value("value"));
    switch (status.kind)
    {
    case PropertyStatus::Ok:
        break;
    case PropertyStatus::NotWritable:
        return fail("NOT_WRITABLE", status.message);
    case PropertyStatus::SdkError:
        return sdkFail(status.sdkCode, QString("setting %1 failed").arg(name));
    default:
        return fail("BAD_VALUE", QString("%1: %2").arg(name).arg(status.message));
    }

    reply.result = QJsonObject{ { "name", name } };

    // read back what the camera actually holds now, where it can be read
    if (def->readable)
    {
        QJsonValue value;
        const PropertyStatus readBack = readProperty(m_backend, *def, value);
        if (readBack.ok())
        {
            reply.result["value"] = value;
        }
        else
        {
            reply.result["read_back_error"] = readBack.message;
        }
    }
    return reply;
}

bool BridgeServer::chooseStream(STREAM_TYPE type, const QJsonObject& request, STREAM_FORMAT defaultFormat,
    int defaultWidth, int defaultHeight, StreamInfo& chosen, QString& error)
{
    std::vector<StreamInfo> infos;
    ERROR_CODE ret = m_backend.getStreamInfos(type, infos);
    if (ret != SUCCESS || infos.empty())
    {
        error = QString("no stream modes reported (%1)").arg(errorCodeName(ret));
        return false;
    }

    // an explicit request must match exactly; otherwise use 3DViewer's defaults
    // (format, then resolution), falling back to the first mode reported
    STREAM_FORMAT format = defaultFormat;
    bool explicitFormat = false;
    if (request.contains("format"))
    {
        if (!streamFormatFromName(request.value("format").toString(), format))
        {
            error = QString("unknown format '%1'").arg(request.value("format").toString());
            return false;
        }
        explicitFormat = true;
    }
    const bool explicitSize = request.contains("width") || request.contains("height");
    const int width = request.value("width").toInt(defaultWidth);
    const int height = request.value("height").toInt(defaultHeight);
    const bool explicitFps = request.contains("fps");
    const double fps = request.value("fps").toDouble();

    const StreamInfo* best = nullptr;
    int bestScore = -1;
    for (const auto& info : infos)
    {
        const bool formatMatch = info.format == format;
        const bool sizeMatch = info.width == width && info.height == height;
        const bool fpsMatch = !explicitFps || qAbs(info.fps - fps) < 0.01;

        if ((explicitFormat && !formatMatch) || (explicitSize && !sizeMatch) || (explicitFps && !fpsMatch))
        {
            continue;
        }

        const int score = (formatMatch ? 2 : 0) + (sizeMatch ? 1 : 0);
        if (score > bestScore)
        {
            best = &info;
            bestScore = score;
        }
    }

    if (!best)
    {
        error = "no reported stream mode matches the request; see capabilities";
        return false;
    }
    chosen = *best;
    return true;
}

BridgeServer::Reply BridgeServer::cmdStartStream(const QJsonObject& args)
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }
    if (m_depthStreaming || m_rgbStreaming)
    {
        return fail("ALREADY_STREAMING", "streams already started; stop_stream first");
    }
    if (!m_depthSupported)
    {
        return fail("NOT_SUPPORTED", "this camera reports no depth stream");
    }

    const QJsonValue depthArg = args.value("depth");
    const QJsonValue rgbArg = args.value("rgb");
    if (!depthArg.isUndefined() && !depthArg.isObject())
    {
        return fail("BAD_VALUE", "\"depth\" must be an object");
    }
    if (!rgbArg.isUndefined() && !rgbArg.isObject() && !rgbArg.isBool())
    {
        return fail("BAD_VALUE", "\"rgb\" must be an object or a boolean");
    }

    // RGB defaults to on when the camera has it; "rgb": false turns it off
    const bool wantRgb = m_rgbSupported && !(rgbArg.isBool() && !rgbArg.toBool());
    if (rgbArg.isObject() && !m_rgbSupported)
    {
        return fail("NOT_SUPPORTED", "this camera reports no RGB stream");
    }

    StreamInfo depthInfo, rgbInfo;
    QString error;
    if (!chooseStream(STREAM_TYPE_DEPTH, depthArg.toObject(), DEFAULT_DEPTH_FORMAT,
        DEFAULT_DEPTH_WIDTH, DEFAULT_DEPTH_HEIGHT, depthInfo, error))
    {
        return fail("BAD_VALUE", "depth: " + error);
    }
    if (wantRgb && !chooseStream(STREAM_TYPE_RGB, rgbArg.toObject(), DEFAULT_RGB_FORMAT,
        DEFAULT_RGB_WIDTH, DEFAULT_RGB_HEIGHT, rgbInfo, error))
    {
        return fail("BAD_VALUE", "rgb: " + error);
    }

    ERROR_CODE ret = m_backend.startStream(STREAM_TYPE_DEPTH, depthInfo);
    if (ret != SUCCESS)
    {
        return sdkFail(ret, "starting the depth stream failed");
    }
    m_depthStreaming = true;
    m_depthStream = depthInfo;

    if (wantRgb)
    {
        ret = m_backend.startStream(STREAM_TYPE_RGB, rgbInfo);
        if (ret != SUCCESS)
        {
            stopStreams();
            return sdkFail(ret, "starting the RGB stream failed");
        }
        m_rgbStreaming = true;
        m_rgbStream = rgbInfo;
    }

    // depth scale is read after the stream starts, as 3DViewer does (CSCamera::onStreamStarted)
    PropertyExtension p;
    std::memset(&p, 0, sizeof(p));
    ret = m_backend.getPropertyExtension(PROPERTY_EXT_DEPTH_SCALE, p);
    m_depthScale = (ret == SUCCESS) ? p.depthScale : 0.0f;

    reply.result = QJsonObject{
        { "depth", streamInfoJson(m_depthStream) },
        { "rgb", m_rgbStreaming ? QJsonValue(streamInfoJson(m_rgbStream)) : QJsonValue() },
        { "depth_scale", m_depthScale },
    };
    if (ret != SUCCESS)
    {
        reply.result["warnings"] = QJsonArray{ QString("reading depth scale failed: %1").arg(errorCodeName(ret)) };
    }
    return reply;
}

BridgeServer::Reply BridgeServer::cmdStopStream()
{
    Reply reply;
    const bool wasStreaming = m_depthStreaming || m_rgbStreaming;
    stopStreams();
    reply.result = QJsonObject{ { "was_streaming", wasStreaming } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdCapture(const QJsonObject& args)
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }
    if (!m_depthStreaming)
    {
        return fail("NOT_STREAMING", "start_stream first");
    }

    CaptureRequest request;
    request.dir = args.value("dir").toString();
    request.name = args.value("name").toString();
    request.texture = args.value("texture").toBool(false);
    request.binaryPly = args.value("binary_ply").toBool(true);

    const QJsonValue outputsArg = args.value("outputs");
    if (outputsArg.isUndefined())
    {
        request.outputs = { "depth", "ply" };
        if (m_rgbStreaming)
        {
            request.outputs.insert("rgb");
        }
    }
    else if (!outputsArg.isArray())
    {
        return fail("BAD_VALUE", "\"outputs\" must be an array of strings");
    }
    else
    {
        for (const QJsonValue& v : outputsArg.toArray())
        {
            request.outputs.insert(v.toString());
        }
    }

    const int timeoutMs = args.value("timeout_ms").toInt(DEFAULT_CAPTURE_TIMEOUT_MS);
    if (timeoutMs <= 0 || timeoutMs > MAX_CAPTURE_TIMEOUT_MS)
    {
        return fail("BAD_VALUE", QString("timeout_ms must be 1-%1").arg(MAX_CAPTURE_TIMEOUT_MS));
    }

    // validates and creates the directory before anything happens on the camera
    const QString error = CaptureWriter::prepare(request);
    if (!error.isEmpty())
    {
        return fail("BAD_VALUE", error);
    }

    QJsonArray warnings;

    // "fresh" (default): drop frames the SDK buffered before this call, so the
    // capture reflects the scene now (e.g. after a turntable move), not earlier
    const bool fresh = args.value("fresh").toBool(true);
    if (fresh)
    {
        PropertyExtension p;
        std::memset(&p, 0, sizeof(p));
        ERROR_CODE ret = m_backend.setPropertyExtension(PROPERTY_EXT_CLEAR_FRAME_BUFFER, p);
        if (ret != SUCCESS)
        {
            warnings.append(QString("clearing the frame buffer failed (%1); the frame may predate this call")
                .arg(errorCodeName(ret)));
        }
    }

    // in software trigger mode a frame is produced only on softTrigger()
    PropertyExtension trigger;
    std::memset(&trigger, 0, sizeof(trigger));
    ERROR_CODE ret = m_backend.getPropertyExtension(PROPERTY_EXT_TRIGGER_MODE, trigger);
    const bool softwareTrigger = (ret == SUCCESS && trigger.triggerMode == TRIGGER_MODE_SOFTWAER);
    if (ret != SUCCESS)
    {
        warnings.append(QString("reading trigger mode failed (%1); not triggering").arg(errorCodeName(ret)));
    }
    if (softwareTrigger)
    {
        ret = m_backend.softTrigger();
        if (ret != SUCCESS)
        {
            return sdkFail(ret, "software trigger failed");
        }
    }

    if (m_depthScale <= 0.0f)
    {
        PropertyExtension p;
        std::memset(&p, 0, sizeof(p));
        if (m_backend.getPropertyExtension(PROPERTY_EXT_DEPTH_SCALE, p) == SUCCESS)
        {
            m_depthScale = p.depthScale;
        }
    }

    CaptureData data;
    ret = m_backend.getFrames(m_rgbStreaming, timeoutMs, data.depth, data.rgb);
    if (ret != SUCCESS)
    {
        return sdkFail(ret, "getting the frame failed");
    }

    data.depthIntrinsics = m_depthIntrinsics;
    data.rgbIntrinsics = m_rgbIntrinsics;
    data.extrinsics = m_extrinsics;
    data.hasRgbCalibration = m_hasRgbCalibration;
    data.depthScale = m_depthScale;

    reply.result = CaptureWriter::write(request, data);
    reply.result["trigger"] = softwareTrigger ? "software" : "stream";
    reply.result["depth_scale"] = m_depthScale;
    reply.result["depth_frame"] = QJsonObject{ { "format", streamFormatName(data.depth.format) },
        { "width", data.depth.width }, { "height", data.depth.height }, { "timestamp", data.depth.timestamp } };
    if (!data.rgb.empty())
    {
        reply.result["rgb_frame"] = QJsonObject{ { "format", streamFormatName(data.rgb.format) },
            { "width", data.rgb.width }, { "height", data.rgb.height }, { "timestamp", data.rgb.timestamp } };
    }
    if (m_hasDepthIntrinsics)
    {
        reply.result["depth_intrinsics"] = intrinsicsJson(m_depthIntrinsics);
    }
    if (m_hasRgbCalibration)
    {
        reply.result["rgb_intrinsics"] = intrinsicsJson(m_rgbIntrinsics);
        reply.result["extrinsics"] = extrinsicsJson(m_extrinsics);
    }
    if (!warnings.isEmpty())
    {
        reply.result["warnings"] = warnings;
    }
    return reply;
}

BridgeServer::Reply BridgeServer::cmdRestart()
{
    Reply reply;
    if (!requireConnected(reply))
    {
        return reply;
    }

    stopStreams();
    ERROR_CODE ret = m_backend.restart();
    // the camera reboots and re-enumerates; the host must connect again
    resetConnectionState();
    if (ret != SUCCESS)
    {
        return sdkFail(ret, "restart failed");
    }
    reply.result = QJsonObject{ { "restarting", true } };
    return reply;
}

BridgeServer::Reply BridgeServer::cmdShutdown()
{
    closeCamera();
    m_exitRequested = true;

    Reply reply;
    reply.result = QJsonObject{ { "bye", true } };
    return reply;
}

} // namespace csbridge
