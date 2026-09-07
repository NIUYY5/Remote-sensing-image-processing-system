#include "LandCoverExtractor.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <map>
#include <QApplication>

LandCoverExtractor::LandCoverExtractor(QObject* parent)
    : QObject(parent)
{
}

LandCoverExtractor::~LandCoverExtractor()
{
}

void LandCoverExtractor::setParams(const ExtractionParams& params)
{
    m_params = params;
}

// ========== 光谱指数计算 ==========

std::vector<double> LandCoverExtractor::computeNDVI(const GeoImageData& image)
{
    // NDVI = (NIR - Red) / (NIR + Red)
    // 假设波段顺序：Red=0, Green=1, Blue=2, NIR=3
    int n = image.pixelCount();
    std::vector<double> ndvi(n, 0);
    if (image.bands < 4) return ndvi;

    const double* red = image.rawBandData(0);
    const double* nir = image.rawBandData(3);
    if (!red || !nir) return ndvi;

    for (int i = 0; i < n; ++i) {
        double sum = nir[i] + red[i];
        ndvi[i] = (std::abs(sum) > 1e-10) ? (nir[i] - red[i]) / sum : 0;
    }
    return ndvi;
}

std::vector<double> LandCoverExtractor::computeNDWI(const GeoImageData& image)
{
    // NDWI = (Green - NIR) / (Green + NIR)
    int n = image.pixelCount();
    std::vector<double> ndwi(n, 0);
    if (image.bands < 4) return ndwi;

    const double* green = image.rawBandData(1);
    const double* nir = image.rawBandData(3);
    if (!green || !nir) return ndwi;

    for (int i = 0; i < n; ++i) {
        double sum = green[i] + nir[i];
        ndwi[i] = (std::abs(sum) > 1e-10) ? (green[i] - nir[i]) / sum : 0;
    }
    return ndwi;
}

std::vector<double> LandCoverExtractor::computeMNDWI(const GeoImageData& image)
{
    // MNDWI = (Green - SWIR) / (Green + SWIR)
    // 改进型NDWI：用SWIR替代NIR，能更好地区分水体和建筑区
    int n = image.pixelCount();
    std::vector<double> mndwi(n, 0);
    if (image.bands < 5) return computeNDWI(image); // 无SWIR波段时回退到NDWI

    const double* green = image.rawBandData(1);
    const double* swir = image.rawBandData(std::min(4, image.bands - 1));
    if (!green || !swir) return computeNDWI(image);

    for (int i = 0; i < n; ++i) {
        double sum = green[i] + swir[i];
        mndwi[i] = (std::abs(sum) > 1e-10) ? (green[i] - swir[i]) / sum : 0;
    }
    return mndwi;
}

std::vector<double> LandCoverExtractor::computeNDBI(const GeoImageData& image)
{
    // NDBI = (SWIR - NIR) / (SWIR + NIR)，若无SWIR则用 (Blue - NIR) / (Blue + NIR) 近似
    int n = image.pixelCount();
    std::vector<double> ndbi(n, 0);

    const double* nir = image.rawBandData(std::min(3, image.bands - 1));
    const double* swir = image.bands >= 5 ? image.rawBandData(4) : image.rawBandData(0);
    if (!nir || !swir) return ndbi;

    for (int i = 0; i < n; ++i) {
        double sum = swir[i] + nir[i];
        ndbi[i] = (std::abs(sum) > 1e-10) ? (swir[i] - nir[i]) / sum : 0;
    }
    return ndbi;
}

std::vector<double> LandCoverExtractor::computeBrightness(const GeoImageData& image)
{
    // 亮度 = 各波段均值
    int n = image.pixelCount();
    std::vector<double> brightness(n, 0);

    for (int b = 0; b < image.bands; ++b) {
        const double* band = image.rawBandData(b);
        if (!band) continue;
        for (int i = 0; i < n; ++i)
            brightness[i] += band[i];
    }
    for (int i = 0; i < n; ++i)
        brightness[i] /= image.bands;

    return brightness;
}

std::vector<double> LandCoverExtractor::computeLocalVariance(const GeoImageData& image, int band, int window)
{
    // 计算局部纹理方差：滑动窗口内像素值的方差
    // 建筑区域通常具有较高的局部方差（屋顶边缘、阴影变化）
    // 水体区域方差低（均匀表面）
    int n = image.pixelCount();
    int w = image.width, h = image.height;
    std::vector<double> variance(n, 0);

    int b = std::min(band, image.bands - 1);
    const double* data = image.rawBandData(b);
    if (!data) return variance;

    int half = window / 2;
    for (int y = half; y < h - half; ++y) {
        for (int x = half; x < w - half; ++x) {
            int idx = y * w + x;
            double sum = 0, sumSq = 0;
            int count = 0;
            for (int dy = -half; dy <= half; ++dy) {
                for (int dx = -half; dx <= half; ++dx) {
                    double v = data[(y + dy) * w + (x + dx)];
                    sum += v;
                    sumSq += v * v;
                    count++;
                }
            }
            double mean = sum / count;
            variance[idx] = sumSq / count - mean * mean;
        }
    }
    return variance;
}

// ========== 自适应参数计算 ==========

ExtractionParams LandCoverExtractor::autoParameters(const GeoImageData& image)
{
    ExtractionParams p;

    if (!image.isValid()) return p;

    std::vector<double> ndwi = computeNDWI(image);
    std::vector<double> ndvi = computeNDVI(image);
    std::vector<double> brightness = computeBrightness(image);

    if (ndwi.empty() || ndvi.empty() || brightness.empty()) return p;

    int n = image.pixelCount();

    // 基于Otsu思想：找NDWI直方图的谷点作为水体阈值
    double ndwiMin = 1e10, ndwiMax = -1e10;
    for (int i = 0; i < n; ++i) {
        ndwiMin = std::min(ndwiMin, ndwi[i]);
        ndwiMax = std::max(ndwiMax, ndwi[i]);
    }

    // 简单直方图Otsu
    const int histBins = 256;
    std::vector<int> hist(histBins, 0);
    for (int i = 0; i < n; ++i) {
        int bin = static_cast<int>((ndwi[i] - ndwiMin) / (ndwiMax - ndwiMin + 1e-10) * (histBins - 1));
        bin = std::max(0, std::min(histBins - 1, bin));
        hist[bin]++;
    }

    // Otsu计算水体阈值
    long long total = n;
    long long sumB = 0, wB = 0;
    long long sumTotal = 0;
    for (int t = 0; t < histBins; ++t) sumTotal += t * hist[t];

    double maxVariance = 0;
    int bestBin = histBins / 2;
    for (int t = 0; t < histBins; ++t) {
        wB += hist[t];
        if (wB == 0 || wB == total) continue;
        long long wF = total - wB;
        sumB += t * hist[t];
        double mB = (double)sumB / wB;
        double mF = (double)(sumTotal - sumB) / wF;
        double var = (double)wB * wF * (mB - mF) * (mB - mF);
        if (var > maxVariance) {
            maxVariance = var;
            bestBin = t;
        }
    }
    p.ndwiThreshold = ndwiMin + (ndwiMax - ndwiMin) * bestBin / (histBins - 1);

    // NDVI自适应性阈值
    double ndviMin = 1e10, ndviMax = -1e10;
    for (int i = 0; i < n; ++i) {
        ndviMin = std::min(ndviMin, ndvi[i]);
        ndviMax = std::max(ndviMax, ndvi[i]);
    }
    p.ndviThreshold = std::max(0.2, ndviMin + 0.35 * (ndviMax - ndviMin));

    // 基于影像亮度分布调整亮度阈值
    std::vector<double> sortedBright = brightness;
    std::sort(sortedBright.begin(), sortedBright.end());
    double medianBright = sortedBright[n / 2];
    double pct90Bright = sortedBright[n * 9 / 10];

    p.waterBrightnessMax = std::max(0.2, std::min(0.5, medianBright * 0.6));
    p.buildingBrightnessMin = std::max(0.2, std::min(0.6, pct90Bright * 0.5));
    p.roadBrightnessThreshold = std::max(0.2, std::min(0.6, pct90Bright * 0.45));

    return p;
}

// ========== 地物提取 ==========

LandCoverResult LandCoverExtractor::extractWater(const GeoImageData& image)
{
    LandCoverResult result;
    result.type = LandCoverType::Water;
    result.typeName = typeName(LandCoverType::Water);
    result.displayColor = typeColor(LandCoverType::Water);
    result.width = image.width;
    result.height = image.height;

    int n = image.pixelCount();
    result.mask.resize(n, 0);
    result.confidence.resize(n, 0);

    std::vector<double> ndwi = computeNDWI(image);
    std::vector<double> mndwi = computeMNDWI(image);
    std::vector<double> brightness = computeBrightness(image);
    std::vector<double> ndbi = computeNDBI(image);
    if (ndwi.empty() || brightness.empty()) return result;

    // 水体特征多条件约束：
    // 1. NDWI > 阈值（核心水体指标）
    // 2. MNDWI > 阈值（SWIR辅助，排除建筑混淆）
    // 3. 亮度低（水体反射率低）
    // 4. NDBI < 上限（排除建筑，建筑NDBI高）
    // 5. 纹理方差低（水体表面均匀，非建筑）

    double maxBrightness = 0;
    for (int i = 0; i < n; ++i)
        maxBrightness = std::max(maxBrightness, brightness[i]);
    double brightThresh = maxBrightness * m_params.waterBrightnessMax;

    // 纹理特征（水体表面平滑）
    std::vector<double> textureVar = computeLocalVariance(image, 0, 5);

    for (int i = 0; i < n; ++i) {
        // 多条件联合判断
        bool ndwiOk = ndwi[i] > m_params.ndwiThreshold;
        bool mndwiOk = (!mndwi.empty()) ? mndwi[i] > m_params.ndwiThreshold : true;
        bool brightOk = brightness[i] < brightThresh;
        bool ndbiOk = (!ndbi.empty()) ? ndbi[i] < m_params.waterNdbilMax : true;
        bool textureOk = (!textureVar.empty()) ? textureVar[i] < m_params.buildingTextureThreshold : true;

        // 需要至少 NDWI + MNDWI 和 亮度 同时满足
        bool isWater = ndwiOk && mndwiOk && brightOk && ndbiOk && textureOk;

        if (isWater) {
            result.mask[i] = 1;
            // 多特征综合置信度
            double ndwiConf = std::min(1.0, (ndwi[i] - m_params.ndwiThreshold) / 0.4);
            double mndwiConf = (!mndwi.empty()) ? std::min(1.0, (mndwi[i] - m_params.ndwiThreshold) / 0.4) : ndwiConf;
            double brightConf = 1.0 - std::min(1.0, brightness[i] / (brightThresh + 1e-10));
            double textureConf = (!textureVar.empty()) ? 
                1.0 - std::min(1.0, textureVar[i] / (m_params.buildingTextureThreshold + 1e-10)) : 0.5;
            result.confidence[i] = 0.35 * ndwiConf + 0.25 * mndwiConf + 0.25 * brightConf + 0.15 * textureConf;
        }
    }

    postProcessResult(result);
    return result;
}

LandCoverResult LandCoverExtractor::extractVegetation(const GeoImageData& image)
{
    LandCoverResult result;
    result.type = LandCoverType::Vegetation;
    result.typeName = typeName(LandCoverType::Vegetation);
    result.displayColor = typeColor(LandCoverType::Vegetation);
    result.width = image.width;
    result.height = image.height;

    int n = image.pixelCount();
    result.mask.resize(n, 0);
    result.confidence.resize(n, 0);

    std::vector<double> ndvi = computeNDVI(image);
    if (ndvi.empty()) return result;

    // 植被特征：高NDVI + 高NIR反射率（排除阴影区域误判）
    const double* nir = image.rawBandData(std::min(3, image.bands - 1));

    for (int i = 0; i < n; ++i) {
        bool ndviOk = ndvi[i] > m_params.ndviThreshold;
        bool nirOk = nir ? nir[i] > m_params.vegNirMin : true;

        if (ndviOk && nirOk) {
            result.mask[i] = 1;
            double ndviConf = std::min(1.0, (ndvi[i] - m_params.ndviThreshold) / 0.5);
            double nirConf = nir ? std::min(1.0, nir[i] / (m_params.vegNirMin * 2)) : 0.5;
            result.confidence[i] = 0.7 * ndviConf + 0.3 * nirConf;
        }
    }

    postProcessResult(result);
    return result;
}

LandCoverResult LandCoverExtractor::extractBuilding(const GeoImageData& image)
{
    LandCoverResult result;
    result.type = LandCoverType::Building;
    result.typeName = typeName(LandCoverType::Building);
    result.displayColor = typeColor(LandCoverType::Building);
    result.width = image.width;
    result.height = image.height;

    int n = image.pixelCount();
    result.mask.resize(n, 0);
    result.confidence.resize(n, 0);

    std::vector<double> ndbi = computeNDBI(image);
    std::vector<double> brightness = computeBrightness(image);
    std::vector<double> ndwi = computeNDWI(image);
    std::vector<double> textureVar = computeLocalVariance(image, 0, 7); // 7x7窗口纹理
    if (ndbi.empty() || brightness.empty()) return result;

    // 建筑特征多条件约束：
    // 1. NDBI > 阈值（建筑在SWIR/Blue反射强）
    // 2. 亮度高（建筑比周围环境亮）
    // 3. NDWI < 阈值（排除水体）
    // 4. 纹理方差高（建筑有屋顶结构，纹理丰富）

    double maxBrightness = 0;
    for (int i = 0; i < n; ++i)
        maxBrightness = std::max(maxBrightness, brightness[i]);
    double brightThresh = maxBrightness * m_params.buildingBrightnessMin;

    for (int i = 0; i < n; ++i) {
        bool ndbiOk = ndbi[i] > m_params.ndbiThreshold;
        bool brightOk = brightness[i] > brightThresh;
        bool ndwiOk = (!ndwi.empty()) ? ndwi[i] < m_params.ndwiThreshold : true;
        bool textureOk = (!textureVar.empty()) ? textureVar[i] > m_params.buildingTextureThreshold : true;

        // NDBI + 亮度 必须满足，NDWI/纹理为辅助
        bool isBuilding = ndbiOk && brightOk && ndwiOk && textureOk;

        if (isBuilding) {
            result.mask[i] = 1;
            double ndbiConf = std::min(1.0, (ndbi[i] - m_params.ndbiThreshold) / 0.3);
            double brightConf = std::min(1.0, brightness[i] / maxBrightness);
            double textureConf = (!textureVar.empty()) ? 
                std::min(1.0, textureVar[i] / (m_params.buildingTextureThreshold * 3)) : 0.5;
            result.confidence[i] = 0.35 * ndbiConf + 0.35 * brightConf + 0.30 * textureConf;
        }
    }

    postProcessResult(result);
    return result;
}

LandCoverResult LandCoverExtractor::extractRoad(const GeoImageData& image)
{
    LandCoverResult result;
    result.type = LandCoverType::Road;
    result.typeName = typeName(LandCoverType::Road);
    result.displayColor = typeColor(LandCoverType::Road);
    result.width = image.width;
    result.height = image.height;

    int n = image.pixelCount();
    result.mask.resize(n, 0);
    result.confidence.resize(n, 0);

    std::vector<double> brightness = computeBrightness(image);
    std::vector<double> ndvi = computeNDVI(image);
    std::vector<double> ndwi = computeNDWI(image);
    if (brightness.empty() || ndvi.empty()) return result;

    // 道路特征：高亮度 + 低NDVI（非植被） + 低NDWI（非水体）
    double maxBrightness = 0;
    for (int i = 0; i < n; ++i)
        maxBrightness = std::max(maxBrightness, brightness[i]);
    double brightThresh = maxBrightness * m_params.roadBrightnessThreshold;

    for (int i = 0; i < n; ++i) {
        bool brightOk = brightness[i] > brightThresh;
        bool ndviOk = ndvi[i] < 0.2;
        bool ndwiOk = (!ndwi.empty()) ? ndwi[i] < m_params.ndwiThreshold : true;

        if (brightOk && ndviOk && ndwiOk) {
            result.mask[i] = 1;
            result.confidence[i] = std::min(1.0, brightness[i] / maxBrightness);
        }
    }

    // 形态学开运算保留线状特征
    morphologicalOpen(result.mask, result.width, result.height, 2);

    // 长宽比过滤：保留细长道路，排除块状建筑
    if (m_params.roadMaxElongation > 1.0) {
        filterByElongation(result.mask, result.width, result.height,
                          static_cast<int>(m_params.roadMaxElongation));
    }

    postProcessResult(result);
    return result;
}

LandCoverResult LandCoverExtractor::extractBareSoil(const GeoImageData& image)
{
    LandCoverResult result;
    result.type = LandCoverType::BareSoil;
    result.typeName = typeName(LandCoverType::BareSoil);
    result.displayColor = typeColor(LandCoverType::BareSoil);
    result.width = image.width;
    result.height = image.height;

    int n = image.pixelCount();
    result.mask.resize(n, 0);
    result.confidence.resize(n, 0);

    std::vector<double> ndvi = computeNDVI(image);
    std::vector<double> brightness = computeBrightness(image);
    std::vector<double> ndwi = computeNDWI(image);
    if (ndvi.empty() || brightness.empty()) return result;

    // 裸土特征：低NDVI + 中等亮度 + 非水体
    for (int i = 0; i < n; ++i) {
        bool ndviOk = ndvi[i] > m_params.bareSoilNdviLo && ndvi[i] < m_params.bareSoilNdviHi;
        bool brightOk = brightness[i] > m_params.bareSoilBrightnessLo;
        bool ndwiOk = (!ndwi.empty()) ? ndwi[i] < m_params.ndwiThreshold : true;

        if (ndviOk && brightOk && ndwiOk) {
            result.mask[i] = 1;
            result.confidence[i] = 1.0 - std::min(1.0, std::abs(ndvi[i]) / 0.15);
        }
    }

    postProcessResult(result);
    return result;
}

LandCoverResult LandCoverExtractor::extractShadow(const GeoImageData& image)
{
    LandCoverResult result;
    result.type = LandCoverType::Shadow;
    result.typeName = typeName(LandCoverType::Shadow);
    result.displayColor = typeColor(LandCoverType::Shadow);
    result.width = image.width;
    result.height = image.height;

    int n = image.pixelCount();
    result.mask.resize(n, 0);
    result.confidence.resize(n, 0);

    std::vector<double> brightness = computeBrightness(image);
    std::vector<double> ndwi = computeNDWI(image);
    if (brightness.empty()) return result;

    double maxBrightness = 0;
    for (int i = 0; i < n; ++i)
        maxBrightness = std::max(maxBrightness, brightness[i]);
    double shadowThresh = maxBrightness * m_params.shadowBrightnessRatio;

    for (int i = 0; i < n; ++i) {
        bool darkOk = brightness[i] < shadowThresh;
        bool ndwiOk = (!ndwi.empty()) ? ndwi[i] < m_params.ndwiThreshold : true;

        // 阴影：极低亮度 + 非水体（水体NDWI高）
        if (darkOk && ndwiOk) {
            result.mask[i] = 1;
            result.confidence[i] = 1.0 - brightness[i] / (shadowThresh + 1e-10);
        }
    }

    postProcessResult(result);
    return result;
}

// ========== 综合提取 ==========

ExtractionResult LandCoverExtractor::extractFromImage(const GeoImageData& image,
                                                       const std::vector<LandCoverType>& types)
{
    ExtractionResult result;
    result.width = image.width;
    result.height = image.height;

    if (!image.isValid() || types.empty()) return result;

    int totalSteps = static_cast<int>(types.size());
    int step = 0;

    for (const auto& type : types) {
        LandCoverResult lcr;
        switch (type) {
        case LandCoverType::Water:      lcr = extractWater(image); break;
        case LandCoverType::Vegetation: lcr = extractVegetation(image); break;
        case LandCoverType::Building:   lcr = extractBuilding(image); break;
        case LandCoverType::Road:       lcr = extractRoad(image); break;
        case LandCoverType::BareSoil:   lcr = extractBareSoil(image); break;
        case LandCoverType::Shadow:     lcr = extractShadow(image); break;
        default: continue;
        }
        result.results.push_back(lcr);
        step++;
        emit progressUpdated(step * 100 / totalSteps);
        QApplication::processEvents();
    }

    // 生成综合标签图，按优先级：水体 > 建筑 > 道路 > 植被 > 裸土 > 阴影
    // 水体优先级最高，确保暗色建筑不会被误覆盖到水体区域
    int n = image.pixelCount();
    result.combinedLabelMap.resize(n, -1);

    // 按优先级排序提取结果索引
    auto priority = [](LandCoverType t) -> int {
        switch (t) {
        case LandCoverType::Water:      return 5;
        case LandCoverType::Building:   return 4;
        case LandCoverType::Road:       return 3;
        case LandCoverType::Vegetation: return 2;
        case LandCoverType::BareSoil:   return 1;
        case LandCoverType::Shadow:     return 0;
        default:                        return -1;
        }
    };
    std::vector<size_t> sortedIdx(result.results.size());
    for (size_t i = 0; i < sortedIdx.size(); ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(), [&](size_t a, size_t b) {
        return priority(result.results[a].type) > priority(result.results[b].type);
    });

    for (size_t ki : sortedIdx) {
        auto& r = result.results[ki];
        for (int i = 0; i < n; ++i) {
            if (r.mask[i] == 1 && result.combinedLabelMap[i] == -1)
                result.combinedLabelMap[i] = static_cast<int>(ki);
        }
    }

    emit progressUpdated(100);
    return result;
}

ExtractionResult LandCoverExtractor::extractFromClassification(const ClassificationResult& classResult,
                                                                 const std::vector<LandCoverType>& types)
{
    ExtractionResult result;
    result.width = classResult.width;
    result.height = classResult.height;

    if (!classResult.isValid() || types.empty()) return result;

    int n = classResult.width * classResult.height;
    result.combinedLabelMap.resize(n, -1);

    // 基于类别名称映射到地物类型
    std::map<QString, LandCoverType> nameMap;
    for (const auto& t : types) {
        switch (t) {
        case LandCoverType::Water:      nameMap[QString::fromUtf8("\u6C34\u4F53")] = t; break;
        case LandCoverType::Vegetation: nameMap[QString::fromUtf8("\u690D\u88AB")] = t; break;
        case LandCoverType::Building:   nameMap[QString::fromUtf8("\u5EFA\u7B51")] = t; break;
        case LandCoverType::Road:       nameMap[QString::fromUtf8("\u9053\u8DEF")] = t; break;
        default: break;
        }
    }

    // 为每种地物类型创建结果
    for (const auto& type : types) {
        LandCoverResult lcr;
        lcr.type = type;
        lcr.typeName = typeName(type);
        lcr.displayColor = typeColor(type);
        lcr.width = classResult.width;
        lcr.height = classResult.height;
        lcr.mask.resize(n, 0);
        lcr.confidence.resize(n, 0);

        result.results.push_back(lcr);
    }

    // 基于标签映射
    for (int i = 0; i < n; ++i) {
        int lbl = classResult.labelMap[i];
        if (lbl >= 0 && lbl < classResult.classCount) {
            QString name = classResult.classNames[lbl];
            auto it = nameMap.find(name);
            if (it != nameMap.end()) {
                int idx = static_cast<int>(it->second);
                if (idx < static_cast<int>(result.results.size())) {
                    result.results[idx].mask[i] = 1;
                    result.results[idx].confidence[i] = 1.0;
                    result.results[idx].pixelCount++;
                    result.combinedLabelMap[i] = idx;
                }
            }
        }
    }

    // 计算覆盖率
    for (auto& r : result.results) {
        if (n > 0)
            r.coveragePercent = 100.0 * r.pixelCount / n;
    }

    emit progressUpdated(100);
    return result;
}

// ========== 后处理 ==========

void LandCoverExtractor::postProcessResult(LandCoverResult& result)
{
    if (!result.isValid()) return;

    if (m_params.useMorphology) {
        // 先闭运算填充空洞，再开运算去除噪点
        morphologicalClose(result.mask, result.width, result.height, m_params.morphologyKernelSize);
        morphologicalOpen(result.mask, result.width, result.height, m_params.morphologyKernelSize);
    }

    // 去除小区域
    removeSmallRegions(result.mask, result.width, result.height, m_params.minRegionArea);

    // 统计像素数
    result.pixelCount = 0;
    for (int v : result.mask)
        if (v == 1) result.pixelCount++;

    int total = result.width * result.height;
    result.coveragePercent = (total > 0) ? 100.0 * result.pixelCount / total : 0;
}

// ========== 形态学操作 ==========

void LandCoverExtractor::morphologicalOpen(std::vector<int>& mask, int width, int height, int kernelSize)
{
    // 开运算 = 先腐蚀再膨胀
    int half = kernelSize / 2;
    std::vector<int> eroded = mask;

    // 腐蚀
    for (int y = half; y < height - half; ++y) {
        for (int x = half; x < width - half; ++x) {
            int idx = y * width + x;
            if (mask[idx] == 0) continue;
            bool allOne = true;
            for (int dy = -half; dy <= half && allOne; ++dy)
                for (int dx = -half; dx <= half; ++dx)
                    if (mask[(y + dy) * width + (x + dx)] == 0) { allOne = false; break; }
            eroded[idx] = allOne ? 1 : 0;
        }
    }

    // 膨胀
    mask = eroded;
    for (int y = half; y < height - half; ++y) {
        for (int x = half; x < width - half; ++x) {
            int idx = y * width + x;
            if (eroded[idx] == 1) continue;
            bool hasOne = false;
            for (int dy = -half; dy <= half && !hasOne; ++dy)
                for (int dx = -half; dx <= half; ++dx)
                    if (eroded[(y + dy) * width + (x + dx)] == 1) { hasOne = true; break; }
            mask[idx] = hasOne ? 1 : 0;
        }
    }
}

void LandCoverExtractor::morphologicalClose(std::vector<int>& mask, int width, int height, int kernelSize)
{
    // 闭运算 = 先膨胀再腐蚀
    int half = kernelSize / 2;
    std::vector<int> dilated = mask;

    // 膨胀
    for (int y = half; y < height - half; ++y) {
        for (int x = half; x < width - half; ++x) {
            int idx = y * width + x;
            if (mask[idx] == 1) continue;
            bool hasOne = false;
            for (int dy = -half; dy <= half && !hasOne; ++dy)
                for (int dx = -half; dx <= half; ++dx)
                    if (mask[(y + dy) * width + (x + dx)] == 1) { hasOne = true; break; }
            dilated[idx] = hasOne ? 1 : 0;
        }
    }

    // 腐蚀
    mask = dilated;
    for (int y = half; y < height - half; ++y) {
        for (int x = half; x < width - half; ++x) {
            int idx = y * width + x;
            if (dilated[idx] == 0) continue;
            bool allOne = true;
            for (int dy = -half; dy <= half && allOne; ++dy)
                for (int dx = -half; dx <= half; ++dx)
                    if (dilated[(y + dy) * width + (x + dx)] == 0) { allOne = false; break; }
            mask[idx] = allOne ? 1 : 0;
        }
    }
}

void LandCoverExtractor::removeSmallRegions(std::vector<int>& mask, int width, int height, int minArea)
{
    if (minArea <= 0) return;
    int total = width * height;
    std::vector<bool> visited(total, false);

    for (int seed = 0; seed < total; ++seed) {
        if (mask[seed] == 0 || visited[seed]) continue;

        // BFS 找连通区域
        std::vector<int> region;
        std::vector<int> queue;
        queue.push_back(seed);
        visited[seed] = true;

        while (!queue.empty()) {
            int idx = queue.back();
            queue.pop_back();
            region.push_back(idx);

            int x = idx % width;
            int y = idx / width;
            int neighbors[8][2] = {{x-1,y},{x+1,y},{x,y-1},{x,y+1},
                                    {x-1,y-1},{x+1,y-1},{x-1,y+1},{x+1,y+1}};
            for (int ni = 0; ni < 8; ++ni) {
                int nx = neighbors[ni][0], ny = neighbors[ni][1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                int nidx = ny * width + nx;
                if (mask[nidx] == 1 && !visited[nidx]) {
                    visited[nidx] = true;
                    queue.push_back(nidx);
                }
            }
        }

        // 小区域清零
        if (static_cast<int>(region.size()) < minArea) {
            for (int idx : region)
                mask[idx] = 0;
        }
    }
}

void LandCoverExtractor::filterByElongation(std::vector<int>& mask, int width, int height, int minElongation)
{
    // 长宽比过滤：仅保留长宽比 > minElongation 的区域
    // 用于道路提取：道路是细长形，建筑是块状
    if (minElongation <= 1) return;

    int total = width * height;
    std::vector<bool> visited(total, false);

    for (int seed = 0; seed < total; ++seed) {
        if (mask[seed] == 0 || visited[seed]) continue;

        // BFS收集连通区域
        std::vector<int> region;
        std::vector<int> queue;
        queue.push_back(seed);
        visited[seed] = true;

        int minX = width, maxX = 0, minY = height, maxY = 0;
        while (!queue.empty()) {
            int idx = queue.back();
            queue.pop_back();
            region.push_back(idx);

            int x = idx % width;
            int y = idx / width;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);

            int neighbors[4][2] = {{x-1,y},{x+1,y},{x,y-1},{x,y+1}};
            for (int ni = 0; ni < 4; ++ni) {
                int nx = neighbors[ni][0], ny = neighbors[ni][1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                int nidx = ny * width + nx;
                if (mask[nidx] == 1 && !visited[nidx]) {
                    visited[nidx] = true;
                    queue.push_back(nidx);
                }
            }
        }

        // 计算边界框长宽比
        int boxW = maxX - minX + 1;
        int boxH = maxY - minY + 1;
        if (boxW <= 0 || boxH <= 0) continue;

        double elongation = (boxW > boxH) ? (double)boxW / boxH : (double)boxH / boxW;

        // 长宽比不足的块状区域清除
        if (elongation < minElongation) {
            for (int idx : region)
                mask[idx] = 0;
        }
    }
}

// ========== 工具方法 ==========

QString LandCoverExtractor::typeName(LandCoverType type)
{
    switch (type) {
    case LandCoverType::Water:      return QString::fromUtf8("\u6C34\u4F53");
    case LandCoverType::Vegetation: return QString::fromUtf8("\u690D\u88AB");
    case LandCoverType::Building:   return QString::fromUtf8("\u5EFA\u7B51");
    case LandCoverType::Road:       return QString::fromUtf8("\u9053\u8DEF");
    case LandCoverType::BareSoil:   return QString::fromUtf8("\u88F8\u571F");
    case LandCoverType::Shadow:     return QString::fromUtf8("\u9634\u5F71");
    default:                        return QString::fromUtf8("\u672A\u77E5");
    }
}

QColor LandCoverExtractor::typeColor(LandCoverType type)
{
    switch (type) {
    case LandCoverType::Water:      return QColor(30, 144, 255);    // 蓝色
    case LandCoverType::Vegetation: return QColor(34, 139, 34);     // 绿色
    case LandCoverType::Building:   return QColor(220, 20, 60);     // 红色
    case LandCoverType::Road:       return QColor(128, 128, 128);   // 灰色
    case LandCoverType::BareSoil:   return QColor(210, 180, 140);   // 棕色
    case LandCoverType::Shadow:     return QColor(40, 40, 40);      // 深灰
    default:                        return QColor(255, 255, 255);
    }
}

// ========== LandCoverResult 图像生成 ==========

QImage LandCoverResult::toMaskImage() const
{
    if (!isValid()) return QImage();
    QImage img(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            img.setPixelColor(x, y, mask[idx] ? displayColor : QColor(0, 0, 0, 0));
        }
    }
    return img;
}

QImage LandCoverResult::toConfidenceImage() const
{
    if (!isValid()) return QImage();
    QImage img(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            int v = static_cast<int>(confidence[idx] * 255);
            img.setPixelColor(x, y, QColor(v, v, v));
        }
    }
    return img;
}

QImage ExtractionResult::toCombinedImage() const
{
    if (!isValid()) return QImage();
    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0, 0));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            int lbl = combinedLabelMap[idx];
            if (lbl >= 0 && lbl < static_cast<int>(results.size())) {
                img.setPixelColor(x, y, results[lbl].displayColor);
            }
        }
    }
    return img;
}

QString ExtractionResult::toSummaryReport() const
{
    QString report;
    report += QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u7EFC\u5408\u62A5\u544A\n");
    report += QString::fromUtf8("=") + QString(40, QChar('=')) + "\n";
    report += QString::fromUtf8("\u5F71\u50CF\u5C3A\u5BF8: %1 x %2 \u50CF\u7D20\n").arg(width).arg(height);
    report += QString::fromUtf8("\u63D0\u53D6\u5730\u7269\u7C7B\u578B: %1 \u79CD\n").arg(results.size());
    report += QString::fromUtf8("-") + QString(40, QChar('-')) + "\n";

    int totalPixels = width * height;
    for (const auto& r : results) {
        report += QString::fromUtf8("  %1: %2 \u50CF\u7D20 (%3%)\n")
                      .arg(r.typeName, -8)
                      .arg(r.pixelCount, 10)
                      .arg(r.coveragePercent, 0, 'f', 2);
    }

    int coveredPixels = 0;
    for (int v : combinedLabelMap)
        if (v >= 0) coveredPixels++;
    report += QString::fromUtf8("-") + QString(40, QChar('-')) + "\n";
    report += QString::fromUtf8("  \u603B\u8986\u76D6\u7387: %1%\n")
                  .arg(100.0 * coveredPixels / totalPixels, 0, 'f', 2);

    return report;
}