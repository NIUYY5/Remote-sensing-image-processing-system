#include "FeatureExtractor.h"
#include <algorithm>
#include <cmath>
#include <map>

FeatureExtractor::FeatureExtractor(QObject* parent)
    : QObject(parent)
{
}

FeatureExtractor::~FeatureExtractor()
{
}

TextureFeatures FeatureExtractor::computeGLCM(const GeoImageData& image, int band,
                                               int windowSize, int levels,
                                               int dx, int dy)
{
    TextureFeatures tf;
    tf.width = image.width;
    tf.height = image.height;
    tf.windowSize = windowSize;

    int n = image.pixelCount();
    tf.contrast.resize(n, 0);
    tf.dissimilarity.resize(n, 0);
    tf.homogeneity.resize(n, 0);
    tf.energy.resize(n, 0);
    tf.correlation.resize(n, 0);
    tf.asm_.resize(n, 0);

    double bandMin, bandMax;
    image.getBandRange(band, bandMin, bandMax);

    if (bandMax - bandMin < 1e-10) return tf;

    std::vector<int> quantized(n);
    for (int i = 0; i < n; ++i) {
        double val = image.pixelValue(band, i);
        quantized[i] = static_cast<int>((val - bandMin) / (bandMax - bandMin) * (levels - 1));
        quantized[i] = std::max(0, std::min(levels - 1, quantized[i]));
    }

    int halfW = windowSize / 2;

    for (int row = halfW; row < image.height - halfW; ++row) {
        for (int col = halfW; col < image.width - halfW; ++col) {
            int idx = row * image.width + col;

            std::vector<std::vector<int>> glcm(levels, std::vector<int>(levels, 0));
            int totalPairs = 0;

            for (int wy = -halfW; wy <= halfW; ++wy) {
                for (int wx = -halfW; wx <= halfW; ++wx) {
                    int nx = col + wx + dx;
                    int ny = row + wy + dy;
                    if (nx < 0 || nx >= image.width || ny < 0 || ny >= image.height)
                        continue;
                    int refIdx = (row + wy) * image.width + (col + wx);
                    int neighIdx = ny * image.width + nx;
                    int refVal = quantized[refIdx];
                    int neighVal = quantized[neighIdx];
                    glcm[refVal][neighVal]++;
                    totalPairs++;
                }
            }

            if (totalPairs == 0) continue;

            for (int i = 0; i < levels; ++i)
                for (int j = 0; j < levels; ++j)
                    glcm[i][j] = glcm[i][j] / static_cast<double>(totalPairs);

            double contrast = 0, dissimilarity = 0, homogeneity = 0;
            double energy = 0, correlation = 0;

            std::vector<double> muX(levels, 0), muY(levels, 0);
            double meanX = 0, meanY = 0, stdX = 0, stdY = 0;

            for (int i = 0; i < levels; ++i) {
                for (int j = 0; j < levels; ++j) {
                    muX[i] += glcm[i][j];
                    muY[j] += glcm[i][j];
                }
            }
            for (int i = 0; i < levels; ++i) {
                meanX += i * muX[i];
                meanY += i * muY[i];
            }
            for (int i = 0; i < levels; ++i) {
                stdX += (i - meanX) * (i - meanX) * muX[i];
                stdY += (i - meanY) * (i - meanY) * muY[i];
            }
            stdX = std::sqrt(stdX);
            stdY = std::sqrt(stdY);

            for (int i = 0; i < levels; ++i) {
                for (int j = 0; j < levels; ++j) {
                    double p = glcm[i][j];
                    contrast += (i - j) * (i - j) * p;
                    dissimilarity += std::abs(i - j) * p;
                    homogeneity += p / (1 + (i - j) * (i - j));
                    energy += p * p;
                    if (stdX > 1e-10 && stdY > 1e-10)
                        correlation += (i - meanX) * (j - meanY) * p / (stdX * stdY);
                }
            }

            tf.contrast[idx] = contrast;
            tf.dissimilarity[idx] = dissimilarity;
            tf.homogeneity[idx] = homogeneity;
            tf.energy[idx] = energy;
            tf.asm_[idx] = energy;
            tf.correlation[idx] = correlation;
        }
        if (row % 100 == 0)
            emit progressUpdated((row * 100) / image.height);
    }

    emit progressUpdated(100);
    return tf;
}

SpectralIndices FeatureExtractor::computeSpectralIndices(const GeoImageData& image)
{
    SpectralIndices si;
    si.width = image.width;
    si.height = image.height;

    int n = image.pixelCount();

    if (image.bands >= 4) {
        si.ndvi.resize(n, 0);
        si.ndwi.resize(n, 0);
        si.ndbi.resize(n, 0);
        si.mndwi.resize(n, 0);

        for (int i = 0; i < n; ++i) {
            double nir = image.pixelValue(3, i);
            double red = image.pixelValue(0, i);
            double green = image.pixelValue(1, i);
            double swir = image.pixelValue(4, i);

            si.ndvi[i] = computeNDVI(nir, red);
            si.ndwi[i] = computeNDWI(green, nir);
            si.ndbi[i] = computeNDBI(nir, swir);
            si.mndwi[i] = computeMNDWI(green, swir);

            if (i % 100000 == 0)
                emit progressUpdated((i * 100) / n);
        }
    } else if (image.bands >= 3) {
        si.ndvi.resize(n, 0);
        si.ndwi.resize(n, 0);
        for (int i = 0; i < n; ++i) {
            double red = image.pixelValue(0, i);
            double green = image.pixelValue(1, i);
            double nir = image.pixelValue(2, i);
            si.ndvi[i] = computeNDVI(nir, red);
            si.ndwi[i] = computeNDWI(green, nir);
        }
    }

    emit progressUpdated(100);
    return si;
}

double FeatureExtractor::computeNDVI(double nir, double red)
{
    double denom = nir + red;
    if (std::abs(denom) < 1e-10) return 0;
    return (nir - red) / denom;
}

double FeatureExtractor::computeNDWI(double green, double nir)
{
    double denom = green + nir;
    if (std::abs(denom) < 1e-10) return 0;
    return (green - nir) / denom;
}

double FeatureExtractor::computeNDBI(double nir, double swir)
{
    double denom = nir + swir;
    if (std::abs(denom) < 1e-10) return 0;
    return (swir - nir) / denom;
}

double FeatureExtractor::computeMNDWI(double green, double swir)
{
    double denom = green + swir;
    if (std::abs(denom) < 1e-10) return 0;
    return (green - swir) / denom;
}

std::vector<std::vector<double>> FeatureExtractor::extractAllFeatures(
    const GeoImageData& image, const std::vector<int>& bandIndices,
    bool includeTexture, bool includeIndices, bool includePosition)
{
    std::vector<int> bands = bandIndices;
    if (bands.empty()) {
        for (int i = 0; i < image.bands; ++i) bands.push_back(i);
    }

    int n = image.pixelCount();
    int featureDim = static_cast<int>(bands.size());

    if (includePosition) featureDim += 2;
    if (includeIndices) featureDim += 4;
    if (includeTexture) featureDim += 5;

    std::vector<std::vector<double>> features(n, std::vector<double>(featureDim));

    SpectralIndices indices;
    if (includeIndices) {
        indices = computeSpectralIndices(image);
    }

    TextureFeatures texture;
    if (includeTexture) {
        texture = computeGLCM(image, 0, 3);
    }

    for (int i = 0; i < n; ++i) {
        int col = i % image.width;
        int row = i / image.width;
        int f = 0;

        for (int b : bands)
            features[i][f++] = image.pixelValue(b, i);

        if (includePosition) {
            features[i][f++] = static_cast<double>(col) / image.width;
            features[i][f++] = static_cast<double>(row) / image.height;
        }

        if (includeIndices && indices.isValid()) {
            features[i][f++] = indices.ndvi[i];
            features[i][f++] = indices.ndwi[i];
            features[i][f++] = indices.ndbi[i];
            features[i][f++] = indices.mndwi[i];
        }

        if (includeTexture) {
            features[i][f++] = texture.contrast[i];
            features[i][f++] = texture.homogeneity[i];
            features[i][f++] = texture.energy[i];
            features[i][f++] = texture.correlation[i];
            features[i][f++] = texture.dissimilarity[i];
        }
    }

    return features;
}

std::vector<double> FeatureExtractor::computeHistogram(const std::vector<double>& data, int bins)
{
    std::vector<double> hist(bins, 0);
    if (data.empty()) return hist;

    double minVal = *std::min_element(data.begin(), data.end());
    double maxVal = *std::max_element(data.begin(), data.end());
    double range = maxVal - minVal;

    if (range < 1e-10) {
        hist[0] = static_cast<double>(data.size());
        return hist;
    }

    for (double val : data) {
        int bin = static_cast<int>((val - minVal) / range * (bins - 1));
        bin = std::max(0, std::min(bins - 1, bin));
        hist[bin]++;
    }

    return hist;
}

QImage SpectralIndices::indexToImage(const std::vector<double>& index, double vmin, double vmax) const
{
    QImage img(width, height, QImage::Format_RGB32);
    double range = vmax - vmin;
    if (range < 1e-10) range = 1.0;
    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < width; ++x) {
            int i = y * width + x;
            double val = index[i];
            double normalized = (val - vmin) / range;
            normalized = std::max(0.0, std::min(1.0, normalized));
            int gray = static_cast<int>(normalized * 255);
            line[x] = qRgb(gray, gray, gray);
        }
    }
    return img;
}