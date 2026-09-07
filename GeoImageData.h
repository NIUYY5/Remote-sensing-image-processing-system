#pragma once

#include <vector>
#include <functional>
#include <QString>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QDateTime>

struct GeoImageData
{
    int width = 0;
    int height = 0;
    int bands = 0;
    std::vector<std::vector<double>> bandData;
    std::vector<QString> bandNames;
    QString filePath;
    QString projection;
    double geoTransform[6] = {0, 1, 0, 0, 0, -1};
    double noDataValue = 0;

    // 缓存：各波段纯 min/max（加载时自动计算）
    mutable std::vector<double> bandMins;
    mutable std::vector<double> bandMaxs;

    // 影像元数据
    QString imageFormat;          // 数据格式 (PNG/JPEG/TIFF等)
    qint64 fileSizeBytes = 0;     // 文件大小 (字节)
    double pixelSizeX = 0;        // 像素分辨率 X (米/像素)
    double pixelSizeY = 0;        // 像素分辨率 Y (米/像素)
    int bitDepth = 8;             // 位深 (bits per band)
    QString crs;                  // 坐标参考系统 (WKT/PROJ)
    QString captureTime;          // 拍摄时间 (如果可获取)

    GeoImageData() = default;

    // 进度回调：percent 0-100, stageName 当前阶段描述
    using ProgressCallback = std::function<void(int percent, const QString& stageName)>;

    bool loadFromImage(const QString& path, ProgressCallback progressCb = nullptr);
    bool loadDownsampled(const QString& path, long long maxPixels, ProgressCallback progressCb = nullptr);
    bool loadMultiBand(const QStringList& bandFiles, ProgressCallback progressCb = nullptr);
    QImage toQImage(int rBand = 0, int gBand = 1, int bBand = 2, double minPercent = 2.0, double maxPercent = 98.0) const;
    QImage toDisplayImage(int maxDim = 4096) const;
    QImage bandToQImage(int band, double minPercent = 2.0, double maxPercent = 98.0) const;
    void getBandRange(int band, double& minVal, double& maxVal, double minPercent = 2.0, double maxPercent = 98.0) const;
    bool isValid() const { return width > 0 && height > 0 && bands > 0 && !bandData.empty(); }
    int pixelCount() const { return width * height; }
    double pixelValue(int band, int x, int y) const;
    double pixelValue(int band, int idx) const;
    const double* rawBandData(int band) const;
    std::vector<double> pixelVector(int x, int y) const;
    std::vector<double> pixelVector(int idx) const;
    void setPixelValue(int band, int x, int y, double val);

    // 生成影像信息摘要
    QString infoSummary() const;
};

struct ClassificationResult
{
    int width = 0;
    int height = 0;
    int classCount = 0;
    std::vector<int> labelMap;
    std::vector<QString> classNames;
    std::vector<QColor> classColors;
    QString methodName;
    QString sourceImagePath;
    double overallAccuracy = -1;
    double kappaCoefficient = -1;

    QImage toClassImage() const;
    QImage toThumbnailImage(int maxSize = 4096) const;
    QImage toProbabilityMap(int classIdx) const;
    bool saveToCSV(const QString& path) const;
    bool isValid() const { return width > 0 && height > 0 && !labelMap.empty(); }
};

struct TrainingSample
{
    QPoint position;
    int classId;
    std::vector<double> features;
};

struct TrainingData
{
    int classCount = 0;
    int featureDim = 0;
    std::vector<QString> classNames;
    std::vector<TrainingSample> samples;
    std::vector<std::vector<double>> classStatistics;
    std::vector<std::vector<double>> classCovariance;

    bool isValid() const { return classCount > 0 && featureDim > 0 && !samples.empty(); }
    int sampleCount() const { return static_cast<int>(samples.size()); }
};

struct AccuracyMetrics
{
    int totalSamples = 0;
    int correctCount = 0;
    double overallAccuracy = 0;
    double kappaCoefficient = 0;
    std::vector<std::vector<int>> confusionMatrix;
    std::vector<double> producerAccuracy;      // PA = 召回率 (Recall)
    std::vector<double> userAccuracy;           // UA = 精确率 (Precision)
    std::vector<double> f1Scores;               // 每类 F1 分数
    double macroF1 = 0;                         // 宏平均 F1
    std::vector<int> classTotalReference;
    std::vector<int> classTotalClassified;
    std::vector<QString> classNames;

    QString toReport() const;
};

// ===== 人工判读与精度评定数据结构 =====

// 单个标注点的判读记录
struct AnnotationRecord
{
    QPoint position;                // 像素坐标
    int autoLabel = -1;            // 自动分类/聚类结果标签
    int manualLabel = -1;          // 人工判读修正标签（-1表示未修正）
    bool confirmed = false;        // 是否已确认
    QString comment;               // 人工注释/备注
    QString marker;                // 标注人标识
    QDateTime editTime;            // 最后编辑时间
    int version = 1;               // 该标注点的修改版本号

    //  有效标签（人工优先，否则自动）
    int effectiveLabel() const { return manualLabel >= 0 ? manualLabel : autoLabel; }
    //  是否为已修正点
    bool isOverridden() const { return manualLabel >= 0 && manualLabel != autoLabel; }
};

// 判读会话的完整快照（用于历史记录）
struct AnnotationSession
{
    QString sessionId;                        // 唯一会话标识 (UUID格式)
    QString name;                             // 会话名称
    QDateTime createTime;                     // 创建时间
    QDateTime lastModified;                   // 最后修改时间
    QString description;                      // 会话描述
    std::vector<AnnotationRecord> records;    // 全部标注记录
    std::vector<QString> classNames;          // 类别名称列表
    QString sourceResultPath;                 // 源分类结果路径
    AccuracyMetrics currentMetrics;           // 当前判读计算的指标

    // 按确认状态统计
    int confirmedCount() const {
        int cnt = 0; for (auto& r : records) if (r.confirmed) cnt++; return cnt;
    }
    // 按人工修正统计
    int overriddenCount() const {
        int cnt = 0; for (auto& r : records) if (r.isOverridden()) cnt++; return cnt;
    }
    // 总记录数
    int totalCount() const { return static_cast<int>(records.size()); }
};

// 判读会话的历史版本管理器
struct AnnotationHistory
{
    std::vector<AnnotationSession> versions;  // 版本列表（按时间排序）
    int activeIndex = -1;                     // 当前活动版本索引

    const AnnotationSession* active() const {
        return (activeIndex >= 0 && activeIndex < static_cast<int>(versions.size()))
                   ? &versions[activeIndex] : nullptr;
    }
    AnnotationSession* active() {
        return (activeIndex >= 0 && activeIndex < static_cast<int>(versions.size()))
                   ? &versions[activeIndex] : nullptr;
    }
    int versionCount() const { return static_cast<int>(versions.size()); }
    void addVersion(const AnnotationSession& session) {
        versions.push_back(session);
        activeIndex = static_cast<int>(versions.size()) - 1;
    }
};