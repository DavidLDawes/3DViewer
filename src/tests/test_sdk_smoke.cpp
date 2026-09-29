/*******************************************************************************
* This file is part of the 3DViewer                                            *
*                                                                              *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU General Public License as published by         *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
********************************************************************************/

// Smoke test of the prebuilt 3DCamera SDK with no camera required: the library
// loads, reports its version, enumerates, and refuses camera operations cleanly
// when nothing is connected. If a camera happens to be attached, enumeration
// just reports it; nothing here connects to or changes a camera.

#include <QtTest>
#include <cstring>

#include <3DCamera.hpp>

class TestSdkSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        cs::enableLoging(false);
    }

    void reportsVersion()
    {
        CS_SDK_VERSION* version = nullptr;
        QCOMPARE(cs::getSdkVersion(&version), SUCCESS);
        QVERIFY(version != nullptr);
        QVERIFY(version->version != nullptr);
        QVERIFY(std::strlen(version->version) > 0);
        qInfo("3DCamera SDK version %s", version->version);
    }

    void errorStringsExist()
    {
        const char* s = cs::getCameraErrorString(ERROR_DEVICE_NOT_FOUND);
        QVERIFY(s != nullptr);
        QVERIFY(std::strlen(s) > 0);
    }

    void cameraTypeNames()
    {
        QVERIFY(cs::getCameraTypeName(CAMERA_POP_3) != nullptr);
    }

    void enumerateWithoutHardware()
    {
        auto system = cs::getSystemPtr();
        QVERIFY(system != nullptr);

        std::vector<CameraInfo> cameras;
        QCOMPARE(system->queryCameras(cameras), SUCCESS);
        qInfo("cameras found: %d", int(cameras.size()));
        for (const auto& info : cameras)
            qInfo("  %s serial=%s fw=%s", info.name, info.serial, info.firmwareVersion);
    }

    void operationsRefusedWhenNotConnected()
    {
        auto camera = cs::getCameraPtr();
        QVERIFY(camera != nullptr);

        // nothing connected: trigger / property calls must fail, not crash
        QVERIFY(camera->softTrigger() != SUCCESS);

        float value = 0.0f;
        QVERIFY(camera->getProperty(STREAM_TYPE_DEPTH, PROPERTY_EXPOSURE, value) != SUCCESS);
    }
};

QTEST_GUILESS_MAIN(TestSdkSmoke)
#include "test_sdk_smoke.moc"
