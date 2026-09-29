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

#include "sdkbackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

namespace csbridge
{

static QString serialOf(const CameraInfo& info)
{
    size_t n = 0;
    while (n < sizeof(info.serial) && info.serial[n])
    {
        n++;
    }
    return QString::fromUtf8(info.serial, (int)n);
}

static void copyFrame(const cs::IFramePtr& src, Frame& dst)
{
    dst = Frame();
    if (!src || src->empty())
    {
        return;
    }
    dst.format = src->getFormat();
    dst.width = src->getWidth();
    dst.height = src->getHeight();
    dst.timestamp = src->getTimeStamp();
    const char* data = src->getData();
    const int size = src->getSize();
    if (data && size > 0)
    {
        dst.data.assign(data, data + size);
    }
}

SdkBackend::SdkBackend()
    : m_camera(cs::getCameraPtr())
{
    auto system = cs::getSystemPtr();
    system->setCameraChangeCallback(&SdkBackend::onCameraChange, this);
    system->setCameraAlarmCallback(&SdkBackend::onCameraAlarm, this);
}

SdkBackend::~SdkBackend()
{
    auto system = cs::getSystemPtr();
    system->setCameraChangeCallback(nullptr, nullptr);
    system->setCameraAlarmCallback(nullptr, nullptr);
}

void SdkBackend::setEventHandler(EventHandler handler)
{
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    m_handler = handler;
}

void SdkBackend::emitEvent(const QJsonObject& event)
{
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    if (m_handler)
    {
        m_handler(event);
    }
}

// called on an SDK thread
void SdkBackend::onCameraChange(std::vector<CameraInfo>& added, std::vector<CameraInfo>& removed, void* user)
{
    SdkBackend* self = static_cast<SdkBackend*>(user);
    for (const auto& info : added)
    {
        self->emitEvent(QJsonObject{ { "event", "camera_added" }, { "serial", serialOf(info) } });
    }
    for (const auto& info : removed)
    {
        self->emitEvent(QJsonObject{ { "event", "camera_removed" }, { "serial", serialOf(info) } });
    }
}

// called on an SDK thread; the payload's schema is not documented, so pass it through
void SdkBackend::onCameraAlarm(const char* jsonData, int length, void* user)
{
    SdkBackend* self = static_cast<SdkBackend*>(user);
    const QByteArray raw = (jsonData && length > 0) ? QByteArray(jsonData, length) : QByteArray();

    QJsonObject event{ { "event", "alarm" } };
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isObject())
    {
        event["data"] = doc.object();
    }
    else if (doc.isArray())
    {
        event["data"] = doc.array();
    }
    else
    {
        event["raw"] = QString::fromUtf8(raw);
    }
    self->emitEvent(event);
}

std::string SdkBackend::sdkVersion()
{
    CS_SDK_VERSION* version = nullptr;
    if (cs::getSdkVersion(&version) == SUCCESS && version && version->version)
    {
        return version->version;
    }
    return "unknown";
}

std::string SdkBackend::errorString(ERROR_CODE code)
{
    const char* s = cs::getCameraErrorString(code);
    return s ? s : "unknown error";
}

std::string SdkBackend::cameraTypeName(const char* serial)
{
    const char* name = cs::getCameraTypeName(cs::getCameraTypeBySN(serial));
    return name ? name : "unknown";
}

CAMERA_STATUS SdkBackend::cameraStatus(const char* serial)
{
    return cs::getCameraStatus(serial);
}

ERROR_CODE SdkBackend::queryCameras(std::vector<CameraInfo>& cameras)
{
    return cs::getSystemPtr()->queryCameras(cameras);
}

ERROR_CODE SdkBackend::connect(const CameraInfo& info)
{
    return m_camera->connect(info);
}

ERROR_CODE SdkBackend::disconnect()
{
    return m_camera->disconnect();
}

ERROR_CODE SdkBackend::restart()
{
    return m_camera->restart();
}

ERROR_CODE SdkBackend::isStreamSupport(STREAM_TYPE type, bool& support)
{
    return m_camera->isStreamSupport(type, support);
}

ERROR_CODE SdkBackend::getStreamInfos(STREAM_TYPE type, std::vector<StreamInfo>& infos)
{
    return m_camera->getStreamInfos(type, infos);
}

ERROR_CODE SdkBackend::startStream(STREAM_TYPE type, const StreamInfo& info)
{
    // no callback: frames are pulled with getFrame/getPairedFrame, as 3DViewer does
    return m_camera->startStream(type, info);
}

ERROR_CODE SdkBackend::stopStream(STREAM_TYPE type)
{
    return m_camera->stopStream(type);
}

ERROR_CODE SdkBackend::softTrigger()
{
    return m_camera->softTrigger();
}

ERROR_CODE SdkBackend::getFrames(bool withRgb, int timeoutMs, Frame& depth, Frame& rgb)
{
    cs::IFramePtr depthFrame, rgbFrame;
    ERROR_CODE ret = withRgb
        ? m_camera->getPairedFrame(depthFrame, rgbFrame, timeoutMs)
        : m_camera->getFrame(STREAM_TYPE_DEPTH, depthFrame, timeoutMs);
    if (ret != SUCCESS)
    {
        return ret;
    }
    copyFrame(depthFrame, depth);
    copyFrame(rgbFrame, rgb);
    return SUCCESS;
}

ERROR_CODE SdkBackend::getPropertyRange(STREAM_TYPE type, PROPERTY_TYPE prop, float& min, float& max, float& step)
{
    return m_camera->getPropertyRange(type, prop, min, max, step);
}

ERROR_CODE SdkBackend::getProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float& value)
{
    return m_camera->getProperty(type, prop, value);
}

ERROR_CODE SdkBackend::setProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float value)
{
    return m_camera->setProperty(type, prop, value);
}

ERROR_CODE SdkBackend::getPropertyExtension(PROPERTY_TYPE_EXTENSION prop, PropertyExtension& value)
{
    return m_camera->getPropertyExtension(prop, value);
}

ERROR_CODE SdkBackend::setPropertyExtension(PROPERTY_TYPE_EXTENSION prop, const PropertyExtension& value)
{
    return m_camera->setPropertyExtension(prop, value);
}

ERROR_CODE SdkBackend::getIntrinsics(STREAM_TYPE type, Intrinsics& intrinsics)
{
    return m_camera->getIntrinsics(type, intrinsics);
}

ERROR_CODE SdkBackend::getExtrinsics(Extrinsics& extrinsics)
{
    return m_camera->getExtrinsics(extrinsics);
}

} // namespace csbridge
