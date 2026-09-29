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

#ifndef _CS_PROPERTYMAP_H
#define _CS_PROPERTYMAP_H

#include <vector>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "camerabackend.h"

namespace csbridge
{

// The allow-list of camera properties the bridge exposes, by stable name
// ("depth.exposure", "trigger_mode", ...). Each maps to one SDK property
// (PROPERTY_TYPE) or extension property (PROPERTY_TYPE_EXTENSION) plus the
// JSON <-> SDK conversion for its value. Anything not listed here cannot be
// read or written through the bridge.

enum class ValueType
{
    Float,
    Int,
    Bool,
    Enum,       // string name (or its integer value)
    Range,      // { "min": int, "max": int }
    Roi,        // { "left", "top", "right", "bottom" }, percent 0-100
    LedCtrl     // { "led": "ir"|"rgb"|"laser", "mode": "steady"|"blink"|"enable"|"disable" }
};

struct EnumEntry
{
    const char* name;
    int value;
};

struct PropertyDef
{
    const char* name;
    bool extension;
    STREAM_TYPE stream;                 // basic properties only
    PROPERTY_TYPE basic;                // basic properties only
    PROPERTY_TYPE_EXTENSION ext;        // extension properties only
    ValueType type;
    bool readable;
    bool writable;
    // true: 3DViewer itself uses it, so it is known to work on its supported cameras;
    // false: only declared in the SDK headers, behavior per model unknown
    bool usedBy3DViewer;
    const char* units;
    const char* description;
    std::vector<EnumEntry> enums;
};

const std::vector<PropertyDef>& propertyTable();
const PropertyDef* findProperty(const QString& name);

// Result of a property operation: ok, a bad value from the caller, or an SDK failure.
struct PropertyStatus
{
    enum Kind { Ok, BadValue, SdkError, NotReadable, NotWritable } kind = Ok;
    ERROR_CODE sdkCode = SUCCESS;
    QString message;

    bool ok() const { return kind == Ok; }
};

PropertyStatus readProperty(CameraBackend& backend, const PropertyDef& def, QJsonValue& value);
PropertyStatus writeProperty(CameraBackend& backend, const PropertyDef& def, const QJsonValue& value);

// static description plus, when connected, the camera's reported range
QJsonObject describeProperty(CameraBackend& backend, const PropertyDef& def, bool queryRange);

// SDK error code <-> stable name ("FRAME_TIMEOUT", ...)
QString errorCodeName(ERROR_CODE code);

// stream format <-> name ("Z16", "Z16Y8Y8", "RGB8", "MJPG", ...)
QString streamFormatName(STREAM_FORMAT format);
bool streamFormatFromName(const QString& name, STREAM_FORMAT& format);

} // namespace csbridge

#endif // _CS_PROPERTYMAP_H
