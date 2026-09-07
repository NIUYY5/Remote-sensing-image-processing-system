#include "fusion_core.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cfloat>

// ============================================================================
// Matrix implementation
// ============================================================================

Matrix::Matrix() : m_rows(0), m_cols(0) {}

Matrix::Matrix(int rows, int cols) : m_rows(rows), m_cols(cols)
{
    m_data.resize(rows * cols, 0.0);
}

Matrix::Matrix(int rows, int cols, double value) : m_rows(rows), m_cols(cols)
{
    m_data.resize(rows * cols, value);
}

Matrix::~Matrix() {}

void Matrix::resize(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    m_data.resize(rows * cols, 0.0);
}

double& Matrix::at(int r, int c) { return m_data[r * m_cols + c]; }
double  Matrix::at(int r, int c) const { return m_data[r * m_cols + c]; }
double& Matrix::operator()(int r, int c) { return m_data[r * m_cols + c]; }
double  Matrix::operator()(int r, int c) const { return m_data[r * m_cols + c]; }

Matrix Matrix::transpose() const
{
    Matrix result(m_cols, m_rows);
    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < m_cols; ++c)
            result(c, r) = at(r, c);
    return result;
}

Matrix Matrix::operator*(const Matrix& other) const
{
    Matrix result(m_rows, other.m_cols);
    for (int i = 0; i < m_rows; ++i)
        for (int k = 0; k < m_cols; ++k)
        {
            double aik = at(i, k);
            if (aik != 0.0)
                for (int j = 0; j < other.m_cols; ++j)
                    result(i, j) += aik * other(k, j);
        }
    return result;
}

Matrix Matrix::operator*(double scalar) const
{
    Matrix result(m_rows, m_cols);
    for (int i = 0; i < size(); ++i)
        result.m_data[i] = m_data[i] * scalar;
    return result;
}

Matrix Matrix::col(int c) const
{
    Matrix result(m_rows, 1);
    for (int r = 0; r < m_rows; ++r)
        result(r, 0) = at(r, c);
    return result;
}

void Matrix::setCol(int c, const std::vector<double>& vec)
{
    for (int r = 0; r < m_rows && r < (int)vec.size(); ++r)
        at(r, c) = vec[r];
}

double* Matrix::colPtr(int c)
{
    return &m_data[c * m_rows];
}

Matrix Matrix::row(int r) const
{
    Matrix result(1, m_cols);
    for (int c = 0; c < m_cols; ++c)
        result(0, c) = at(r, c);
    return result;
}

double Matrix::mean() const
{
    double s = 0.0;
    for (const double& v : m_data) s += v;
    return s / m_data.size();
}

double Matrix::variance() const
{
    double m = mean();
    double s = 0.0;
    for (const double& v : m_data) s += (v - m) * (v - m);
    return s / m_data.size();
}

double Matrix::stddev() const
{
    return std::sqrt(variance());
}

void Matrix::zero()
{
    std::fill(m_data.begin(), m_data.end(), 0.0);
}

// ============================================================================
// Eigenvalue decomposition (Jacobi method for symmetric matrices)
// ============================================================================

int eigenSymmetric(const Matrix& A, std::vector<double>& eigenvalues, Matrix& eigenvectors, int maxIter)
{
    int n = A.rows();
    if (n != A.cols() || n == 0) return -1;

    eigenvectors.resize(n, n);
    for (int i = 0; i < n; ++i)
    {
        eigenvectors(i, i) = 1.0;
        for (int j = 0; j < n; ++j)
            if (i != j) eigenvectors(i, j) = 0.0;
    }

    Matrix work(A);

    for (int iter = 0; iter < maxIter; ++iter)
    {
        int p = 0, q = 1;
        double maxOff = 0.0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
            {
                double absVal = std::abs(work(i, j));
                if (absVal > maxOff)
                {
                    maxOff = absVal;
                    p = i;
                    q = j;
                }
            }

        if (maxOff < 1e-12)
            break;

        double app = work(p, p);
        double aqq = work(q, q);
        double apq = work(p, q);

        double theta = 0.5 * std::atan2(2.0 * apq, app - aqq);
        double c = std::cos(theta);
        double s = std::sin(theta);

        work(p, p) = c * c * app + s * s * aqq + 2.0 * s * c * apq;
        work(q, q) = s * s * app + c * c * aqq - 2.0 * s * c * apq;
        work(p, q) = 0.0;
        work(q, p) = 0.0;

        for (int r = 0; r < n; ++r)
        {
            if (r == p || r == q) continue;
            double arp = c * work(r, p) + s * work(r, q);
            double arq = -s * work(r, p) + c * work(r, q);
            work(r, p) = arp; work(p, r) = arp;
            work(r, q) = arq; work(q, r) = arq;
        }

        for (int r = 0; r < n; ++r)
        {
            double vrp = eigenvectors(r, p);
            double vrq = eigenvectors(r, q);
            eigenvectors(r, p) = c * vrp + s * vrq;
            eigenvectors(r, q) = -s * vrp + c * vrq;
        }
    }

    eigenvalues.resize(n);
    for (int i = 0; i < n; ++i)
        eigenvalues[i] = work(i, i);

    std::vector<std::pair<double, int>> evPairs(n);
    for (int i = 0; i < n; ++i)
        evPairs[i] = std::make_pair(eigenvalues[i], i);
    std::sort(evPairs.begin(), evPairs.end(),
        [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            return a.first > b.first;
        });

    std::vector<double> sortedEigenvalues(n);
    Matrix sortedEigenvectors(n, n);
    for (int i = 0; i < n; ++i)
    {
        sortedEigenvalues[i] = evPairs[i].first;
        for (int r = 0; r < n; ++r)
            sortedEigenvectors(r, i) = eigenvectors(r, evPairs[i].second);
    }

    eigenvalues = sortedEigenvalues;
    eigenvectors = sortedEigenvectors;

    return 0;
}

// ============================================================================
// Histogram matching
// ============================================================================

Matrix histogramMatch(const std::vector<float>& source, const std::vector<float>& target)
{
    Matrix result(1, (int)source.size());

    std::vector<float> sortedSource = source;
    std::vector<float> sortedTarget = target;
    std::sort(sortedSource.begin(), sortedSource.end());
    std::sort(sortedTarget.begin(), sortedTarget.end());

    float srcMin = sortedSource.front();
    float srcMax = sortedSource.back();
    float tgtMin = sortedTarget.front();
    float tgtMax = sortedTarget.back();

    for (size_t i = 0; i < source.size(); ++i)
    {
        float pct = (srcMax > srcMin) ? (source[i] - srcMin) / (srcMax - srcMin) : 0.5f;
        pct = std::max(0.0f, std::min(1.0f, pct));
        size_t idx = static_cast<size_t>(pct * (sortedTarget.size() - 1));
        if (idx >= sortedTarget.size()) idx = sortedTarget.size() - 1;
        result(0, (int)i) = sortedTarget[idx];
    }

    return result;
}

// ============================================================================
// MultiBandImage implementation
// ============================================================================

MultiBandImage::MultiBandImage() : width(0), height(0), bands(0) {}

MultiBandImage::MultiBandImage(int w, int h, int b) : width(w), height(h), bands(b)
{
    bandData.resize(b);
    for (int i = 0; i < b; ++i)
        bandData[i].resize(w * h, 0.0f);
}

void MultiBandImage::create(int w, int h, int b)
{
    width = w;
    height = h;
    bands = b;
    bandData.resize(b);
    for (int i = 0; i < b; ++i)
        bandData[i].resize(w * h, 0.0f);
}

void MultiBandImage::clear()
{
    width = 0;
    height = 0;
    bands = 0;
    bandData.clear();
}

bool MultiBandImage::isValid() const
{
    if (width <= 0 || height <= 0 || bands <= 0) return false;
    for (const auto& b : bandData)
        if ((int)b.size() != width * height) return false;
    return true;
}

float& MultiBandImage::pixel(int x, int y, int band)
{
    return bandData[band][y * width + x];
}

float MultiBandImage::pixel(int x, int y, int band) const
{
    return bandData[band][y * width + x];
}

std::vector<float>& MultiBandImage::getBand(int band)
{
    return bandData[band];
}

const std::vector<float>& MultiBandImage::getBand(int band) const
{
    return bandData[band];
}

void MultiBandImage::setBand(int band, const std::vector<float>& data)
{
    if (band >= 0 && band < bands)
        bandData[band] = data;
}

void MultiBandImage::setBandFromQImage(int band, const QImage& img, int channel)
{
    if (band < 0 || band >= bands) return;
    int nPixels = width * height;
    bandData[band].resize(nPixels, 0.0f);

    if (img.width() != width || img.height() != height)
        return;

    for (int y = 0; y < height; ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < width; ++x)
        {
            QRgb pixel = line[x];
            int val = 0;
            switch (channel)
            {
            case 0: val = qRed(pixel); break;
            case 1: val = qGreen(pixel); break;
            case 2: val = qBlue(pixel); break;
            default: val = qGray(pixel); break;
            }
            bandData[band][y * width + x] = static_cast<float>(val);
        }
    }
}

QImage MultiBandImage::toQImageBand(int band) const
{
    if (band < 0 || band >= bands) return QImage();

    QImage img(width, height, QImage::Format_Grayscale8);
    float bandMin = FLT_MAX, bandMax = -FLT_MAX;
    for (int i = 0; i < width * height; ++i)
    {
        float v = bandData[band][i];
        if (v < bandMin) bandMin = v;
        if (v > bandMax) bandMax = v;
    }

    float range = bandMax - bandMin;
    for (int y = 0; y < height; ++y)
    {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            float v = bandData[band][y * width + x];
            float normalized = (range > 0.0f) ? (v - bandMin) / range * 255.0f : 128.0f;
            line[x] = static_cast<uchar>(std::max(0.0f, std::min(255.0f, normalized)));
        }
    }
    return img;
}

QImage MultiBandImage::toQImageRGB() const
{
    QImage img(width, height, QImage::Format_RGB32);

    std::vector<float> mins(bands, FLT_MAX), maxs(bands, -FLT_MAX);
    int nPixels = width * height;
    for (int b = 0; b < bands; ++b)
        for (int i = 0; i < nPixels; ++i)
        {
            float v = bandData[b][i];
            if (v < mins[b]) mins[b] = v;
            if (v > maxs[b]) maxs[b] = v;
        }

    std::vector<float> ranges(bands);
    for (int b = 0; b < bands; ++b)
        ranges[b] = maxs[b] - mins[b];

    for (int y = 0; y < height; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < width; ++x)
        {
            int idx = y * width + x;
            int rVal = 0, gVal = 0, bVal = 0;

            if (bands >= 3)
            {
                rVal = static_cast<int>((ranges[2] > 0 ? (bandData[2][idx] - mins[2]) / ranges[2] : 0.5f) * 255.0f);
                gVal = static_cast<int>((ranges[1] > 0 ? (bandData[1][idx] - mins[1]) / ranges[1] : 0.5f) * 255.0f);
                bVal = static_cast<int>((ranges[0] > 0 ? (bandData[0][idx] - mins[0]) / ranges[0] : 0.5f) * 255.0f);
            }
            else if (bands >= 2)
            {
                rVal = static_cast<int>((ranges[1] > 0 ? (bandData[1][idx] - mins[1]) / ranges[1] : 0.5f) * 255.0f);
                gVal = static_cast<int>((ranges[0] > 0 ? (bandData[0][idx] - mins[0]) / ranges[0] : 0.5f) * 255.0f);
                bVal = 0;
            }
            else
            {
                int v = static_cast<int>((ranges[0] > 0 ? (bandData[0][idx] - mins[0]) / ranges[0] : 0.5f) * 255.0f);
                rVal = gVal = bVal = v;
            }

            rVal = std::max(0, std::min(255, rVal));
            gVal = std::max(0, std::min(255, gVal));
            bVal = std::max(0, std::min(255, bVal));

            line[x] = qRgb(rVal, gVal, bVal);
        }
    }
    return img;
}

QImage MultiBandImage::toQImageGrayscale() const
{
    if (bands == 0) return QImage();
    if (bands == 1) return toQImageBand(0);

    QImage img(width, height, QImage::Format_Grayscale8);
    int nPixels = width * height;

    float globalMin = FLT_MAX, globalMax = -FLT_MAX;
    for (int i = 0; i < nPixels; ++i)
    {
        float sum = 0.0f;
        for (int b = 0; b < bands; ++b)
            sum += bandData[b][i];
        float avg = sum / bands;
        if (avg < globalMin) globalMin = avg;
        if (avg > globalMax) globalMax = avg;
    }

    float range = globalMax - globalMin;
    for (int y = 0; y < height; ++y)
    {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0f;
            int idx = y * width + x;
            for (int b = 0; b < bands; ++b)
                sum += bandData[b][idx];
            float avg = sum / bands;
            float normalized = (range > 0.0f) ? (avg - globalMin) / range * 255.0f : 128.0f;
            line[x] = static_cast<uchar>(std::max(0.0f, std::min(255.0f, normalized)));
        }
    }
    return img;
}

void MultiBandImage::normalizeBand(int band)
{
    if (band < 0 || band >= bands) return;
    auto& b = bandData[band];
    float mn = *std::min_element(b.begin(), b.end());
    float mx = *std::max_element(b.begin(), b.end());
    float range = mx - mn;
    if (range > 0)
        for (float& v : b) v = (v - mn) / range;
}

void MultiBandImage::normalizeAllBands()
{
    for (int b = 0; b < bands; ++b)
        normalizeBand(b);
}

MultiBandImage MultiBandImage::extractBands(const std::vector<int>& bandIndices) const
{
    int nBands = (int)bandIndices.size();
    MultiBandImage result(width, height, nBands);
    for (int i = 0; i < nBands; ++i)
    {
        int srcBand = bandIndices[i];
        if (srcBand >= 0 && srcBand < bands)
            result.bandData[i] = bandData[srcBand];
    }
    return result;
}

// ============================================================================
// FusionParameters
// ============================================================================

FusionParameters::FusionParameters()
{
    setDefaults();
}

void FusionParameters::setDefaults()
{
    algorithmType = 0;
    panBandIndex = 0;
    stretchPanHistogram = true;
    weightCoefficient = 1.0;
    interpolationMethod = 1;
    useAdaptiveFilter = false;
    filterSigma = 1.0;
}

std::string FusionParameters::serialize() const
{
    QString json = QString(
        "{\"algorithmType\":%1,\"panBandIndex\":%2,\"stretchPanHistogram\":%3,"
        "\"weightCoefficient\":%4,\"interpolationMethod\":%5,\"useAdaptiveFilter\":%6,"
        "\"filterSigma\":%7}")
        .arg(algorithmType)
        .arg(panBandIndex)
        .arg(stretchPanHistogram ? "true" : "false")
        .arg(weightCoefficient, 0, 'f', 4)
        .arg(interpolationMethod)
        .arg(useAdaptiveFilter ? "true" : "false")
        .arg(filterSigma, 0, 'f', 4);
    return json.toStdString();
}

bool FusionParameters::deserialize(const std::string& jsonStr)
{
    QByteArray data = QByteArray::fromStdString(jsonStr);
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) return false;

    QJsonObject obj = doc.object();
    algorithmType = obj["algorithmType"].toInt(0);
    panBandIndex = obj["panBandIndex"].toInt(0);
    stretchPanHistogram = obj["stretchPanHistogram"].toBool(true);
    weightCoefficient = obj["weightCoefficient"].toDouble(1.0);
    interpolationMethod = obj["interpolationMethod"].toInt(1);
    useAdaptiveFilter = obj["useAdaptiveFilter"].toBool(false);
    filterSigma = obj["filterSigma"].toDouble(1.0);

    return true;
}

// ============================================================================
// EvaluationResult
// ============================================================================

EvaluationResult::EvaluationResult()
{
    clear();
}

void EvaluationResult::clear()
{
    spectralDistortion = 0.0;
    spatialDetailPreservation = 0.0;
    informationEntropy = 0.0;
    psnr = 0.0;
    processingTimeMs = 0.0;
    perBandSpectralDistortion.clear();
    perBandEntropy.clear();
    perBandPSNR.clear();
}

// ============================================================================
// Upsampling
// ============================================================================

static float bilinearInterpolate(const std::vector<float>& src, int srcW, int srcH,
                                   double x, double y)
{
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    x0 = std::max(0, std::min(srcW - 1, x0));
    y0 = std::max(0, std::min(srcH - 1, y0));
    x1 = std::max(0, std::min(srcW - 1, x1));
    y1 = std::max(0, std::min(srcH - 1, y1));

    double fx = x - x0;
    double fy = y - y0;

    float v00 = src[y0 * srcW + x0];
    float v10 = src[y0 * srcW + x1];
    float v01 = src[y1 * srcW + x0];
    float v11 = src[y1 * srcW + x1];

    return static_cast<float>((1.0 - fx) * (1.0 - fy) * v00 +
           fx * (1.0 - fy) * v10 +
           (1.0 - fx) * fy * v01 +
           fx * fy * v11);
}

MultiBandImage upsampleMS(const MultiBandImage& msImage, int targetWidth, int targetHeight, int method)
{
    if (!msImage.isValid()) return MultiBandImage();

    MultiBandImage result(targetWidth, targetHeight, msImage.bands);

    double scaleX = static_cast<double>(msImage.width) / targetWidth;
    double scaleY = static_cast<double>(msImage.height) / targetHeight;

    for (int b = 0; b < msImage.bands; ++b)
    {
        auto& dst = result.bandData[b];
        const auto& src = msImage.bandData[b];

        for (int y = 0; y < targetHeight; ++y)
        {
            double srcY = (y + 0.5) * scaleY - 0.5;

            for (int x = 0; x < targetWidth; ++x)
            {
                double srcX = (x + 0.5) * scaleX - 0.5;
                float val = 0.0f;

                if (method == 0)
                {
                    int sx = static_cast<int>(std::round(srcX));
                    int sy = static_cast<int>(std::round(srcY));
                    sx = std::max(0, std::min(msImage.width - 1, sx));
                    sy = std::max(0, std::min(msImage.height - 1, sy));
                    val = src[sy * msImage.width + sx];
                }
                else if (method == 1)
                {
                    val = bilinearInterpolate(src, msImage.width, msImage.height, srcX, srcY);
                }
                else
                {
                    val = bilinearInterpolate(src, msImage.width, msImage.height, srcX, srcY);
                }

                dst[y * targetWidth + x] = val;
            }
        }
    }

    return result;
}

// ============================================================================
// PCA Fusion
// ============================================================================

MultiBandImage pcaFusion(const MultiBandImage& panImage,
                          const MultiBandImage& msImage,
                          const FusionParameters& params,
                          double* elapsedMs)
{
    QElapsedTimer timer;
    timer.start();

    MultiBandImage result;
    if (!panImage.isValid() || !msImage.isValid()) return result;

    int nBands = msImage.bands;

    MultiBandImage msUpsampled = msImage;
    if (msImage.width != panImage.width || msImage.height != panImage.height)
        msUpsampled = upsampleMS(msImage, panImage.width, panImage.height, params.interpolationMethod);

    int nPixels = panImage.width * panImage.height;
    result.create(panImage.width, panImage.height, nBands);

    std::vector<double> bandMeans(nBands, 0.0);
    for (int b = 0; b < nBands; ++b)
    {
        const auto& srcBand = msUpsampled.bandData[b];
        double sum = 0.0;
        for (int i = 0; i < nPixels; ++i) sum += srcBand[i];
        bandMeans[b] = sum / nPixels;
    }

    Matrix covMatrix(nBands, nBands);
    for (int i = 0; i < nPixels; ++i)
    {
        for (int b1 = 0; b1 < nBands; ++b1)
        {
            double v1 = msUpsampled.bandData[b1][i] - bandMeans[b1];
            for (int b2 = b1; b2 < nBands; ++b2)
            {
                double v2 = msUpsampled.bandData[b2][i] - bandMeans[b2];
                covMatrix(b1, b2) += v1 * v2;
            }
        }
    }
    for (int i = 0; i < nBands; ++i)
        for (int j = 0; j < nBands; ++j)
        {
            if (i == j) continue;
            if (i < j)
                covMatrix(i, j) /= (nPixels - 1);
            else
                covMatrix(i, j) = covMatrix(j, i);
        }
    for (int i = 0; i < nBands; ++i)
        covMatrix(i, i) /= (nPixels - 1);

    std::vector<double> eigenvalues;
    Matrix eigenvectors;
    eigenSymmetric(covMatrix, eigenvalues, eigenvectors);

    double pc1Sum = 0.0, pc1SumSq = 0.0;
    for (int i = 0; i < nPixels; ++i)
    {
        double pc1 = 0.0;
        for (int b = 0; b < nBands; ++b)
            pc1 += (msUpsampled.bandData[b][i] - bandMeans[b]) * eigenvectors(b, 0);
        pc1Sum += pc1;
        pc1SumSq += pc1 * pc1;
    }
    double pc1Mean = pc1Sum / nPixels;
    double pc1Var = pc1SumSq / nPixels - pc1Mean * pc1Mean;
    double pc1Std = (pc1Var > 0.0) ? std::sqrt(pc1Var) : 0.0;

    const auto& panBand = panImage.bandData[0];
    double panMean = 0.0, panSumSq = 0.0;
    for (int i = 0; i < nPixels; ++i)
    {
        panMean += panBand[i];
        panSumSq += panBand[i] * panBand[i];
    }
    panMean /= nPixels;
    double panVar = panSumSq / nPixels - panMean * panMean;
    double panStd = (panVar > 0.0) ? std::sqrt(panVar) : 0.0;

    std::vector<double> pcTemp(nBands);
    for (int i = 0; i < nPixels; ++i)
    {
        for (int j = 0; j < nBands; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < nBands; ++k)
                sum += (msUpsampled.bandData[k][i] - bandMeans[k]) * eigenvectors(k, j);
            pcTemp[j] = sum;
        }

        double panNormalized = (panStd > 0) ? ((panBand[i] - panMean) / panStd * pc1Std + pc1Mean) : panBand[i];
        pcTemp[0] = params.weightCoefficient * panNormalized +
                    (1.0 - params.weightCoefficient) * pcTemp[0];

        int x = i % panImage.width;
        int y = i / panImage.width;
        for (int j = 0; j < nBands; ++j)
        {
            double sum = 0.0;
            for (int k = 0; k < nBands; ++k)
                sum += pcTemp[k] * eigenvectors(j, k);
            result.pixel(x, y, j) = sum + bandMeans[j];
        }
    }

    for (int b = 0; b < nBands; ++b)
    {
        double minV = result.bandData[b][0];
        double maxV = result.bandData[b][0];
        for (int i = 1; i < nPixels; ++i)
        {
            if (result.bandData[b][i] < minV) minV = result.bandData[b][i];
            if (result.bandData[b][i] > maxV) maxV = result.bandData[b][i];
        }
        if (maxV > 255.0 || minV < 0.0)
        {
            double range = maxV - minV;
            for (int i = 0; i < nPixels; ++i)
                result.bandData[b][i] = (range > 0) ? (result.bandData[b][i] - minV) / range * 255.0 : 128.0;
        }
    }

    if (elapsedMs) *elapsedMs = timer.elapsed();

    return result;
}

// ============================================================================
// HIS Fusion
// ============================================================================

static void rgbToHis(double r, double g, double b, double& h, double& i, double& s)
{
    i = (r + g + b) / 3.0;

    double minVal = std::min(std::min(r, g), b);
    double sum = r + g + b;

    if (sum > 0)
        s = 1.0 - 3.0 * minVal / sum;
    else
        s = 0.0;

    if (s < 1e-10)
    {
        h = 0.0;
    }
    else
    {
        double numerator = (r - g) + (r - b);
        double denominator = 2.0 * std::sqrt((r - g) * (r - g) + (r - b) * (g - b));
        if (denominator < 1e-10) denominator = 1e-10;
        double theta = std::acos(numerator / denominator);

        if (b <= g)
            h = theta;
        else
            h = 2.0 * PI - theta;
    }
}

static void hisToRgb(double h, double i, double s, double& r, double& g, double& b)
{
    if (s < 1e-10)
    {
        r = g = b = i;
        return;
    }

    double hDeg = h;
    if (hDeg < 0) hDeg += 2.0 * PI;

    if (hDeg < 2.0 * PI / 3.0)
    {
        b = i * (1.0 - s);
        r = i * (1.0 + s * std::cos(hDeg) / std::cos(PI / 3.0 - hDeg));
        g = 3.0 * i - r - b;
    }
    else if (hDeg < 4.0 * PI / 3.0)
    {
        hDeg -= 2.0 * PI / 3.0;
        r = i * (1.0 - s);
        g = i * (1.0 + s * std::cos(hDeg) / std::cos(PI / 3.0 - hDeg));
        b = 3.0 * i - r - g;
    }
    else
    {
        hDeg -= 4.0 * PI / 3.0;
        g = i * (1.0 - s);
        b = i * (1.0 + s * std::cos(hDeg) / std::cos(PI / 3.0 - hDeg));
        r = 3.0 * i - g - b;
    }

    r = std::max(0.0, std::min(255.0, r));
    g = std::max(0.0, std::min(255.0, g));
    b = std::max(0.0, std::min(255.0, b));
}

MultiBandImage hisFusion(const MultiBandImage& panImage,
                          const MultiBandImage& msImage,
                          const FusionParameters& params,
                          double* elapsedMs)
{
    QElapsedTimer timer;
    timer.start();

    MultiBandImage result;
    if (!panImage.isValid() || !msImage.isValid()) return result;

    MultiBandImage msUpsampled = msImage;
    if (msImage.width != panImage.width || msImage.height != panImage.height)
        msUpsampled = upsampleMS(msImage, panImage.width, panImage.height, params.interpolationMethod);

    int nPixels = panImage.width * panImage.height;
    int nBands = msUpsampled.bands;
    result.create(panImage.width, panImage.height, nBands);

    int rBand = 2, gBand = 1, bBand = 0;
    if (nBands >= 4)
    {
        rBand = 2;
        gBand = 1;
        bBand = 0;
    }
    else if (nBands == 3)
    {
        rBand = 2;
        gBand = 1;
        bBand = 0;
    }
    else if (nBands == 2)
    {
        rBand = 1;
        gBand = 1;
        bBand = 0;
    }

    const auto& panBand = panImage.bandData[0];

    if (nBands >= 3)
    {
        double panMin = *std::min_element(panBand.begin(), panBand.end());
        double panMax = *std::max_element(panBand.begin(), panBand.end());
        double panRange = panMax - panMin;

        for (int i = 0; i < nPixels; ++i)
        {
            double r = msUpsampled.bandData[rBand][i];
            double g = msUpsampled.bandData[gBand][i];
            double b = msUpsampled.bandData[bBand][i];

            double h, intensity, s;
            rgbToHis(r, g, b, h, intensity, s);

            double panVal = (panRange > 0) ? (panBand[i] - panMin) / panRange * 255.0 : 128.0;
            double newIntensity = params.weightCoefficient * panVal + (1.0 - params.weightCoefficient) * intensity;
            newIntensity = std::max(0.0, std::min(255.0, newIntensity));

            double newR, newG, newB;
            hisToRgb(h, newIntensity, s, newR, newG, newB);

            result.bandData[rBand][i] = newR;
            result.bandData[gBand][i] = newG;
            result.bandData[bBand][i] = newB;

            if (nBands > 3)
            {
                for (int extra = 3; extra < nBands; ++extra)
                    result.bandData[extra][i] = msUpsampled.bandData[extra][i];
            }
        }
    }
    else
    {
        for (int i = 0; i < nPixels; ++i)
        {
            for (int b = 0; b < nBands; ++b)
                result.bandData[b][i] = params.weightCoefficient * panBand[i] +
                                        (1.0 - params.weightCoefficient) * msUpsampled.bandData[b][i];
        }
    }

    if (elapsedMs) *elapsedMs = timer.elapsed();

    return result;
}

// ============================================================================
// Evaluation metrics
// ============================================================================

double computeSpectralDistortion(const MultiBandImage& original, const MultiBandImage& fused)
{
    if (!original.isValid() || !fused.isValid()) return 0.0;
    if (original.width != fused.width || original.height != fused.height) return 0.0;

    int nBands = std::min(original.bands, fused.bands);
    int nPixels = original.width * original.height;
    double totalDistortion = 0.0;

    for (int b = 0; b < nBands; ++b)
    {
        double bandDistortion = 0.0;
        const auto& oBand = original.bandData[b];
        const auto& fBand = fused.bandData[b];

        float oMin = *std::min_element(oBand.begin(), oBand.end());
        float oMax = *std::max_element(oBand.begin(), oBand.end());
        float fMin = *std::min_element(fBand.begin(), fBand.end());
        float fMax = *std::max_element(fBand.begin(), fBand.end());

        for (int i = 0; i < nPixels; ++i)
        {
            double oNorm = (oMax > oMin) ? (oBand[i] - oMin) / (oMax - oMin) : 0.5;
            double fNorm = (fMax > fMin) ? (fBand[i] - fMin) / (fMax - fMin) : 0.5;
            bandDistortion += std::abs(oNorm - fNorm);
        }
        totalDistortion += bandDistortion / nPixels;
    }

    return totalDistortion / nBands;
}

double computeSpatialDetail(const MultiBandImage& msImage, const MultiBandImage& fused,
                             const MultiBandImage& pan)
{
    if (!msImage.isValid() || !fused.isValid() || !pan.isValid()) return 0.0;

    int w = fused.width, h = fused.height;
    int nBands = std::min(msImage.bands, fused.bands);
    int nPixels = w * h;

    if (fused.width != pan.width || fused.height != pan.height) return 0.0;

    MultiBandImage msUpsampled = msImage;
    if (msImage.width != w || msImage.height != h)
        msUpsampled = upsampleMS(msImage, w, h, 1);

    double correlationSum = 0.0;
    int validBands = 0;

    const auto& panBand = pan.bandData[0];
    double panMean = 0.0;
    for (int i = 0; i < nPixels; ++i) panMean += panBand[i];
    panMean /= nPixels;

    for (int b = 0; b < nBands; ++b)
    {
        const auto& fBand = fused.bandData[b];

        double fMean = 0.0;
        for (int i = 0; i < nPixels; ++i) fMean += fBand[i];
        fMean /= nPixels;

        double num = 0.0, den1 = 0.0, den2 = 0.0;
        for (int i = 0; i < nPixels; ++i)
        {
            double df = fBand[i] - fMean;
            double dp = panBand[i] - panMean;
            num += df * dp;
            den1 += df * df;
            den2 += dp * dp;
        }

        if (den1 > 0 && den2 > 0)
        {
            double corr = num / std::sqrt(den1 * den2);
            correlationSum += corr;
            validBands++;
        }
    }

    return validBands > 0 ? correlationSum / validBands : 0.0;
}

double computeEntropy(const std::vector<float>& band, int width, int height)
{
    int nPixels = width * height;
    if (nPixels == 0) return 0.0;

    float minVal = *std::min_element(band.begin(), band.end());
    float maxVal = *std::max_element(band.begin(), band.end());
    float range = maxVal - minVal;

    if (range < 1e-10f) return 0.0;

    int numBins = 256;
    std::vector<int> histogram(numBins, 0);

    for (int i = 0; i < nPixels; ++i)
    {
        int bin = static_cast<int>((band[i] - minVal) / range * (numBins - 1));
        bin = std::max(0, std::min(numBins - 1, bin));
        histogram[bin]++;
    }

    double entropy = 0.0;
    for (int i = 0; i < numBins; ++i)
    {
        if (histogram[i] > 0)
        {
            double p = static_cast<double>(histogram[i]) / nPixels;
            entropy -= p * std::log2(p);
        }
    }

    return entropy;
}

double computePSNR(const std::vector<float>& original, const std::vector<float>& fused)
{
    if (original.size() != fused.size() || original.empty()) return 0.0;

    double mse = 0.0;
    for (size_t i = 0; i < original.size(); ++i)
    {
        double diff = original[i] - fused[i];
        mse += diff * diff;
    }
    mse /= original.size();

    if (mse < 1e-10) return 100.0;

    double maxVal = 255.0;
    return 10.0 * std::log10(maxVal * maxVal / mse);
}

double computeRMSE(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty()) return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum / a.size());
}

// ============================================================================
// Full evaluation
// ============================================================================

EvaluationResult evaluateFusion(const MultiBandImage& originalMs,
                                 const MultiBandImage& fusedImage,
                                 const MultiBandImage& panImage)
{
    EvaluationResult result;

    if (!originalMs.isValid() || !fusedImage.isValid()) return result;

    MultiBandImage msUpsampled = originalMs;
    if (originalMs.width != fusedImage.width || originalMs.height != fusedImage.height)
        msUpsampled = upsampleMS(originalMs, fusedImage.width, fusedImage.height, 1);

    result.spectralDistortion = computeSpectralDistortion(msUpsampled, fusedImage);

    if (panImage.isValid())
        result.spatialDetailPreservation = computeSpatialDetail(originalMs, fusedImage, panImage);

    int nBands = fusedImage.bands;
    double totalEntropy = 0.0;
    double totalPSNR = 0.0;

    int minBands = std::min(originalMs.bands, fusedImage.bands);

    for (int b = 0; b < minBands; ++b)
    {
        double entropy = computeEntropy(fusedImage.bandData[b], fusedImage.width, fusedImage.height);
        result.perBandEntropy.push_back(entropy);
        totalEntropy += entropy;

        const auto& oBand = msUpsampled.bandData[b];
        const auto& fBand = fusedImage.bandData[b];
        float oMin = *std::min_element(oBand.begin(), oBand.end());
        float oMax = *std::max_element(oBand.begin(), oBand.end());
        float fMin = *std::min_element(fBand.begin(), fBand.end());
        float fMax = *std::max_element(fBand.begin(), fBand.end());
        int nPixels = msUpsampled.width * msUpsampled.height;

        double bandDistortion = 0.0;
        for (int i = 0; i < nPixels; ++i)
        {
            double oNorm = (oMax > oMin) ? (oBand[i] - oMin) / (oMax - oMin) : 0.5;
            double fNorm = (fMax > fMin) ? (fBand[i] - fMin) / (fMax - fMin) : 0.5;
            bandDistortion += std::abs(oNorm - fNorm);
        }
        result.perBandSpectralDistortion.push_back(bandDistortion / nPixels);

        double psnr = computePSNR(msUpsampled.bandData[b], fusedImage.bandData[b]);
        result.perBandPSNR.push_back(psnr);
        totalPSNR += psnr;
    }

    result.informationEntropy = minBands > 0 ? totalEntropy / minBands : 0.0;
    result.psnr = minBands > 0 ? totalPSNR / minBands : 0.0;

    return result;
}

// ============================================================================
// Report generation
// ============================================================================

std::string evaluationReportCSV(const MultiBandImage& originalMs,
                                 const MultiBandImage& fusedImage,
                                 const MultiBandImage& panImage,
                                 const EvaluationResult& result,
                                 const FusionParameters& params)
{
    Q_UNUSED(panImage);
    Q_UNUSED(params);

    std::string csv;
    csv += "Metric,Value\n";
    csv += "Spectral Distortion," + std::to_string(result.spectralDistortion) + "\n";
    csv += "Spatial Detail Preservation," + std::to_string(result.spatialDetailPreservation) + "\n";
    csv += "Information Entropy," + std::to_string(result.informationEntropy) + "\n";
    csv += "PSNR (dB)," + std::to_string(result.psnr) + "\n";
    csv += "Processing Time (ms)," + std::to_string(result.processingTimeMs) + "\n";

    int minBands = std::min(originalMs.bands, fusedImage.bands);
    csv += "\nPer-Band Metrics\n";
    csv += "Band,Entropy,PSNR\n";
    for (int b = 0; b < minBands; ++b)
    {
        csv += std::to_string(b + 1) + ",";
        csv += (b < (int)result.perBandEntropy.size() ? std::to_string(result.perBandEntropy[b]) : "N/A") + ",";
        csv += (b < (int)result.perBandPSNR.size() ? std::to_string(result.perBandPSNR[b]) : "N/A") + "\n";
    }

    return csv;
}

std::string evaluationReportHTML(const MultiBandImage& originalMs,
                                  const MultiBandImage& fusedImage,
                                  const MultiBandImage& panImage,
                                  const EvaluationResult& result,
                                  const FusionParameters& params)
{
    Q_UNUSED(panImage);
    Q_UNUSED(params);

    QString html;
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>影像融合评估报告</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;color:#333;}"
            "h1{color:#007ACC;border-bottom:2px solid #007ACC;padding-bottom:10px;}"
            "h2{color:#555;margin-top:20px;}"
            "table{border-collapse:collapse;width:100%;margin:10px 0;}"
            "th,td{border:1px solid #ddd;padding:8px;text-align:left;}"
            "th{background-color:#007ACC;color:white;}"
            "tr:nth-child(even){background-color:#f2f2f2;}"
            ".metric-good{color:green;}.metric-warn{color:orange;}.metric-bad{color:red;}"
            "</style></head><body>";

    html += "<h1>遥感影像融合质量评估报告</h1>";
    html += "<p>生成时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "</p>";

    html += "<h2>融合参数</h2><table>";
    html += "<tr><th>参数</th><th>值</th></tr>";
    html += "<tr><td>融合算法</td><td>" + QString(params.algorithmType == 0 ? "PCA (主成分分析)" : "HIS (色度-亮度-饱和度)") + "</td></tr>";
    html += "<tr><td>权重系数</td><td>" + QString::number(params.weightCoefficient, 'f', 4) + "</td></tr>";
    html += "<tr><td>直方图匹配</td><td>" + QString(params.stretchPanHistogram ? "是" : "否") + "</td></tr>";
    html += "<tr><td>插值方法</td><td>" + QString(params.interpolationMethod == 0 ? "最近邻" : (params.interpolationMethod == 1 ? "双线性" : "双三次")) + "</td></tr>";
    html += "</table>";

    html += "<h2>综合评估指标</h2><table>";
    html += "<tr><th>指标</th><th>数值</th><th>评价</th></tr>";

    auto classifySD = [](double v) -> QString {
        if (v < 0.05) return "<span class='metric-good'>优秀</span>";
        if (v < 0.15) return "<span class='metric-warn'>良好</span>";
        return "<span class='metric-bad'>需改进</span>";
    };
    auto classifySP = [](double v) -> QString {
        if (v > 0.8) return "<span class='metric-good'>优秀</span>";
        if (v > 0.6) return "<span class='metric-warn'>良好</span>";
        return "<span class='metric-bad'>需改进</span>";
    };
    auto classifyPSNR = [](double v) -> QString {
        if (v > 40) return "<span class='metric-good'>优秀</span>";
        if (v > 30) return "<span class='metric-warn'>良好</span>";
        return "<span class='metric-bad'>需改进</span>";
    };

    html += "<tr><td>光谱扭曲度</td><td>" + QString::number(result.spectralDistortion, 'f', 6) + "</td><td>" + classifySD(result.spectralDistortion) + "</td></tr>";
    html += "<tr><td>空间细节保持度</td><td>" + QString::number(result.spatialDetailPreservation, 'f', 4) + "</td><td>" + classifySP(result.spatialDetailPreservation) + "</td></tr>";
    html += "<tr><td>信息熵</td><td>" + QString::number(result.informationEntropy, 'f', 4) + "</td><td>-</td></tr>";
    html += "<tr><td>PSNR (dB)</td><td>" + QString::number(result.psnr, 'f', 2) + "</td><td>" + classifyPSNR(result.psnr) + "</td></tr>";
    html += "<tr><td>处理时间 (ms)</td><td>" + QString::number(result.processingTimeMs, 'f', 1) + "</td><td>-</td></tr>";
    html += "</table>";

    int minBands = std::min(originalMs.bands, fusedImage.bands);
    html += "<h2>逐波段评估</h2><table>";
    html += "<tr><th>波段</th><th>信息熵</th><th>PSNR (dB)</th></tr>";
    for (int b = 0; b < minBands; ++b)
    {
        html += "<tr><td>波段 " + QString::number(b + 1) + "</td>";
        html += "<td>" + (b < (int)result.perBandEntropy.size() ? QString::number(result.perBandEntropy[b], 'f', 4) : "N/A") + "</td>";
        html += "<td>" + (b < (int)result.perBandPSNR.size() ? QString::number(result.perBandPSNR[b], 'f', 2) + " " + classifyPSNR(result.perBandPSNR[b]) : "N/A") + "</td>";
        html += "</tr>";
    }
    html += "</table>";

    html += "<h2>影像信息</h2><table>";
    html += "<tr><td>全色影像尺寸</td><td>" + QString::number(panImage.width) + " x " + QString::number(panImage.height) + "</td></tr>";
    html += "<tr><td>多光谱影像尺寸</td><td>" + QString::number(originalMs.width) + " x " + QString::number(originalMs.height) + "</td></tr>";
    html += "<tr><td>融合结果尺寸</td><td>" + QString::number(fusedImage.width) + " x " + QString::number(fusedImage.height) + "</td></tr>";
    html += "<tr><td>多光谱波段数</td><td>" + QString::number(originalMs.bands) + "</td></tr>";
    html += "</table>";

    html += "</body></html>";
    return html.toStdString();
}
