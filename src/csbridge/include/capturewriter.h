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

#ifndef _CS_CAPTUREWRITER_H
#define _CS_CAPTUREWRITER_H

#include <QJsonObject>
#include <QSet>
#include <QString>

#include "camerabackend.h"

namespace csbridge
{

// What one capture produced, as the SDK delivered it.
struct CaptureData
{
    Frame depth;                    // Z16, Z16Y8Y8 or PAIR
    Frame rgb;                      // RGB8 or MJPG, empty if none
    Intrinsics depthIntrinsics;
    Intrinsics rgbIntrinsics;
    Extrinsics extrinsics;          // depth -> RGB
    bool hasRgbCalibration = false;
    float depthScale = 0.0f;        // raw depth unit -> mm
};

struct CaptureRequest
{
    QString dir;                    // existing or creatable directory
    QString name;                   // file name stem, no path separators
    QSet<QString> outputs;          // "depth", "ir", "rgb", "ply"
    // Color the PLY from the RGB frame. The SDK then keeps only points that
    // project inside the RGB image (not its first row/column either), so a
    // textured cloud can have fewer points than an untextured one.
    bool texture = false;
    bool binaryPly = true;
};

// Writes the requested files, the same formats 3DViewer saves:
//   <name>_depth.png                 16-bit grayscale, raw depth units (x depth_scale = mm)
//   <name>_ir_left.png / _ir_right.png  8-bit grayscale (Z16Y8Y8 / PAIR only)
//   <name>_rgb.png (RGB8) or _rgb.jpg (MJPG, as delivered)
//   <name>.ply                       point cloud in mm, xyz + normals (+ rgb if textured)
// Returns { "files": [{kind, path, bytes}], "skipped": [{kind, reason}], "points": n }.
// An output that can't be made from this frame is listed in "skipped", not an error.
class CaptureWriter
{
public:
    static const QStringList& knownOutputs();

    // validates dir/name/outputs and creates dir; returns an error message, or
    // empty if the capture can go ahead. Call before triggering the camera.
    static QString prepare(const CaptureRequest& request);

    static QJsonObject write(const CaptureRequest& request, const CaptureData& data);
};

} // namespace csbridge

#endif // _CS_CAPTUREWRITER_H
