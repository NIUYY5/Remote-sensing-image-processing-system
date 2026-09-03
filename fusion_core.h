#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstdint>
#include <cstdarg>


#include <QtGui/QImage>

static const double PI = 3.14159265358979323846;

class Matrix
{
public:
    Matrix();
    Matrix(int rows, int cols);
    Matrix(int rows, int cols, double value);
    ~Matrix();

    void resize(int rows, int cols);
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    int size() const { return m_rows * m_cols; }

    double& at(int r, int c);
    double  at(int r, int c) const;
    double& operator()(int r, int c);
    double  operator()(int r, int c) const;

    std::vector<double>& data() { return m_data; }
    const std::vector<double>& data() const { return m_data; }

    Matrix transpose() const;
    Matrix operator*(const Matrix& other) const;
    Matrix operator*(double scalar) const;

    Matrix col(int c) const;
    void setCol(int c, const std::vector<double>& vec);
    double* colPtr(int c);

    Matrix row(int r) const;

    double mean() const;
    double variance() const;
    double stddev() const;

    void zero();

private:
    int m_rows;
    int m_cols;
    std::vector<double> m_data;
};

int eigenSymmetric(const Matrix& A, std::vector<double>& eigenvalues, Matrix& eigenvectors, int maxIter = 100);
Matrix histogramMatch(const std::vector<float>& source, const std::vector<float>& target);

struct MultiBandImage
{
    int width;
    int height;
    int bands;
    std::vector<std::vector<float>> bandData;

    MultiBandImage();
    MultiBandImage(int w, int h, int b);

    void create(int w, int h, int b);
    void clear();
    bool isValid() const;

    float& pixel(int x, int y, int band);
    float  pixel(int x, int y, int band) const;

    std::vector<float>& getBand(int band);
    const std::vector<float>& getBand(int band) const;

    void setBand(int band, const std::vector<float>& data);
    void setBandFromQImage(int band, const QImage& img, int channel);

    QImage toQImageBand(int band) const;
    QImage toQImageRGB() const;
    QImage toQImageGrayscale() const;

    void normalizeBand(int band);
    void normalizeAllBands();

    MultiBandImage extractBands(const std::vector<int>& bandIndices) const;
};

struct FusionParameters
{
    int algorithmType;         // 0=PCA, 1=HIS
    int panBandIndex;          // PAN band index (for PCA)
    bool stretchPanHistogram;
    double weightCoefficient;  // fusion weight [0.0, 1.0]
    int interpolationMethod;   // 0=nearest, 1=bilinear, 2=bicubic
    bool useAdaptiveFilter;
    double filterSigma;

    FusionParameters();
    void setDefaults();
    std::string serialize() const;
    bool deserialize(const std::string& json);
};

struct EvaluationResult
{
    double spectralDistortion;
    double spatialDetailPreservation;
    double informationEntropy;
    double psnr;
    double processingTimeMs;
    std::vector<double> perBandSpectralDistortion;
    std::vector<double> perBandEntropy;
    std::vector<double> perBandPSNR;

    EvaluationResult();
    void clear();
};

MultiBandImage pcaFusion(const MultiBandImage& panImage,
                          const MultiBandImage& msImage,
                          const FusionParameters& params,
                          double* elapsedMs = nullptr);

MultiBandImage hisFusion(const MultiBandImage& panImage,
                          const MultiBandImage& msImage,
                          const FusionParameters& params,
                          double* elapsedMs = nullptr);

EvaluationResult evaluateFusion(const MultiBandImage& originalMs,
                                 const MultiBandImage& fusedImage,
                                 const MultiBandImage& panImage);

std::string evaluationReportCSV(const MultiBandImage& originalMs,
                                 const MultiBandImage& fusedImage,
                                 const MultiBandImage& panImage,
                                 const EvaluationResult& result,
                                 const FusionParameters& params);

std::string evaluationReportHTML(const MultiBandImage& originalMs,
                                  const MultiBandImage& fusedImage,
                                  const MultiBandImage& panImage,
                                  const EvaluationResult& result,
                                  const FusionParameters& params);

MultiBandImage upsampleMS(const MultiBandImage& msImage, int targetWidth, int targetHeight, int method);

double computeSpectralDistortion(const MultiBandImage& original, const MultiBandImage& fused);
double computeSpatialDetail(const MultiBandImage& msImage, const MultiBandImage& fused, const MultiBandImage& pan);
double computeEntropy(const std::vector<float>& band, int width, int height);
double computePSNR(const std::vector<float>& original, const std::vector<float>& fused);
double computeRMSE(const std::vector<float>& a, const std::vector<float>& b);
