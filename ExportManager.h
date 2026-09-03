#pragma once

#include "GeoImageData.h"
#include "LandCoverExtractor.h"
#include <QObject>

enum class ExportFormat
{
    GeoTIFF,
    Shapefile,
    CSV,
    ENVI_BSQ
};

struct ExportConfig
{
    ExportFormat format = ExportFormat::CSV;
    QString outputPath;
    QString projection;
    double geoTransform[6] = {0, 1, 0, 0, 0, -1};
    bool includeMetadata = true;
    bool compressOutput = false;
    bool createWorldFile = true;
};

class ExportManager : public QObject
{
    Q_OBJECT

public:
    explicit ExportManager(QObject* parent = nullptr);
    ~ExportManager();

    void setConfig(const ExportConfig& config);
    ExportConfig config() const { return m_config; }

    bool exportClassificationResult(const ClassificationResult& result);

    bool exportToGeoTIFF(const ClassificationResult& result);
    bool exportToShapefile(const ClassificationResult& result);
    bool exportToCSV(const ClassificationResult& result);
    bool exportToENVI(const ClassificationResult& result);

    // 地物提取结果导出
    bool exportExtractionResult(const ExtractionResult& result);
    bool exportExtractionToGeoTIFF(const ExtractionResult& result);
    bool exportExtractionToShapefile(const ExtractionResult& result);
    bool exportExtractionToCSV(const ExtractionResult& result);

    bool exportAccuracyReport(const AccuracyMetrics& metrics, const QString& filePath);

    bool exportGeoImage(const GeoImageData& image, const QString& filePath);

    static bool writeWorldFile(const QString& tiffPath, const double geoTransform[6]);

signals:
    void progressUpdated(int percent);
    void statusMessage(const QString& msg);
    void exportFinished(const QString& filePath);
    void exportError(const QString& errorMsg);

private:
    ExportConfig m_config;

    bool writeGeoTIFFRaw(const QString& path, const std::vector<int>& data,
                         int width, int height, int bands,
                         const std::vector<QString>& classNames,
                         const std::vector<QColor>& colors);

    bool writeCSVFile(const QString& path, const ClassificationResult& result);

    bool writeENVIFile(const QString& path, const ClassificationResult& result);

    bool writeShapefileRaw(const QString& path, const ClassificationResult& result);

    bool writeGeoTIFFWithGDAL(const QString& path, const ClassificationResult& result);
    bool writeShapefileWithGDAL(const QString& path, const ClassificationResult& result);

    bool gdalAvailable();
};