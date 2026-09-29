/*******************************************************************************
* This file is part of the 3DViewer                                            *
*                                                                              *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU General Public License as published by         *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
********************************************************************************/

// revo-bridge's protocol, property table and capture writer, driven through a
// fake camera backend. Proves the bridge's own logic; says nothing about how a
// real camera behaves.

#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstring>
#include <map>

#include "bridgeserver.h"
#include "imageutil.h"
#include "propertymap.h"

using namespace csbridge;

// ------------------------------------------------------------------ fake camera

class FakeBackend : public CameraBackend
{
public:
    std::vector<CameraInfo> cameras;
    CAMERA_STATUS status = CS_IDLE;
    bool connected = false;
    int disconnects = 0;
    int softTriggers = 0;
    int bufferClears = 0;
    bool hasRgb = true;
    std::map<std::pair<int, int>, float> props;
    // PropertyExtension is a union: each property gets its own copy
    std::map<int, PropertyExtension> ext;
    STREAM_FORMAT depthFormat = STREAM_FORMAT_COUNT;
    bool rgbStreaming = false;
    EventHandler handler;

    static const int W = 16;
    static const int H = 10;
    static const int RGB_W = 8;
    static const int RGB_H = 5;

    FakeBackend()
    {
        cameras.push_back(makeCamera("FAKE0001"));
        ext[PROPERTY_EXT_DEPTH_SCALE].depthScale = 0.1f;
        ext[PROPERTY_EXT_TRIGGER_MODE].triggerMode = TRIGGER_MODE_OFF;
        ext[PROPERTY_EXT_DEPTH_RANGE].depthRange.min = 50;
        ext[PROPERTY_EXT_DEPTH_RANGE].depthRange.max = 2000;
        ext[PROPERTY_EXT_CPU_TEMPRATRUE].uiTempratrue_ = 41;
        props[{ STREAM_TYPE_DEPTH, PROPERTY_GAIN }] = 1.0f;
        props[{ STREAM_TYPE_DEPTH, PROPERTY_EXPOSURE }] = 7000.0f;
    }

    static CameraInfo makeCamera(const char* serial)
    {
        CameraInfo info;
        std::memset(&info, 0, sizeof(info));
        std::strncpy(info.name, "3DCamera", sizeof(info.name) - 1);
        std::strncpy(info.serial, serial, sizeof(info.serial) - 1);
        std::strncpy(info.uniqueId, "usb-1", sizeof(info.uniqueId) - 1);
        std::strncpy(info.firmwareVersion, "1.2.3", sizeof(info.firmwareVersion) - 1);
        return info;
    }

    std::string sdkVersion() override { return "FAKE"; }
    std::string errorString(ERROR_CODE code) override { return "fake error " + std::to_string((int)code); }
    std::string cameraTypeName(const char*) override { return "POP2"; }
    CAMERA_STATUS cameraStatus(const char*) override { return connected ? CS_CONNECTED_BY_SDK : status; }
    void setEventHandler(EventHandler h) override { handler = h; }

    ERROR_CODE queryCameras(std::vector<CameraInfo>& out) override { out = cameras; return SUCCESS; }
    ERROR_CODE connect(const CameraInfo&) override { connected = true; return SUCCESS; }
    ERROR_CODE disconnect() override { connected = false; disconnects++; return SUCCESS; }
    ERROR_CODE restart() override { connected = false; return SUCCESS; }

    ERROR_CODE isStreamSupport(STREAM_TYPE type, bool& support) override
    {
        support = (type == STREAM_TYPE_DEPTH) || hasRgb;
        return SUCCESS;
    }

    ERROR_CODE getStreamInfos(STREAM_TYPE type, std::vector<StreamInfo>& infos) override
    {
        infos.clear();
        if (type == STREAM_TYPE_DEPTH)
        {
            infos.push_back({ STREAM_FORMAT_Z16, W, H, 10.0f });
            infos.push_back({ STREAM_FORMAT_Z16Y8Y8, W, H, 10.0f });
            infos.push_back({ STREAM_FORMAT_PAIR, W, H, 10.0f });
        }
        else if (hasRgb)
        {
            infos.push_back({ STREAM_FORMAT_RGB8, RGB_W, RGB_H, 10.0f });
        }
        return SUCCESS;
    }

    ERROR_CODE startStream(STREAM_TYPE type, const StreamInfo& info) override
    {
        if (type == STREAM_TYPE_DEPTH)
            depthFormat = info.format;
        else
            rgbStreaming = true;
        return SUCCESS;
    }

    ERROR_CODE stopStream(STREAM_TYPE type) override
    {
        if (type == STREAM_TYPE_DEPTH)
            depthFormat = STREAM_FORMAT_COUNT;
        else
            rgbStreaming = false;
        return SUCCESS;
    }

    ERROR_CODE softTrigger() override { softTriggers++; return SUCCESS; }

    // a flat plane 5000 raw units (500 mm) away, with pixel 0 invalid
    ERROR_CODE getFrames(bool withRgb, int, Frame& depth, Frame& rgb) override
    {
        depth = Frame();
        depth.format = depthFormat;
        depth.width = W;
        depth.height = H;
        depth.timestamp = 123.0;

        const int pixels = W * H;
        if (depthFormat == STREAM_FORMAT_Z16 || depthFormat == STREAM_FORMAT_Z16Y8Y8)
        {
            std::vector<unsigned short> z(pixels, 5000);
            z[0] = 0;
            const char* p = (const char*)z.data();
            depth.data.assign(p, p + pixels * 2);
        }
        if (depthFormat == STREAM_FORMAT_Z16Y8Y8 || depthFormat == STREAM_FORMAT_PAIR)
        {
            for (int i = 0; i < pixels; i++) depth.data.push_back((char)(i % 256));        // left
            for (int i = 0; i < pixels; i++) depth.data.push_back((char)(255 - i % 256));  // right
        }

        rgb = Frame();
        if (withRgb)
        {
            rgb.format = STREAM_FORMAT_RGB8;
            rgb.width = RGB_W;
            rgb.height = RGB_H;
            for (int i = 0; i < RGB_W * RGB_H; i++)
            {
                rgb.data.push_back((char)200);
                rgb.data.push_back((char)100);
                rgb.data.push_back((char)50);
            }
        }
        return SUCCESS;
    }

    ERROR_CODE getPropertyRange(STREAM_TYPE, PROPERTY_TYPE prop, float& min, float& max, float& step) override
    {
        if (prop == PROPERTY_GAIN) { min = 1; max = 16; step = 1; return SUCCESS; }
        if (prop == PROPERTY_EXPOSURE) { min = 3000; max = 60000; step = 1; return SUCCESS; }
        return ERROR_NOT_SUPPORT;
    }

    ERROR_CODE getProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float& value) override
    {
        auto it = props.find({ type, prop });
        if (it == props.end()) return ERROR_PROPERTY_GET_FAILED;
        value = it->second;
        return SUCCESS;
    }

    ERROR_CODE setProperty(STREAM_TYPE type, PROPERTY_TYPE prop, float value) override
    {
        props[{ type, prop }] = value;
        return SUCCESS;
    }

    ERROR_CODE getPropertyExtension(PROPERTY_TYPE_EXTENSION prop, PropertyExtension& value) override
    {
        auto it = ext.find(prop);
        if (it == ext.end() || prop == PROPERTY_EXT_LED_CTRL) return ERROR_NOT_SUPPORT;
        value = it->second;
        return SUCCESS;
    }

    ERROR_CODE setPropertyExtension(PROPERTY_TYPE_EXTENSION prop, const PropertyExtension& value) override
    {
        switch (prop)
        {
        case PROPERTY_EXT_TRIGGER_MODE:
        case PROPERTY_EXT_DEPTH_RANGE:
        case PROPERTY_EXT_LED_CTRL:
            ext[prop] = value;
            return SUCCESS;
        case PROPERTY_EXT_CLEAR_FRAME_BUFFER:
            bufferClears++;
            return SUCCESS;
        case PROPERTY_EXT_DEPTH_RGB_MATCH_PARAM:
            return SUCCESS;
        default:
            return ERROR_NOT_SUPPORT;
        }
    }

    ERROR_CODE getIntrinsics(STREAM_TYPE type, Intrinsics& intr) override
    {
        std::memset(&intr, 0, sizeof(intr));
        intr.width = (type == STREAM_TYPE_DEPTH) ? W : RGB_W;
        intr.height = (type == STREAM_TYPE_DEPTH) ? H : RGB_H;
        // the RGB view covers the depth view (half the resolution, same field of view)
        intr.fx = intr.fy = (type == STREAM_TYPE_DEPTH) ? 10.0f : 5.0f;
        intr.cx = intr.width / 2.0f;
        intr.cy = intr.height / 2.0f;
        intr.one22 = 1.0f;
        return SUCCESS;
    }

    ERROR_CODE getExtrinsics(Extrinsics& extr) override
    {
        std::memset(&extr, 0, sizeof(extr));
        extr.rotation[0] = extr.rotation[4] = extr.rotation[8] = 1.0f;
        return SUCCESS;
    }
};

// --------------------------------------------------------------------- the tests

class TestBridge : public QObject
{
    Q_OBJECT

private:
    FakeBackend* m_backend = nullptr;
    BridgeServer* m_server = nullptr;
    QList<QJsonObject> m_events;

    QJsonObject call(const QString& cmd, const QJsonObject& args = QJsonObject(), int id = 7)
    {
        const QJsonObject request{ { "id", id }, { "cmd", cmd }, { "args", args } };
        const QByteArray reply = m_server->handleLine(QJsonDocument(request).toJson(QJsonDocument::Compact));
        const QJsonObject o = QJsonDocument::fromJson(reply).object();
        [&]() { QCOMPARE(o.value("id").toInt(), id); }();
        return o;
    }

    static QString errorCode(const QJsonObject& reply)
    {
        return reply.value("error").toObject().value("code").toString();
    }

    void connectAndStream(const QJsonObject& streamArgs = QJsonObject())
    {
        QVERIFY(call("connect").value("ok").toBool());
        const QJsonObject r = call("start_stream", streamArgs);
        QVERIFY2(r.value("ok").toBool(), QJsonDocument(r).toJson());
    }

private slots:
    void init()
    {
        m_events.clear();
        m_backend = new FakeBackend();
        m_server = new BridgeServer(*m_backend, [this](const QByteArray& line)
        {
            m_events << QJsonDocument::fromJson(line).object();
        });
    }

    void cleanup()
    {
        delete m_server;
        delete m_backend;
    }

    void helloAndReady()
    {
        const QJsonObject ready = m_server->readyEvent();
        QCOMPARE(ready.value("event").toString(), QString("ready"));
        QCOMPARE(ready.value("protocol").toInt(), BridgeServer::PROTOCOL_VERSION);

        const QJsonObject r = call("hello");
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("result").toObject().value("sdk_version").toString(), QString("FAKE"));
    }

    void malformedRequests()
    {
        QJsonObject r = QJsonDocument::fromJson(m_server->handleLine("not json")).object();
        QCOMPARE(errorCode(r), QString("BAD_REQUEST"));
        QVERIFY(r.value("id").isNull());

        r = QJsonDocument::fromJson(m_server->handleLine(R"({"id":"a","cmd":5})")).object();
        QCOMPARE(errorCode(r), QString("BAD_REQUEST"));
        QCOMPARE(r.value("id").toString(), QString("a"));

        r = QJsonDocument::fromJson(m_server->handleLine(R"({"id":1,"cmd":"get","args":[1]})")).object();
        QCOMPARE(errorCode(r), QString("BAD_REQUEST"));

        QCOMPARE(errorCode(call("fly")), QString("UNKNOWN_COMMAND"));
    }

    void listAndConnect()
    {
        QJsonObject r = call("list");
        const QJsonArray cameras = r.value("result").toObject().value("cameras").toArray();
        QCOMPARE(cameras.size(), 1);
        QCOMPARE(cameras[0].toObject().value("serial").toString(), QString("FAKE0001"));
        QCOMPARE(cameras[0].toObject().value("connection").toString(), QString("usb"));
        QCOMPARE(cameras[0].toObject().value("model").toString(), QString("POP2"));

        QCOMPARE(errorCode(call("info")), QString("NOT_CONNECTED"));

        r = call("connect");
        QVERIFY(r.value("ok").toBool());
        QVERIFY(r.value("result").toObject().value("rgb_supported").toBool());
        QVERIFY(m_backend->connected);

        // idempotent for the same camera
        r = call("connect", QJsonObject{ { "serial", "FAKE0001" } });
        QVERIFY(r.value("result").toObject().value("already_connected").toBool());

        r = call("info");
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("result").toObject().value("depth_intrinsics").toObject().value("fx").toDouble(), 10.0);
    }

    void connectChoosesOrRefuses()
    {
        m_backend->cameras.push_back(FakeBackend::makeCamera("FAKE0002"));
        QCOMPARE(errorCode(call("connect")), QString("AMBIGUOUS_CAMERA"));
        QCOMPARE(errorCode(call("connect", QJsonObject{ { "serial", "NOPE" } })), QString("CAMERA_NOT_FOUND"));

        m_backend->status = CS_CONNECTED_BY_OTHER;
        QCOMPARE(errorCode(call("connect", QJsonObject{ { "serial", "FAKE0002" } })), QString("CAMERA_IN_USE"));
        QVERIFY(!m_backend->connected);
    }

    void getAndSetProperties()
    {
        QVERIFY(call("connect").value("ok").toBool());

        QJsonObject r = call("set", QJsonObject{ { "name", "depth.gain" }, { "value", 2 } });
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("result").toObject().value("value").toDouble(), 2.0);

        // outside the camera's reported range: refused before reaching the camera
        r = call("set", QJsonObject{ { "name", "depth.gain" }, { "value", 50 } });
        QCOMPARE(errorCode(r), QString("BAD_VALUE"));
        const float gain = m_backend->props[std::make_pair((int)STREAM_TYPE_DEPTH, (int)PROPERTY_GAIN)];
        QCOMPARE(gain, 2.0f);

        r = call("set", QJsonObject{ { "name", "trigger_mode" }, { "value", "software" } });
        QCOMPARE(r.value("result").toObject().value("value").toString(), QString("software"));
        QCOMPARE(m_backend->ext[PROPERTY_EXT_TRIGGER_MODE].triggerMode, TRIGGER_MODE_SOFTWAER);

        QCOMPARE(errorCode(call("set", QJsonObject{ { "name", "trigger_mode" }, { "value", "sometimes" } })),
            QString("BAD_VALUE"));

        r = call("set", QJsonObject{ { "name", "depth.range" }, { "value", QJsonObject{ { "min", 100 }, { "max", 900 } } } });
        QCOMPARE(r.value("result").toObject().value("value").toObject().value("max").toInt(), 900);
        QCOMPARE(errorCode(call("set", QJsonObject{ { "name", "depth.range" },
            { "value", QJsonObject{ { "min", 900 }, { "max", 100 } } } })), QString("BAD_VALUE"));

        QCOMPARE(errorCode(call("set", QJsonObject{ { "name", "depth.scale" }, { "value", 1 } })), QString("NOT_WRITABLE"));
        QCOMPARE(errorCode(call("get", QJsonObject{ { "name", "led_ctrl" } })), QString("NOT_READABLE"));
        QCOMPARE(errorCode(call("get", QJsonObject{ { "name", "warp_drive" } })), QString("UNKNOWN_PROPERTY"));
        QCOMPARE(errorCode(call("set", QJsonObject{ { "name", "depth.gain" } })), QString("BAD_REQUEST"));

        // write-only: accepted, no read-back
        r = call("set", QJsonObject{ { "name", "led_ctrl" },
            { "value", QJsonObject{ { "led", "ir" }, { "mode", "disable" } } } });
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(m_backend->ext[PROPERTY_EXT_LED_CTRL].ledCtrlParam.emLedId, IR_LED);
        QCOMPARE(m_backend->ext[PROPERTY_EXT_LED_CTRL].ledCtrlParam.emCtrlType, DISABLE_LED);

        // the SDK refusing is reported with its code
        r = call("set", QJsonObject{ { "name", "laser.on" }, { "value", 0 } });
        QCOMPARE(errorCode(r), QString("SDK_ERROR"));
        QCOMPARE(r.value("error").toObject().value("sdk_error").toString(), QString("NOT_SUPPORT"));

        r = call("get", QJsonObject{ { "name", "cpu_temperature" } });
        QCOMPARE(r.value("result").toObject().value("value").toInt(), 41);
    }

    void capabilitiesListStreamsAndRanges()
    {
        QVERIFY(call("connect").value("ok").toBool());
        const QJsonObject result = call("capabilities").value("result").toObject();

        const QJsonArray depthModes = result.value("streams").toObject().value("depth").toObject().value("modes").toArray();
        QCOMPARE(depthModes.size(), 3);
        QCOMPARE(depthModes[1].toObject().value("format").toString(), QString("Z16Y8Y8"));

        bool sawGainRange = false;
        for (const QJsonValue& v : result.value("properties").toArray())
        {
            const QJsonObject p = v.toObject();
            if (p.value("name").toString() == "depth.gain")
            {
                QCOMPARE(p.value("range").toObject().value("max").toDouble(), 16.0);
                sawGainRange = true;
            }
        }
        QVERIFY(sawGainRange);
    }

    void streamSelection()
    {
        QVERIFY(call("connect").value("ok").toBool());

        // defaults: Z16 depth + RGB8 color
        QJsonObject r = call("start_stream");
        QCOMPARE(r.value("result").toObject().value("depth").toObject().value("format").toString(), QString("Z16"));
        QCOMPARE(r.value("result").toObject().value("rgb").toObject().value("format").toString(), QString("RGB8"));
        QCOMPARE(r.value("result").toObject().value("depth_scale").toDouble(), (double)0.1f);
        QCOMPARE(errorCode(call("start_stream")), QString("ALREADY_STREAMING"));

        QVERIFY(call("stop_stream").value("result").toObject().value("was_streaming").toBool());

        r = call("start_stream", QJsonObject{ { "depth", QJsonObject{ { "format", "Z16Y8Y8" } } }, { "rgb", false } });
        QCOMPARE(m_backend->depthFormat, STREAM_FORMAT_Z16Y8Y8);
        QVERIFY(!m_backend->rgbStreaming);
        QVERIFY(r.value("result").toObject().value("rgb").isNull());
        call("stop_stream");

        QCOMPARE(errorCode(call("start_stream", QJsonObject{ { "depth", QJsonObject{ { "width", 999 } } } })),
            QString("BAD_VALUE"));
    }

    void captureRequiresStreamAndValidArgs()
    {
        QTemporaryDir dir;
        QVERIFY(call("connect").value("ok").toBool());
        QCOMPARE(errorCode(call("capture", QJsonObject{ { "dir", dir.path() }, { "name", "a" } })), QString("NOT_STREAMING"));

        QVERIFY(call("start_stream").value("ok").toBool());
        QCOMPARE(errorCode(call("capture", QJsonObject{ { "dir", dir.path() }, { "name", "../escape" } })), QString("BAD_VALUE"));
        QCOMPARE(errorCode(call("capture", QJsonObject{ { "dir", "relative/dir" }, { "name", "a" } })), QString("BAD_VALUE"));
        QCOMPARE(errorCode(call("capture", QJsonObject{ { "dir", dir.path() }, { "name", "a" },
            { "outputs", QJsonArray{ "hologram" } } })), QString("BAD_VALUE"));
        QCOMPARE(m_backend->softTriggers, 0);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0);
    }

    void captureWritesAllOutputs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        connectAndStream(QJsonObject{ { "depth", QJsonObject{ { "format", "Z16Y8Y8" } } } });
        QVERIFY(call("set", QJsonObject{ { "name", "trigger_mode" }, { "value", "software" } }).value("ok").toBool());

        const QJsonObject r = call("capture", QJsonObject{ { "dir", dir.path() + "/scan1" }, { "name", "view_000" },
            { "outputs", QJsonArray{ "depth", "ir", "rgb", "ply" } }, { "texture", true } });
        QVERIFY2(r.value("ok").toBool(), QJsonDocument(r).toJson());
        const QJsonObject result = r.value("result").toObject();

        QCOMPARE(result.value("trigger").toString(), QString("software"));
        QCOMPARE(m_backend->softTriggers, 1);
        QCOMPARE(m_backend->bufferClears, 1);
        // textured: the SDK keeps only points that land inside the RGB image
        // (and, since it tests an int against 0.00001, not in its first row/column)
        const int points = result.value("points").toInt();
        QVERIFY(points > 0 && points < FakeBackend::W * FakeBackend::H - 1);
        QCOMPARE(result.value("skipped").toArray().size(), 0);

        QStringList kinds;
        for (const QJsonValue& v : result.value("files").toArray())
        {
            const QJsonObject f = v.toObject();
            kinds << f.value("kind").toString();
            QVERIFY(QFile::exists(f.value("path").toString()));
            QVERIFY(f.value("bytes").toDouble() > 0);
        }
        kinds.sort();
        QCOMPARE(kinds, QStringList({ "depth", "ir_left", "ir_right", "ply", "rgb" }));

        // depth PNG holds the raw 16-bit values
        QFile png(dir.path() + "/scan1/view_000_depth.png");
        QVERIFY(png.open(QIODevice::ReadOnly));
        int w = 0, h = 0, bits = 0;
        QByteArray pix;
        QVERIFY(ImageUtil::genPixDataFromPngData(png.readAll(), w, h, bits, pix));
        QCOMPARE(bits, 16);
        QCOMPARE(((const unsigned short*)pix.constData())[1], (unsigned short)5000);

        // PLY header uses LF only and carries color
        QFile ply(dir.path() + "/scan1/view_000.ply");
        QVERIFY(ply.open(QIODevice::ReadOnly));
        const QByteArray content = ply.readAll();
        const int end = content.indexOf("end_header\n");
        QVERIFY(end > 0);
        const QByteArray header = content.left(end);
        QVERIFY(!header.contains('\r'));
        QVERIFY(header.contains("format binary_little_endian 1.0"));
        QVERIFY(header.contains("property uchar red"));
        QCOMPARE(content.size() - (end + 11), points * (6 * 4 + 3));
    }

    void captureSkipsWhatTheFrameLacks()
    {
        QTemporaryDir dir;
        connectAndStream(QJsonObject{ { "rgb", false } });   // Z16, no RGB, trigger off

        const QJsonObject result = call("capture", QJsonObject{ { "dir", dir.path() }, { "name", "v" },
            { "outputs", QJsonArray{ "depth", "ir", "rgb", "ply" } }, { "texture", true }, { "fresh", false } })
            .value("result").toObject();

        QCOMPARE(result.value("trigger").toString(), QString("stream"));
        QCOMPARE(m_backend->softTriggers, 0);
        QCOMPARE(m_backend->bufferClears, 0);

        QStringList skipped;
        for (const QJsonValue& v : result.value("skipped").toArray())
        {
            skipped << v.toObject().value("kind").toString();
        }
        skipped.sort();
        QCOMPARE(skipped, QStringList({ "ir", "ply_texture", "rgb" }));
        QCOMPARE(result.value("files").toArray().size(), 2);   // depth + ply
        QCOMPARE(result.value("points").toInt(), FakeBackend::W * FakeBackend::H - 1);   // all but the zero pixel
    }

    void cameraRemovedResetsState()
    {
        connectAndStream();
        m_backend->handler(QJsonObject{ { "event", "camera_removed" }, { "serial", "FAKE0001" } });

        QCOMPARE(m_events.size(), 1);
        QCOMPARE(m_events[0].value("event").toString(), QString("camera_removed"));
        QCOMPARE(errorCode(call("info")), QString("NOT_CONNECTED"));

        // a different camera going away doesn't affect the connection
        QVERIFY(call("connect").value("ok").toBool());
        m_backend->handler(QJsonObject{ { "event", "camera_removed" }, { "serial", "OTHER" } });
        QVERIFY(call("info").value("ok").toBool());
    }

    void shutdownReleasesCamera()
    {
        connectAndStream();
        const QJsonObject r = call("shutdown");
        QVERIFY(r.value("ok").toBool());
        QVERIFY(m_server->exitRequested());
        QVERIFY(!m_backend->connected);
        QCOMPARE(m_backend->depthFormat, STREAM_FORMAT_COUNT);
        QVERIFY(!m_backend->rgbStreaming);
    }

    void propertyTableIsConsistent()
    {
        QSet<QString> names;
        for (const auto& def : propertyTable())
        {
            QVERIFY2(!names.contains(def.name), def.name);
            names.insert(def.name);
            QVERIFY(def.readable || def.writable);
            QVERIFY(findProperty(def.name) == &def);
            if (def.type == ValueType::Enum)
            {
                QVERIFY(!def.enums.empty());
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestBridge)
#include "test_bridge.moc"
