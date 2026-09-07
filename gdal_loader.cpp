#include "gdal_loader.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDebug>
#include <QtGui/QImage>
#include <QtGui/QPixmap>

#include "gdal_priv.h"
#include "gdal.h"
#include "cpl_conv.h"

GdalImageLoader::GdalImageLoader() {}
GdalImageLoader::~GdalImageLoader() {}

// 初始化 GDAL - 注册所有驱动
void GdalImageLoader::initialize()
{
    GDALAllRegister();
    CPLSetConfigOption("GDAL_PAM_ENABLED", "NO");
    CPLSetConfigOption("GDAL_MAX_DATASET_POOL_SIZE", "200");
}

// 清理 GDAL 资源
void GdalImageLoader::cleanup()
{
    GDALDestroyDriverManager();
}

// 检查 GDAL 是否支持打开该文件
bool GdalImageLoader::canOpen(const QString &filePath)
{
    GDALDatasetH hDS = GDALOpen(filePath.toUtf8().constData(), GA_ReadOnly);
    if (hDS) { GDALClose(hDS); return true; }
    return false;
}

// 读取图像元数据（尺寸/波段/投影等）
GdalImageInfo GdalImageLoader::info(const QString &filePath)
{
    GdalImageInfo result;
    result.filePath = filePath;
    GDALDataset *poDS = openDataset(filePath);
    if (!poDS) return result;

    result.rasterCountX = poDS->GetRasterXSize();
    result.rasterCountY = poDS->GetRasterYSize();
    result.bandCount = poDS->GetRasterCount();

    if (poDS->GetGeoTransform(result.geoTransform) != CE_None)
    {
        for (int i = 0; i < 6; i++) result.geoTransform[i] = (i == 1 || i == 5) ? 1.0 : 0.0;
    }
    const char *pszProj = poDS->GetProjectionRef();
    if (pszProj) result.projection = QString::fromUtf8(pszProj);

    GDALDriver *poDriver = poDS->GetDriver();
    if (poDriver)
    {
        result.driverName = QString::fromUtf8(poDriver->GetDescription());
        const char *pszLong = poDriver->GetMetadataItem(GDAL_DMD_LONGNAME);
        if (pszLong) result.driverLongName = QString::fromUtf8(pszLong);
    }

    for (int i = 1; i <= result.bandCount; i++)
    {
        GDALRasterBand *poBand = poDS->GetRasterBand(i);
        if (!poBand) continue;
        GdalBandInfo bi;
        bi.bandIndex = i;
        const char *desc = poBand->GetDescription();
        if (desc) bi.description = QString::fromUtf8(desc);
        bi.dataType = poBand->GetRasterDataType();
        poBand->GetBlockSize(&bi.blockXSize, &bi.blockYSize);
        int bOk = FALSE;
        bi.noDataValue = poBand->GetNoDataValue(&bOk);
        bi.hasNoData = (bOk != FALSE);
        result.bands.append(bi);
    }

    char **papszMD = poDS->GetMetadata();
    if (papszMD)
    {
        for (int i = 0; papszMD[i]; i++)
        {
            QString entry = QString::fromUtf8(papszMD[i]);
            int eq = entry.indexOf('=');
            if (eq > 0) result.metadata[entry.left(eq)] = entry.mid(eq + 1);
        }
    }
    GDALClose(poDS);
    return result;
}

// 读取多波段图像为 RGB QImage
QImage GdalImageLoader::readImage(const QString &filePath,
                                   const QList<int> &bandSelection,
                                   QSize scaledSize)
{
    GDALDataset *poDS = openDataset(filePath);
    if (!poDS) return QImage();
    QImage result = datasetToQImage(poDS, bandSelection, scaledSize);
    GDALClose(poDS);
    return result;
}

// 读取指定波段为灰度图
QImage GdalImageLoader::readBand(const QString &filePath, int bandIndex, QSize scaledSize)
{
    GDALDataset *poDS = openDataset(filePath);
    if (!poDS) return QImage();
    if (bandIndex < 1 || bandIndex > poDS->GetRasterCount())
    {
        GDALClose(poDS);
        return QImage();
    }

    GDALRasterBand *poBand = poDS->GetRasterBand(bandIndex);
    if (!poBand) { GDALClose(poDS); return QImage(); }

    int nXSize = poDS->GetRasterXSize();
    int nYSize = poDS->GetRasterYSize();
    int outW = nXSize, outH = nYSize;
    if (scaledSize.width() > 0 && scaledSize.height() > 0)
    {
        double aspect = (double)nXSize / nYSize;
        if (scaledSize.width() / (double)scaledSize.height() > aspect)
            { outH = qMax(1, qMin(scaledSize.height(), 1024)); outW = qMax(1, (int)(outH * aspect)); }
        else
            { outW = qMax(1, qMin(scaledSize.width(), 1024)); outH = qMax(1, (int)(outW / aspect)); }
    }
    else if (outW > 1024 || outH > 1024)
    {
        outH = qMax(1, qMin(outH, 1024));
        outW = qMax(1, (int)(outH * (double)nXSize / nYSize));
    }

    float *pBuf = new float[outW * outH];
    CPLErr err = poBand->RasterIO(GF_Read, 0, 0, nXSize, nYSize, pBuf, outW, outH, GDT_Float32, 0, 0);
    if (err != CE_None) { delete[] pBuf; GDALClose(poDS); return QImage(); }

    float fMin = 1e30f, fMax = -1e30f;
    int nPixels = outW * outH;
    for (int i = 0; i < nPixels; i++) { float v = pBuf[i]; if (v < fMin) fMin = v; if (v > fMax) fMax = v; }
    float fRange = fMax - fMin; if (fRange <= 0) fRange = 1.0f;

    QImage image(outW, outH, QImage::Format_Grayscale8);
    for (int y = 0; y < outH; y++)
    {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < outW; x++)
        {
            float v = (pBuf[y * outW + x] - fMin) / fRange * 255.0f;
            line[x] = (uchar)qBound(0.0f, v, 255.0f);
        }
    }
    delete[] pBuf;
    GDALClose(poDS);
    return image;
}

// 生成预览缩略图（使用金字塔概览加速）
QPixmap GdalImageLoader::preview(const QString &filePath, QSize maxSize)
{
    QImage img = readImage(filePath, QList<int>(), maxSize);
    return img.isNull() ? QPixmap() : QPixmap::fromImage(img);
}

// 返回 GDAL 版本字符串
QString GdalImageLoader::version()
{
    return QString::fromUtf8(GDALVersionInfo("RELEASE_NAME"));
}

// GDAL 支持的格式过滤器（用于 QFileDialog）
QString GdalImageLoader::gdalFormatFilter()
{
    QStringList formats;
    formats << QString::fromUtf8("GeoTIFF (*.tif *.tiff)")
            << QString::fromUtf8("ENVI (*.dat *.img *.hdr)")
            << QString::fromUtf8("HDF (*.hdf *.h5 *.hdf5 *.he5)")
            << QString::fromUtf8("ERDAS Imagine (*.img)")
            << QString::fromUtf8("JPEG2000 (*.jp2 *.j2k)")
            << QString::fromUtf8("NetCDF (*.nc)");
    return formats.join(";;");
}

// 打开 GDAL 数据集（内部辅助）
GDALDataset* GdalImageLoader::openDataset(const QString &filePath)
{
    return (GDALDataset*)GDALOpen(filePath.toUtf8().constData(), GA_ReadOnly);
}

QImage GdalImageLoader::datasetToQImage(GDALDataset *dataset,
                                         const QList<int> &bandSelection,
                                         QSize scaledSize)
{
    if (!dataset) return QImage();

    int nXSize = dataset->GetRasterXSize();
    int nYSize = dataset->GetRasterYSize();
    int nBands = dataset->GetRasterCount();
    if (nBands == 0) return QImage();

    // Determine bands for RGB
    QList<int> bands = bandSelection;
    if (bands.isEmpty())
    {
        int c = qMin(nBands, 3);
        for (int i = 1; i <= c; i++) bands.append(i);
    }
    int nSrcBands = bands.size();

    // Calculate output size (use GDAL's native RasterIO resampling)
    const int MAX_PREVIEW = 4096;
    int outW = nXSize, outH = nYSize;
    if (scaledSize.width() > 0 && scaledSize.height() > 0)
    {
        double aspect = (double)nXSize / nYSize;
        if (scaledSize.width() / (double)scaledSize.height() > aspect)
            { outH = qMax(1, qMin(scaledSize.height(), MAX_PREVIEW)); outW = qMax(1, (int)(outH * aspect)); }
        else
            { outW = qMax(1, qMin(scaledSize.width(), MAX_PREVIEW)); outH = qMax(1, (int)(outW / aspect)); }
    }
    else
    {
        if (outW > MAX_PREVIEW || outH > MAX_PREVIEW)
        {
            double aspect = (double)nXSize / nYSize;
            if (nXSize >= nYSize)
                { outW = MAX_PREVIEW; outH = qMax(1, (int)(MAX_PREVIEW / aspect)); }
            else
                { outH = MAX_PREVIEW; outW = qMax(1, (int)(MAX_PREVIEW * aspect)); }
        }
    }

    if (outW <= 0 || outH <= 0) return QImage();

    // --- Step 1: Sample full-resolution data for accurate min/max ---
    int sampleStep = qMax(1, nYSize / 200);
    int colStep = qMax(1, nXSize / 200);

    float bandMin[4], bandMax[4];
    for (int b = 0; b < 4; b++) { bandMin[b] = 1e30f; bandMax[b] = -1e30f; }

    float *sampleRow = new float[nXSize];
    for (int b = 0; b < nSrcBands; b++)
    {
        GDALRasterBand *poBand = dataset->GetRasterBand(bands[b]);
        if (!poBand) continue;

        int bOk = FALSE;
        double ndv = poBand->GetNoDataValue(&bOk);
        bool hasNdv = (bOk != FALSE);

        for (int row = 0; row < nYSize; row += sampleStep)
        {
            poBand->RasterIO(GF_Read, 0, row, nXSize, 1, sampleRow, nXSize, 1, GDT_Float32, 0, 0);
            for (int col = 0; col < nXSize; col += colStep)
            {
                float v = sampleRow[col];
                if (hasNdv && qFuzzyCompare((double)v, ndv)) continue;
                if (v < bandMin[b]) bandMin[b] = v;
                if (v > bandMax[b]) bandMax[b] = v;
            }
        }
    }
    delete[] sampleRow;

    // Ensure valid ranges
    for (int b = 0; b < nSrcBands; b++)
    {
        if (bandMin[b] >= bandMax[b])
            { bandMin[b] = 0.0f; bandMax[b] = 255.0f; }
    }
    for (int b = nSrcBands; b < 3; b++)
        { bandMin[b] = bandMin[0]; bandMax[b] = bandMax[0]; }

    // --- Read all bands at target resolution using GDAL's native resampling ---
    float **ppData = new float*[nSrcBands];
    for (int b = 0; b < nSrcBands; b++)
        ppData[b] = new float[outW * outH];

    bool hasValidData = false;
    for (int b = 0; b < nSrcBands; b++)
    {
        GDALRasterBand *poBand = dataset->GetRasterBand(bands[b]);
        if (!poBand) { memset(ppData[b], 0, outW * outH * sizeof(float)); continue; }

        // Use overview only if it covers target size (avoid upscaling blur)
        int nOv = poBand->GetOverviewCount();
        int bestOv = -1;
        int bestOvW = 0;
        for (int o = 0; o < nOv; o++)
        {
            GDALRasterBand *ovBand = poBand->GetOverview(o);
            if (!ovBand) continue;
            int ovW = ovBand->GetXSize();
            // Pick the smallest overview that is still >= output width
            if (ovW >= outW && (bestOv < 0 || ovW < bestOvW))
                { bestOv = o; bestOvW = ovW; }
        }

        if (bestOv >= 0)
        {
            GDALRasterBand *ovBand = poBand->GetOverview(bestOv);
            int ovW = ovBand->GetXSize();
            int ovH = ovBand->GetYSize();
            ovBand->RasterIO(GF_Read, 0, 0, ovW, ovH,
                             ppData[b], outW, outH, GDT_Float32, 0, 0);
        }
        else
        {
            // Fallback to GDAL resampling from full resolution
            poBand->RasterIO(GF_Read, 0, 0, nXSize, nYSize,
                             ppData[b], outW, outH, GDT_Float32, 0, 0);
        }
        hasValidData = true;
    }

    if (!hasValidData)
    {
        for (int b = 0; b < nSrcBands; b++) delete[] ppData[b];
        delete[] ppData;
        return QImage();
    }

    // --- Build ARGB32 QImage with min-max stretch (ranges from full-res sampling) ---
    QImage image(outW, outH, QImage::Format_ARGB32);
    image.fill(0xFF000000);

    for (int y = 0; y < outH; y++)
    {
        uchar *line = image.scanLine(y);
        int rowOff = y * outW;

        for (int x = 0; x < outW; x++)
        {
            int idx = rowOff + x;

            float r = ppData[0][idx];
            float g = (nSrcBands >= 2) ? ppData[1][idx] : r;
            float b = (nSrcBands >= 3) ? ppData[2][idx] : g;

            float rangeR = bandMax[0] - bandMin[0]; if (rangeR <= 0) rangeR = 255.0f;
            float rangeG = bandMax[1] - bandMin[1]; if (rangeG <= 0) rangeG = 255.0f;
            float rangeB = bandMax[2] - bandMin[2]; if (rangeB <= 0) rangeB = 255.0f;

            int offset = x * 4;
            line[offset + 0] = (uchar)qBound(0.0f, (b - bandMin[2]) / rangeB * 255.0f, 255.0f);
            line[offset + 1] = (uchar)qBound(0.0f, (g - bandMin[1]) / rangeG * 255.0f, 255.0f);
            line[offset + 2] = (uchar)qBound(0.0f, (r - bandMin[0]) / rangeR * 255.0f, 255.0f);
            line[offset + 3] = 255;
        }
    }

    for (int b = 0; b < nSrcBands; b++) delete[] ppData[b];
    delete[] ppData;
    return image;
}
