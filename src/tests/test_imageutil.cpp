/*******************************************************************************
* This file is part of the 3DViewer                                            *
*                                                                              *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU General Public License as published by         *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
********************************************************************************/

// 16-bit grayscale PNG round trip through csutil's ImageUtil. Captured depth
// maps are stored this way (OutputSaver, CapturedZipParser), so a lossless
// round trip is what keeps saved depth usable for later point cloud export.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "imageutil.h"

class TestImageUtil : public QObject
{
    Q_OBJECT

private slots:
    void depthPngRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const int width = 7, height = 5;
        QByteArray depth(width * height * 2, 0);
        unsigned short* pix = reinterpret_cast<unsigned short*>(depth.data());
        for (int i = 0; i < width * height; i++)
            pix[i] = (unsigned short)(i * 997 + 3);   // spans both bytes, incl. > 255
        pix[0] = 0;                                   // invalid depth stays 0
        pix[1] = 0xFFFF;

        const QString path = dir.filePath("depth.png");
        QVERIFY(ImageUtil::saveGrayScale16ByLibpng(width, height, depth, path));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray png = file.readAll();

        int outWidth = 0, outHeight = 0, bitDepth = 0;
        QByteArray outPix;
        QVERIFY(ImageUtil::genPixDataFromPngData(png, outWidth, outHeight, bitDepth, outPix));

        QCOMPARE(outWidth, width);
        QCOMPARE(outHeight, height);
        QCOMPARE(bitDepth, 16);
        QCOMPARE(outPix.size(), depth.size());
        QCOMPARE(outPix, depth);
    }

    void rejectsNonPng()
    {
        int w = 0, h = 0, bitDepth = 0;
        QByteArray pix;
        QVERIFY(!ImageUtil::genPixDataFromPngData(QByteArray(64, 'x'), w, h, bitDepth, pix));
    }
};

QTEST_GUILESS_MAIN(TestImageUtil)
#include "test_imageutil.moc"
