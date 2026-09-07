#pragma once

#include "GeoImageData.h"
#include <QObject>
#include <random>

// 监督分类统一使用最大似然法
struct ClassifierConfig
{
    bool enableMajorityFilter = true;   // 启用多数投票滤波，消除椒盐噪声
    int filterWindowSize = 5;           // 滤波窗口大小（3/5/7），越大越平滑
    int minRegionSize = 20;             // 最小连通区域像素数，小于此值将被合并
};

class ClassificationEngine : public QObject
{
    Q_OBJECT

public:
    explicit ClassificationEngine(QObject* parent = nullptr);
    ~ClassificationEngine();

    void setConfig(const ClassifierConfig& config);
    ClassifierConfig config() const { return m_config; }

    TrainingData createTrainingData(const GeoImageData& image,
                                     const std::vector<QPoint>& samplePositions,
                                     const std::vector<int>& sampleLabels,
                                     const std::vector<QString>& classNames,
                                     const std::vector<int>& bandIndices);

    TrainingData createTrainingDataFromROI(const GeoImageData& image,
                                            const std::vector<std::vector<QPoint>>& roiPolygons,
                                            const std::vector<QString>& classNames,
                                            const std::vector<int>& bandIndices);

    // 最大似然法分类（唯一分类方法）
    ClassificationResult classify(const GeoImageData& image,
                                   const TrainingData& trainingData,
                                   const std::vector<int>& bandIndices);

    ClassificationResult classifyMaximumLikelihood(const GeoImageData& image,
                                                     const TrainingData& trainingData,
                                                     const std::vector<int>& bandIndices);

    void computeClassStatistics(const std::vector<std::vector<double>>& samples,
                                 const std::vector<int>& labels,
                                 int classCount, int featureDim,
                                 std::vector<std::vector<double>>& means,
                                 std::vector<std::vector<double>>& covariances);

    static double mahalanobisDistance(const std::vector<double>& x,
                                       const std::vector<double>& mean,
                                       const std::vector<double>& invCov,
                                       int dim);

    static bool invertMatrix(const std::vector<double>& matrix, int n,
                              std::vector<double>& inverse);

    static double determinant(const std::vector<double>& matrix, int n);

    static std::vector<QColor> predefinedClassColors(int n);

    // 空间后处理：多数投票滤波，消除椒盐噪声和细碎斑块
    static void majorityFilter(std::vector<int>& labelMap, int width, int height,
                                int classCount, int windowSize);
    // 合并小连通区域：将小于minSize的孤立区域合并到相邻最大区域
    static void mergeSmallRegions(std::vector<int>& labelMap, int width, int height,
                                   int classCount, int minSize);

signals:
    void progressUpdated(int percent);
    void statusMessage(const QString& msg);

private:
    ClassifierConfig m_config;
};