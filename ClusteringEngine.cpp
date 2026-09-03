#include "ClusteringEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <cassert>
#include <QApplication>
#include <QElapsedTimer>

ClusteringEngine::ClusteringEngine(QObject* parent)
    : QObject(parent), m_rng(42)
{
}

ClusteringEngine::~ClusteringEngine()
{
}

void ClusteringEngine::setConfig(const ClusteringConfig& config)
{
    m_config = config;
    m_rng.seed(config.randomSeed);
}

ClusteringResult ClusteringEngine::kMeans(const GeoImageData& image, const std::vector<int>& bandIndices)
{
    std::vector<int> bands = bandIndices;
    if (bands.empty()) {
        for (int i = 0; i < image.bands; ++i)
            bands.push_back(i);
    }

    emit progressUpdated(0);
    emit statusMessage(QString::fromUtf8("K-Means: 正在提取像素特征..."));
    QApplication::processEvents();

    // 使用扁平化存储，缓存友好
    std::vector<double> flatFeatures;
    int featureDim;
    extractPixelFeaturesFlat(image, bands, flatFeatures, featureDim,
                              m_config.usePixelPositions, m_config.positionWeight);

    if (flatFeatures.empty() || featureDim == 0) {
        emit statusMessage(QString::fromUtf8("内存不足：无法分配特征空间，请关闭重试或降低图像分辨率"));
        emit progressUpdated(0);
        QApplication::processEvents();
        ClusteringResult empty;
        return empty;
    }

    int n = image.pixelCount();

    emit progressUpdated(5);
    emit statusMessage(QString::fromUtf8("K-Means: 特征提取完成 (%1 像素)，开始聚类...").arg(n));
    QApplication::processEvents();

    if (m_config.normalizeFeatures) {
        normalizeFeatureMatrixFlat(flatFeatures, n, featureDim);
    } else if (m_config.featureScaleFactor > 0.001) {
        // 部分特征缩放：保留波段间自然方差，仅做温和缩放
        scaleFeatureMatrixFlat(flatFeatures, n, featureDim, m_config.featureScaleFactor);
    }

    emit progressUpdated(10);
    QApplication::processEvents();

    return kMeansFlat(flatFeatures, n, featureDim);
}

ClusteringResult ClusteringEngine::kMeans(const std::vector<std::vector<double>>& features, int featureDim)
{
    ClusteringResult result;
    int n = static_cast<int>(features.size());
    int k = m_config.numClusters;

    if (n == 0 || k <= 0 || featureDim <= 0)
        return result;

    k = std::min(k, n);

    // 多轮初始化选最优
    const int nInit = 3;
    double bestInertia = 1e300;
    std::vector<int> bestLabels;
    std::vector<std::vector<double>> bestCentroids;
    const int totalIters = nInit * m_config.maxIterations;

    for (int init = 0; init < nInit; ++init) {
        std::vector<std::vector<double>> centroids(k, std::vector<double>(featureDim, 0));
        initializeCentroids(features, centroids, k, featureDim);

        std::vector<int> labels(n, 0);

        for (int iter = 0; iter < m_config.maxIterations; ++iter) {
            std::vector<double> distances(n, 0);
            assignLabels(features, centroids, labels, distances);

            double inertia = 0;
            for (double d : distances) inertia += d;

            std::vector<std::vector<double>> oldCentroids = centroids;

            bool changed = updateCentroids(features, labels, centroids, k, featureDim);

            // 逐迭代更新进度，避免进度条长时间停滞
            int globalIter = init * m_config.maxIterations + iter + 1;
            emit progressUpdated(globalIter * 100 / totalIters);
            QApplication::processEvents();

            if (!changed) {
                if (inertia < bestInertia) {
                    bestInertia = inertia;
                    bestLabels = labels;
                    bestCentroids = centroids;
                }
                break;
            }

            double maxShift = 0;
            for (int j = 0; j < k; ++j) {
                double shift = euclideanDistance(oldCentroids[j], centroids[j]);
                maxShift = std::max(maxShift, shift);
            }
            if (maxShift < m_config.convergenceThreshold) {
                if (inertia < bestInertia) {
                    bestInertia = inertia;
                    bestLabels = labels;
                    bestCentroids = centroids;
                }
                break;
            }

            if (iter == m_config.maxIterations - 1 && inertia < bestInertia) {
                bestInertia = inertia;
                bestLabels = labels;
                bestCentroids = centroids;
            }
        }
    }

    result.labels = bestLabels;
    result.inertia = bestInertia;
    result.converged = true;
    result.iterations = m_config.maxIterations;
    result.centroids = bestCentroids;
    result.clusterSizes.resize(k, 0);
    for (int label : result.labels) {
        if (label >= 0 && label < k)
            result.clusterSizes[label]++;
    }

    emit progressUpdated(100);
    return result;
}

ClusteringResult ClusteringEngine::isodata(const GeoImageData& image,
                                            const std::vector<int>& bandIndices,
                                            int minClusterSize, double maxStdDev,
                                            double minClusterDistance, int maxMergePairs)
{
    QElapsedTimer totalTimer, stageTimer;
    totalTimer.start();

    // ===== Stage 1: Feature Extraction (0% ~ 8%) =====
    emit statusMessage(QString::fromUtf8("ISODATA: 正在提取特征..."));
    QApplication::processEvents();
    stageTimer.start();

    std::vector<int> bands = bandIndices;
    if (bands.empty()) {
        for (int i = 0; i < image.bands; ++i)
            bands.push_back(i);
    }

    std::vector<double> flatFeatures;
    int featureDim;
    extractPixelFeaturesFlat(image, bands, flatFeatures, featureDim,
                              m_config.usePixelPositions, m_config.positionWeight);

    if (flatFeatures.empty() || featureDim == 0) {
        emit statusMessage(QString::fromUtf8("ISODATA: 内存不足，无法分配特征空间"));
        emit progressUpdated(0);
        QApplication::processEvents();
        ClusteringResult empty;
        return empty;
    }

    int n = image.pixelCount();
    if (n == 0 || m_config.numClusters <= 0)
        return ClusteringResult();

    emit progressUpdated(8);
    QApplication::processEvents();

    double extractTime = stageTimer.elapsed() / 1000.0;
    emit statusMessage(QString::fromUtf8("ISODATA: 特征提取完成 (%1 样本, %2 维度, 耗时 %3s)")
                           .arg(n).arg(featureDim).arg(extractTime, 0, 'f', 2));

    // ===== Stage 2: Normalization / Scaling (8% ~ 12%) =====
    stageTimer.restart();
    if (m_config.normalizeFeatures) {
        normalizeFeatureMatrixFlat(flatFeatures, n, featureDim);
    } else if (m_config.featureScaleFactor > 0.001) {
        scaleFeatureMatrixFlat(flatFeatures, n, featureDim, m_config.featureScaleFactor);
    }
    emit progressUpdated(12);
    QApplication::processEvents();

    double prepTime = stageTimer.elapsed() / 1000.0;
    emit statusMessage(QString::fromUtf8("ISODATA: 数据预处理完成 (耗时 %1s)，开始迭代聚类...")
                           .arg(prepTime, 0, 'f', 2));

    // ===== Stage 3: Core ISODATA Clustering (12% ~ 90%) =====
    stageTimer.restart();
    ClusteringResult result = isodataFlat(flatFeatures, n, featureDim,
                                           minClusterSize, maxStdDev,
                                           minClusterDistance, maxMergePairs);

    double clusterTime = stageTimer.elapsed() / 1000.0;

    // ===== Stage 4: Finalization (90% ~ 100%) =====
    emit progressUpdated(95);
    emit statusMessage(QString::fromUtf8("ISODATA: 正在整理聚类结果..."));
    QApplication::processEvents();

    // Ensure centroids are in correct format
    int actualK = static_cast<int>(result.centroids.size());

    result.inertia = 0;
    const double* featPtr = flatFeatures.data();
    for (int i = 0; i < n; ++i) {
        const double* fi = featPtr + static_cast<size_t>(i) * featureDim;
        if (result.labels[i] >= 0 && result.labels[i] < actualK) {
            result.inertia += euclideanDistSqFlat(fi,
                result.centroids[result.labels[i]].data(), featureDim);
        }
    }

    result.clusterSizes.resize(actualK, 0);
    for (int label : result.labels) {
        if (label >= 0 && label < actualK)
            result.clusterSizes[label]++;
    }

    emit progressUpdated(100);

    double totalTime = totalTimer.elapsed() / 1000.0;
    emit statusMessage(QString::fromUtf8("ISODATA: 聚类完成 - 类别数: %1, 迭代: %2, 惯性: %3, 总耗时 %4s (聚类核心 %5s)")
                           .arg(actualK).arg(result.iterations)
                           .arg(result.inertia, 0, 'f', 2)
                           .arg(totalTime, 0, 'f', 2)
                           .arg(clusterTime, 0, 'f', 2));

    return result;
}

std::vector<std::vector<double>> ClusteringEngine::extractPixelFeatures(
    const GeoImageData& image, const std::vector<int>& bandIndices,
    bool includePosition, double posWeight)
{
    int n = image.pixelCount();
    int featureDim = static_cast<int>(bandIndices.size());
    if (includePosition) featureDim += 2;

    std::vector<std::vector<double>> features(n, std::vector<double>(featureDim));

    for (int i = 0; i < n; ++i) {
        int col = i % image.width;
        int row = i / image.width;
        for (size_t b = 0; b < bandIndices.size(); ++b) {
            features[i][b] = image.pixelValue(bandIndices[b], col, row);
        }
        if (includePosition) {
            int offset = static_cast<int>(bandIndices.size());
            features[i][offset] = col * posWeight / image.width;
            features[i][offset + 1] = row * posWeight / image.height;
        }
    }
    return features;
}

void ClusteringEngine::extractPixelFeaturesFlat(
    const GeoImageData& image, const std::vector<int>& bandIndices,
    std::vector<double>& flatFeatures, int& featureDim,
    bool includePosition, double posWeight)
{
    int n = image.pixelCount();
    int dim = static_cast<int>(bandIndices.size());
    if (includePosition) dim += 2;
    featureDim = dim;

    try {
        flatFeatures.resize(static_cast<size_t>(n) * dim);
    } catch (const std::bad_alloc&) {
        // 内存不足，返回空结果
        flatFeatures.clear();
        featureDim = 0;
        return;
    }

    // 预缓存波段数据指针，避免每个像素调用 pixelValue() 产生函数调用开销
    int nBands = static_cast<int>(bandIndices.size());
    std::vector<const double*> bandPtrs(nBands);
    for (int b = 0; b < nBands; ++b) {
        bandPtrs[b] = image.rawBandData(bandIndices[b]);
    }

    int w = image.width;

    for (int i = 0; i < n; ++i) {
        double* dst = &flatFeatures[static_cast<size_t>(i) * dim];
        int col = i % w;
        int row = i / w;
        int pixelOffset = row * w + col;

        for (int b = 0; b < nBands; ++b) {
            dst[b] = bandPtrs[b][pixelOffset];
        }
        if (includePosition) {
            dst[nBands] = col * posWeight / w;
            dst[nBands + 1] = row * posWeight / image.height;
        }

        // 大图特征提取时保持 UI 响应
        if (i % 500000 == 0) {
            QApplication::processEvents();
        }
    }
}

void ClusteringEngine::normalizeFeatureMatrix(std::vector<std::vector<double>>& features)
{
    if (features.empty()) return;
    int n = static_cast<int>(features.size());
    int dim = static_cast<int>(features[0].size());

    for (int d = 0; d < dim; ++d) {
        double mean = 0;
        for (int i = 0; i < n; ++i)
            mean += features[i][d];
        mean /= n;

        double stddev = 0;
        for (int i = 0; i < n; ++i) {
            double diff = features[i][d] - mean;
            stddev += diff * diff;
        }
        stddev = std::sqrt(stddev / n);
        if (stddev < 1e-10) continue;

        for (int i = 0; i < n; ++i)
            features[i][d] = (features[i][d] - mean) / stddev;
    }
}

void ClusteringEngine::normalizeFeatureMatrixFlat(std::vector<double>& features, int n, int dim)
{
    if (features.empty() || n == 0) return;

    for (int d = 0; d < dim; ++d) {
        double mean = 0;
        for (int i = 0; i < n; ++i)
            mean += features[i * dim + d];
        mean /= n;

        double stddev = 0;
        for (int i = 0; i < n; ++i) {
            double diff = features[i * dim + d] - mean;
            stddev += diff * diff;
        }
        stddev = std::sqrt(stddev / n);
        if (stddev < 1e-10) continue;

        for (int i = 0; i < n; ++i)
            features[i * dim + d] = (features[i * dim + d] - mean) / stddev;
    }
}

void ClusteringEngine::scaleFeatureMatrixFlat(std::vector<double>& features, int n, int dim, double factor)
{
    // 部分特征缩放：对每个波段做温和的缩放，保留波段间自然方差差异
    // factor=0: 完全不缩放，factor=1: 等效于完整Z-score归一化
    if (features.empty() || n == 0 || factor <= 0) return;

    for (int d = 0; d < dim; ++d) {
        double mean = 0;
        for (int i = 0; i < n; ++i)
            mean += features[i * dim + d];
        mean /= n;

        double stddev = 0;
        for (int i = 0; i < n; ++i) {
            double diff = features[i * dim + d] - mean;
            stddev += diff * diff;
        }
        stddev = std::sqrt(stddev / n);
        if (stddev < 1e-10) continue;

        // 混合：原始值 + factor * (标准化值 - 原始值)，即部分缩放
        double targetStd = stddev * (1.0 - factor) + 1.0 * factor;
        double scale = targetStd / stddev;
        for (int i = 0; i < n; ++i)
            features[i * dim + d] = (features[i * dim + d] - mean) * scale + mean;
    }
}

void ClusteringEngine::initializeCentroids(const std::vector<std::vector<double>>& features,
                                            std::vector<std::vector<double>>& centroids,
                                            int k, int featureDim)
{
    int n = static_cast<int>(features.size());
    centroids.assign(k, std::vector<double>(featureDim, 0));

    std::uniform_int_distribution<int> dist(0, n - 1);
    int firstIdx = dist(m_rng);
    centroids[0] = features[firstIdx];

    for (int j = 1; j < k; ++j) {
        std::vector<double> minDistSq(n, std::numeric_limits<double>::max());
        double totalDist = 0;
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < j; ++c) {
                double d = euclideanDistance(features[i], centroids[c]);
                minDistSq[i] = std::min(minDistSq[i], d * d);
            }
            totalDist += minDistSq[i];
        }

        std::uniform_real_distribution<double> randDist(0, totalDist);
        double threshold = randDist(m_rng);
        double cumulative = 0;
        int chosenIdx = n - 1;
        for (int i = 0; i < n; ++i) {
            cumulative += minDistSq[i];
            if (cumulative >= threshold) {
                chosenIdx = i;
                break;
            }
        }
        centroids[j] = features[chosenIdx];
    }
}

void ClusteringEngine::assignLabels(const std::vector<std::vector<double>>& features,
                                     const std::vector<std::vector<double>>& centroids,
                                     std::vector<int>& labels,
                                     std::vector<double>& distances)
{
    int n = static_cast<int>(features.size());
    int k = static_cast<int>(centroids.size());

    for (int i = 0; i < n; ++i) {
        double bestDist = std::numeric_limits<double>::max();
        int bestLabel = 0;
        for (int j = 0; j < k; ++j) {
            double d = euclideanDistance(features[i], centroids[j]);
            if (d < bestDist) {
                bestDist = d;
                bestLabel = j;
            }
        }
        labels[i] = bestLabel;
        distances[i] = bestDist * bestDist;
    }
}

bool ClusteringEngine::updateCentroids(const std::vector<std::vector<double>>& features,
                                        const std::vector<int>& labels,
                                        std::vector<std::vector<double>>& centroids,
                                        int k, int featureDim)
{
    int n = static_cast<int>(features.size());
    std::vector<std::vector<double>> newCentroids(k, std::vector<double>(featureDim, 0));
    std::vector<int> counts(k, 0);

    for (int i = 0; i < n; ++i) {
        int label = labels[i];
        if (label < 0 || label >= k) continue;
        for (int d = 0; d < featureDim; ++d)
            newCentroids[label][d] += features[i][d];
        counts[label]++;
    }

    bool changed = false;
    for (int j = 0; j < k; ++j) {
        if (counts[j] > 0) {
            for (int d = 0; d < featureDim; ++d) {
                double newVal = newCentroids[j][d] / counts[j];
                if (std::abs(newVal - centroids[j][d]) > 1e-10)
                    changed = true;
                centroids[j][d] = newVal;
            }
        }
    }
    return changed;
}

double ClusteringEngine::euclideanDistance(const std::vector<double>& a, const std::vector<double>& b)
{
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

double ClusteringEngine::euclideanDistFlat(const double* a, const double* b, int dim)
{
    double sum = 0;
    for (int i = 0; i < dim; ++i) {
        double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

double ClusteringEngine::euclideanDistSqFlat(const double* a, const double* b, int dim)
{
    double sum = 0;
    for (int i = 0; i < dim; ++i) {
        double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sum;
}

// =============== 扁平化存储核心算法 ===============

void ClusteringEngine::initializeCentroidsFlat(const std::vector<double>& features, int n, int dim,
                                                std::vector<double>& centroidsFlat, int k)
{
    centroidsFlat.assign(static_cast<size_t>(k) * dim, 0.0);

    std::uniform_int_distribution<int> dist(0, n - 1);
    int firstIdx = dist(m_rng);
    std::copy(&features[static_cast<size_t>(firstIdx) * dim],
              &features[(static_cast<size_t>(firstIdx) + 1) * dim],
              &centroidsFlat[0]);

    // K-Means++ 初始化
    std::vector<double> minDistSq(n, 1e300);

    for (int j = 1; j < k; ++j) {
        double totalDist = 0;
        const double* cj = &centroidsFlat[0];

        for (int i = 0; i < n; ++i) {
            const double* fi = &features[static_cast<size_t>(i) * dim];
            double bestDist = 1e300;
            for (int c = 0; c < j; ++c) {
                double d = euclideanDistFlat(fi, cj + static_cast<size_t>(c) * dim, dim);
                if (d < bestDist) bestDist = d;
            }
            minDistSq[i] = bestDist * bestDist;
            totalDist += minDistSq[i];
        }

        std::uniform_real_distribution<double> randDist(0, totalDist);
        double threshold = randDist(m_rng);
        double cumulative = 0;
        int chosenIdx = n - 1;
        for (int i = 0; i < n; ++i) {
            cumulative += minDistSq[i];
            if (cumulative >= threshold) {
                chosenIdx = i;
                break;
            }
        }
        std::copy(&features[static_cast<size_t>(chosenIdx) * dim],
                  &features[(static_cast<size_t>(chosenIdx) + 1) * dim],
                  &centroidsFlat[static_cast<size_t>(j) * dim]);
    }
}

void ClusteringEngine::assignLabelsFlat(const std::vector<double>& features, int n, int dim,
                                         const std::vector<double>& centroidsFlat, int k,
                                         std::vector<int>& labels, std::vector<double>& distances)
{
    for (int i = 0; i < n; ++i) {
        const double* fi = &features[static_cast<size_t>(i) * dim];
        double bestDist = 1e300;
        int bestLabel = 0;
        for (int j = 0; j < k; ++j) {
            double d = euclideanDistFlat(fi, &centroidsFlat[static_cast<size_t>(j) * dim], dim);
            if (d < bestDist) {
                bestDist = d;
                bestLabel = j;
            }
        }
        labels[i] = bestLabel;
        distances[i] = bestDist * bestDist;
    }
}

bool ClusteringEngine::updateCentroidsFlat(const std::vector<double>& features, int n, int dim,
                                            const std::vector<int>& labels,
                                            std::vector<double>& centroidsFlat, int k)
{
    std::vector<double> newCentroids(static_cast<size_t>(k) * dim, 0.0);
    std::vector<int> counts(k, 0);

    for (int i = 0; i < n; ++i) {
        int label = labels[i];
        if (label < 0 || label >= k) continue;
        const double* fi = &features[static_cast<size_t>(i) * dim];
        double* nc = &newCentroids[static_cast<size_t>(label) * dim];
        for (int d = 0; d < dim; ++d)
            nc[d] += fi[d];
        counts[label]++;
    }

    bool changed = false;
    for (int j = 0; j < k; ++j) {
        if (counts[j] > 0) {
            double* nc = &newCentroids[static_cast<size_t>(j) * dim];
            double* old = &centroidsFlat[static_cast<size_t>(j) * dim];
            double invCount = 1.0 / counts[j];
            for (int d = 0; d < dim; ++d) {
                double newVal = nc[d] * invCount;
                if (std::abs(newVal - old[d]) > 1e-10)
                    changed = true;
                old[d] = newVal;
            }
        }
    }
    return changed;
}

ClusteringResult ClusteringEngine::kMeansFlat(const std::vector<double>& features, int n, int featureDim)
{
    ClusteringResult result;
    int k = m_config.numClusters;

    if (n == 0 || k <= 0 || featureDim <= 0)
        return result;

    k = std::min(k, n);

    // 多轮初始化选最优：尝试 n_init 次不同初始化，取惯性最小的结果
    const int nInit = 3;
    double bestInertia = 1e300;
    std::vector<int> bestLabels;
    std::vector<double> bestCentroids;
    const int totalIters = nInit * m_config.maxIterations;

    for (int init = 0; init < nInit; ++init) {
        std::vector<double> centroidsFlat;
        initializeCentroidsFlat(features, n, featureDim, centroidsFlat, k);

        std::vector<int> labels(n, 0);

        for (int iter = 0; iter < m_config.maxIterations; ++iter) {
            std::vector<double> distances(n, 0);
            assignLabelsFlat(features, n, featureDim, centroidsFlat, k, labels, distances);

            double inertia = 0;
            for (double d : distances) inertia += d;

            std::vector<double> oldCentroids = centroidsFlat;

            bool changed = updateCentroidsFlat(features, n, featureDim, labels, centroidsFlat, k);

            // 逐迭代更新进度，避免进度条长时间停滞
            int globalIter = init * m_config.maxIterations + iter + 1;
            emit progressUpdated(globalIter * 100 / totalIters);
            QApplication::processEvents();

            if (!changed) {
                if (inertia < bestInertia) {
                    bestInertia = inertia;
                    bestLabels = labels;
                    bestCentroids = centroidsFlat;
                }
                break;
            }

            double maxShift = 0;
            for (int j = 0; j < k; ++j) {
                double shift = euclideanDistFlat(&oldCentroids[static_cast<size_t>(j) * featureDim],
                                                  &centroidsFlat[static_cast<size_t>(j) * featureDim],
                                                  featureDim);
                maxShift = std::max(maxShift, shift);
            }
            if (maxShift < m_config.convergenceThreshold) {
                if (inertia < bestInertia) {
                    bestInertia = inertia;
                    bestLabels = labels;
                    bestCentroids = centroidsFlat;
                }
                break;
            }

            if (iter == m_config.maxIterations - 1 && inertia < bestInertia) {
                bestInertia = inertia;
                bestLabels = labels;
                bestCentroids = centroidsFlat;
            }
        }
    }

    // 使用最优结果
    result.labels = bestLabels;
    result.inertia = bestInertia;
    result.converged = true;
    result.iterations = m_config.maxIterations;

    // 转换 centroids 为 vector<vector<double>> 格式
    result.centroids.resize(k, std::vector<double>(featureDim));
    for (int j = 0; j < k; ++j) {
        for (int d = 0; d < featureDim; ++d) {
            result.centroids[j][d] = bestCentroids[static_cast<size_t>(j) * featureDim + d];
        }
    }

    result.clusterSizes.resize(k, 0);
    for (int label : result.labels) {
        if (label >= 0 && label < k)
            result.clusterSizes[label]++;
    }

    emit progressUpdated(100);
    return result;
}

// =============== ISODATA 扁平化优化算法 ===============

void ClusteringEngine::assignLabelsFlatParallel(const std::vector<double>& features, int n, int dim,
                                                  const std::vector<double>& centroidsFlat, int k,
                                                  std::vector<int>& labels, std::vector<double>& distancesSq)
{
    if (n == 0 || k == 0) return;

    // 防御性断言：验证输入维度一致
    assert(features.size() == static_cast<size_t>(n) * dim);
    assert(centroidsFlat.size() == static_cast<size_t>(k) * dim);
    assert(labels.size() == static_cast<size_t>(n));
    assert(distancesSq.size() == static_cast<size_t>(n));

    // 确定并行线程数（至少1个，最多取硬件并发数或合理上限）
    int numThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (numThreads <= 0) numThreads = 1;
    numThreads = std::min(numThreads, 8); // 限制最大线程数，避免过度竞争
    numThreads = std::min(numThreads, n); // 不要超过数据量

    if (numThreads <= 1 || n < 5000) {
        // 数据量较小，单线程反而更快（避免线程创建开销）
        for (int i = 0; i < n; ++i) {
            const double* fi = &features[static_cast<size_t>(i) * dim];
            double bestDistSq = 1e300;
            int bestLabel = 0;
            for (int j = 0; j < k; ++j) {
                double dSq = euclideanDistSqFlat(fi, &centroidsFlat[static_cast<size_t>(j) * dim], dim);
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    bestLabel = j;
                }
            }
            labels[i] = bestLabel;
            distancesSq[i] = bestDistSq;
        }
        return;
    }

    // 多线程分块处理
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    int chunkSize = (n + numThreads - 1) / numThreads;

    for (int t = 0; t < numThreads; ++t) {
        int start = t * chunkSize;
        int end = std::min(start + chunkSize, n);
        if (start >= n) break;

        threads.emplace_back([&features, &centroidsFlat, &labels, &distancesSq,
                               start, end, dim, k]() {
            for (int i = start; i < end; ++i) {
                const double* fi = &features[static_cast<size_t>(i) * dim];
                double bestDistSq = 1e300;
                int bestLabel = 0;
                for (int j = 0; j < k; ++j) {
                    double dSq = euclideanDistSqFlat(fi,
                        &centroidsFlat[static_cast<size_t>(j) * dim], dim);
                    if (dSq < bestDistSq) {
                        bestDistSq = dSq;
                        bestLabel = j;
                    }
                }
                labels[i] = bestLabel;
                distancesSq[i] = bestDistSq;
            }
        });
    }

    for (auto& t : threads)
        t.join();
}

ClusteringResult ClusteringEngine::isodataFlat(const std::vector<double>& features, int n, int featureDim,
                                                 int minClusterSize, double maxStdDev,
                                                 double minClusterDistance, int maxMergePairs)
{
    ClusteringResult result;
    int k = m_config.numClusters;
    const int initialK = k;

    if (n == 0 || k <= 0 || featureDim <= 0)
        return result;

    // 防御性断言：验证输入数据尺寸一致
    assert(static_cast<size_t>(n) * featureDim == features.size());
    assert(n > 0 && featureDim > 0 && k > 0);

    k = std::min(k, n);

    // 使用扁平化中心点存储
    std::vector<double> centroidsFlat;
    initializeCentroidsFlat(features, n, featureDim, centroidsFlat, k);
    assert(centroidsFlat.size() == static_cast<size_t>(k) * featureDim);

    std::vector<int> labels(n, 0);
    std::vector<double> distancesSq(n, 0);
    std::vector<int> clusterSizes(k, 0);

    // 预分配临时数组（复用减少内存分配）
    std::vector<double> clusterSums;  // per-cluster per-dim sums
    std::vector<double> clusterSqSums; // per-cluster per-dim sum of squares

    int actualIterations = 0;

    for (int iter = 0; iter < m_config.maxIterations; ++iter) {
        actualIterations = iter + 1;

        // ---- Step A: 并行标签分配 (使用平方距离) ----
        assert(centroidsFlat.size() == static_cast<size_t>(k) * featureDim);
        assignLabelsFlatParallel(features, n, featureDim, centroidsFlat, k, labels, distancesSq);

        // ---- Step B: 更新中心点 ----
        updateCentroidsFlat(features, n, featureDim, labels, centroidsFlat, k);

        // ---- Step C: 计算各类别大小 ----
        clusterSizes.assign(k, 0);
        for (int i = 0; i < n; ++i) {
            if (labels[i] >= 0 && labels[i] < k)
                clusterSizes[labels[i]]++;
        }

        // ---- 进度更新 (12% ~ 90% 范围内按比例) ----
        {
            int progressInPhase = iter * 78 / m_config.maxIterations;
            emit progressUpdated(12 + progressInPhase);
            if (iter % 3 == 0 || iter == m_config.maxIterations - 1) {
                emit statusMessage(QString::fromUtf8("ISODATA: 迭代 %1/%2, 当前类别数 %3")
                                       .arg(iter + 1).arg(m_config.maxIterations).arg(k));
            }
        }
        QApplication::processEvents();

        // ---- Step D: 删除过小的聚类 ----
        bool modified = false;
        {
            // 构建索引映射表，标记要删除的聚类
            std::vector<int> indexMap(k, -1); // oldIndex -> newIndex, -1 表示删除
            int newK = 0;
            std::vector<int> toDelete;

            for (int j = 0; j < k; ++j) {
                if (clusterSizes[j] < minClusterSize && k > 1 && toDelete.size() < static_cast<size_t>(k - 1)) {
                    toDelete.push_back(j);
                    modified = true;
                } else {
                    indexMap[j] = newK++;
                }
            }

            if (modified) {
                // 压缩中心点
                std::vector<double> newCentroids(static_cast<size_t>(newK) * featureDim);
                for (int j = 0; j < k; ++j) {
                    if (indexMap[j] >= 0) {
                        std::copy(&centroidsFlat[static_cast<size_t>(j) * featureDim],
                                  &centroidsFlat[(static_cast<size_t>(j) + 1) * featureDim],
                                  &newCentroids[static_cast<size_t>(indexMap[j]) * featureDim]);
                    }
                }
                centroidsFlat.swap(newCentroids);

                // 重新分配标签到最近的有效聚类
                for (int i = 0; i < n; ++i) {
                    int oldLabel = labels[i];
                    if (indexMap[oldLabel] < 0) {
                        // 该点属于被删除的聚类，重新分配到最近聚类
                        const double* fi = &features[static_cast<size_t>(i) * featureDim];
                        double bestDistSq = 1e300;
                        int bestLabel = 0;
                        for (int j = 0; j < newK; ++j) {
                            double dSq = euclideanDistSqFlat(fi,
                                &centroidsFlat[static_cast<size_t>(j) * featureDim], featureDim);
                            if (dSq < bestDistSq) {
                                bestDistSq = dSq;
                                bestLabel = j;
                            }
                        }
                        labels[i] = bestLabel;
                    } else {
                        labels[i] = indexMap[oldLabel];
                    }
                }
                k = newK;
                assert(centroidsFlat.size() == static_cast<size_t>(k) * featureDim);
            }
        }

        // ---- Step E: 分裂高方差的聚类 (仅当聚类数未达到上限) ----
        if (k < 2 * initialK && k > 0) {
            // 一次性计算每个聚类的均值和标准差
            // 使用 Welford 在线算法避免浮点精度问题
            clusterSums.assign(static_cast<size_t>(k) * featureDim, 0.0);
            clusterSqSums.assign(static_cast<size_t>(k) * featureDim, 0.0);
            clusterSizes.assign(k, 0);

            for (int i = 0; i < n; ++i) {
                int lbl = labels[i];
                if (lbl < 0 || lbl >= k) continue;
                const double* fi = &features[static_cast<size_t>(i) * featureDim];
                double* sumPtr = &clusterSums[static_cast<size_t>(lbl) * featureDim];
                double* sqSumPtr = &clusterSqSums[static_cast<size_t>(lbl) * featureDim];
                for (int d = 0; d < featureDim; ++d) {
                    sumPtr[d] += fi[d];
                    sqSumPtr[d] += fi[d] * fi[d];
                }
                clusterSizes[lbl]++;
            }

            // 寻找标准差最大的聚类
            int splitIdx = -1;
            int splitDim = -1;
            double maxStdDevFound = 0;

            for (int j = 0; j < k; ++j) {
                if (clusterSizes[j] < minClusterSize * 2) continue; // 太小不分裂
                double invN = 1.0 / clusterSizes[j];
                const double* sumPtr = &clusterSums[static_cast<size_t>(j) * featureDim];
                const double* sqSumPtr = &clusterSqSums[static_cast<size_t>(j) * featureDim];
                for (int d = 0; d < featureDim; ++d) {
                    double mean = sumPtr[d] * invN;
                    double var = sqSumPtr[d] * invN - mean * mean;
                    if (var < 0) var = 0;
                    double stdDev = std::sqrt(var);
                    if (stdDev > maxStdDevFound) {
                        maxStdDevFound = stdDev;
                        splitIdx = j;
                        splitDim = d;
                    }
                }
            }

            if (splitIdx >= 0 && maxStdDevFound > maxStdDev) {
                // 分裂：沿最大标准差维度偏移 ±0.5*stdDev
                double invN = 1.0 / clusterSizes[splitIdx];
                const double* sumPtr = &clusterSums[static_cast<size_t>(splitIdx) * featureDim];
                double mean = sumPtr[splitDim] * invN;

                // 扩展中心点数组
                std::vector<double> newCentroids(static_cast<size_t>(k + 1) * featureDim);
                std::copy(centroidsFlat.begin(), centroidsFlat.end(), newCentroids.begin());

                // 复制原中心点作为新中心点的基础
                std::copy(&centroidsFlat[static_cast<size_t>(splitIdx) * featureDim],
                          &centroidsFlat[(static_cast<size_t>(splitIdx) + 1) * featureDim],
                          &newCentroids[static_cast<size_t>(k) * featureDim]);

                double* splitPtr = &newCentroids[static_cast<size_t>(splitIdx) * featureDim + splitDim];
                double* newPtr = &newCentroids[static_cast<size_t>(k) * featureDim + splitDim];
                *splitPtr = mean + 0.5 * maxStdDevFound;
                *newPtr = mean - 0.5 * maxStdDevFound;

                centroidsFlat.swap(newCentroids);
                k++;
                assert(centroidsFlat.size() == static_cast<size_t>(k) * featureDim);
                modified = true;
            }
        }

        // ---- Step F: 合并过近的聚类（最多合并 maxMergePairs 对） ----
        {
            // 计算所有聚类对之间的距离
            struct MergePair {
                int i, j;
                double distSq;
                bool operator<(const MergePair& o) const { return distSq < o.distSq; }
            };
            std::vector<MergePair> candidates;

            // 更新 clusterSizes
            clusterSizes.assign(k, 0);
            for (int i = 0; i < n; ++i) {
                if (labels[i] >= 0 && labels[i] < k)
                    clusterSizes[labels[i]]++;
            }

            for (int i = 0; i < k; ++i) {
                for (int j = i + 1; j < k; ++j) {
                    double dist = euclideanDistFlat(
                        &centroidsFlat[static_cast<size_t>(i) * featureDim],
                        &centroidsFlat[static_cast<size_t>(j) * featureDim],
                        featureDim);
                    if (dist < minClusterDistance) {
                        candidates.push_back({i, j, dist * dist});
                    }
                }
            }

            // 按距离升序排列
            std::sort(candidates.begin(), candidates.end());

            // 执行合并（最多 maxMergePairs 对）
            int mergedCount = 0;
            // 使用简单的并查集 parent 数组，parent[j] == j 表示根节点
            std::vector<int> parent(k);
            for (int j = 0; j < k; ++j)
                parent[j] = j;

            // 辅助函数：查找根节点（带路径压缩）
            auto findRoot = [&parent](int x) -> int {
                while (parent[x] != x) {
                    parent[x] = parent[parent[x]]; // 路径压缩
                    x = parent[x];
                }
                return x;
            };

            for (const auto& c : candidates) {
                if (mergedCount >= maxMergePairs) break;

                int rootI = findRoot(c.i);
                int rootJ = findRoot(c.j);

                if (rootI == rootJ) continue; // 已合并

                // 合并到索引较小的聚类
                int target = std::min(rootI, rootJ);
                int source = std::max(rootI, rootJ);

                // 加权平均更新中心点
                int sizeTarget = clusterSizes[target];
                int sizeSource = clusterSizes[source];
                int totalSize = sizeTarget + sizeSource;
                if (totalSize > 0) {
                    double* tgt = &centroidsFlat[static_cast<size_t>(target) * featureDim];
                    const double* src = &centroidsFlat[static_cast<size_t>(source) * featureDim];
                    for (int d = 0; d < featureDim; ++d) {
                        tgt[d] = (tgt[d] * sizeTarget + src[d] * sizeSource) / totalSize;
                    }
                }

                parent[source] = target;
                clusterSizes[target] = totalSize;
                clusterSizes[source] = 0;
                mergedCount++;
                modified = true;
            }

            if (mergedCount > 0) {
                // ---- 简化压缩：一次遍历构建清晰的 oldIndex -> newIndex 映射 ----
                // Step 1: 收集所有存活根节点，为其分配连续的 new index
                std::vector<int> rootToNewIdx(k, -1);
                int newK = 0;
                for (int j = 0; j < k; ++j) {
                    if (parent[j] == j && clusterSizes[j] > 0) {
                        rootToNewIdx[j] = newK++;
                    }
                }

                // Step 2: 为每个 old index 构建直接映射
                // oldToNew[j] = 新的压缩索引，或 -1（已被合并掉）
                std::vector<int> oldToNew(k, -1);
                for (int j = 0; j < k; ++j) {
                    int root = findRoot(j);
                    if (root >= 0 && root < k) {
                        oldToNew[j] = rootToNewIdx[root];
                    }
                }

                // Step 3: 压缩中心点（只复制存活根节点的中心点）
                std::vector<double> newCentroids(static_cast<size_t>(newK) * featureDim, 0.0);
                for (int j = 0; j < k; ++j) {
                    if (parent[j] == j && clusterSizes[j] > 0) {
                        int target = rootToNewIdx[j];
                        if (target >= 0) {
                            std::copy(&centroidsFlat[static_cast<size_t>(j) * featureDim],
                                      &centroidsFlat[(static_cast<size_t>(j) + 1) * featureDim],
                                      &newCentroids[static_cast<size_t>(target) * featureDim]);
                        }
                    }
                }
                centroidsFlat.swap(newCentroids);

                // Step 4: 更新所有标签
                for (int i = 0; i < n; ++i) {
                    int lbl = labels[i];
                    if (lbl >= 0 && lbl < k) {
                        int newLbl = oldToNew[lbl];
                        labels[i] = (newLbl >= 0) ? newLbl : 0;
                    }
                }

                k = newK;
                assert(centroidsFlat.size() == static_cast<size_t>(k) * featureDim);
            }
        }

        // ---- Step G: 检查收敛 ----
        if (!modified) {
            emit statusMessage(QString::fromUtf8("ISODATA: 收敛于迭代 %1，最终类别数 %2")
                                   .arg(iter + 1).arg(k));
            break;
        }
    }

    // ---- 构建返回结果 ----
    assert(k > 0);
    assert(centroidsFlat.size() == static_cast<size_t>(k) * featureDim);
    result.labels = labels;
    result.iterations = actualIterations;
    result.converged = true;

    // 转换扁平中心点为 vector<vector<double>> 格式
    result.centroids.resize(k, std::vector<double>(featureDim));
    for (int j = 0; j < k; ++j) {
        for (int d = 0; d < featureDim; ++d) {
            result.centroids[j][d] = centroidsFlat[static_cast<size_t>(j) * featureDim + d];
        }
    }

    return result;
}

std::vector<QColor> ClusteringEngine::generateDistinctColors(int n)
{
    std::vector<QColor> colors;
    for (int i = 0; i < n; ++i) {
        int hue = (i * 360 / n) % 360;
        colors.push_back(QColor::fromHsv(hue, 200, 255));
    }
    return colors;
}

ClassificationResult ClusteringResult::toClassificationResult(int width, int height) const
{
    ClassificationResult cr;
    cr.width = width;
    cr.height = height;
    cr.classCount = static_cast<int>(centroids.size());
    cr.labelMap = labels;
    cr.methodName = QString::fromUtf8("\u805A\u7C7B\u5206\u6790");

    int k = static_cast<int>(centroids.size());
    for (int i = 0; i < k; ++i) {
        cr.classNames.push_back(QString::fromUtf8("\u7C7B\u522B_") + QString::number(i + 1));
        int hue = (i * 360 / k) % 360;
        cr.classColors.push_back(QColor::fromHsv(hue, 200, 255));
    }
    return cr;
}