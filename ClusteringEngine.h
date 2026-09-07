#pragma once

#include "GeoImageData.h"
#include <QObject>
#include <QElapsedTimer>
#include <random>
#include <functional>
#include <vector>

struct ClusteringConfig
{
    int numClusters = 8;                // 增加默认聚类数，保留更多细节
    int maxIterations = 100;
    double convergenceThreshold = 1e-4;
    bool usePixelPositions = true;      // 默认启用空间位置特征，避免忽略空间邻近性
    double positionWeight = 0.5;        // 提高空间位置权重，增强空间约束
    bool normalizeFeatures = false;     // 默认关闭Z-score归一化，保留波段自然方差
    double featureScaleFactor = 0.3;    // 特征缩放因子（0=不缩放，1=完全归一化），折中保留部分方差
    int randomSeed = 42;
};

struct ClusteringResult
{
    std::vector<int> labels;
    std::vector<std::vector<double>> centroids;
    std::vector<int> clusterSizes;
    int iterations = 0;
    double inertia = 0;
    bool converged = false;

    ClassificationResult toClassificationResult(int width, int height) const;
};

class ClusteringEngine : public QObject
{
    Q_OBJECT

public:
    explicit ClusteringEngine(QObject* parent = nullptr);
    ~ClusteringEngine();

    void setConfig(const ClusteringConfig& config);
    ClusteringConfig config() const { return m_config; }

    ClusteringResult kMeans(const GeoImageData& image, const std::vector<int>& bandIndices = {});
    ClusteringResult kMeans(const std::vector<std::vector<double>>& features, int featureDim);
    ClusteringResult isodata(const GeoImageData& image, const std::vector<int>& bandIndices = {},
                             int minClusterSize = 10, double maxStdDev = 2.0,
                             double minClusterDistance = 1.0, int maxMergePairs = 2);

    // 扁平化特征提取（性能优化版）
    static void extractPixelFeaturesFlat(const GeoImageData& image,
                                          const std::vector<int>& bandIndices,
                                          std::vector<double>& flatFeatures,
                                          int& featureDim,
                                          bool includePosition = false,
                                          double posWeight = 0.1);

    static std::vector<std::vector<double>> extractPixelFeatures(const GeoImageData& image,
                                                                  const std::vector<int>& bandIndices,
                                                                  bool includePosition = false,
                                                                  double posWeight = 0.1);
    static void normalizeFeatureMatrix(std::vector<std::vector<double>>& features);
    static void normalizeFeatureMatrixFlat(std::vector<double>& features, int n, int dim);
    static void scaleFeatureMatrixFlat(std::vector<double>& features, int n, int dim, double factor);

signals:
    void progressUpdated(int percent);
    void statusMessage(const QString& msg);

private:
    ClusteringConfig m_config;
    std::mt19937 m_rng;

    // 传统 vector<vector<double>> 版本
    void initializeCentroids(const std::vector<std::vector<double>>& features,
                             std::vector<std::vector<double>>& centroids,
                             int k, int featureDim);
    void assignLabels(const std::vector<std::vector<double>>& features,
                      const std::vector<std::vector<double>>& centroids,
                      std::vector<int>& labels,
                      std::vector<double>& distances);
    bool updateCentroids(const std::vector<std::vector<double>>& features,
                         const std::vector<int>& labels,
                         std::vector<std::vector<double>>& centroids,
                         int k, int featureDim);

    // 扁平化存储版本（高性能）
    void initializeCentroidsFlat(const std::vector<double>& features, int n, int dim,
                                  std::vector<double>& centroidsFlat, int k);
    void assignLabelsFlat(const std::vector<double>& features, int n, int dim,
                          const std::vector<double>& centroidsFlat, int k,
                          std::vector<int>& labels, std::vector<double>& distances);
    bool updateCentroidsFlat(const std::vector<double>& features, int n, int dim,
                             const std::vector<int>& labels,
                             std::vector<double>& centroidsFlat, int k);

    // kMeans 核心（扁平化版本）
    ClusteringResult kMeansFlat(const std::vector<double>& features, int n, int featureDim);

    // ISODATA 核心（扁平化版本，优化性能）
    ClusteringResult isodataFlat(const std::vector<double>& features, int n, int featureDim,
                                  int minClusterSize, double maxStdDev,
                                  double minClusterDistance, int maxMergePairs);

    // 并行分块标签分配（使用平方距离，避免开方运算）
    void assignLabelsFlatParallel(const std::vector<double>& features, int n, int dim,
                                   const std::vector<double>& centroidsFlat, int k,
                                   std::vector<int>& labels, std::vector<double>& distancesSq);

    // 平方欧氏距离（避免开方，用于比较场景）
    static double euclideanDistSqFlat(const double* a, const double* b, int dim);

    double euclideanDistance(const std::vector<double>& a, const std::vector<double>& b);
    static double euclideanDistFlat(const double* a, const double* b, int dim);
    std::vector<QColor> generateDistinctColors(int n);
};