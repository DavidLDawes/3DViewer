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

#include "capturewriter.h"

#include <cstring>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QRegExp>

#include <hpp/Processing.hpp>

#include "imageutil.h"

namespace csbridge
{

const QStringList& CaptureWriter::knownOutputs()
{
    static const QStringList outputs = { "depth", "ir", "rgb", "ply" };
    return outputs;
}

QString CaptureWriter::prepare(const CaptureRequest& request)
{
    if (request.dir.isEmpty() || !QDir::isAbsolutePath(request.dir))
    {
        return "dir must be an absolute path";
    }

    static const QRegExp validName("[A-Za-z0-9][A-Za-z0-9._-]{0,99}");
    if (!validName.exactMatch(request.name) || request.name.contains(".."))
    {
        return "name must be 1-100 characters of A-Z a-z 0-9 . _ - (no path separators)";
    }

    if (request.outputs.isEmpty())
    {
        return "outputs must list at least one of: " + knownOutputs().join(", ");
    }
    for (const QString& output : request.outputs)
    {
        if (!knownOutputs().contains(output))
        {
            return QString("unknown output '%1', expected: %2").arg(output).arg(knownOutputs().join(", "));
        }
    }

    if (!QDir().mkpath(request.dir))
    {
        return QString("could not create directory %1").arg(request.dir);
    }
    return QString();
}

static bool hasDepth(const Frame& frame)
{
    return frame.format == STREAM_FORMAT_Z16 || frame.format == STREAM_FORMAT_Z16Y8Y8;
}

// bytes the SDK should have delivered for this frame, or 0 if the format isn't handled
static size_t expectedDepthFrameSize(const Frame& frame)
{
    const size_t pixels = (size_t)frame.width * frame.height;
    switch (frame.format)
    {
    case STREAM_FORMAT_Z16:     return pixels * 2;
    case STREAM_FORMAT_Z16Y8Y8: return pixels * 4;   // depth (2 bytes) + left IR + right IR
    case STREAM_FORMAT_PAIR:    return pixels * 2;   // left IR + right IR
    default:                    return 0;
    }
}

// cs::Pointcloud writes the header in text mode, so on Windows the header (and an
// ascii body) end lines with CRLF. Normalize to LF so strict PLY readers accept it.
static void normalizePlyLineEndings(const QString& path, bool binary)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    QByteArray content = file.readAll();
    file.close();

    if (!content.contains("\r\n"))
    {
        return;
    }

    if (binary)
    {
        const QByteArray marker = "end_header\r\n";
        const int end = content.indexOf(marker);
        if (end < 0)
        {
            return;
        }
        QByteArray header = content.left(end + marker.size());
        header.replace("\r\n", "\n");
        content = header + content.mid(end + marker.size());
    }
    else
    {
        content.replace("\r\n", "\n");
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(content);
    }
}

// decode the RGB frame to a tightly packed RGB888 buffer
static bool decodeRgb(const Frame& rgb, std::vector<unsigned char>& pixels, int& width, int& height)
{
    QImage image;
    if (rgb.format == STREAM_FORMAT_RGB8)
    {
        if (rgb.data.size() < (size_t)rgb.width * rgb.height * 3)
        {
            return false;
        }
        image = QImage((const uchar*)rgb.data.data(), rgb.width, rgb.height, rgb.width * 3, QImage::Format_RGB888);
    }
    else if (rgb.format == STREAM_FORMAT_MJPG)
    {
        image = QImage::fromData((const uchar*)rgb.data.data(), (int)rgb.data.size(), "JPG");
        image = image.convertToFormat(QImage::Format_RGB888);
    }

    if (image.isNull())
    {
        return false;
    }

    width = image.width();
    height = image.height();
    pixels.resize((size_t)width * height * 3);
    for (int y = 0; y < height; y++)
    {
        std::memcpy(pixels.data() + (size_t)y * width * 3, image.constScanLine(y), (size_t)width * 3);
    }
    return true;
}

QJsonObject CaptureWriter::write(const CaptureRequest& request, const CaptureData& data)
{
    QJsonArray files;
    QJsonArray skipped;
    int points = 0;

    const QDir dir(request.dir);
    const Frame& depth = data.depth;
    const int w = depth.width;
    const int h = depth.height;

    auto addFile = [&](const QString& kind, const QString& path)
    {
        files.append(QJsonObject{ { "kind", kind }, { "path", QDir::cleanPath(path) },
                                  { "bytes", (double)QFileInfo(path).size() } });
    };
    auto skip = [&](const QString& kind, const QString& reason)
    {
        skipped.append(QJsonObject{ { "kind", kind }, { "reason", reason } });
    };

    const size_t expected = expectedDepthFrameSize(depth);
    const bool depthFrameOk = expected > 0 && depth.data.size() >= expected && w > 0 && h > 0;
    const QString badFrame = depth.empty() ? QString("no depth frame")
        : QString("depth frame format %1, %2 bytes for %3x%4 is not usable")
            .arg((int)depth.format).arg(depth.data.size()).arg(w).arg(h);

    // depth: first w*h 16-bit values of Z16 / Z16Y8Y8
    if (request.outputs.contains("depth"))
    {
        if (!depthFrameOk)
        {
            skip("depth", badFrame);
        }
        else if (!hasDepth(depth))
        {
            skip("depth", "the depth stream format carries no depth (PAIR is IR only)");
        }
        else
        {
            const QString path = dir.filePath(request.name + "_depth.png");
            QByteArray raw(depth.data.data(), w * h * 2);
            if (ImageUtil::saveGrayScale16ByLibpng(w, h, raw, path))
            {
                addFile("depth", path);
            }
            else
            {
                skip("depth", "writing the PNG failed");
            }
        }
    }

    // ir: the binocular pair, after the depth data in Z16Y8Y8, alone in PAIR
    if (request.outputs.contains("ir"))
    {
        if (!depthFrameOk)
        {
            skip("ir", badFrame);
        }
        else if (depth.format != STREAM_FORMAT_Z16Y8Y8 && depth.format != STREAM_FORMAT_PAIR)
        {
            skip("ir", "IR images need the Z16Y8Y8 or PAIR depth stream format");
        }
        else
        {
            const size_t offset = (depth.format == STREAM_FORMAT_Z16Y8Y8) ? (size_t)w * h * 2 : 0;
            const char* sides[] = { "left", "right" };
            for (int i = 0; i < 2; i++)
            {
                const uchar* src = (const uchar*)depth.data.data() + offset + (size_t)i * w * h;
                QImage image(src, w, h, w, QImage::Format_Grayscale8);
                const QString path = dir.filePath(QString("%1_ir_%2.png").arg(request.name).arg(sides[i]));
                if (image.save(path, "PNG"))
                {
                    addFile(QString("ir_%1").arg(sides[i]), path);
                }
                else
                {
                    skip(QString("ir_%1").arg(sides[i]), "writing the PNG failed");
                }
            }
        }
    }

    // rgb: as delivered (MJPG -> .jpg, RGB8 -> .png)
    if (request.outputs.contains("rgb"))
    {
        const Frame& rgb = data.rgb;
        if (rgb.empty())
        {
            skip("rgb", "no RGB frame (RGB stream not started or not supported)");
        }
        else if (rgb.format == STREAM_FORMAT_MJPG)
        {
            const QString path = dir.filePath(request.name + "_rgb.jpg");
            QFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(rgb.data.data(), (qint64)rgb.data.size()) == (qint64)rgb.data.size())
            {
                file.close();
                addFile("rgb", path);
            }
            else
            {
                skip("rgb", "writing the JPEG failed");
            }
        }
        else if (rgb.format == STREAM_FORMAT_RGB8 && rgb.data.size() >= (size_t)rgb.width * rgb.height * 3)
        {
            QImage image((const uchar*)rgb.data.data(), rgb.width, rgb.height, rgb.width * 3, QImage::Format_RGB888);
            const QString path = dir.filePath(request.name + "_rgb.png");
            if (image.save(path, "PNG"))
            {
                addFile("rgb", path);
            }
            else
            {
                skip("rgb", "writing the PNG failed");
            }
        }
        else
        {
            skip("rgb", QString("unsupported RGB frame format %1").arg((int)rgb.format));
        }
    }

    // ply: the SDK's own depth -> point cloud, as 3DViewer's OutputSaver does
    if (request.outputs.contains("ply"))
    {
        if (!depthFrameOk)
        {
            skip("ply", badFrame);
        }
        else if (!hasDepth(depth))
        {
            skip("ply", "the depth stream format carries no depth (PAIR is IR only)");
        }
        else if (data.depthScale <= 0.0f)
        {
            skip("ply", "depth scale unknown");
        }
        else
        {
            std::vector<unsigned char> texture;
            int texWidth = 0, texHeight = 0;
            bool textured = false;
            if (request.texture)
            {
                if (data.rgb.empty() || !data.hasRgbCalibration)
                {
                    skip("ply_texture", "no RGB frame or RGB calibration; PLY written without color");
                }
                else if (!decodeRgb(data.rgb, texture, texWidth, texHeight))
                {
                    skip("ply_texture", "could not decode the RGB frame; PLY written without color");
                }
                else
                {
                    textured = true;
                }
            }

            cs::Pointcloud pc;
            Intrinsics depthIntr = data.depthIntrinsics;
            unsigned short* depthPtr = (unsigned short*)depth.data.data();
            if (textured)
            {
                Intrinsics rgbIntr = data.rgbIntrinsics;
                Extrinsics extr = data.extrinsics;
                pc.generatePoints<unsigned short>(depthPtr, w, h, data.depthScale, &depthIntr, &rgbIntr, &extr, true);
            }
            else
            {
                pc.generatePoints<unsigned short>(depthPtr, w, h, data.depthScale, &depthIntr, nullptr, nullptr, true);
            }

            points = pc.size();
            const QString path = dir.filePath(request.name + ".ply");
            pc.exportToFile(QDir::toNativeSeparators(path).toLocal8Bit().toStdString(),
                textured ? texture.data() : nullptr, texWidth, texHeight, request.binaryPly);

            if (QFileInfo::exists(path))
            {
                normalizePlyLineEndings(path, request.binaryPly);
                addFile("ply", path);
            }
            else
            {
                skip("ply", "writing the PLY failed");
            }
        }
    }

    return QJsonObject{ { "files", files }, { "skipped", skipped }, { "points", points } };
}

} // namespace csbridge
