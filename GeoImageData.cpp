#include "GeoImageData.h"
#include "AccuracyAssessment.h"
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <gdal_priv.h>

bool GeoImageData::loadFromImage(const QString& path, ProgressCallback progressCb)
{
    QFileInfo fi(path);
    filePath = path;
    fileSizeBytes = fi.size();
    imageFormat = fi.suffix().toUpper();
    if (imageFormat.isEmpty()) imageFormat = QString::fromUtf8("\u672A\u77E5");

    // 策略：先用 GDAL 加载，失败再用 QImage
    GDALDataset* dataset = nullptr;

    // ========== GDAL 加载路径 ==========
    dataset = static_cast<GDALDataset*>(GDALOpen(path.toUtf8().constData(), GA_ReadOnly));
    if (dataset)
    {
        width = dataset->GetRasterXSize();
        height = dataset->GetRasterYSize();
        bands = dataset->GetRasterCount();
        bitDepth = GDALGetDataTypeSize(dataset->GetRasterBand(1)->GetRasterDataType());

        // 读取地理元数据
        if (dataset->GetProjectionRef() && strlen(dataset->GetProjectionRef()) > 0)
            projection = QString::fromUtf8(dataset->GetProjectionRef());
        crs = projection;
        double gt[6] = {0};
        if (dataset->GetGeoTransform(gt) == CE_None) {
            std::copy(gt, gt + 6, geoTransform);
            pixelSizeX = std::abs(gt[1]);
            pixelSizeY = std::abs(gt[5]);
        }
        noDataValue = dataset->GetRasterBand(1)->GetNoDataValue();

        // 读取元数据域
        char** papszMetadata = dataset->GetMetadata();
        if (papszMetadata) {
            for (int i = 0; papszMetadata[i]; ++i) {
                QString entry = QString::fromUtf8(papszMetadata[i]);
                if (entry.startsWith("ACQUISITIONDATETIME=", Qt::CaseInsensitive))
                    captureTime = entry.section('=', 1);
            }
        }

        // 检查图片像素数是否超过 int 上限（防止 pixelCount 溢出）
        long long totalPixels64 = static_cast<long long>(width) * static_cast<long long>(height);
        if (totalPixels64 > static_cast<long long>(std::numeric_limits<int>::max())) {
            GDALClose(dataset);
            return false;
        }
        int totalPixels = static_cast<int>(totalPixels64);

        // 检查图像是否过大（>1亿像素，提示用户）
        if (totalPixels > 100000000 && progressCb) {
            progressCb(0, QString::fromUtf8("\u5927\u56FE\u52A0\u8F7D\u4E2D (%1\u4E07\u50CF\u7D20)\uFF0C\u8BF7\u8010\u5FC3\u7B49\u5F85...")
                           .arg(totalPixels / 10000.0, 0, 'f', 1));
        }

        if (progressCb) progressCb(5, QString::fromUtf8("\u8BFB\u53D6\u6CE2\u6BB5\u6570\u636E..."));

        // 分配波段数据（直接用 double，兼容所有算法）
        bandData.resize(bands);
        bandNames.clear();
        bandMins.resize(bands);
        bandMaxs.resize(bands);

        for (int b = 0; b < bands; ++b) {
            if (progressCb) {
                int pct = 5 + (b * 85 / bands);
                progressCb(pct, QString::fromUtf8("\u52A0\u8F7D\u6CE2\u6BB5 %1/%2...").arg(b + 1).arg(bands));
            }

            GDALRasterBand* rb = dataset->GetRasterBand(b + 1); // 1-based

            // 波段名称
            const char* desc = rb->GetDescription();
            bandNames.push_back(desc && strlen(desc) > 0
                ? QString::fromUtf8(desc)
                : QString::fromUtf8("\u6CE2\u6BB5_%1").arg(b + 1));

            // 为每个波段预分配空间
            bandData[b].resize(totalPixels);

            // 使用 GDAL RasterIO 按块读取，避免一次性大块分配
            int blockXSize = 0, blockYSize = 0;
            rb->GetBlockSize(&blockXSize, &blockYSize);
            if (blockXSize <= 0) blockXSize = width;
            if (blockYSize <= 0) blockYSize = 1;

            // 逐块读取并同时追踪 min/max
            double bMin = 1e308, bMax = -1e308;

            for (int y0 = 0; y0 < height; y0 += blockYSize) {
                int readH = std::min(blockYSize, height - y0);
                size_t offset = static_cast<size_t>(y0) * static_cast<size_t>(width);
                CPLErr err = rb->RasterIO(GF_Read, 0, y0, width, readH,
                                          &bandData[b][offset],
                                          width, readH, GDT_Float64, 0, 0);
                if (err != CE_None) {
                    GDALClose(dataset);
                    return false;
                }

                // 追踪该块的 min/max
                size_t blockLen = static_cast<size_t>(readH) * static_cast<size_t>(width);
                for (size_t i = 0; i < blockLen; ++i) {
                    double v = bandData[b][offset + i];
                    if (v < bMin) bMin = v;
                    if (v > bMax) bMax = v;
                }

                if (progressCb && (y0 % (blockYSize * 10) == 0)) {
                    int subPct = (b * 85 / bands) + (y0 * 85 / bands / height);
                    progressCb(std::min(95, subPct),
                               QString::fromUtf8("\u52A0\u8F7D\u6CE2\u6BB5 %1...").arg(b + 1));
                }
            }

            bandMins[b] = bMin;
            bandMaxs[b] = bMax;
        }

        GDALClose(dataset);
        dataset = nullptr;

        if (progressCb) progressCb(98, QString::fromUtf8("\u52A0\u8F7D\u5B8C\u6210"));
        return true;
    }

    // ========== QImage 回退路径（小图 / GDAL 不可用时） ==========
    if (progressCb) progressCb(0, QString::fromUtf8("GDAL\u52A0\u8F7D\u5931\u8D25\uFF0C\u5C1D\u8BD5QImage..."));

    QImage img(path);
    if (img.isNull()) return false;

    if (progressCb) progressCb(5, QString::fromUtf8("\u89E3\u6790\u5143\u6570\u636E..."));

    width = img.width();
    height = img.height();
    bitDepth = img.depth();

    // 转换为 Format_RGB32 以确保 scanLine 格式一致
    QImage src = img;
    if (src.format() != QImage::Format_RGB32 && src.format() != QImage::Format_ARGB32) {
        src = src.convertToFormat(QImage::Format_RGB32);
    }

    int totalPixels = width * height;
    if (progressCb) progressCb(10, QString::fromUtf8("\u63D0\u53D6\u50CF\u7D20\u6570\u636E..."));

    if (img.isGrayscale() || img.format() == QImage::Format_Grayscale8) {
        bands = 1;
        bandData.resize(1);
        bandData[0].resize(totalPixels);
        bandNames = {QString::fromUtf8("\u7070\u5EA6\u6CE2\u6BB5")};
        bandMins.resize(1);
        bandMaxs.resize(1);
        bandMins[0] = 255; bandMaxs[0] = 0;

        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            double* dst = &bandData[0][y * width];
            for (int x = 0; x < width; ++x) {
                double v = qGray(line[x]);
                dst[x] = v;
                if (v < bandMins[0]) bandMins[0] = v;
                if (v > bandMaxs[0]) bandMaxs[0] = v;
            }
            if (progressCb && (y % 100 == 0)) {
                int pct = 10 + (y * 80 / height);
                progressCb(pct, QString::fromUtf8("\u63D0\u53D6\u50CF\u7D20\u6570\u636E..."));
            }
        }
    } else {
        bands = 3;
        bandData.resize(3);
        for (int b = 0; b < 3; ++b)
            bandData[b].resize(totalPixels);
        bandNames = {QString::fromUtf8("\u7EA2\u6CE2\u6BB5"),
                     QString::fromUtf8("\u7EFF\u6CE2\u6BB5"),
                     QString::fromUtf8("\u84DD\u6CE2\u6BB5")};
        bandMins.resize(3);
        bandMaxs.resize(3);
        for (int b = 0; b < 3; ++b) { bandMins[b] = 255; bandMaxs[b] = 0; }

        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            double* dstR = &bandData[0][y * width];
            double* dstG = &bandData[1][y * width];
            double* dstB = &bandData[2][y * width];
            for (int x = 0; x < width; ++x) {
                QRgb pixel = line[x];
                double r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel);
                dstR[x] = r; dstG[x] = g; dstB[x] = b;
                if (r < bandMins[0]) bandMins[0] = r;
                if (r > bandMaxs[0]) bandMaxs[0] = r;
                if (g < bandMins[1]) bandMins[1] = g;
                if (g > bandMaxs[1]) bandMaxs[1] = g;
                if (b < bandMins[2]) bandMins[2] = b;
                if (b > bandMaxs[2]) bandMaxs[2] = b;
            }
            if (progressCb && (y % 100 == 0)) {
                int pct = 10 + (y * 80 / height);
                progressCb(pct, QString::fromUtf8("\u63D0\u53D6\u50CF\u7D20\u6570\u636E..."));
            }
        }
    }

    if (progressCb) progressCb(98, QString::fromUtf8("\u52A0\u8F7D\u5B8C\u6210"));
    return true;
}

bool GeoImageData::loadDownsampled(const QString& path, long long maxPixels, ProgressCallback progressCb)
{
    if (maxPixels <= 0) {
        // 无限制，走普通加载
        return loadFromImage(path, progressCb);
    }

    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(path.toUtf8().constData(), GA_ReadOnly));
    if (!dataset) {
        // GDAL 打不开则回退到 QImage
        return loadFromImage(path, progressCb);
    }

    int srcW = dataset->GetRasterXSize();
    int srcH = dataset->GetRasterYSize();
    int srcBands = dataset->GetRasterCount();

    long long totalSrcPixels = static_cast<long long>(srcW) * static_cast<long long>(srcH);
    if (totalSrcPixels <= maxPixels) {
        // 原图不大，直接完整加载
        GDALClose(dataset);
        return loadFromImage(path, progressCb);
    }

    // 计算降采样因子（保持宽高比）
    double scale = std::sqrt(static_cast<double>(maxPixels) / totalSrcPixels);
    int dstW = qMax(1, static_cast<int>(srcW * scale));
    int dstH = qMax(1, static_cast<int>(srcH * scale));

    // 初始化 GeoImageData
    QFileInfo fi(path);
    filePath = path;
    fileSizeBytes = fi.size();
    imageFormat = fi.suffix().toUpper();
    if (imageFormat.isEmpty()) imageFormat = QString::fromUtf8("\u672A\u77E5");
    width = dstW;
    height = dstH;
    bands = srcBands;
    bitDepth = GDALGetDataTypeSize(dataset->GetRasterBand(1)->GetRasterDataType());

    // 地理元数据
    if (dataset->GetProjectionRef() && strlen(dataset->GetProjectionRef()) > 0)
        projection = QString::fromUtf8(dataset->GetProjectionRef());
    crs = projection;
    double gt[6] = {0};
    if (dataset->GetGeoTransform(gt) == CE_None) {
        std::copy(gt, gt + 6, geoTransform);
        pixelSizeX = std::abs(gt[1]) / scale;
        pixelSizeY = std::abs(gt[5]) / scale;
    }
    noDataValue = dataset->GetRasterBand(1)->GetNoDataValue();

    if (progressCb)
        progressCb(0, QString::fromUtf8("\u964D\u91C7\u6837\u52A0\u8F7D\u4E2D (%1x%2 \u2192 %3x%4)...")
                       .arg(srcW).arg(srcH).arg(dstW).arg(dstH));

    int totalDstPixels = dstW * dstH;
    bandData.resize(bands);
    bandNames.clear();
    bandMins.resize(bands);
    bandMaxs.resize(bands);

    for (int b = 0; b < bands; ++b) {
        if (progressCb) {
            int pct = (b + 1) * 90 / bands;
            progressCb(pct, QString::fromUtf8("\u964D\u91C7\u6837\u6CE2\u6BB5 %1/%2...").arg(b + 1).arg(bands));
        }

        GDALRasterBand* rb = dataset->GetRasterBand(b + 1);

        const char* desc = rb->GetDescription();
        bandNames.push_back(desc && strlen(desc) > 0
            ? QString::fromUtf8(desc)
            : QString::fromUtf8("\u6CE2\u6BB5_%1").arg(b + 1));

        bandData[b].resize(totalDstPixels);

        // 使用 GDAL RasterIO 做降采样（GDAL 内部使用最近邻/平均采样取决于配置）
        CPLErr err = rb->RasterIO(GF_Read, 0, 0, srcW, srcH,
                                  bandData[b].data(),
                                  dstW, dstH, GDT_Float64, 0, 0);
        if (err != CE_None) {
            GDALClose(dataset);
            return false;
        }

        // 计算降采样后波段的 min/max
        double bMin = 1e308, bMax = -1e308;
        for (int i = 0; i < totalDstPixels; ++i) {
            double v = bandData[b][i];
            if (v < bMin) bMin = v;
            if (v > bMax) bMax = v;
        }
        bandMins[b] = bMin;
        bandMaxs[b] = bMax;
    }

    GDALClose(dataset);

    if (progressCb) progressCb(100, QString::fromUtf8("\u964D\u91C7\u6837\u52A0\u8F7D\u5B8C\u6210 (%1x%2)").arg(dstW).arg(dstH));
    return true;
}

bool GeoImageData::loadMultiBand(const QStringList& bandFiles, ProgressCallback progressCb)
{
    if (bandFiles.isEmpty()) return false;

    bands = bandFiles.size();
    bandData.resize(bands);
    bandNames.clear();
    bandMins.resize(bands);
    bandMaxs.resize(bands);

    // 解析元数据
    QFileInfo fi(bandFiles[0]);
    fileSizeBytes = 0;
    for (const auto& f : bandFiles) {
        QFileInfo bf(f);
        fileSizeBytes += bf.size();
    }
    imageFormat = fi.suffix().toUpper();
    if (imageFormat.isEmpty()) imageFormat = QString::fromUtf8("\u591A\u6CE2\u6BB5");

    int totalPixels = 0;

    for (int b = 0; b < bands; ++b) {
        if (progressCb) {
            int pct = 5 + (b * 85 / bands);
            progressCb(pct, QString::fromUtf8("\u52A0\u8F7D\u6CE2\u6BB5 %1/%2...").arg(b + 1).arg(bands));
        }

        // 优先用 GDAL 加载单波段
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(bandFiles[b].toUtf8().constData(), GA_ReadOnly));
        if (ds && ds->GetRasterCount() >= 1) {
            if (b == 0) {
                width = ds->GetRasterXSize();
                height = ds->GetRasterYSize();
                totalPixels = width * height;
                bitDepth = GDALGetDataTypeSize(ds->GetRasterBand(1)->GetRasterDataType());
            }

            bandData[b].resize(totalPixels);
            bandNames.push_back(QFileInfo(bandFiles[b]).baseName());

            GDALRasterBand* rb = ds->GetRasterBand(1);
            CPLErr err = rb->RasterIO(GF_Read, 0, 0, width, height,
                                      bandData[b].data(), width, height, GDT_Float64, 0, 0);
            GDALClose(ds);
            if (err != CE_None) return false;

            // 追踪 min/max
            double bMin = 1e308, bMax = -1e308;
            for (size_t i = 0; i < static_cast<size_t>(totalPixels); ++i) {
                double v = bandData[b][i];
                if (v < bMin) bMin = v;
                if (v > bMax) bMax = v;
            }
            bandMins[b] = bMin;
            bandMaxs[b] = bMax;
            continue;
        }
        if (ds) GDALClose(ds);

        // QImage 回退
        QImage img(bandFiles[b]);
        if (img.isNull()) return false;

        if (b == 0) {
            width = img.width();
            height = img.height();
            totalPixels = width * height;
            bitDepth = img.depth();
        } else if (img.width() != width || img.height() != height) {
            return false;
        }

        bandData[b].resize(totalPixels);
        bandNames.push_back(QFileInfo(bandFiles[b]).baseName());

        QImage src = img;
        if (src.format() != QImage::Format_RGB32 && src.format() != QImage::Format_ARGB32) {
            src = src.convertToFormat(QImage::Format_RGB32);
        }

        double* dst = bandData[b].data();
        double bMin = 255, bMax = 0;
        for (int y = 0; y < height; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < width; ++x) {
                double v = qGray(line[x]);
                dst[y * width + x] = v;
                if (v < bMin) bMin = v;
                if (v > bMax) bMax = v;
            }
        }
        bandMins[b] = bMin;
        bandMaxs[b] = bMax;
    }

    filePath = bandFiles[0];

    if (progressCb) progressCb(98, QString::fromUtf8("\u52A0\u8F7D\u5B8C\u6210"));
    return true;
}

QImage GeoImageData::toQImage(int rBand, int gBand, int bBand,
                               double minPercent, double maxPercent) const
{
    if (!isValid()) return QImage();

    QImage img(width, height, QImage::Format_RGB32);

    if (bands == 1) {
        double minVal, maxVal;
        getBandRange(0, minVal, maxVal, minPercent, maxPercent);
        double range = maxVal - minVal;
        if (range < 1e-10) range = 1;

        for (int y = 0; y < height; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            const double* src = &bandData[0][y * static_cast<size_t>(width)];
            for (int x = 0; x < width; ++x) {
                int gray = static_cast<int>((src[x] - minVal) / range * 255);
                gray = std::max(0, std::min(255, gray));
                line[x] = qRgb(gray, gray, gray);
            }
        }
        return img;
    }

    rBand = std::max(0, std::min(bands - 1, rBand));
    gBand = std::max(0, std::min(bands - 1, gBand));
    bBand = std::max(0, std::min(bands - 1, bBand));

    double rMin, rMax, gMin, gMax, bMin, bMax;
    getBandRange(rBand, rMin, rMax, minPercent, maxPercent);
    getBandRange(gBand, gMin, gMax, minPercent, maxPercent);
    getBandRange(bBand, bMin, bMax, minPercent, maxPercent);

    double rRange = rMax - rMin; if (rRange < 1e-10) rRange = 1;
    double gRange = gMax - gMin; if (gRange < 1e-10) gRange = 1;
    double bRange = bMax - bMin; if (bRange < 1e-10) bRange = 1;

    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const double* srcR = &bandData[rBand][y * static_cast<size_t>(width)];
        const double* srcG = &bandData[gBand][y * static_cast<size_t>(width)];
        const double* srcB = &bandData[bBand][y * static_cast<size_t>(width)];
        for (int x = 0; x < width; ++x) {
            int r = static_cast<int>((srcR[x] - rMin) / rRange * 255);
            int g = static_cast<int>((srcG[x] - gMin) / gRange * 255);
            int b = static_cast<int>((srcB[x] - bMin) / bRange * 255);
            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));
            line[x] = qRgb(r, g, b);
        }
    }
    return img;
}

QImage GeoImageData::toDisplayImage(int maxDim) const
{
    if (!isValid()) return QImage();

    // 如果原图已经足够小，直接返回 toQImage 结果
    if (width <= maxDim && height <= maxDim)
        return toQImage();

    // 计算降采样尺寸
    int dispW, dispH;
    double aspect = static_cast<double>(width) / height;
    if (width >= height) {
        dispW = maxDim;
        dispH = std::max(1, static_cast<int>(maxDim / aspect));
    } else {
        dispH = maxDim;
        dispW = std::max(1, static_cast<int>(maxDim * aspect));
    }

    QImage img(dispW, dispH, QImage::Format_RGB32);

    if (bands == 1) {
        double minVal, maxVal;
        getBandRange(0, minVal, maxVal, 2.0, 98.0);
        double range = maxVal - minVal;
        if (range < 1e-10) range = 1;
        double scaleX = static_cast<double>(width) / dispW;
        double scaleY = static_cast<double>(height) / dispH;

        for (int dy = 0; dy < dispH; ++dy) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(dy));
            int srcY = std::min(static_cast<int>(dy * scaleY), height - 1);
            const double* src = &bandData[0][static_cast<size_t>(srcY) * static_cast<size_t>(width)];
            for (int dx = 0; dx < dispW; ++dx) {
                int srcX = std::min(static_cast<int>(dx * scaleX), width - 1);
                int gray = static_cast<int>((src[srcX] - minVal) / range * 255);
                gray = std::max(0, std::min(255, gray));
                line[dx] = qRgb(gray, gray, gray);
            }
        }
        return img;
    }

    int rB = 0, gB = std::min(1, bands - 1), bB = std::min(2, bands - 1);
    double rMin, rMax, gMin, gMax, bMin, bMax;
    getBandRange(rB, rMin, rMax, 2.0, 98.0);
    getBandRange(gB, gMin, gMax, 2.0, 98.0);
    getBandRange(bB, bMin, bMax, 2.0, 98.0);
    double rRange = rMax - rMin; if (rRange < 1e-10) rRange = 1;
    double gRange = gMax - gMin; if (gRange < 1e-10) gRange = 1;
    double bRange = bMax - bMin; if (bRange < 1e-10) bRange = 1;

    double scaleX = static_cast<double>(width) / dispW;
    double scaleY = static_cast<double>(height) / dispH;

    for (int dy = 0; dy < dispH; ++dy) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(dy));
        int srcY = std::min(static_cast<int>(dy * scaleY), height - 1);
        size_t rowOff = static_cast<size_t>(srcY) * static_cast<size_t>(width);
        const double* srcR = &bandData[rB][rowOff];
        const double* srcG = &bandData[gB][rowOff];
        const double* srcB = &bandData[bB][rowOff];
        for (int dx = 0; dx < dispW; ++dx) {
            int srcX = std::min(static_cast<int>(dx * scaleX), width - 1);
            int r = static_cast<int>((srcR[srcX] - rMin) / rRange * 255);
            int g = static_cast<int>((srcG[srcX] - gMin) / gRange * 255);
            int b = static_cast<int>((srcB[srcX] - bMin) / bRange * 255);
            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));
            line[dx] = qRgb(r, g, b);
        }
    }
    return img;
}

QImage GeoImageData::bandToQImage(int band, double minPercent, double maxPercent) const
{
    if (!isValid() || band < 0 || band >= bands) return QImage();

    QImage img(width, height, QImage::Format_RGB32);
    double minVal, maxVal;
    getBandRange(band, minVal, maxVal, minPercent, maxPercent);
    double range = maxVal - minVal;
    if (range < 1e-10) range = 1;

    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const double* src = &bandData[band][y * static_cast<size_t>(width)];
        for (int x = 0; x < width; ++x) {
            int gray = static_cast<int>((src[x] - minVal) / range * 255);
            gray = std::max(0, std::min(255, gray));
            line[x] = qRgb(gray, gray, gray);
        }
    }
    return img;
}

void GeoImageData::getBandRange(int band, double& minVal, double& maxVal,
                                 double minPercent, double maxPercent) const
{
    if (band < 0 || band >= bands) {
        minVal = 0; maxVal = 1;
        return;
    }

    const auto& data = bandData[band];
    if (data.empty()) {
        minVal = 0; maxVal = 1;
        return;
    }

    int n = static_cast<int>(data.size());

    // 快速路径：0%-100% 直接使用缓存（加载时已计算）
    if (minPercent <= 0.01 && maxPercent >= 99.99) {
        if (band < static_cast<int>(bandMins.size()) && band < static_cast<int>(bandMaxs.size())) {
            minVal = bandMins[band];
            maxVal = bandMaxs[band];
            return;
        }
        // 回退：无缓存时计算
        minVal = *std::min_element(data.begin(), data.end());
        maxVal = *std::max_element(data.begin(), data.end());
        return;
    }

    // 大图用系统采样估算百分位（避免全量拷贝）
    const int SAMPLE_SIZE = 200000;
    if (n > SAMPLE_SIZE) {
        std::vector<double> sample;
        sample.reserve(SAMPLE_SIZE);
        int step = std::max(1, n / SAMPLE_SIZE);
        for (int i = 0; i < n; i += step)
            sample.push_back(data[static_cast<size_t>(i)]);
        // 补上最后一个像素确保右尾
        if (sample.size() < static_cast<size_t>(SAMPLE_SIZE) && n > 0)
            sample.push_back(data[static_cast<size_t>(n - 1)]);

        std::sort(sample.begin(), sample.end());
        int sN = static_cast<int>(sample.size());
        int minIdx = std::max(0, std::min(sN - 1, static_cast<int>(sN * minPercent / 100.0)));
        int maxIdx = std::max(0, std::min(sN - 1, static_cast<int>(sN * maxPercent / 100.0)));
        minVal = sample[minIdx];
        maxVal = sample[maxIdx];
        return;
    }

    // 小图直接用全量排序
    std::vector<double> sorted = data;
    std::sort(sorted.begin(), sorted.end());

    int minIdx = static_cast<int>(n * minPercent / 100.0);
    int maxIdx = static_cast<int>(n * maxPercent / 100.0);
    minIdx = std::max(0, std::min(n - 1, minIdx));
    maxIdx = std::max(0, std::min(n - 1, maxIdx));

    minVal = sorted[minIdx];
    maxVal = sorted[maxIdx];
}

double GeoImageData::pixelValue(int band, int x, int y) const
{
    if (band < 0 || band >= bands || x < 0 || x >= width || y < 0 || y >= height)
        return 0;
    return bandData[band][static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

double GeoImageData::pixelValue(int band, int idx) const
{
    if (band < 0 || band >= bands || idx < 0 || idx >= pixelCount())
        return 0;
    return bandData[band][static_cast<size_t>(idx)];
}

const double* GeoImageData::rawBandData(int band) const
{
    if (band < 0 || band >= bands) return nullptr;
    return bandData[band].data();
}

std::vector<double> GeoImageData::pixelVector(int x, int y) const
{
    std::vector<double> vec(bands);
    for (int b = 0; b < bands; ++b)
        vec[b] = pixelValue(b, x, y);
    return vec;
}

std::vector<double> GeoImageData::pixelVector(int idx) const
{
    std::vector<double> vec(bands);
    for (int b = 0; b < bands; ++b)
        vec[b] = pixelValue(b, idx);
    return vec;
}

void GeoImageData::setPixelValue(int band, int x, int y, double val)
{
    if (band < 0 || band >= bands || x < 0 || x >= width || y < 0 || y >= height)
        return;
    bandData[band][static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = val;
}

QString GeoImageData::infoSummary() const
{
    if (!isValid()) return QString::fromUtf8("\u65E0\u5F71\u50CF");

    QString summary;
    summary += QString::fromUtf8("\u5F71\u50CF\u4FE1\u606F\u6458\u8981:\n");
    summary += QString::fromUtf8("  \u6587\u4EF6\u8DEF\u5F84: %1\n").arg(filePath);
    summary += QString::fromUtf8("  \u6570\u636E\u683C\u5F0F: %1\n").arg(imageFormat);
    summary += QString::fromUtf8("  \u5F71\u50CF\u5C3A\u5BF8: %1 x %2 \u50CF\u7D20\n").arg(width).arg(height);
    summary += QString::fromUtf8("  \u6CE2\u6BB5\u6570\u91CF: %1\n").arg(bands);
    summary += QString::fromUtf8("  \u4F4D\u6DF1: %1 bits\n").arg(bitDepth);
    summary += QString::fromUtf8("  \u603B\u50CF\u7D20\u6570: %L1\n").arg(pixelCount());

    // 文件大小
    if (fileSizeBytes >= 1024 * 1024 * 1024)
        summary += QString::fromUtf8("  \u6587\u4EF6\u5927\u5C0F: %1 GB\n").arg(fileSizeBytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    else if (fileSizeBytes >= 1024 * 1024)
        summary += QString::fromUtf8("  \u6587\u4EF6\u5927\u5C0F: %1 MB\n").arg(fileSizeBytes / (1024.0 * 1024), 0, 'f', 2);
    else if (fileSizeBytes >= 1024)
        summary += QString::fromUtf8("  \u6587\u4EF6\u5927\u5C0F: %1 KB\n").arg(fileSizeBytes / 1024.0, 0, 'f', 1);
    else
        summary += QString::fromUtf8("  \u6587\u4EF6\u5927\u5C0F: %1 B\n").arg(fileSizeBytes);

    // 分辨率
    if (pixelSizeX > 0 && pixelSizeY > 0)
        summary += QString::fromUtf8("  \u50CF\u7D20\u5206\u8FA8\u7387: %1 x %2 \u7C73/\u50CF\u7D20\n").arg(pixelSizeX, 0, 'f', 2).arg(pixelSizeY, 0, 'f', 2);

    // 坐标系统
    if (!projection.isEmpty())
        summary += QString::fromUtf8("  \u6295\u5F71\u7CFB\u7EDF: %1\n").arg(projection);
    if (!crs.isEmpty())
        summary += QString::fromUtf8("  \u5750\u6807\u53C2\u8003\u7CFB: %1\n").arg(crs);

    // 拍摄时间
    if (!captureTime.isEmpty())
        summary += QString::fromUtf8("  \u62CD\u6444\u65F6\u95F4: %1\n").arg(captureTime);

    // 波段名称
    if (!bandNames.empty()) {
        summary += QString::fromUtf8("  \u6CE2\u6BB5\u540D\u79F0: ");
        for (int i = 0; i < bands; ++i) {
            if (i > 0) summary += ", ";
            summary += bandNames[i];
        }
        summary += "\n";
    }

    return summary;
}

QImage ClassificationResult::toClassImage() const
{
    if (!isValid()) return QImage();

    QImage img(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int* src = &labelMap[static_cast<size_t>(y) * static_cast<size_t>(width)];
        for (int x = 0; x < width; ++x) {
            int label = src[x];
            if (label >= 0 && label < static_cast<int>(classColors.size()))
                line[x] = classColors[label].rgb();
            else
                line[x] = qRgb(0, 0, 0);
        }
    }
    return img;
}

QImage ClassificationResult::toThumbnailImage(int maxSize) const
{
    if (!isValid()) return QImage();

    // 如果原图已经足够小，直接用全分辨率
    if (width <= maxSize && height <= maxSize)
        return toClassImage();

    // 计算缩略图尺寸，保持宽高比
    int thumbW, thumbH;
    double aspect = static_cast<double>(width) / height;
    if (width >= height) {
        thumbW = maxSize;
        thumbH = qMax(1, static_cast<int>(maxSize / aspect));
    } else {
        thumbH = maxSize;
        thumbW = qMax(1, static_cast<int>(maxSize * aspect));
    }

    QImage img(thumbW, thumbH, QImage::Format_RGB32);

    double scaleX = static_cast<double>(width) / thumbW;
    double scaleY = static_cast<double>(height) / thumbH;

    for (int ty = 0; ty < thumbH; ++ty) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(ty));
        int srcY = qMin(static_cast<int>(ty * scaleY), height - 1);
        const int* src = &labelMap[static_cast<size_t>(srcY) * static_cast<size_t>(width)];

        for (int tx = 0; tx < thumbW; ++tx) {
            int srcX = qMin(static_cast<int>(tx * scaleX), width - 1);
            int label = src[srcX];
            if (label >= 0 && label < static_cast<int>(classColors.size()))
                line[tx] = classColors[label].rgb();
            else
                line[tx] = qRgb(0, 0, 0);
        }
    }
    return img;
}

QImage ClassificationResult::toProbabilityMap(int classIdx) const
{
    if (!isValid()) return QImage();

    QImage img(width, height, QImage::Format_RGB32);
    img.fill(qRgb(0, 0, 0));

    QRgb color = (classIdx >= 0 && classIdx < static_cast<int>(classColors.size()))
                     ? classColors[classIdx].rgb() : qRgb(255, 255, 255);

    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int* src = &labelMap[static_cast<size_t>(y) * static_cast<size_t>(width)];
        for (int x = 0; x < width; ++x) {
            if (src[x] == classIdx) {
                line[x] = color;
            }
        }
    }
    return img;
}

bool ClassificationResult::saveToCSV(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << "X,Y,ClassID,ClassName\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            int label = labelMap[idx];
            out << x << "," << y << "," << label << ","
                << (label >= 0 && label < classCount ? classNames[label] : "Unknown")
                << "\n";
        }
    }
    file.close();
    return true;
}

QString AccuracyMetrics::toReport() const
{
    return AccuracyAssessment::formatAccuracyReport(*this);
}
