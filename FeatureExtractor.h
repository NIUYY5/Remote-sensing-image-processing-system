#pragma once

#include "GeoImageData.h"
#include <QObject>

struct TextureFeatures
{
    std::vector<double> contrast;
    std::vector<double> dissimilarity;
    std::vector<double> homogeneity;
    std::vector<double> energy;
    std::vector<double> correlation;
    std::vector<double> asm_;
    int width = 0;
    int height = 0;
    int windowSize = 3;
};

struct SpectralIndices
{
    std::vector<double> ndvi;
    std::vector<double> ndwi;
    std::vector<double> ndbi;
    std::vector<double> mndwi;
    int width = 0;
    int height = 0;

    bool isValid() const { return width > 0 && height > 0; }
    QImage indexToImage(const std::vector<double>& index, double vmin = -1, double vmax = 1) const;
};

class FeatureExtractor : public QObject
{
    Q_OBJECT

public:
    explicit FeatureExtractor(QObject* parent = nullptr);
    ~FeatureExtractor();

    TextureFeatures computeGLCM(const GeoImageData& image, int band,
                                 int windowSize = 3, int levels = 32,
                                 int dx = 1, int dy = 0);

    SpectralIndices computeSpectralIndices(const GeoImageData& image);

    static double computeNDVI(double nir, double red);
    static double computeNDWI(double green, double nir);
    static double computeNDBI(double nir, double swir);
    static double computeMNDWI(double green, double swir);

    std::vector<std::vector<double>> extractAllFeatures(const GeoImageData& image,
                                                          const std::vector<int>& bandIndices,
                                                          bool includeTexture = false,
                                                          bool includeIndices = false,
                                                          bool includePosition = false);

    static std::vector<double> computeHistogram(const std::vector<double>& data, int bins = 256);

signals:
    void progressUpdated(int percent);
    void statusMessage(const QString& msg);
};