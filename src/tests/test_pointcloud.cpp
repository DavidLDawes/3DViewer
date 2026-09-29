/*******************************************************************************
* This file is part of the 3DViewer                                            *
*                                                                              *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU General Public License as published by         *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
********************************************************************************/

// Depth map -> point cloud -> PLY, using the header-only cs::Pointcloud from
// the 3DCamera SDK (the same path OutputSaver uses to write .ply results).

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include <hpp/Processing.hpp>

class TestPointcloud : public QObject
{
    Q_OBJECT

private:
    static Intrinsics makeIntrinsics(int width, int height, float f)
    {
        Intrinsics intr = {};
        intr.width = (short)width;
        intr.height = (short)height;
        intr.fx = f;
        intr.fy = f;
        intr.cx = width / 2.0f;
        intr.cy = height / 2.0f;
        intr.one22 = 1.0f;
        return intr;
    }

    // read the PLY header; returns vertex count, or -1 if malformed
    static int readPlyHeader(const QString& path, QStringList& header, qint64& bodyOffset)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return -1;

        int vertexCount = -1;
        while (!file.atEnd())
        {
            QString line = QString::fromLatin1(file.readLine()).trimmed();
            header << line;
            if (line.startsWith("element vertex "))
                vertexCount = line.mid(15).toInt();
            if (line == "end_header")
            {
                bodyOffset = file.pos();
                return vertexCount;
            }
        }
        return -1;
    }

private slots:
    void pinholeProjection()
    {
        const int w = 4, h = 4;
        const float f = 2.0f;
        Intrinsics intr = makeIntrinsics(w, h, f);

        // flat plane 1000 units away (depthScale 0.1 -> z = 100)
        QVector<unsigned short> depth(w * h, 1000);

        cs::Pointcloud pc;
        pc.generatePoints<unsigned short>(depth.data(), w, h, 0.1f, &intr, nullptr, nullptr, true);

        QCOMPARE(pc.size(), w * h);
        const auto& v = pc.getVertices();
        for (int i = 0; i < pc.size(); i++)
            QVERIFY(qFuzzyCompare(v[i].z, 100.0f));

        // pixel (u=0, v=0): x = (0 - cx) * z / fx = -2 * 100 / 2 = -100
        QVERIFY(qFuzzyCompare(v[0].x, -100.0f));
        QVERIFY(qFuzzyCompare(v[0].y, -100.0f));
    }

    void intrinsicsScaleWithResolution()
    {
        // intrinsics calibrated at 8x8, depth delivered at 4x4: fx, cx must scale by 0.5
        Intrinsics intr = makeIntrinsics(8, 8, 4.0f);
        QVector<unsigned short> depth(4 * 4, 10);

        cs::Pointcloud pc;
        pc.generatePoints<unsigned short>(depth.data(), 4, 4, 1.0f, &intr, nullptr, nullptr, true);

        // scaled: fx = 2, cx = 2; pixel (0,0): x = (0 - 2) * 10 / 2 = -10
        QCOMPARE(pc.size(), 16);
        QVERIFY(qFuzzyCompare(pc.getVertices()[0].x, -10.0f));
    }

    void removeInvalidDropsZeroDepth()
    {
        const int w = 4, h = 2;
        Intrinsics intr = makeIntrinsics(w, h, 2.0f);
        QVector<unsigned short> depth(w * h, 500);
        depth[0] = 0;
        depth[5] = 0;

        cs::Pointcloud kept;
        kept.generatePoints<unsigned short>(depth.data(), w, h, 1.0f, &intr, nullptr, nullptr, false);
        QCOMPARE(kept.size(), w * h);

        cs::Pointcloud removed;
        removed.generatePoints<unsigned short>(depth.data(), w, h, 1.0f, &intr, nullptr, nullptr, true);
        QCOMPARE(removed.size(), w * h - 2);
        for (int i = 0; i < removed.size(); i++)
            QVERIFY(removed.getVertices()[i].z > 0.0f);
    }

    void exportAsciiPly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const int w = 3, h = 3;
        Intrinsics intr = makeIntrinsics(w, h, 2.0f);
        QVector<float> depth(w * h, 250.0f);

        cs::Pointcloud pc;
        pc.generatePoints<float>(depth.data(), w, h, 1.0f, &intr, nullptr, nullptr, true);

        const QString path = dir.filePath("cloud.ply");
        pc.exportToFile(path.toStdString(), nullptr, 0, 0, false);

        QStringList header;
        qint64 bodyOffset = 0;
        QCOMPARE(readPlyHeader(path, header, bodyOffset), w * h);
        QCOMPARE(header.first(), QString("ply"));
        QVERIFY(header.contains("format ascii 1.0"));
        QVERIFY(header.contains("property float nz"));
        QVERIFY(!header.contains("property uchar red"));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        file.seek(bodyOffset);
        // rows end with a trailing space, and with CRLF on Windows (text-mode ofstream)
        const QStringList rows = QString::fromLatin1(file.readAll()).split(QRegExp("[\r\n]+"), QString::SkipEmptyParts);
        QCOMPARE(rows.size(), w * h);
        QCOMPARE(rows.first().split(QRegExp("\\s+"), QString::SkipEmptyParts).size(), 6);
    }

    void exportBinaryPly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const int w = 5, h = 4;
        Intrinsics intr = makeIntrinsics(w, h, 3.0f);
        QVector<unsigned short> depth(w * h, 800);

        cs::Pointcloud pc;
        pc.generatePoints<unsigned short>(depth.data(), w, h, 0.5f, &intr, nullptr, nullptr, true);

        const QString path = dir.filePath("cloud.ply");
        pc.exportToFile(path.toStdString(), nullptr, 0, 0, true);

        QStringList header;
        qint64 bodyOffset = 0;
        QCOMPARE(readPlyHeader(path, header, bodyOffset), w * h);
        QVERIFY(header.contains("format binary_little_endian 1.0"));

        // 6 floats (xyz + normal) per vertex, no color
        QCOMPARE(QFileInfo(path).size() - bodyOffset, qint64(w * h * 6 * sizeof(float)));
    }
};

QTEST_GUILESS_MAIN(TestPointcloud)
#include "test_pointcloud.moc"
