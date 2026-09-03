#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtCore/QMap>

// GDAL forward declarations (avoid including full GDAL headers in header)
class GDALDataset;
typedef void* GDALRasterBandH;

struct GdalBandInfo {
    int bandIndex;       // 1-based GDAL band index
    QString description; // band description from metadata
    int dataType;        // GDALDataType enum value
    int blockXSize;      // block width
    int blockYSize;      // block height
    double noDataValue;  // NoData value
    bool hasNoData;
};

struct GdalImageInfo {
    QString filePath;
    int rasterCountX;    // cols
    int rasterCountY;    // rows
    int bandCount;       // number of bands
    double geoTransform[6];  // affine transform
    QString projection;       // WKT projection string
    QString driverName;       // GDAL driver short name
    QString driverLongName;   // GDAL driver long name
    QList<GdalBandInfo> bands;
    QMap<QString, QString> metadata;

    bool isValid() const { return rasterCountX > 0 && rasterCountY > 0; }
};

class GdalImageLoader
{
public:
    GdalImageLoader();
    ~GdalImageLoader();

    // Initialize/cleanup GDAL
    static void initialize();
    static void cleanup();

    // Check if a file can be opened by GDAL
    static bool canOpen(const QString &filePath);

    // Open and get image metadata
    static GdalImageInfo info(const QString &filePath);

    // Read full image as QImage (RGB/RGBA)
    // bandSelection: 1-based band indices for R,G,B(,A), empty = first 3 bands
    static QImage readImage(const QString &filePath,
                            const QList<int> &bandSelection = QList<int>(),
                            QSize scaledSize = QSize());

    // Read a single band as grayscale QImage
    static QImage readBand(const QString &filePath, int bandIndex,
                           QSize scaledSize = QSize());

    // Generate quick preview thumbnail using overviews or downsampling
    static QPixmap preview(const QString &filePath, QSize maxSize = QSize(1024, 1024));

    // Get GDAL version string
    static QString version();

    // Get supported GDAL format filters for QFileDialog
    static QString gdalFormatFilter();

private:
    static GDALDataset* openDataset(const QString &filePath);
    static QImage datasetToQImage(GDALDataset *dataset,
                                  const QList<int> &bandSelection,
                                  QSize scaledSize);
};
