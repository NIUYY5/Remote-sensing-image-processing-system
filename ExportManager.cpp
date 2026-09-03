#include "ExportManager.h"
#include "AccuracyAssessment.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QRegExp>
#include <fstream>

ExportManager::ExportManager(QObject* parent)
    : QObject(parent)
{
}

ExportManager::~ExportManager()
{
}

void ExportManager::setConfig(const ExportConfig& config)
{
    m_config = config;
}

bool ExportManager::exportClassificationResult(const ClassificationResult& result)
{
    if (!result.isValid()) {
        emit exportError(QString::fromUtf8("\u5206\u7C7B\u7ED3\u679C\u65E0\u6548"));
        return false;
    }

    switch (m_config.format) {
    case ExportFormat::GeoTIFF:
        return exportToGeoTIFF(result);
    case ExportFormat::Shapefile:
        return exportToShapefile(result);
    case ExportFormat::CSV:
        return exportToCSV(result);
    case ExportFormat::ENVI_BSQ:
        return exportToENVI(result);
    default:
        return exportToCSV(result);
    }
}

bool ExportManager::exportToGeoTIFF(const ClassificationResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA GeoTIFF..."));

    if (gdalAvailable()) {
        return writeGeoTIFFWithGDAL(m_config.outputPath, result);
    }

    return writeGeoTIFFRaw(m_config.outputPath, result.labelMap,
                           result.width, result.height, 1,
                           result.classNames, result.classColors);
}

bool ExportManager::exportToShapefile(const ClassificationResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA Shapefile..."));

    if (gdalAvailable()) {
        return writeShapefileWithGDAL(m_config.outputPath, result);
    }

    return writeShapefileRaw(m_config.outputPath, result);
}

bool ExportManager::exportToCSV(const ClassificationResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA CSV..."));

    bool ok = writeCSVFile(m_config.outputPath, result);
    if (ok) {
        emit exportFinished(m_config.outputPath);
        emit progressUpdated(100);
    }
    return ok;
}

bool ExportManager::exportToENVI(const ClassificationResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA ENVI..."));

    bool ok = writeENVIFile(m_config.outputPath, result);
    if (ok) {
        emit exportFinished(m_config.outputPath);
        emit progressUpdated(100);
    }
    return ok;
}

bool ExportManager::exportAccuracyReport(const AccuracyMetrics& metrics, const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u521B\u5EFA\u6587\u4EF6: ") + filePath);
        return false;
    }

    QTextStream out(&file);
    out << AccuracyAssessment::formatAccuracyReport(metrics);
    out << "\nHTML Report:\n";
    out << AccuracyAssessment::confusionMatrixToHTML(metrics);
    file.close();

    emit exportFinished(filePath);
    return true;
}

bool ExportManager::exportGeoImage(const GeoImageData& image, const QString& filePath)
{
    QImage qimg = image.toQImage(0, 1, 2);
    if (qimg.isNull()) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u8F6C\u6362\u56FE\u50CF"));
        return false;
    }

    if (!qimg.save(filePath)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u4FDD\u5B58\u56FE\u50CF: ") + filePath);
        return false;
    }

    if (m_config.createWorldFile) {
        writeWorldFile(filePath, m_config.geoTransform);
    }

    emit exportFinished(filePath);
    return true;
}

bool ExportManager::writeWorldFile(const QString& tiffPath, const double geoTransform[6])
{
    QFileInfo fi(tiffPath);
    QString suffix = fi.suffix().toLower();
    QString ext;
    if (suffix == "tif" || suffix == "tiff")
        ext = ".tfw";
    else if (suffix == "jpg" || suffix == "jpeg")
        ext = ".jgw";
    else if (suffix == "png")
        ext = ".pgw";
    else
        ext = ".wld";

    QString worldPath = fi.absolutePath() + "/" + fi.completeBaseName() + ext;

    QFile file(worldPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setRealNumberPrecision(12);
    out << geoTransform[1] << "\n";
    out << geoTransform[2] << "\n";
    out << geoTransform[4] << "\n";
    out << geoTransform[5] << "\n";
    out << geoTransform[0] + geoTransform[1] * 0.5 << "\n";
    out << geoTransform[3] + geoTransform[5] * 0.5 << "\n";
    file.close();
    return true;
}

bool ExportManager::writeGeoTIFFRaw(const QString& path, const std::vector<int>& data,
                                     int width, int height, int /*bands*/,
                                     const std::vector<QString>& classNames,
                                     const std::vector<QColor>& colors)
{
    QImage img(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int* src = &data[y * width];
        for (int x = 0; x < width; ++x) {
            int label = src[x];
            if (label >= 0 && label < static_cast<int>(colors.size()))
                line[x] = colors[label].rgb();
            else
                line[x] = qRgb(0, 0, 0);
        }
    }

    if (!img.save(path)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u4FDD\u5B58 GeoTIFF: ") + path);
        return false;
    }

    if (m_config.createWorldFile)
        writeWorldFile(path, m_config.geoTransform);

    emit exportFinished(path);
    emit progressUpdated(100);
    return true;
}

bool ExportManager::writeCSVFile(const QString& path, const ClassificationResult& result)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u521B\u5EFA CSV: ") + path);
        return false;
    }

    QTextStream out(&file);
    out << "X,Y,ClassID,ClassName\n";

    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            int idx = y * result.width + x;
            int label = result.labelMap[idx];
            out << x << "," << y << "," << label << ","
                << (label >= 0 && label < result.classCount ? result.classNames[label] : "Unknown")
                << "\n";
        }
        if (y % 100 == 0)
            emit progressUpdated((y * 100) / result.height);
    }

    file.close();
    emit exportFinished(path);
    emit progressUpdated(100);
    return true;
}

bool ExportManager::writeENVIFile(const QString& path, const ClassificationResult& result)
{
    QString basePath = path;
    if (basePath.endsWith(".hdr"))
        basePath = basePath.left(basePath.length() - 4);
    if (basePath.endsWith(".dat"))
        basePath = basePath.left(basePath.length() - 4);

    QString dataPath = basePath + ".dat";
    QString hdrPath = basePath + ".hdr";

    std::ofstream dataFile(dataPath.toStdString(), std::ios::binary);
    if (!dataFile.is_open()) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u521B\u5EFA ENVI\u6570\u636E\u6587\u4EF6: ") + dataPath);
        return false;
    }

    for (int i = 0; i < static_cast<int>(result.labelMap.size()); ++i) {
        short val = static_cast<short>(result.labelMap[i]);
        dataFile.write(reinterpret_cast<const char*>(&val), sizeof(short));
    }
    dataFile.close();

    QFile hdrFile(hdrPath);
    if (hdrFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&hdrFile);
        out << "ENVI\n";
        out << "description = {Classification Result}\n";
        out << "samples = " << result.width << "\n";
        out << "lines = " << result.height << "\n";
        out << "bands = 1\n";
        out << "header offset = 0\n";
        out << "file type = ENVI Standard\n";
        out << "data type = 2\n";
        out << "interleave = bsq\n";
        out << "byte order = 0\n";
        out << "class names = {";
        for (int i = 0; i < result.classCount; ++i) {
            if (i > 0) out << ", ";
            out << result.classNames[i];
        }
        out << "}\n";
        hdrFile.close();
    }

    emit exportFinished(basePath);
    emit progressUpdated(100);
    return true;
}

bool ExportManager::writeShapefileRaw(const QString& path, const ClassificationResult& result)
{
    QString csvPath = path;
    if (csvPath.endsWith(".shp", Qt::CaseInsensitive))
        csvPath = csvPath.left(csvPath.length() - 4) + "_polygons.csv";

    if (!writeCSVFile(csvPath, result)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u5BFC\u51FA Shapefile \u66FF\u4EE3\u6587\u4EF6"));
        return false;
    }

    emit statusMessage(QString::fromUtf8("\u5F53\u524D\u4F7F\u7528 CSV \u66FF\u4EE3 Shapefile\uFF0C\u8BF7\u5B89\u88C5 GDAL \u4EE5\u652F\u6301\u5B8C\u6574\u529F\u80FD"));
    emit exportFinished(csvPath);
    return true;
}

bool ExportManager::writeGeoTIFFWithGDAL(const QString& path, const ClassificationResult& result)
{
    emit statusMessage(QString::fromUtf8("\u5F53\u524D\u4F7F\u7528\u5185\u7F6E\u5199\u5165\u5668\u5BFC\u51FA GeoTIFF"));
    return writeGeoTIFFRaw(path, result.labelMap, result.width, result.height, 1,
                           result.classNames, result.classColors);
}

bool ExportManager::writeShapefileWithGDAL(const QString& path, const ClassificationResult& result)
{
    emit statusMessage(QString::fromUtf8("\u5F53\u524D\u4F7F\u7528 CSV \u66FF\u4EE3 Shapefile\u5BFC\u51FA"));
    return writeShapefileRaw(path, result);
}

bool ExportManager::gdalAvailable()
{
    return false;
}

// ========== 地物提取结果导出 ==========

bool ExportManager::exportExtractionResult(const ExtractionResult& result)
{
    if (!result.isValid()) {
        emit exportError(QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u7ED3\u679C\u65E0\u6548"));
        return false;
    }

    switch (m_config.format) {
    case ExportFormat::GeoTIFF:
        return exportExtractionToGeoTIFF(result);
    case ExportFormat::Shapefile:
        return exportExtractionToShapefile(result);
    case ExportFormat::CSV:
        return exportExtractionToCSV(result);
    default:
        return exportExtractionToCSV(result);
    }
}

bool ExportManager::exportExtractionToGeoTIFF(const ExtractionResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA\u5730\u7269\u63D0\u53D6\u7ED3\u679C GeoTIFF..."));

    // 生成综合分类图
    QImage img = result.toCombinedImage();
    if (img.isNull()) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u751F\u6210\u5730\u7269\u63D0\u53D6\u56FE\u50CF"));
        return false;
    }

    if (!img.save(m_config.outputPath)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u4FDD\u5B58 GeoTIFF: ") + m_config.outputPath);
        return false;
    }

    if (m_config.createWorldFile)
        writeWorldFile(m_config.outputPath, m_config.geoTransform);

    // 导出图例元数据
    QString legendPath = m_config.outputPath;
    legendPath.replace(QRegExp("\\.(tif|tiff)$", Qt::CaseInsensitive), "_legend.txt");
    QFile legendFile(legendPath);
    if (legendFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&legendFile);
        out << QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u56FE\u4F8B\n");
        out << QString(40, QChar('=')) << "\n";
        for (const auto& r : result.results) {
            out << QString::fromUtf8("%1 - %2 \u50CF\u7D20 (%3%)\n")
                       .arg(r.typeName)
                       .arg(r.pixelCount)
                       .arg(r.coveragePercent, 0, 'f', 1);
        }
        legendFile.close();
    }

    emit exportFinished(m_config.outputPath);
    emit progressUpdated(100);
    return true;
}

bool ExportManager::exportExtractionToShapefile(const ExtractionResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA\u5730\u7269\u63D0\u53D6\u7ED3\u679C Shapefile..."));

    // 使用CSV替代Shapefile（无GDAL环境）
    QString csvPath = m_config.outputPath;
    if (csvPath.endsWith(".shp", Qt::CaseInsensitive))
        csvPath = csvPath.left(csvPath.length() - 4) + "_extraction.csv";

    return exportExtractionToCSV(result);
}

bool ExportManager::exportExtractionToCSV(const ExtractionResult& result)
{
    emit statusMessage(QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA\u5730\u7269\u63D0\u53D6\u7ED3\u679C CSV..."));

    QFile file(m_config.outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportError(QString::fromUtf8("\u65E0\u6CD5\u521B\u5EFA CSV: ") + m_config.outputPath);
        return false;
    }

    QTextStream out(&file);
    // CSV头
    out << "X,Y,LandCoverType,TypeIndex,Confidence\n";

    int total = result.width * result.height;
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            int idx = y * result.width + x;
            int lbl = result.combinedLabelMap[idx];
            if (lbl >= 0 && lbl < static_cast<int>(result.results.size())) {
                const auto& r = result.results[lbl];
                if (r.mask[idx] == 1) {
                    out << x << "," << y << ","
                        << r.typeName << ","
                        << lbl << ","
                        << QString::number(r.confidence[idx], 'f', 4) << "\n";
                }
            }
        }
        if (y % 100 == 0)
            emit progressUpdated((y * 100) / result.height);
    }

    file.close();
    emit exportFinished(m_config.outputPath);
    emit progressUpdated(100);
    return true;
}