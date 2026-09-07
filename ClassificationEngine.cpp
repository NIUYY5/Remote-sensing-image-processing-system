#include "ClassificationEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <random>
#include <QApplication>

ClassificationEngine::ClassificationEngine(QObject* parent)
    : QObject(parent)
{
}

ClassificationEngine::~ClassificationEngine()
{
}

void ClassificationEngine::setConfig(const ClassifierConfig& config)
{
    m_config = config;
}

TrainingData ClassificationEngine::createTrainingData(
    const GeoImageData& image,
    const std::vector<QPoint>& samplePositions,
    const std::vector<int>& sampleLabels,
    const std::vector<QString>& classNames,
    const std::vector<int>& bandIndices)
{
    TrainingData td;
    std::vector<int> bands = bandIndices;
    if (bands.empty()) {
        for (int i = 0; i < image.bands; ++i)
            bands.push_back(i);
    }

    td.classCount = static_cast<int>(classNames.size());
    td.featureDim = static_cast<int>(bands.size());
    td.classNames = classNames;

    for (size_t i = 0; i < samplePositions.size() && i < sampleLabels.size(); ++i) {
        TrainingSample sample;
        sample.position = samplePositions[i];
        sample.classId = sampleLabels[i];
        for (int b : bands)
            sample.features.push_back(image.pixelValue(b, samplePositions[i].x(), samplePositions[i].y()));
        td.samples.push_back(sample);
    }

    td.classStatistics.resize(td.classCount, std::vector<double>(td.featureDim, 0));
    td.classCovariance.resize(td.classCount, std::vector<double>(td.featureDim * td.featureDim, 0));

    std::vector<int> counts(td.classCount, 0);
    for (const auto& s : td.samples) {
        for (int d = 0; d < td.featureDim; ++d)
            td.classStatistics[s.classId][d] += s.features[d];
        counts[s.classId]++;
    }

    for (int c = 0; c < td.classCount; ++c) {
        if (counts[c] > 0) {
            for (int d = 0; d < td.featureDim; ++d)
                td.classStatistics[c][d] /= counts[c];
        }
    }

    for (const auto& s : td.samples) {
        for (int d1 = 0; d1 < td.featureDim; ++d1) {
            double diff1 = s.features[d1] - td.classStatistics[s.classId][d1];
            for (int d2 = 0; d2 < td.featureDim; ++d2) {
                double diff2 = s.features[d2] - td.classStatistics[s.classId][d2];
                td.classCovariance[s.classId][d1 * td.featureDim + d2] += diff1 * diff2;
            }
        }
    }

    for (int c = 0; c < td.classCount; ++c) {
        if (counts[c] > 1) {
            for (int i = 0; i < td.featureDim * td.featureDim; ++i)
                td.classCovariance[c][i] /= (counts[c] - 1);
        }
    }

    return td;
}

TrainingData ClassificationEngine::createTrainingDataFromROI(
    const GeoImageData& image,
    const std::vector<std::vector<QPoint>>& roiPolygons,
    const std::vector<QString>& classNames,
    const std::vector<int>& bandIndices)
{
    std::vector<int> bands = bandIndices;
    if (bands.empty()) {
        for (int i = 0; i < image.bands; ++i)
            bands.push_back(i);
    }

    TrainingData td;
    td.classCount = static_cast<int>(classNames.size());
    td.featureDim = static_cast<int>(bands.size());
    td.classNames = classNames;

    for (size_t c = 0; c < roiPolygons.size(); ++c) {
        if (roiPolygons[c].size() < 3) continue;

        int minX = image.width, maxX = 0, minY = image.height, maxY = 0;
        for (const auto& pt : roiPolygons[c]) {
            minX = std::min(minX, pt.x()); maxX = std::max(maxX, pt.x());
            minY = std::min(minY, pt.y()); maxY = std::max(maxY, pt.y());
        }
        minX = std::max(0, minX); maxX = std::min(image.width - 1, maxX);
        minY = std::max(0, minY); maxY = std::min(image.height - 1, maxY);

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                bool inside = false;
                int n = static_cast<int>(roiPolygons[c].size());
                for (int i = 0, j = n - 1; i < n; j = i++) {
                    if (((roiPolygons[c][i].y() > y) != (roiPolygons[c][j].y() > y)) &&
                        (x < (roiPolygons[c][j].x() - roiPolygons[c][i].x()) * (y - roiPolygons[c][i].y())
                             / (roiPolygons[c][j].y() - roiPolygons[c][i].y()) + roiPolygons[c][i].x()))
                        inside = !inside;
                }
                if (inside) {
                    TrainingSample sample;
                    sample.position = QPoint(x, y);
                    sample.classId = static_cast<int>(c);
                    for (int b : bands)
                        sample.features.push_back(image.pixelValue(b, x, y));
                    td.samples.push_back(sample);
                }
            }
        }
    }

    td.classStatistics.resize(td.classCount, std::vector<double>(td.featureDim, 0));
    td.classCovariance.resize(td.classCount, std::vector<double>(td.featureDim * td.featureDim, 0));
    std::vector<int> counts(td.classCount, 0);

    for (const auto& s : td.samples) {
        for (int d = 0; d < td.featureDim; ++d)
            td.classStatistics[s.classId][d] += s.features[d];
        counts[s.classId]++;
    }

    for (int c = 0; c < td.classCount; ++c) {
        if (counts[c] > 0) {
            for (int d = 0; d < td.featureDim; ++d)
                td.classStatistics[c][d] /= counts[c];
        }
    }

    for (const auto& s : td.samples) {
        for (int d1 = 0; d1 < td.featureDim; ++d1) {
            double diff1 = s.features[d1] - td.classStatistics[s.classId][d1];
            for (int d2 = 0; d2 < td.featureDim; ++d2) {
                double diff2 = s.features[d2] - td.classStatistics[s.classId][d2];
                td.classCovariance[s.classId][d1 * td.featureDim + d2] += diff1 * diff2;
            }
        }
    }

    for (int c = 0; c < td.classCount; ++c) {
        if (counts[c] > 1) {
            for (int i = 0; i < td.featureDim * td.featureDim; ++i)
                td.classCovariance[c][i] /= (counts[c] - 1);
        }
    }

    return td;
}

// 统一入口：仅使用最大似然法
ClassificationResult ClassificationEngine::classify(
    const GeoImageData& image, const TrainingData& trainingData,
    const std::vector<int>& bandIndices)
{
    return classifyMaximumLikelihood(image, trainingData, bandIndices);
}

ClassificationResult ClassificationEngine::classifyMaximumLikelihood(
    const GeoImageData& image, const TrainingData& trainingData,
    const std::vector<int>& bandIndices)
{
    ClassificationResult result;
    result.width = image.width;
    result.height = image.height;
    result.classCount = trainingData.classCount;
    result.classNames = trainingData.classNames;
    result.classColors = predefinedClassColors(trainingData.classCount);
    result.methodName = QString::fromUtf8("\u6700\u5927\u4F3C\u7136\u5206\u7C7B\u5668");
    result.sourceImagePath = image.filePath;

    std::vector<int> bands = bandIndices;
    if (bands.empty()) {
        for (int i = 0; i < image.bands; ++i) bands.push_back(i);
    }

    int dim = static_cast<int>(bands.size());
    int n = image.pixelCount();
    int k = trainingData.classCount;

    // 预计算各类别的逆协方差矩阵和对数行列式
    std::vector<std::vector<double>> invCov(k, std::vector<double>(dim * dim, 0));
    std::vector<double> logDet(k, 0);

    for (int c = 0; c < k; ++c) {
        std::vector<double> cov = trainingData.classCovariance[c];
        // 自适应正则化：基于协方差矩阵对角线均值的比例因子
        double diagMean = 0;
        for (int i = 0; i < dim; ++i)
            diagMean += cov[i * dim + i];
        diagMean /= dim;
        double reg = std::max(1e-4, diagMean * 0.01);  // 对角线均值的1%，至少1e-4
        for (int i = 0; i < dim; ++i)
            cov[i * dim + i] += reg;
        invertMatrix(cov, dim, invCov[c]);
        double det = determinant(cov, dim);
        logDet[c] = (det > 0) ? std::log(det) : 0;
    }

    // 预缓存波段数据指针
    std::vector<const double*> bandPtrs(dim);
    for (int d = 0; d < dim; ++d) {
        bandPtrs[d] = image.rawBandData(bands[d]);
    }
    // 预缓存类别均值 + 逆协方差指针
    std::vector<const double*> classMeanPtrs(k);
    std::vector<const double*> invCovPtrs(k);
    for (int c = 0; c < k; ++c) {
        classMeanPtrs[c] = trainingData.classStatistics[c].data();
        invCovPtrs[c] = invCov[c].data();
    }

    // 为分类结果预分配标签图（大图可能内存不足，try-catch 保证友好报错）
    try {
        result.labelMap.resize(n, 0);
    } catch (const std::bad_alloc&) {
        emit statusMessage(QString::fromUtf8("内存不足：无法分配 %1 像素的分类结果空间").arg(n));
        emit progressUpdated(0);
        QApplication::processEvents();
        result.labelMap.clear();
        result.width = 0;
        result.height = 0;
        return result;
    }

    // 预分配临时向量（栈上分配，减少频繁分配开销）
    std::vector<double> x(dim);
    std::vector<double> diff(dim);

    // 动态调整进度更新步长：大图降低更新频率（避免 processEvents 开销），小图频繁更新
    size_t progressStep = std::max<size_t>(1000, n / 200);
    int lastReportedPercent = -1;

    for (int i = 0; i < n; ++i) {
        // 提取像素特征
        for (int d = 0; d < dim; ++d)
            x[d] = bandPtrs[d][i];

        double bestScore = -std::numeric_limits<double>::max();
        int bestClass = 0;

        for (int c = 0; c < k; ++c) {
            const double* mean = classMeanPtrs[c];
            const double* invC = invCovPtrs[c];

            // 内联马氏距离计算
            double mahDist = 0;
            for (int d1 = 0; d1 < dim; ++d1) {
                diff[d1] = x[d1] - mean[d1];
            }
            for (int d1 = 0; d1 < dim; ++d1) {
                double sum = 0;
                for (int d2 = 0; d2 < dim; ++d2) {
                    sum += diff[d2] * invC[d1 * dim + d2];
                }
                mahDist += diff[d1] * sum;
            }

            double score = -0.5 * (mahDist + logDet[c]);
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }
        result.labelMap[i] = bestClass;

        // 进度更新：大图每 N 像素更新一次，避免整数截断导致长时间 0%
        if (i % progressStep == 0) {
            int pct = static_cast<int>(100.0 * i / n);
            if (pct != lastReportedPercent) {
                lastReportedPercent = pct;
                emit progressUpdated(pct);
                QApplication::processEvents();
            }
        }
    }

    emit progressUpdated(100);
    QApplication::processEvents();

    // 空间后处理：消除细碎斑块和椒盐噪声
    if (m_config.enableMajorityFilter) {
        int ws = std::max(3, std::min(m_config.filterWindowSize, 9));
        // 确保窗口为奇数
        if (ws % 2 == 0) ws++;
        majorityFilter(result.labelMap, result.width, result.height, result.classCount, ws);
        emit progressUpdated(100);
        QApplication::processEvents();
    }
    if (m_config.minRegionSize > 0 && result.classCount > 1) {
        mergeSmallRegions(result.labelMap, result.width, result.height, result.classCount, m_config.minRegionSize);
    }

    return result;
}

void ClassificationEngine::computeClassStatistics(
    const std::vector<std::vector<double>>& samples,
    const std::vector<int>& labels, int classCount, int featureDim,
    std::vector<std::vector<double>>& means,
    std::vector<std::vector<double>>& covariances)
{
    means.assign(classCount, std::vector<double>(featureDim, 0));
    covariances.assign(classCount, std::vector<double>(featureDim * featureDim, 0));
    std::vector<int> counts(classCount, 0);

    for (size_t i = 0; i < samples.size(); ++i) {
        int c = labels[i];
        for (int d = 0; d < featureDim; ++d)
            means[c][d] += samples[i][d];
        counts[c]++;
    }

    for (int c = 0; c < classCount; ++c) {
        if (counts[c] > 0) {
            for (int d = 0; d < featureDim; ++d)
                means[c][d] /= counts[c];
        }
    }

    for (size_t i = 0; i < samples.size(); ++i) {
        int c = labels[i];
        for (int d1 = 0; d1 < featureDim; ++d1) {
            double diff1 = samples[i][d1] - means[c][d1];
            for (int d2 = 0; d2 < featureDim; ++d2) {
                double diff2 = samples[i][d2] - means[c][d2];
                covariances[c][d1 * featureDim + d2] += diff1 * diff2;
            }
        }
    }
    for (int c = 0; c < classCount; ++c) {
        if (counts[c] > 1) {
            for (int i = 0; i < featureDim * featureDim; ++i)
                covariances[c][i] /= (counts[c] - 1);
        }
    }
}

double ClassificationEngine::mahalanobisDistance(
    const std::vector<double>& x, const std::vector<double>& mean,
    const std::vector<double>& invCov, int dim)
{
    std::vector<double> diff(dim);
    for (int i = 0; i < dim; ++i)
        diff[i] = x[i] - mean[i];

    double result = 0;
    for (int i = 0; i < dim; ++i) {
        double sum = 0;
        for (int j = 0; j < dim; ++j)
            sum += invCov[i * dim + j] * diff[j];
        result += diff[i] * sum;
    }
    return result;
}

bool ClassificationEngine::invertMatrix(const std::vector<double>& matrix, int n,
                                         std::vector<double>& inverse)
{
    inverse.resize(n * n, 0);
    std::vector<double> a = matrix;

    for (int i = 0; i < n; ++i)
        inverse[i * n + i] = 1;

    for (int i = 0; i < n; ++i) {
        double pivot = a[i * n + i];
        if (std::abs(pivot) < 1e-12) {
            int swapRow = -1;
            for (int r = i + 1; r < n; ++r) {
                if (std::abs(a[r * n + i]) > 1e-12) {
                    swapRow = r;
                    break;
                }
            }
            if (swapRow < 0) return false;
            for (int j = 0; j < n; ++j) {
                std::swap(a[i * n + j], a[swapRow * n + j]);
                std::swap(inverse[i * n + j], inverse[swapRow * n + j]);
            }
            pivot = a[i * n + i];
        }

        for (int j = 0; j < n; ++j) {
            a[i * n + j] /= pivot;
            inverse[i * n + j] /= pivot;
        }

        for (int r = 0; r < n; ++r) {
            if (r == i) continue;
            double factor = a[r * n + i];
            for (int j = 0; j < n; ++j) {
                a[r * n + j] -= factor * a[i * n + j];
                inverse[r * n + j] -= factor * inverse[i * n + j];
            }
        }
    }
    return true;
}

double ClassificationEngine::determinant(const std::vector<double>& matrix, int n)
{
    if (n == 1) return matrix[0];
    if (n == 2) return matrix[0] * matrix[3] - matrix[1] * matrix[2];

    std::vector<double> a = matrix;
    double det = 1;
    for (int i = 0; i < n; ++i) {
        double pivot = a[i * n + i];
        if (std::abs(pivot) < 1e-12) {
            int swapRow = -1;
            for (int r = i + 1; r < n; ++r) {
                if (std::abs(a[r * n + i]) > 1e-12) {
                    swapRow = r;
                    break;
                }
            }
            if (swapRow < 0) return 0;
            for (int j = 0; j < n; ++j)
                std::swap(a[i * n + j], a[swapRow * n + j]);
            det = -det;
            pivot = a[i * n + i];
        }
        det *= pivot;
        for (int r = i + 1; r < n; ++r) {
            double factor = a[r * n + i] / pivot;
            for (int j = i; j < n; ++j)
                a[r * n + j] -= factor * a[i * n + j];
        }
    }
    return det;
}

std::vector<QColor> ClassificationEngine::predefinedClassColors(int n)
{
    // 统一使用HSV色彩空间生成区分度高的颜色
    // 与聚类分析模块保持一致的颜色编码标准
    std::vector<QColor> colors;
    for (int i = 0; i < n; ++i) {
        int hue = (i * 360 / n) % 360;
        colors.push_back(QColor::fromHsv(hue, 200, 255));
    }
    return colors;
}

// ========== 空间后处理函数 ==========

void ClassificationEngine::majorityFilter(std::vector<int>& labelMap, int width, int height,
                                           int classCount, int windowSize)
{
    // 多数投票滤波：以每个像素为中心，统计窗口内各标签的频数，
    // 将中心像素替换为出现次数最多的标签（众数）。
    // 消除椒盐噪声和孤立像素点，增强区域连贯性。
    int half = windowSize / 2;
    std::vector<int> work = labelMap;  // 在副本上读取，原地写入避免交叉干扰

    for (int y = half; y < height - half; ++y) {
        for (int x = half; x < width - half; ++x) {
            std::vector<int> counts(classCount, 0);

            // 统计窗口内各类别出现次数
            for (int dy = -half; dy <= half; ++dy) {
                for (int dx = -half; dx <= half; ++dx) {
                    int idx = (y + dy) * width + (x + dx);
                    int lbl = work[idx];
                    if (lbl >= 0 && lbl < classCount)
                        counts[lbl]++;
                }
            }

            // 找众数
            int bestLabel = work[y * width + x];  // 保留原值作为默认
            int maxCount = 0;
            for (int c = 0; c < classCount; ++c) {
                if (counts[c] > maxCount) {
                    maxCount = counts[c];
                    bestLabel = c;
                }
            }
            labelMap[y * width + x] = bestLabel;
        }
    }
}

void ClassificationEngine::mergeSmallRegions(std::vector<int>& labelMap, int width, int height,
                                              int classCount, int minSize)
{
    // 合并小连通区域：使用BFS找到所有连通区域，将像素数小于minSize的区域
    // 合并到其相邻的最大区域中，消除零星孤立斑块。
    if (minSize <= 0) return;

    int total = width * height;
    std::vector<bool> visited(total, false);
    // 对每个连通区域，记录其边界接触的相邻区域标签
    // 使用BFS遍历

    for (int seed = 0; seed < total; ++seed) {
        if (visited[seed]) continue;

        int regionLabel = labelMap[seed];
        std::vector<int> regionPixels;
        std::vector<int> queue;
        queue.push_back(seed);
        visited[seed] = true;

        // 记录该区域边界接触的相邻区域标签及其像素数
        std::map<int, int> neighborCounts;

        while (!queue.empty()) {
            int idx = queue.back();
            queue.pop_back();
            regionPixels.push_back(idx);

            int x = idx % width;
            int y = idx / width;

            // 4邻域
            int neighbors[4][2] = {{x-1, y}, {x+1, y}, {x, y-1}, {x, y+1}};
            for (int ni = 0; ni < 4; ++ni) {
                int nx = neighbors[ni][0];
                int ny = neighbors[ni][1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

                int nidx = ny * width + nx;
                int nlbl = labelMap[nidx];
                if (nlbl == regionLabel) {
                    if (!visited[nidx]) {
                        visited[nidx] = true;
                        queue.push_back(nidx);
                    }
                } else {
                    // 边界邻居
                    neighborCounts[nlbl]++;
                }
            }
        }

        // 如果区域太小，合并到相邻最大区域
        if (static_cast<int>(regionPixels.size()) < minSize && !neighborCounts.empty()) {
            int bestNeighbor = -1;
            int bestCount = 0;
            for (const auto& nc : neighborCounts) {
                if (nc.second > bestCount) {
                    bestCount = nc.second;
                    bestNeighbor = nc.first;
                }
            }
            if (bestNeighbor >= 0) {
                for (int idx : regionPixels) {
                    labelMap[idx] = bestNeighbor;
                }
            }
        }
    }
}