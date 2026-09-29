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

#ifndef _CS_SDKBACKEND_H
#define _CS_SDKBACKEND_H

#include <mutex>

#include <hpp/Camera.hpp>
#include <hpp/System.hpp>

#include "camerabackend.h"

namespace csbridge
{

// CameraBackend on the Revopoint 3DCamera SDK's C++ API (cs::ISystem / cs::ICamera).
class SdkBackend : public CameraBackend
{
public:
    SdkBackend();
    ~SdkBackend() override;

    std::string sdkVersion() override;
    std::string errorString(ERROR_CODE code) override;
    std::string cameraTypeName(const char* serial) override;
    CAMERA_STATUS cameraStatus(const char* serial) override;
    void setEventHandler(EventHandler handler) override;

    ERROR_CODE queryCameras(std::vector<CameraInfo>& cameras) override;
    ERROR_CODE connect(const CameraInfo& info) override;
    ERROR_CODE disconnect() override;
    ERROR_CODE restart() override;

    ERROR_CODE isStreamSupport(STREAM_TYPE type, bool& support) override;
    ERROR_CODE getStreamInfos(STREAM_TYPE type, std::vector<StreamInfo>& infos) override;
    ERROR_CODE startStream(STREAM_TYPE type, const StreamInfo& info) override;
    ERROR_CODE stopStream(STREAM_TYPE type) override;

    ERROR_CODE softTrigger() override;
    ERROR_CODE getFrames(bool withRgb, int timeoutMs, Frame& depth, Frame& rgb) override;

    ERROR_CODE getPropertyRange(STREAM_TYPE type, PROPERTY_TYPE prop, float& min, float& max, float& step) override;
    ERROR_CODE getProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float& value) override;
    ERROR_CODE setProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float value) override;
    ERROR_CODE getPropertyExtension(PROPERTY_TYPE_EXTENSION prop, PropertyExtension& value) override;
    ERROR_CODE setPropertyExtension(PROPERTY_TYPE_EXTENSION prop, const PropertyExtension& value) override;

    ERROR_CODE getIntrinsics(STREAM_TYPE type, Intrinsics& intrinsics) override;
    ERROR_CODE getExtrinsics(Extrinsics& extrinsics) override;

private:
    static void onCameraChange(std::vector<CameraInfo>& added, std::vector<CameraInfo>& removed, void* user);
    static void onCameraAlarm(const char* jsonData, int length, void* user);
    void emitEvent(const QJsonObject& event);

    cs::ICameraPtr m_camera;
    std::mutex m_handlerMutex;
    EventHandler m_handler;
};

} // namespace csbridge

#endif // _CS_SDKBACKEND_H
