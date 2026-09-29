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

#ifndef _CS_CAMERABACKEND_H
#define _CS_CAMERABACKEND_H

#include <functional>
#include <string>
#include <vector>

#include <QJsonObject>

#include <hpp/Types.hpp>

namespace csbridge
{

// one frame copied out of the SDK, so it outlives the SDK's frame object
struct Frame
{
    STREAM_FORMAT format = STREAM_FORMAT_COUNT;
    int width = 0;
    int height = 0;
    double timestamp = 0.0;
    std::vector<char> data;

    bool empty() const { return data.empty(); }
};

// Everything the bridge needs from a camera. SdkBackend implements it on the
// 3DCamera SDK; tests implement it with a synthetic camera. It uses the SDK's
// own plain types (Types.hpp) so a backend is a thin pass-through.
class CameraBackend
{
public:
    // an unsolicited event for the host: hot-plug, alarm
    typedef std::function<void(const QJsonObject&)> EventHandler;

    virtual ~CameraBackend() {}

    virtual std::string sdkVersion() = 0;
    virtual std::string errorString(ERROR_CODE code) = 0;
    virtual std::string cameraTypeName(const char* serial) = 0;
    virtual CAMERA_STATUS cameraStatus(const char* serial) = 0;
    virtual void setEventHandler(EventHandler handler) = 0;

    virtual ERROR_CODE queryCameras(std::vector<CameraInfo>& cameras) = 0;
    virtual ERROR_CODE connect(const CameraInfo& info) = 0;
    virtual ERROR_CODE disconnect() = 0;
    virtual ERROR_CODE restart() = 0;

    virtual ERROR_CODE isStreamSupport(STREAM_TYPE type, bool& support) = 0;
    virtual ERROR_CODE getStreamInfos(STREAM_TYPE type, std::vector<StreamInfo>& infos) = 0;
    virtual ERROR_CODE startStream(STREAM_TYPE type, const StreamInfo& info) = 0;
    virtual ERROR_CODE stopStream(STREAM_TYPE type) = 0;

    virtual ERROR_CODE softTrigger() = 0;
    // next depth frame, plus the paired RGB frame when withRgb is set
    virtual ERROR_CODE getFrames(bool withRgb, int timeoutMs, Frame& depth, Frame& rgb) = 0;

    virtual ERROR_CODE getPropertyRange(STREAM_TYPE type, PROPERTY_TYPE prop, float& min, float& max, float& step) = 0;
    virtual ERROR_CODE getProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float& value) = 0;
    virtual ERROR_CODE setProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float value) = 0;
    virtual ERROR_CODE getPropertyExtension(PROPERTY_TYPE_EXTENSION prop, PropertyExtension& value) = 0;
    virtual ERROR_CODE setPropertyExtension(PROPERTY_TYPE_EXTENSION prop, const PropertyExtension& value) = 0;

    virtual ERROR_CODE getIntrinsics(STREAM_TYPE type, Intrinsics& intrinsics) = 0;
    virtual ERROR_CODE getExtrinsics(Extrinsics& extrinsics) = 0;
};

} // namespace csbridge

#endif // _CS_CAMERABACKEND_H
