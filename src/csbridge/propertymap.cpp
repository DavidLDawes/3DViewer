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

#include "propertymap.h"

#include <cmath>
#include <cstring>

#include <QJsonArray>

namespace csbridge
{

static const std::vector<EnumEntry> AUTO_EXPOSURE_ENUMS =
{
    { "close",          AUTO_EXPOSURE_MODE_CLOSE },
    { "fix_frametime",  AUTO_EXPOSURE_MODE_FIX_FRAMETIME },
    { "high_quality",   AUTO_EXPOSURE_MODE_HIGH_QUALITY },
    { "foreground",     AUTO_EXPOSURE_MODE_FORE_GROUND },
};

static const std::vector<EnumEntry> HDR_ENUMS =
{
    { "off",            HDR_MODE_OFF },
    { "high_reflect",   HDR_MODE_HIGH_RELECT },
    { "low_reflect",    HDR_MODE_LOW_RELECT },
    { "all_reflect",    HDR_MODE_ALL_RELECT },
};

static const std::vector<EnumEntry> TRIGGER_ENUMS =
{
    { "off",            TRIGGER_MODE_OFF },
    { "hardware",       TRIGGER_MODE_HARDWAER },
    { "software",       TRIGGER_MODE_SOFTWAER },
};

static const std::vector<EnumEntry> FRINGE_ENUMS =
{
    { "standard",                           EN_FRINGE_PATTERN_TYPE_STANDARD },
    { "standard_white_added",               EN_FRINGE_PATTERN_TYPE_STANDARD_WHITE_ADDED },
    { "multi_depth",                        EN_FRINGE_PATTERN_TYPE_MULTI_DEPTH },
    { "multi_depth_white_added",            EN_FRINGE_PATTERN_TYPE_MULTI_DEPTH_WHITE_ADDED },
    { "multi_depth_double_white_added",     EN_FRINGE_PATTERN_TYPE_MULTI_DEPTH_DOUBLE_WHITE_ADDED },
    { "3freq4step",                         EN_FRINGE_PATTERN_TYPE_3FREQ4STEP },
    { "3freq4step_multi_depth",             EN_FRINGE_PATTERN_TYPE_3FREQ4STEP_MULTI_DEPTH },
};

static const std::vector<EnumEntry> LED_ID_ENUMS =
{
    { "ir",     IR_LED },
    { "rgb",    RGB_LED },
    { "laser",  LASER_LED },
};

static const std::vector<EnumEntry> LED_MODE_ENUMS =
{
    { "steady",     OFTEN_BRIGHT_LED },
    { "blink",      TWINKLE_LED },
    { "enable",     ENABLE_LED },
    { "disable",    DISABLE_LED },
};

static PropertyDef basic(const char* name, STREAM_TYPE stream, PROPERTY_TYPE prop, ValueType type,
    const char* units, const char* description)
{
    return PropertyDef{ name, false, stream, prop, PROPERTY_EXT_DEPTH_SCALE, type,
        true, true, true, units, description, {} };
}

static PropertyDef ext(const char* name, PROPERTY_TYPE_EXTENSION prop, ValueType type, bool readable, bool writable,
    bool usedBy3DViewer, const char* units, const char* description, std::vector<EnumEntry> enums = {})
{
    return PropertyDef{ name, true, STREAM_TYPE_DEPTH, PROPERTY_GAIN, prop, type,
        readable, writable, usedBy3DViewer, units, description, enums };
}

const std::vector<PropertyDef>& propertyTable()
{
    static const std::vector<PropertyDef> table =
    {
        basic("depth.gain", STREAM_TYPE_DEPTH, PROPERTY_GAIN, ValueType::Float, "",
            "Depth camera gain. Higher is brighter but noisier; the SDK suggests <= 3 for accuracy"),
        basic("depth.exposure", STREAM_TYPE_DEPTH, PROPERTY_EXPOSURE, ValueType::Float, "us",
            "Depth camera exposure time; longer lowers the frame rate"),
        basic("depth.frame_time", STREAM_TYPE_DEPTH, PROPERTY_FRAMETIME, ValueType::Float, "",
            "Depth camera frame time"),
        basic("rgb.gain", STREAM_TYPE_RGB, PROPERTY_GAIN, ValueType::Float, "",
            "RGB camera gain"),
        basic("rgb.auto_exposure", STREAM_TYPE_RGB, PROPERTY_ENABLE_AUTO_EXPOSURE, ValueType::Bool, "",
            "RGB camera auto exposure"),
        basic("rgb.auto_white_balance", STREAM_TYPE_RGB, PROPERTY_ENABLE_AUTO_WHITEBALANCE, ValueType::Bool, "",
            "RGB camera auto white balance"),
        basic("rgb.white_balance", STREAM_TYPE_RGB, PROPERTY_WHITEBALANCE, ValueType::Float, "",
            "RGB camera white balance (when auto white balance is off)"),

        ext("depth.scale", PROPERTY_EXT_DEPTH_SCALE, ValueType::Float, true, false, true, "mm/unit",
            "Multiply a raw 16-bit depth value by this to get millimetres"),
        ext("depth.range", PROPERTY_EXT_DEPTH_RANGE, ValueType::Range, true, true, true, "mm",
            "Depth range kept in the output; 3DViewer's default is 50-2000"),
        ext("depth.roi", PROPERTY_EXT_DEPTH_ROI, ValueType::Roi, true, true, true, "percent",
            "Region of interest of the depth image, each edge 0-100 percent of width/height"),
        ext("depth.auto_exposure_mode", PROPERTY_EXT_AUTO_EXPOSURE_MODE, ValueType::Enum, true, true, true, "",
            "Depth camera auto exposure mode", AUTO_EXPOSURE_ENUMS),
        ext("depth.hdr_mode", PROPERTY_EXT_HDR_MODE, ValueType::Enum, true, true, true, "",
            "Depth HDR mode: extra exposures fused for shiny (high_reflect), dark (low_reflect) or mixed objects. "
            "3DViewer also adjusts HDR exposure settings when changing this; the bridge sets only the mode",
            HDR_ENUMS),
        ext("depth.contrast_min", PROPERTY_EXT_CONTRAST_MIN, ValueType::Int, true, true, true, "",
            "Depth threshold (3DViewer's 'threshold'): minimum contrast for a valid depth point"),
        ext("trigger_mode", PROPERTY_EXT_TRIGGER_MODE, ValueType::Enum, true, true, true, "",
            "off: stream continuously; software: a frame per capture (soft trigger); hardware: external trigger input",
            TRIGGER_ENUMS),
        ext("rgb.exposure_time", PROPERTY_EXT_EXPOSURE_TIME_RGB, ValueType::Int, true, true, true, "us",
            "RGB camera exposure time"),
        ext("cpu_temperature", PROPERTY_EXT_CPU_TEMPRATRUE, ValueType::Int, true, false, false, "degC",
            "Camera CPU temperature"),
        ext("gyro_supported", PROPERTY_EXT_IS_SUPPORT_GYRO, ValueType::Bool, true, false, false, "",
            "Whether the camera has a gyroscope/IMU"),
        ext("fast_scan_mode", PROPERTY_EXT_FAST_SCAN_MODE, ValueType::Bool, true, true, false, "",
            "Fast scan mode"),
        ext("multiframe_fusion", PROPERTY_EXT_MULTIFRAME_FUSION, ValueType::Bool, true, true, false, "",
            "Multi-frame fusion (double exposure)"),
        ext("fringe_pattern", PROPERTY_EXT_SET_FRINGE_PATTERN, ValueType::Enum, true, true, false, "",
            "Projected fringe pattern. An SDK header note says POP 2 uses standard_white_added (1) for marker "
            "mode and multi_depth_white_added (3) otherwise",
            FRINGE_ENUMS),
        ext("led", PROPERTY_EXT_LED_ON_OFF, ValueType::Int, true, true, false, "",
            "LED on/off"),
        ext("led_ctrl", PROPERTY_EXT_LED_CTRL, ValueType::LedCtrl, false, true, false, "",
            "Control one LED: led ir|rgb|laser, mode steady|blink|enable|disable"),
        ext("laser.on", PROPERTY_EXT_LASER_ON_OFF, ValueType::Int, true, true, false, "",
            "Laser/projector on (1) or off (0)"),
        ext("laser.brightness", PROPERTY_EXT_LASER_BRIGHTNESS, ValueType::Int, true, true, false, "",
            "Laser/projector brightness level"),
    };
    return table;
}

const PropertyDef* findProperty(const QString& name)
{
    for (const auto& def : propertyTable())
    {
        if (name == QLatin1String(def.name))
        {
            return &def;
        }
    }
    return nullptr;
}

static PropertyStatus makeStatus(PropertyStatus::Kind kind, const QString& message, ERROR_CODE code = SUCCESS)
{
    PropertyStatus status;
    status.kind = kind;
    status.message = message;
    status.sdkCode = code;
    return status;
}

static PropertyStatus sdkFailure(CameraBackend& backend, ERROR_CODE code)
{
    return makeStatus(PropertyStatus::SdkError, QString::fromStdString(backend.errorString(code)), code);
}

static QJsonValue enumToJson(const std::vector<EnumEntry>& enums, int value)
{
    for (const auto& e : enums)
    {
        if (e.value == value)
        {
            return QString(e.name);
        }
    }
    // a value the table doesn't know: pass it through rather than hide it
    return value;
}

static bool enumFromJson(const std::vector<EnumEntry>& enums, const QJsonValue& json, int& value)
{
    if (json.isString())
    {
        for (const auto& e : enums)
        {
            if (json.toString() == QLatin1String(e.name))
            {
                value = e.value;
                return true;
            }
        }
        return false;
    }

    if (json.isDouble())
    {
        const double d = json.toDouble();
        for (const auto& e : enums)
        {
            if (d == e.value)
            {
                value = e.value;
                return true;
            }
        }
    }
    return false;
}

static QString enumNames(const std::vector<EnumEntry>& enums)
{
    QStringList names;
    for (const auto& e : enums)
    {
        names << e.name;
    }
    return names.join(", ");
}

static bool intFromJson(const QJsonValue& json, int& value)
{
    if (!json.isDouble())
    {
        return false;
    }
    const double d = json.toDouble();
    if (d != std::floor(d) || d < -2147483648.0 || d > 2147483647.0)
    {
        return false;
    }
    value = (int)d;
    return true;
}

static bool boolFromJson(const QJsonValue& json, bool& value)
{
    if (json.isBool())
    {
        value = json.toBool();
        return true;
    }
    if (json.isDouble() && (json.toDouble() == 0.0 || json.toDouble() == 1.0))
    {
        value = json.toDouble() != 0.0;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- extension <-> JSON

static QJsonValue extensionToJson(const PropertyDef& def, const PropertyExtension& p)
{
    switch (def.ext)
    {
    case PROPERTY_EXT_DEPTH_SCALE:          return p.depthScale;
    case PROPERTY_EXT_DEPTH_RANGE:
        return QJsonObject{ { "min", p.depthRange.min }, { "max", p.depthRange.max } };
    case PROPERTY_EXT_DEPTH_ROI:
        return QJsonObject{ { "left", p.depthRoi.left }, { "top", p.depthRoi.top },
                            { "right", p.depthRoi.right }, { "bottom", p.depthRoi.bottom } };
    case PROPERTY_EXT_AUTO_EXPOSURE_MODE:   return enumToJson(def.enums, p.autoExposureMode);
    case PROPERTY_EXT_HDR_MODE:             return enumToJson(def.enums, p.hdrMode);
    case PROPERTY_EXT_CONTRAST_MIN:         return p.algorithmContrast;
    case PROPERTY_EXT_TRIGGER_MODE:         return enumToJson(def.enums, p.triggerMode);
    case PROPERTY_EXT_EXPOSURE_TIME_RGB:    return (double)p.uiExposureTime;
    case PROPERTY_EXT_CPU_TEMPRATRUE:       return (double)p.uiTempratrue_;
    case PROPERTY_EXT_IS_SUPPORT_GYRO:      return p.bSupportGyro;
    case PROPERTY_EXT_FAST_SCAN_MODE:       return p.bFastScanMode;
    case PROPERTY_EXT_MULTIFRAME_FUSION:    return p.multiframeFusion;
    case PROPERTY_EXT_SET_FRINGE_PATTERN:   return enumToJson(def.enums, p.fringePatternType);
    case PROPERTY_EXT_LED_ON_OFF:           return p.ledOnOff;
    case PROPERTY_EXT_LASER_ON_OFF:         return p.laserOnOff;
    case PROPERTY_EXT_LASER_BRIGHTNESS:     return p.laserBrightness;
    default:                                return QJsonValue();
    }
}

static QString extensionFromJson(const PropertyDef& def, const QJsonValue& json, PropertyExtension& p)
{
    int i = 0;
    bool b = false;

    switch (def.ext)
    {
    case PROPERTY_EXT_DEPTH_RANGE:
    {
        const QJsonObject o = json.toObject();
        int min = 0, max = 0;
        if (!json.isObject() || !intFromJson(o.value("min"), min) || !intFromJson(o.value("max"), max))
        {
            return "expected {\"min\": int, \"max\": int}";
        }
        if (min < 0 || max <= min)
        {
            return "expected 0 <= min < max";
        }
        p.depthRange.min = min;
        p.depthRange.max = max;
        return QString();
    }
    case PROPERTY_EXT_DEPTH_ROI:
    {
        const QJsonObject o = json.toObject();
        int l = 0, t = 0, r = 0, btm = 0;
        if (!json.isObject() || !intFromJson(o.value("left"), l) || !intFromJson(o.value("top"), t)
            || !intFromJson(o.value("right"), r) || !intFromJson(o.value("bottom"), btm))
        {
            return "expected {\"left\", \"top\", \"right\", \"bottom\"} as integers";
        }
        if (l < 0 || t < 0 || r > 100 || btm > 100 || l >= r || t >= btm)
        {
            return "expected 0 <= left < right <= 100 and 0 <= top < bottom <= 100";
        }
        p.depthRoi.left = l;
        p.depthRoi.top = t;
        p.depthRoi.right = r;
        p.depthRoi.bottom = btm;
        return QString();
    }
    case PROPERTY_EXT_AUTO_EXPOSURE_MODE:
    case PROPERTY_EXT_HDR_MODE:
    case PROPERTY_EXT_TRIGGER_MODE:
    case PROPERTY_EXT_SET_FRINGE_PATTERN:
        if (!enumFromJson(def.enums, json, i))
        {
            return QString("expected one of: %1").arg(enumNames(def.enums));
        }
        if (def.ext == PROPERTY_EXT_AUTO_EXPOSURE_MODE)  p.autoExposureMode = (AUTO_EXPOSURE_MODE)i;
        if (def.ext == PROPERTY_EXT_HDR_MODE)           p.hdrMode = (HDR_MODE)i;
        if (def.ext == PROPERTY_EXT_TRIGGER_MODE)       p.triggerMode = (TRIGGER_MODE)i;
        if (def.ext == PROPERTY_EXT_SET_FRINGE_PATTERN) p.fringePatternType = (FRINGE_PATTERN_TYPE)i;
        return QString();
    case PROPERTY_EXT_CONTRAST_MIN:
    case PROPERTY_EXT_EXPOSURE_TIME_RGB:
    case PROPERTY_EXT_LED_ON_OFF:
    case PROPERTY_EXT_LASER_ON_OFF:
    case PROPERTY_EXT_LASER_BRIGHTNESS:
        if (!intFromJson(json, i) || i < 0)
        {
            return "expected a non-negative integer";
        }
        if (def.ext == PROPERTY_EXT_CONTRAST_MIN)       p.algorithmContrast = i;
        if (def.ext == PROPERTY_EXT_EXPOSURE_TIME_RGB)  p.uiExposureTime = (unsigned int)i;
        if (def.ext == PROPERTY_EXT_LED_ON_OFF)         p.ledOnOff = i;
        if (def.ext == PROPERTY_EXT_LASER_ON_OFF)       p.laserOnOff = i;
        if (def.ext == PROPERTY_EXT_LASER_BRIGHTNESS)   p.laserBrightness = i;
        return QString();
    case PROPERTY_EXT_FAST_SCAN_MODE:
    case PROPERTY_EXT_MULTIFRAME_FUSION:
        if (!boolFromJson(json, b))
        {
            return "expected true or false";
        }
        if (def.ext == PROPERTY_EXT_FAST_SCAN_MODE)     p.bFastScanMode = b;
        if (def.ext == PROPERTY_EXT_MULTIFRAME_FUSION)  p.multiframeFusion = b;
        return QString();
    case PROPERTY_EXT_LED_CTRL:
    {
        const QJsonObject o = json.toObject();
        int led = 0, mode = 0;
        if (!json.isObject() || !enumFromJson(LED_ID_ENUMS, o.value("led"), led)
            || !enumFromJson(LED_MODE_ENUMS, o.value("mode"), mode))
        {
            return QString("expected {\"led\": %1, \"mode\": %2}")
                .arg(enumNames(LED_ID_ENUMS)).arg(enumNames(LED_MODE_ENUMS));
        }
        p.ledCtrlParam.emLedId = (LED_ID)led;
        p.ledCtrlParam.emCtrlType = (LED_CTRL_TYPE)mode;
        return QString();
    }
    default:
        return "not writable";
    }
}

// ---------------------------------------------------------------------- read / write

PropertyStatus readProperty(CameraBackend& backend, const PropertyDef& def, QJsonValue& value)
{
    if (!def.readable)
    {
        return makeStatus(PropertyStatus::NotReadable, QString("%1 is write-only").arg(def.name));
    }

    if (!def.extension)
    {
        float f = 0.0f;
        ERROR_CODE ret = backend.getProperty(def.stream, def.basic, f);
        if (ret != SUCCESS)
        {
            return sdkFailure(backend, ret);
        }
        value = (def.type == ValueType::Bool) ? QJsonValue(f != 0.0f) : QJsonValue((double)f);
        return PropertyStatus();
    }

    PropertyExtension p;
    std::memset(&p, 0, sizeof(p));
    ERROR_CODE ret = backend.getPropertyExtension(def.ext, p);
    if (ret != SUCCESS)
    {
        return sdkFailure(backend, ret);
    }
    value = extensionToJson(def, p);
    return PropertyStatus();
}

PropertyStatus writeProperty(CameraBackend& backend, const PropertyDef& def, const QJsonValue& value)
{
    if (!def.writable)
    {
        return makeStatus(PropertyStatus::NotWritable, QString("%1 is read-only").arg(def.name));
    }

    if (!def.extension)
    {
        float f = 0.0f;
        if (def.type == ValueType::Bool)
        {
            bool b = false;
            if (!boolFromJson(value, b))
            {
                return makeStatus(PropertyStatus::BadValue, "expected true or false");
            }
            f = b ? 1.0f : 0.0f;
        }
        else
        {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
            {
                return makeStatus(PropertyStatus::BadValue, "expected a number");
            }
            f = (float)value.toDouble();

            // defense in depth: refuse values outside the range the camera reports
            float min = 0.0f, max = 0.0f, step = 0.0f;
            if (backend.getPropertyRange(def.stream, def.basic, min, max, step) == SUCCESS
                && max > min && (f < min || f > max))
            {
                return makeStatus(PropertyStatus::BadValue,
                    QString("%1 is outside the camera's range [%2, %3]").arg(f).arg(min).arg(max));
            }
        }

        ERROR_CODE ret = backend.setProperty(def.stream, def.basic, f);
        return (ret == SUCCESS) ? PropertyStatus() : sdkFailure(backend, ret);
    }

    PropertyExtension p;
    std::memset(&p, 0, sizeof(p));
    const QString error = extensionFromJson(def, value, p);
    if (!error.isEmpty())
    {
        return makeStatus(PropertyStatus::BadValue, error);
    }

    ERROR_CODE ret = backend.setPropertyExtension(def.ext, p);
    return (ret == SUCCESS) ? PropertyStatus() : sdkFailure(backend, ret);
}

static QString valueTypeName(ValueType type)
{
    switch (type)
    {
    case ValueType::Float:      return "float";
    case ValueType::Int:        return "int";
    case ValueType::Bool:       return "bool";
    case ValueType::Enum:       return "enum";
    case ValueType::Range:      return "range";
    case ValueType::Roi:        return "roi";
    case ValueType::LedCtrl:    return "led_ctrl";
    }
    return "unknown";
}

QJsonObject describeProperty(CameraBackend& backend, const PropertyDef& def, bool queryRange)
{
    QJsonObject o;
    o["name"] = def.name;
    o["type"] = valueTypeName(def.type);
    o["readable"] = def.readable;
    o["writable"] = def.writable;
    o["used_by_3dviewer"] = def.usedBy3DViewer;
    o["description"] = def.description;
    if (def.units[0])
    {
        o["units"] = def.units;
    }

    const std::vector<EnumEntry>& enums = (def.type == ValueType::LedCtrl) ? LED_MODE_ENUMS : def.enums;
    if (!enums.empty())
    {
        QJsonArray names;
        for (const auto& e : enums)
        {
            names.append(QString(e.name));
        }
        o[def.type == ValueType::LedCtrl ? "modes" : "values"] = names;
    }
    if (def.type == ValueType::LedCtrl)
    {
        QJsonArray leds;
        for (const auto& e : LED_ID_ENUMS)
        {
            leds.append(QString(e.name));
        }
        o["leds"] = leds;
    }

    if (!queryRange)
    {
        return o;
    }

    if (!def.extension && def.type == ValueType::Float)
    {
        float min = 0.0f, max = 0.0f, step = 0.0f;
        ERROR_CODE ret = backend.getPropertyRange(def.stream, def.basic, min, max, step);
        if (ret == SUCCESS)
        {
            o["range"] = QJsonObject{ { "min", min }, { "max", max }, { "step", step } };
        }
        else
        {
            o["range_error"] = errorCodeName(ret);
        }
    }
    else if (def.extension && def.ext == PROPERTY_EXT_EXPOSURE_TIME_RGB)
    {
        PropertyExtension p;
        std::memset(&p, 0, sizeof(p));
        ERROR_CODE ret = backend.getPropertyExtension(PROPERTY_EXT_EXPOSURE_TIME_RANGE_RGB, p);
        if (ret == SUCCESS)
        {
            o["range"] = QJsonObject{ { "min", p.objVRange_.fMin_ }, { "max", p.objVRange_.fMax_ },
                                      { "step", p.objVRange_.fStep_ } };
        }
        else
        {
            o["range_error"] = errorCodeName(ret);
        }
    }

    return o;
}

// ------------------------------------------------------------------------ names

QString errorCodeName(ERROR_CODE code)
{
    switch (code)
    {
    case SUCCESS:                       return "SUCCESS";
    case ERROR_PARAM:                   return "PARAM";
    case ERROR_DEVICE_NOT_FOUND:        return "DEVICE_NOT_FOUND";
    case ERROR_DEVICE_NOT_CONNECT:      return "DEVICE_NOT_CONNECT";
    case ERROR_DEVICE_BUSY:             return "DEVICE_BUSY";
    case ERROR_STREAM_NOT_START:        return "STREAM_NOT_START";
    case ERROR_STREAM_BUSY:             return "STREAM_BUSY";
    case ERROR_FRAME_TIMEOUT:           return "FRAME_TIMEOUT";
    case ERROR_NOT_SUPPORT:             return "NOT_SUPPORT";
    case ERROR_PROPERTY_GET_FAILED:     return "PROPERTY_GET_FAILED";
    case ERROR_PROPERTY_SET_FAILED:     return "PROPERTY_SET_FAILED";
    case ERROR_HID_CHANNEL_ERROR:       return "HID_CHANNEL_ERROR";
    case ERROR_HID_WRITE_ERROR:         return "HID_WRITE_ERROR";
    case ERROR_HID_READ_ERROR:          return "HID_READ_ERROR";
    default:                            return "UNKNOWN";
    }
}

struct FormatName
{
    STREAM_FORMAT format;
    const char* name;
};

static const FormatName FORMAT_NAMES[] =
{
    { STREAM_FORMAT_MJPG,       "MJPG" },
    { STREAM_FORMAT_RGB8,       "RGB8" },
    { STREAM_FORMAT_Z16,        "Z16" },
    { STREAM_FORMAT_Z16Y8Y8,    "Z16Y8Y8" },
    { STREAM_FORMAT_PAIR,       "PAIR" },
    { STREAM_FORMAT_H264,       "H264" },
    { STREAM_FORMAT_I8DS,       "I8DS" },
    { STREAM_FORMAT_XZ32,       "XZ32" },
    { STREAM_FORMAT_GRAY,       "GRAY" },
};

QString streamFormatName(STREAM_FORMAT format)
{
    for (const auto& f : FORMAT_NAMES)
    {
        if (f.format == format)
        {
            return f.name;
        }
    }
    return QString("0x%1").arg((int)format, 0, 16);
}

bool streamFormatFromName(const QString& name, STREAM_FORMAT& format)
{
    for (const auto& f : FORMAT_NAMES)
    {
        if (name.compare(QLatin1String(f.name), Qt::CaseInsensitive) == 0)
        {
            format = f.format;
            return true;
        }
    }
    return false;
}

} // namespace csbridge
