#pragma once

#include "GeoImageData.h"
#include <QObject>
#include <QImage>
#include <vector>

// 地物类型枚举
enum class LandCoverType
{
    Water = 0,      // 水体
    Vegetation,     // 植被
    Building,       // 建筑
    Road,           // 道路
    BareSoil,       // 裸土
    Shadow,         // 阴影
    Custom          // 自定义
};

// 地物提取参数
struct ExtractionParams
{
    // === 水体参数 ===
    double ndwiThreshold = 0.0;           // NDWI 水体提取阈值
    double waterBrightnessMax = 0.35;     // 水体最大亮度约束
    double waterNdbilMax = 0.05;          // 水体 NDBI 上限（排除建筑）

    // === 建筑参数 ===
    double ndbiThreshold = 0.1;           // NDBI 建筑提取阈值
    double buildingBrightnessMin = 0.45;  // 建筑最小亮度比
    double buildingTextureThreshold = 0.02; // 建筑纹理方差阈值

    // === 植被参数 ===
    double ndviThreshold = 0.3;           // NDVI 植被提取阈值
    double vegNirMin = 0.2;              // 植被 NIR 最小反射率

    // === 道路参数 ===
    double roadBrightnessThreshold = 0.4; // 道路亮度阈值
    double roadMaxElongation = 3.0;       // 道路最大长宽比（排除块状建筑）

    // === 裸土参数 ===
    double bareSoilNdviLo = -0.1;         // 裸土 NDVI 下限
    double bareSoilNdviHi = 0.2;          // 裸土 NDVI 上限
    double bareSoilBrightnessLo = 0.3;    // 裸土最低亮度

    // === 阴影参数 ===
    double shadowBrightnessRatio = 0.15;  // 阴影亮度比上限

    // === 通用后处理 ===
    int minRegionArea = 30;               // 最小区域面积
    bool useMorphology = true;            // 形态学后处理
    int morphologyKernelSize = 3;         // 形态学核大小
};

// 单个地物提取结果
struct LandCoverResult
{
    LandCoverType type;
    QString typeName;
    std::vector<int> mask;          // 二值掩膜 (width*height)
    std::vector<double> confidence; // 置信度 (width*height)，0-1
    int width = 0;
    int height = 0;
    double coveragePercent = 0;     // 覆盖率百分比
    int pixelCount = 0;             // 像素数
    QColor displayColor;

    bool isValid() const { return width > 0 && height > 0 && !mask.empty(); }
    QImage toMaskImage() const;
    QImage toConfidenceImage() const;
};

// 多类地物提取综合结果
struct ExtractionResult
{
    std::vector<LandCoverResult> results;
    std::vector<int> combinedLabelMap;  // 综合标签图，每个像素对应一种地物类型
    int width = 0;
    int height = 0;

    bool isValid() const { return width > 0 && height > 0 && !results.empty(); }
    QImage toCombinedImage() const;
    QString toSummaryReport() const;
};

class LandCoverExtractor : public QObject
{
    Q_OBJECT

public:
    explicit LandCoverExtractor(QObject* parent = nullptr);
    ~LandCoverExtractor();

    // 从原始影像提取地物（基于光谱指数）
    ExtractionResult extractFromImage(const GeoImageData& image,
                                       const std::vector<LandCoverType>& types);

    // 从分类结果提取地物（基于标签映射）
    ExtractionResult extractFromClassification(const ClassificationResult& result,
                                                const std::vector<LandCoverType>& types);

    // 参数设置
    void setParams(const ExtractionParams& params);
    ExtractionParams params() const { return m_params; }

    // 光谱指数计算（静态工具方法）
    static std::vector<double> computeNDVI(const GeoImageData& image);
    static std::vector<double> computeNDWI(const GeoImageData& image);
    static std::vector<double> computeMNDWI(const GeoImageData& image);  // 改进型NDWI (SWIR)
    static std::vector<double> computeNDBI(const GeoImageData& image);
    static std::vector<double> computeBrightness(const GeoImageData& image);
    static std::vector<double> computeLocalVariance(const GeoImageData& image, int band, int window); // 局部纹理方差

    // 自适应参数计算（基于影像统计信息自动推荐阈值）
    static ExtractionParams autoParameters(const GeoImageData& image);

    // 形态学后处理
    static void morphologicalOpen(std::vector<int>& mask, int width, int height, int kernelSize);
    static void morphologicalClose(std::vector<int>& mask, int width, int height, int kernelSize);
    static void removeSmallRegions(std::vector<int>& mask, int width, int height, int minArea);
    static void filterByElongation(std::vector<int>& mask, int width, int height, int minElongation); // 长宽比过滤

    // 获取地物类型名称和颜色
    static QString typeName(LandCoverType type);
    static QColor typeColor(LandCoverType type);

signals:
    void progressUpdated(int percent);
    void statusMessage(const QString& msg);

private:
    ExtractionParams m_params;

    // 提取水体
    LandCoverResult extractWater(const GeoImageData& image);
    // 提取植被
    LandCoverResult extractVegetation(const GeoImageData& image);
    // 提取建筑
    LandCoverResult extractBuilding(const GeoImageData& image);
    // 提取道路
    LandCoverResult extractRoad(const GeoImageData& image);
    // 提取裸土
    LandCoverResult extractBareSoil(const GeoImageData& image);
    // 提取阴影
    LandCoverResult extractShadow(const GeoImageData& image);

    // 后处理单个结果
    void postProcessResult(LandCoverResult& result);
};