#include "stitch_core.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QString>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cfloat>
#include <random>
#include <ctime>
#include <cstdio>

// ============================================================================
// StitchConfig
// ============================================================================

StitchConfig::StitchConfig() { setDefaults(); }

void StitchConfig::setDefaults()
{
    algorithmType = 0;
    featureDetector = 2;
    matchMethod = 1;
    ransacThreshold = 3.0;
    maxIterations = 1000;
    blendMethod = 1;
    blendWidth = 64;
    downsampleScale = 0.5;
    useGeoCoordinates = false;
    enableRPCMode = false;
    rpcHeight = 0.0;
    rpcOutputResolution = 0.0;
}

std::string StitchConfig::serialize() const
{
    QString json = QString(
        "{\"algorithmType\":%1,\"featureDetector\":%2,\"matchMethod\":%3,"
        "\"ransacThreshold\":%4,\"maxIterations\":%5,\"blendMethod\":%6,"
        "\"blendWidth\":%7,\"downsampleScale\":%8,\"useGeoCoordinates\":%9,"
        "\"enableRPCMode\":%10,\"rpcHeight\":%11,\"rpcOutputResolution\":%12}")
        .arg(algorithmType)
        .arg(featureDetector)
        .arg(matchMethod)
        .arg(ransacThreshold, 0, 'f', 2)
        .arg(maxIterations)
        .arg(blendMethod)
        .arg(blendWidth)
        .arg(downsampleScale, 0, 'f', 2)
        .arg(useGeoCoordinates ? "true" : "false")
        .arg(enableRPCMode ? "true" : "false")
        .arg(rpcHeight, 0, 'f', 2)
        .arg(rpcOutputResolution, 0, 'f', 10);
    return json.toStdString();
}

bool StitchConfig::deserialize(const std::string& jsonStr)
{
    QByteArray data = QByteArray::fromStdString(jsonStr);
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) return false;

    QJsonObject obj = doc.object();
    algorithmType = obj["algorithmType"].toInt(0);
    featureDetector = obj["featureDetector"].toInt(2);
    matchMethod = obj["matchMethod"].toInt(1);
    ransacThreshold = obj["ransacThreshold"].toDouble(3.0);
    maxIterations = obj["maxIterations"].toInt(1000);
    blendMethod = obj["blendMethod"].toInt(1);
    blendWidth = obj["blendWidth"].toInt(64);
    downsampleScale = obj["downsampleScale"].toDouble(0.5);
    useGeoCoordinates = obj.contains("useGeoCoordinates") ? obj["useGeoCoordinates"].toBool() : false;
    enableRPCMode = obj.contains("enableRPCMode") ? obj["enableRPCMode"].toBool() : false;
    rpcHeight = obj.contains("rpcHeight") ? obj["rpcHeight"].toDouble(0.0) : 0.0;
    rpcOutputResolution = obj.contains("rpcOutputResolution") ? obj["rpcOutputResolution"].toDouble(0.0) : 0.0;
    return true;
}

// ============================================================================
// Image downsampling
// ============================================================================

void gaussianPyramidDownsample(const std::vector<double>& src, int srcW, int srcH,
                               std::vector<double>& dst, int& dstW, int& dstH)
{
    const double kernel[5][5] = {
        {1.0/256, 4.0/256,  6.0/256,  4.0/256,  1.0/256},
        {4.0/256, 16.0/256, 24.0/256, 16.0/256, 4.0/256},
        {6.0/256, 24.0/256, 36.0/256, 24.0/256, 6.0/256},
        {4.0/256, 16.0/256, 24.0/256, 16.0/256, 4.0/256},
        {1.0/256, 4.0/256,  6.0/256,  4.0/256,  1.0/256}
    };

    int bw = 2, bh = 2;
    int newW = (srcW - 2 * bw) / 2;
    int newH = (srcH - 2 * bh) / 2;
    if (newW <= 0) newW = 1;
    if (newH <= 0) newH = 1;

    std::vector<double> newDst(newW * newH, 0.0);

    for (int y = 0; y < newH; ++y)
    {
        int sy = y * 2;
        for (int x = 0; x < newW; ++x)
        {
            int sx = x * 2;
            double sum = 0.0;
            for (int ky = -2; ky <= 2; ++ky)
                for (int kx = -2; kx <= 2; ++kx)
                {
                    int px = sx + kx;
                    int py = sy + ky;
                    if (px >= 0 && px < srcW && py >= 0 && py < srcH)
                        sum += kernel[ky + 2][kx + 2] * src[py * srcW + px];
                }
            newDst[y * newW + x] = sum;
        }
    }

    dst = std::move(newDst);
    dstW = newW;
    dstH = newH;
}

// ============================================================================
// Feature detection
// ============================================================================

std::vector<KeyPoint> detectFASTCorners(const std::vector<double>& image, int width, int height,
                                         double threshold, int maxCorners)
{
    std::vector<KeyPoint> keypoints;
    if (width < 7 || height < 7) return keypoints;

    int circleOffsets[16][2] = {
        {0, -3}, {1, -3}, {2, -2}, {3, -1}, {3, 0}, {3, 1},
        {2, 2}, {1, 3}, {0, 3}, {-1, 3}, {-2, 2}, {-3, 1},
        {-3, 0}, {-3, -1}, {-2, -2}, {-1, -3}
    };

    for (int y = 3; y < height - 3; ++y)
    {
        for (int x = 3; x < width - 3; ++x)
        {
            double center = image[y * width + x];
            int brighter = 0, darker = 0;

            for (int i = 0; i < 16; ++i)
            {
                double val = image[(y + circleOffsets[i][1]) * width + (x + circleOffsets[i][0])];
                if (val > center + threshold) brighter++;
                else if (val < center - threshold) darker++;
            }

            if (brighter >= 12 || darker >= 12)
            {
                KeyPoint kp;
                kp.x = static_cast<double>(x);
                kp.y = static_cast<double>(y);
                kp.response = std::abs(
                    image[(y - 1) * width + (x - 1)] + image[(y - 1) * width + x] + image[(y - 1) * width + (x + 1)] +
                    image[y * width + (x - 1)] + image[y * width + x] + image[y * width + (x + 1)] +
                    image[(y + 1) * width + (x - 1)] + image[(y + 1) * width + x] + image[(y + 1) * width + (x + 1)]
                    - 9.0 * image[y * width + x]);
                keypoints.push_back(kp);
            }
        }
    }

    std::sort(keypoints.begin(), keypoints.end(),
        [](const KeyPoint& a, const KeyPoint& b) { return a.response > b.response; });

    if ((int)keypoints.size() > maxCorners)
        keypoints.resize(maxCorners);

    return keypoints;
}

std::vector<KeyPoint> detectHarrisCorners(const std::vector<double>& image, int width, int height,
                                           double k, int maxCorners)
{
    std::vector<KeyPoint> keypoints;
    if (width < 3 || height < 3) return keypoints;

    std::vector<double> Ix(static_cast<size_t>(width) * height, 0.0),
                         Iy(static_cast<size_t>(width) * height, 0.0);

    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
        {
            Ix[y * width + x] = image[y * width + (x + 1)] - image[y * width + (x - 1)];
            Iy[y * width + x] = image[(y + 1) * width + x] - image[(y - 1) * width + x];
        }

    for (int y = 2; y < height - 2; ++y)
        for (int x = 2; x < width - 2; ++x)
        {
            double sIxx = 0, sIyy = 0, sIxy = 0;
            for (int wy = -2; wy <= 2; ++wy)
                for (int wx = -2; wx <= 2; ++wx)
                {
                    double ix = Ix[(y + wy) * width + (x + wx)];
                    double iy = Iy[(y + wy) * width + (x + wx)];
                    sIxx += ix * ix;
                    sIyy += iy * iy;
                    sIxy += ix * iy;
                }

            double det = sIxx * sIyy - sIxy * sIxy;
            double trace = sIxx + sIyy;
            double R = det - k * trace * trace;

            if (R > 1e6)
            {
                KeyPoint kp;
                kp.x = static_cast<double>(x);
                kp.y = static_cast<double>(y);
                kp.response = R;
                keypoints.push_back(kp);
            }
        }

    std::sort(keypoints.begin(), keypoints.end(),
        [](const KeyPoint& a, const KeyPoint& b) { return a.response > b.response; });

    if ((int)keypoints.size() > maxCorners)
        keypoints.resize(maxCorners);

    return keypoints;
}

std::vector<KeyPoint> detectSiftLike(const std::vector<double>& image, int width, int height,
                                      int maxKeypoints)
{
    std::vector<KeyPoint> keypoints;
    if (width < 11 || height < 11) return keypoints;

    std::vector<double> blurred(static_cast<size_t>(width) * height);
    const double gaussKernel[5] = { 0.0625, 0.25, 0.375, 0.25, 0.0625 };

    for (int y = 2; y < height - 2; ++y)
        for (int x = 0; x < width; ++x)
            blurred[y * width + x] =
                gaussKernel[0] * image[(y - 2) * width + x] +
                gaussKernel[1] * image[(y - 1) * width + x] +
                gaussKernel[2] * image[y * width + x] +
                gaussKernel[3] * image[(y + 1) * width + x] +
                gaussKernel[4] * image[(y + 2) * width + x];

    std::vector<double> dog(static_cast<size_t>(width) * height, 0.0);

    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
        {
            dog[y * width + x] = std::abs(blurred[y * width + x] - image[(y - 1) * width + (x - 1)]
                - image[(y - 1) * width + x] - image[(y - 1) * width + (x + 1)]
                - image[y * width + (x - 1)] + 8.0 * image[y * width + x]
                - image[y * width + (x + 1)] - image[(y + 1) * width + (x - 1)]
                - image[(y + 1) * width + x] - image[(y + 1) * width + (x + 1)]);
        }

    for (int y = 5; y < height - 5; ++y)
        for (int x = 5; x < width - 5; ++x)
        {
            double val = dog[y * width + x];
            bool isMax = true;
            for (int dy = -1; dy <= 1 && isMax; ++dy)
                for (int dx = -1; dx <= 1 && isMax; ++dx)
                    if (dy != 0 || dx != 0)
                        if (dog[(y + dy) * width + (x + dx)] >= val) isMax = false;

            if (isMax && val > 1.0)
            {
                KeyPoint kp;
                kp.x = static_cast<double>(x);
                kp.y = static_cast<double>(y);
                kp.response = val;
                keypoints.push_back(kp);
            }
        }

    std::sort(keypoints.begin(), keypoints.end(),
        [](const KeyPoint& a, const KeyPoint& b) { return a.response > b.response; });

    if ((int)keypoints.size() > maxKeypoints)
        keypoints.resize(maxKeypoints);

    return keypoints;
}

std::vector<KeyPoint> detectKeypoints(const std::vector<double>& image, int width, int height,
                                       int detectorType, const StitchConfig& config)
{
    Q_UNUSED(config);
    switch (detectorType)
    {
    case 0: return detectFASTCorners(image, width, height, 40.0, 500);
    case 1: return detectHarrisCorners(image, width, height, 0.04, 500);
    case 2:
    default: return detectSiftLike(image, width, height, 500);
    }
}

// ============================================================================
// Descriptor computation
// ============================================================================

std::vector<std::vector<double>> computePatchDescriptors(
    const std::vector<double>& image, int width, int height,
    const std::vector<KeyPoint>& keypoints, int patchSize)
{
    std::vector<std::vector<double>> descriptors;
    int half = patchSize / 2;

    for (const auto& kp : keypoints)
    {
        int cx = static_cast<int>(kp.x);
        int cy = static_cast<int>(kp.y);

        std::vector<double> desc;
        std::vector<double> raw;

        for (int py = cy - half; py <= cy + half; ++py)
            for (int px = cx - half; px <= cx + half; ++px)
                if (px >= 0 && px < width && py >= 0 && py < height)
                    raw.push_back(image[py * width + px]);
                else
                    raw.push_back(0.0);

        double mean = std::accumulate(raw.begin(), raw.end(), 0.0) / raw.size();
        double stdv = 0.0;
        for (double v : raw) stdv += (v - mean) * (v - mean);
        stdv = std::sqrt(stdv / raw.size() + 1e-8);

        desc.resize(raw.size());
        for (size_t i = 0; i < raw.size(); ++i)
            desc[i] = (raw[i] - mean) / stdv;

        descriptors.push_back(desc);
    }

    return descriptors;
}

// ============================================================================
// Descriptor matching
// ============================================================================

double computeNCC(const std::vector<double>& patch1, const std::vector<double>& patch2)
{
    if (patch1.size() != patch2.size() || patch1.empty()) return 0.0;

    double m1 = 0.0, m2 = 0.0;
    for (size_t i = 0; i < patch1.size(); ++i)
    {
        m1 += patch1[i];
        m2 += patch2[i];
    }
    m1 /= patch1.size();
    m2 /= patch2.size();

    double num = 0.0, den1 = 0.0, den2 = 0.0;
    for (size_t i = 0; i < patch1.size(); ++i)
    {
        double d1 = patch1[i] - m1;
        double d2 = patch2[i] - m2;
        num += d1 * d2;
        den1 += d1 * d1;
        den2 += d2 * d2;
    }

    double den = std::sqrt(den1 * den2);
    return (den > 1e-10) ? num / den : 0.0;
}

std::vector<StitchMatch> matchDescriptors(
    const std::vector<std::vector<double>>& desc1,
    const std::vector<std::vector<double>>& desc2,
    int matchMethod, double ratioThreshold)
{
    std::vector<StitchMatch> matches;

    if (desc1.empty() || desc2.empty()) return matches;

    for (size_t i = 0; i < desc1.size(); ++i)
    {
        double bestDist = DBL_MAX;
        double secondDist = DBL_MAX;
        int bestIdx = -1;

        for (size_t j = 0; j < desc2.size(); ++j)
        {
            double dist = 0.0;

            if (matchMethod == 0)
            {
                if (desc1[i].size() != desc2[j].size()) continue;
                for (size_t k = 0; k < desc1[i].size(); ++k)
                {
                    double d = desc1[i][k] - desc2[j][k];
                    dist += d * d;
                }
            }
            else
            {
                dist = 1.0 - computeNCC(desc1[i], desc2[j]);
            }

            if (dist < bestDist)
            {
                secondDist = bestDist;
                bestDist = dist;
                bestIdx = static_cast<int>(j);
            }
            else if (dist < secondDist)
            {
                secondDist = dist;
            }
        }

        if (bestIdx >= 0 && bestDist < ratioThreshold * secondDist)
        {
            StitchMatch m;
            m.srcIdx = static_cast<int>(i);
            m.dstIdx = bestIdx;
            m.distance = bestDist;
            matches.push_back(m);
        }
    }

    return matches;
}

// ============================================================================
// Homography estimation via RANSAC
// ============================================================================

Homography estimateHomographyRANSAC(
    const std::vector<KeyPoint>& kp1,
    const std::vector<KeyPoint>& kp2,
    const std::vector<StitchMatch>& matches,
    double threshold, int maxIterations)
{
    Homography bestH;
    for (int i = 0; i < 9; ++i) bestH.H[i] = (i == 0 || i == 4 || i == 8) ? 1.0 : 0.0;
    bestH.H[2] = 0.0;
    bestH.H[5] = 0.0;

    if (matches.size() < 4) return bestH;

    int bestInliers = 0;
    std::mt19937 rng(static_cast<unsigned>(time(nullptr)));

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        std::vector<int> indices(4);
        for (int i = 0; i < 4; ++i)
            indices[i] = rng() % static_cast<int>(matches.size());

        double x1[4], y1[4], x2[4], y2[4];
        for (int i = 0; i < 4; ++i)
        {
            x1[i] = kp1[matches[indices[i]].srcIdx].x;
            y1[i] = kp1[matches[indices[i]].srcIdx].y;
            x2[i] = kp2[matches[indices[i]].dstIdx].x;
            y2[i] = kp2[matches[indices[i]].dstIdx].y;
        }

        Matrix A(8, 8);
        std::vector<double> B(8, 0.0);

        for (int i = 0; i < 4; ++i)
        {
            A(2 * i, 0) = x1[i]; A(2 * i, 1) = y1[i]; A(2 * i, 2) = 1.0;
            A(2 * i, 3) = 0.0;    A(2 * i, 4) = 0.0;    A(2 * i, 5) = 0.0;
            A(2 * i, 6) = -x2[i] * x1[i]; A(2 * i, 7) = -x2[i] * y1[i];
            B[2 * i] = x2[i];

            A(2 * i + 1, 0) = 0.0;    A(2 * i + 1, 1) = 0.0;    A(2 * i + 1, 2) = 0.0;
            A(2 * i + 1, 3) = x1[i]; A(2 * i + 1, 4) = y1[i]; A(2 * i + 1, 5) = 1.0;
            A(2 * i + 1, 6) = -y2[i] * x1[i]; A(2 * i + 1, 7) = -y2[i] * y1[i];
            B[2 * i + 1] = y2[i];
        }

        std::vector<double> h(8, 0.0);

        for (int i = 0; i < 8; ++i)
        {
            int pivot = i;
            double maxVal = std::abs(A(i, i));
            for (int j = i + 1; j < 8; ++j)
            {
                double absVal = std::abs(A(j, i));
                if (absVal > maxVal) { maxVal = absVal; pivot = j; }
            }
            if (maxVal < 1e-10) continue;

            for (int j = 0; j < 8; ++j) std::swap(A(i, j), A(pivot, j));
            std::swap(B[i], B[pivot]);

            double pivVal = A(i, i);
            for (int j = i; j < 8; ++j) A(i, j) /= pivVal;
            B[i] /= pivVal;

            for (int j = 0; j < 8; ++j)
                if (j != i)
                {
                    double factor = A(j, i);
                    for (int k = i; k < 8; ++k) A(j, k) -= factor * A(i, k);
                    B[j] -= factor * B[i];
                }
        }

        for (int i = 0; i < 8; ++i) h[i] = B[i];

        Homography curH;
        curH.H[0] = h[0]; curH.H[1] = h[1]; curH.H[2] = h[2];
        curH.H[3] = h[3]; curH.H[4] = h[4]; curH.H[5] = h[5];
        curH.H[6] = h[6]; curH.H[7] = h[7]; curH.H[8] = 1.0;

        int inliers = 0;
        for (const auto& m : matches)
        {
            double ox, oy;
            applyHomography(curH, kp1[m.srcIdx].x, kp1[m.srcIdx].y, ox, oy);
            double dx = ox - kp2[m.dstIdx].x;
            double dy = oy - kp2[m.dstIdx].y;
            if (std::sqrt(dx * dx + dy * dy) < threshold) inliers++;
        }

        if (inliers > bestInliers)
        {
            bestInliers = inliers;
            bestH = curH;
        }
    }

    return bestH;
}

// ============================================================================
// Homography application
// ============================================================================

void applyHomography(const Homography& H, double x, double y, double& ox, double& oy)
{
    double w = H.H[6] * x + H.H[7] * y + H.H[8];
    if (std::abs(w) < 1e-10) w = 1e-10;
    ox = (H.H[0] * x + H.H[1] * y + H.H[2]) / w;
    oy = (H.H[3] * x + H.H[4] * y + H.H[5]) / w;
}

void invertHomography(const Homography& H, Homography& Hinv)
{
    double det = H.H[0] * (H.H[4] * H.H[8] - H.H[5] * H.H[7])
               - H.H[1] * (H.H[3] * H.H[8] - H.H[5] * H.H[6])
               + H.H[2] * (H.H[3] * H.H[7] - H.H[4] * H.H[6]);

    if (std::abs(det) < 1e-15)
    {
        for (int i = 0; i < 9; ++i)
            Hinv.H[i] = (i == 0 || i == 4 || i == 8) ? 1.0 : 0.0;
        return;
    }

    double invDet = 1.0 / det;
    Hinv.H[0] = (H.H[4] * H.H[8] - H.H[5] * H.H[7]) * invDet;
    Hinv.H[1] = (H.H[2] * H.H[7] - H.H[1] * H.H[8]) * invDet;
    Hinv.H[2] = (H.H[1] * H.H[5] - H.H[2] * H.H[4]) * invDet;
    Hinv.H[3] = (H.H[5] * H.H[6] - H.H[3] * H.H[8]) * invDet;
    Hinv.H[4] = (H.H[0] * H.H[8] - H.H[2] * H.H[6]) * invDet;
    Hinv.H[5] = (H.H[2] * H.H[3] - H.H[0] * H.H[5]) * invDet;
    Hinv.H[6] = (H.H[3] * H.H[7] - H.H[4] * H.H[6]) * invDet;
    Hinv.H[7] = (H.H[1] * H.H[6] - H.H[0] * H.H[7]) * invDet;
    Hinv.H[8] = (H.H[0] * H.H[4] - H.H[1] * H.H[3]) * invDet;
}

void computeOutputBounds(const std::vector<double>& img1, int w1, int h1,
                          const std::vector<double>& img2, int w2, int h2,
                          const Homography& H,
                          int& outW, int& outH, int& offsetX, int& offsetY)
{
    Q_UNUSED(img1);
    Q_UNUSED(img2);

    double minX = DBL_MAX, maxX = -DBL_MAX;
    double minY = DBL_MAX, maxY = -DBL_MAX;

    int corners1[4][2] = { {0, 0}, {w1 - 1, 0}, {0, h1 - 1}, {w1 - 1, h1 - 1} };
    for (int i = 0; i < 4; ++i)
    {
        if (corners1[i][0] < minX) minX = corners1[i][0];
        if (corners1[i][0] > maxX) maxX = corners1[i][0];
        if (corners1[i][1] < minY) minY = corners1[i][1];
        if (corners1[i][1] > maxY) maxY = corners1[i][1];
    }

    Homography Hinv;
    invertHomography(H, Hinv);

    int corners2[4][2] = { {0, 0}, {w2 - 1, 0}, {0, h2 - 1}, {w2 - 1, h2 - 1} };
    for (int i = 0; i < 4; ++i)
    {
        double ox, oy;
        applyHomography(Hinv, corners2[i][0], corners2[i][1], ox, oy);
        if (ox < minX) minX = ox; if (ox > maxX) maxX = ox;
        if (oy < minY) minY = oy; if (oy > maxY) maxY = oy;
    }

    const double MAX_DIM = 50000.0;
    if (maxX > MAX_DIM) maxX = MAX_DIM;
    if (maxY > MAX_DIM) maxY = MAX_DIM;
    if (minX < -MAX_DIM) minX = -MAX_DIM;
    if (minY < -MAX_DIM) minY = -MAX_DIM;

    offsetX = static_cast<int>(-minX);
    offsetY = static_cast<int>(-minY);
    outW = static_cast<int>(maxX - minX + 1);
    outH = static_cast<int>(maxY - minY + 1);
    if (outW <= 0) outW = 1;
    if (outH <= 0) outH = 1;

    const int MAX_PIXELS_PER_DIM = 30000;
    if (outW > MAX_PIXELS_PER_DIM) outW = MAX_PIXELS_PER_DIM;
    if (outH > MAX_PIXELS_PER_DIM) outH = MAX_PIXELS_PER_DIM;
}

// ============================================================================
// Linear ramp blending
// ============================================================================

void blendLinearRamp(std::vector<double>& output, int outW, int outH,
                      const std::vector<double>& img1, int w1, int h1,
                      const std::vector<double>& img2, int w2, int h2,
                      int offsetX, int offsetY, const Homography& H, int blendWidth)
{
    if (outW <= 0 || outH <= 0) return;

    for (int y = 0; y < outH; ++y)
    {
        for (int x = 0; x < outW; ++x)
        {
            int px = x - offsetX;
            int py = y - offsetY;

            double i1x = static_cast<double>(px);
            double i1y = static_cast<double>(py);

            bool in1 = (px >= 0 && px < w1 && py >= 0 && py < h1);

            double ox, oy;
            applyHomography(H, static_cast<double>(px), static_cast<double>(py), ox, oy);
            int i2x = static_cast<int>(std::round(ox));
            int i2y = static_cast<int>(std::round(oy));
            bool in2 = (i2x >= 0 && i2x < w2 && i2y >= 0 && i2y < h2);

            if (in1 && in2)
            {
                double v1 = img1[py * w1 + px];
                double v2 = img2[i2y * w2 + i2x];

                double distToBorder1 = std::min({static_cast<double>(px), static_cast<double>(w1 - 1 - px),
                                                  static_cast<double>(py), static_cast<double>(h1 - 1 - py)});
                double distToBorder2 = std::min({std::abs(ox), std::abs(static_cast<double>(w2 - 1) - ox),
                                                  std::abs(oy), std::abs(static_cast<double>(h2 - 1) - oy)});

                double alpha1 = std::min(1.0, distToBorder1 / static_cast<double>(blendWidth));
                double alpha2 = std::min(1.0, distToBorder2 / static_cast<double>(blendWidth));

                double sumAlpha = alpha1 + alpha2;
                if (sumAlpha > 1e-10)
                    output[y * outW + x] = (alpha1 * v1 + alpha2 * v2) / sumAlpha;
                else
                    output[y * outW + x] = (v1 + v2) * 0.5;
            }
            else if (in1)
            {
                output[y * outW + x] = img1[py * w1 + px];
            }
            else if (in2)
            {
                output[y * outW + x] = img2[i2y * w2 + i2x];
            }
        }
    }
}

// ============================================================================
// Edge map
// ============================================================================

std::vector<double> computeEdgeMap(const std::vector<double>& image, int width, int height)
{
    std::vector<double> edges(static_cast<size_t>(width) * height, 0.0);

    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
        {
            double gx = image[y * width + (x + 1)] - image[y * width + (x - 1)];
            double gy = image[(y + 1) * width + x] - image[(y - 1) * width + x];
            edges[y * width + x] = std::sqrt(gx * gx + gy * gy);
        }

    return edges;
}

// ============================================================================
// Traditional stitching pipeline
// ============================================================================

MultiBandImage stitchImagesTraditional(const MultiBandImage& img1, const MultiBandImage& img2,
                                        const StitchConfig& config, double* elapsedMs)
{
    QElapsedTimer timer;
    timer.start();

    MultiBandImage result;
    if (!img1.isValid() || !img2.isValid()) return result;

    int minBands = std::min(img1.bands, img2.bands);
    int w1 = img1.width, h1 = img1.height;
    int w2 = img2.width, h2 = img2.height;

    std::vector<double> gray1(static_cast<size_t>(w1) * h1), gray2(static_cast<size_t>(w2) * h2);
    size_t np1 = gray1.size(), np2 = gray2.size();
    for (size_t i = 0; i < np1; ++i)
    {
        double sum = 0.0;
        for (int b = 0; b < minBands; ++b) sum += img1.bandData[b][i];
        gray1[i] = sum / minBands;
    }
    for (size_t i = 0; i < np2; ++i)
    {
        double sum = 0.0;
        for (int b = 0; b < minBands; ++b) sum += img2.bandData[b][i];
        gray2[i] = sum / minBands;
    }

    std::vector<double> working1 = gray1, working2 = gray2;
    int ww1 = w1, hh1 = h1, ww2 = w2, hh2 = h2;

    if (config.downsampleScale < 1.0)
    {
        gaussianPyramidDownsample(working1, ww1, hh1, working1, ww1, hh1);
        gaussianPyramidDownsample(working2, ww2, hh2, working2, ww2, hh2);
    }

    auto kp1 = detectKeypoints(working1, ww1, hh1, config.featureDetector, config);
    auto kp2 = detectKeypoints(working2, ww2, hh2, config.featureDetector, config);

    auto desc1 = computePatchDescriptors(working1, ww1, hh1, kp1, 9);
    auto desc2 = computePatchDescriptors(working2, ww2, hh2, kp2, 9);

    auto matches = matchDescriptors(desc1, desc2, config.matchMethod, 0.8);

    double scaleFactor = 1.0 / config.downsampleScale;
    for (auto& kp : kp1) { kp.x *= scaleFactor; kp.y *= scaleFactor; }
    for (auto& kp : kp2) { kp.x *= scaleFactor; kp.y *= scaleFactor; }

    double ransacThresh = config.ransacThreshold;
    Homography H = estimateHomographyRANSAC(kp1, kp2, matches, ransacThresh, config.maxIterations);

    int outW, outH, offsetX, offsetY;
    computeOutputBounds(gray1, w1, h1, gray2, w2, h2, H, outW, outH, offsetX, offsetY);

    result.create(outW, outH, minBands);

    for (int b = 0; b < minBands; ++b)
    {
        std::vector<double> bandOut(static_cast<size_t>(outW) * outH, 0.0);
        blendLinearRamp(bandOut, outW, outH,
                         const_cast<MultiBandImage&>(img1).bandData[b], w1, h1,
                         const_cast<MultiBandImage&>(img2).bandData[b], w2, h2,
                         offsetX, offsetY, H, config.blendWidth);
        result.bandData[b] = bandOut;
    }

    if (elapsedMs) *elapsedMs = timer.elapsed();
    return result;
}

// ============================================================================
// Integrity validation
// ============================================================================

StitchIntegrityReport::StitchIntegrityReport()
    : emptyPixelCount(0)
    , totalPixels(0)
    , emptyRatio(0.0)
    , deadColumnCount(0)
    , edgeMismatchCount(0)
    , hasValidData(false)
    , passed(false)
{
}

StitchIntegrityReport validateStitchIntegrity(const MultiBandImage& stitched,
                                               int srcW1, int srcH1,
                                               int srcW2, int srcH2,
                                               int offsetX, int offsetY)
{
    StitchIntegrityReport report;
    if (!stitched.isValid()) return report;

    int w = stitched.width;
    int h = stitched.height;
    report.totalPixels = w * h;

    int emptyCount = 0;
    int deadColumns = 0;
    int edgeMismatches = 0;

    for (int b = 0; b < stitched.bands; ++b)
    {
        const auto& band = stitched.bandData[b];

        for (int col = 0; col < w; ++col)
        {
            bool colDead = true;
            for (int row = 0; row < h; ++row)
            {
                double v = band[row * w + col];
                if (std::abs(v) < 1e-10)
                    emptyCount++;
                if (std::abs(v) > 1e-10)
                    colDead = false;
            }
            if (colDead) deadColumns++;
        }

        if (stitched.bands >= 3)
        {
            const auto& r = stitched.bandData[0];
            const auto& g = stitched.bandData[1];
            const auto& bl = stitched.bandData[2];
            for (int row = 0; row < h; ++row)
                for (int col = 0; col < w - 1; ++col)
                {
                    double dr = std::abs(r[row * w + col] - r[row * w + col + 1]);
                    double dg = std::abs(g[row * w + col] - g[row * w + col + 1]);
                    double db = std::abs(bl[row * w + col] - bl[row * w + col + 1]);
                    if (dr > 200.0 || dg > 200.0 || db > 200.0)
                        edgeMismatches++;
                }
            break;
        }
    }

    report.emptyPixelCount = emptyCount;
    report.deadColumnCount = deadColumns;
    report.edgeMismatchCount = edgeMismatches;

    long long totalSamples = static_cast<long long>(report.totalPixels) * stitched.bands;
    if (totalSamples > 0)
        report.emptyRatio = static_cast<double>(report.emptyPixelCount) / totalSamples;
    report.hasValidData = (report.emptyRatio < 0.99);

    report.passed = report.hasValidData &&
                    report.emptyRatio < 0.60 &&
                    report.edgeMismatchCount < w * h * 0.05;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "空像素比:%.1f%% | 死列:%d | 边缘跳变:%d | %s",
        report.emptyRatio * 100.0, report.deadColumnCount,
        report.edgeMismatchCount,
        report.passed ? "通过" : "异常");
    report.summary = std::string(buf);

    return report;
}

// ============================================================================
// StitchValidationResult
// ============================================================================

StitchValidationResult::StitchValidationResult()
    : samplePointCount(0)
    , meanErrorMeters(0.0)
    , maxErrorMeters(0.0)
    , rmsErrorMeters(0.0)
    , passed(false)
    , thresholdMeters(10.0)
{
}

void StitchValidationResult::clear()
{
    samplePointCount = 0;
    point1Line.clear();
    point1Samp.clear();
    point1Lon.clear();
    point1Lat.clear();
    point2Line.clear();
    point2Samp.clear();
    point2Lon.clear();
    point2Lat.clear();
    geoDistanceMeters.clear();
    meanErrorMeters = 0.0;
    maxErrorMeters = 0.0;
    rmsErrorMeters = 0.0;
    passed = false;
}

std::string StitchValidationResult::toReportCSV() const
{
    std::string report;
    report += "Index,Img1Line,Img1Samp,Img1Lon,Img1Lat,Img2Line,Img2Samp,Img2Lon,Img2Lat,GeoDistance_Meters\n";
    for (int i = 0; i < samplePointCount; ++i)
    {
        report += std::to_string(i) + ",";
        report += std::to_string(point1Line[i]) + ",";
        report += std::to_string(point1Samp[i]) + ",";
        report += std::to_string(point1Lon[i]) + ",";
        report += std::to_string(point1Lat[i]) + ",";
        report += std::to_string(point2Line[i]) + ",";
        report += std::to_string(point2Samp[i]) + ",";
        report += std::to_string(point2Lon[i]) + ",";
        report += std::to_string(point2Lat[i]) + ",";
        report += std::to_string(geoDistanceMeters[i]) + "\n";
    }
    return report;
}

std::string StitchValidationResult::toReportSummary() const
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "样本点数: %d | 平均误差: %.3fm | 最大误差: %.3fm | RMS误差: %.3fm | 阈值: %.1fm | %s",
        samplePointCount, meanErrorMeters, maxErrorMeters, rmsErrorMeters,
        thresholdMeters, passed ? "通过" : "未通过");
    return std::string(buf);
}

// ============================================================================
// StitchOutputMetadata
// ============================================================================

StitchOutputMetadata::StitchOutputMetadata()
    : hasRPC(false)
    , minLon(0.0)
    , maxLon(0.0)
    , minLat(0.0)
    , maxLat(0.0)
    , outputWidth(0)
    , outputHeight(0)
{
}

// ============================================================================
// RPC-based stitching
// ============================================================================

Homography computeHomographyFromRPC(const RPCModel& rpc1, const RPCModel& rpc2,
                                     int w1, int h1, int w2, int h2,
                                     double height, double outputGSD,
                                     int& outW, int& outH,
                                     double& outMinLon, double& outMaxLon,
                                     double& outMinLat, double& outMaxLat)
{
    Homography H;
    for (int i = 0; i < 9; ++i) H.H[i] = (i == 0 || i == 4 || i == 8) ? 1.0 : 0.0;

    if (!rpc1.valid || !rpc2.valid)
        return H;

    double minLon1, maxLon1, minLat1, maxLat1;
    double minLon2, maxLon2, minLat2, maxLat2;

    rpc1.computeGeoBounds(height, minLon1, maxLon1, minLat1, maxLat1);
    rpc2.computeGeoBounds(height, minLon2, maxLon2, minLat2, maxLat2);

    outMinLon = std::min(minLon1, minLon2);
    outMaxLon = std::max(maxLon1, maxLon2);
    outMinLat = std::min(minLat1, minLat2);
    outMaxLat = std::max(maxLat1, maxLat2);

    double pw1 = (maxLon1 - minLon1) / static_cast<double>(w1);
    double ph1 = (maxLat1 - minLat1) / static_cast<double>(h1);
    double pw2 = (maxLon2 - minLon2) / static_cast<double>(w2);
    double ph2 = (maxLat2 - minLat2) / static_cast<double>(h2);

    if (outputGSD <= 0.0)
        outputGSD = std::min(std::abs(pw1), std::abs(pw2));

    outW = static_cast<int>(std::ceil((outMaxLon - outMinLon) / outputGSD));
    outH = static_cast<int>(std::ceil((outMaxLat - outMinLat) / outputGSD));
    if (outW <= 0) outW = std::max(w1, w2);
    if (outH <= 0) outH = std::max(h1, h2);

    double dx = (minLon2 - minLon1);
    double dy = (maxLat2 - maxLat1);

    if (std::abs(pw1) < 1e-15) pw1 = outputGSD;
    if (std::abs(ph1) < 1e-15) ph1 = outputGSD;

    H.H[0] = pw1 / pw2;
    H.H[1] = 0.0;
    H.H[2] = (minLon1 - minLon2) / pw2;
    H.H[3] = 0.0;
    H.H[4] = ph1 / ph2;
    H.H[5] = dy / ph2;
    H.H[6] = 0.0;
    H.H[7] = 0.0;
    H.H[8] = 1.0;

    return H;
}

bool validateRPCMetadataForStitch(const RPCModel& rpc1, const RPCModel& rpc2, std::string& warning)
{
    warning.clear();

    if (!rpc1.valid)
    {
        warning = "左/上影像的RPC模型无效";
        return false;
    }

    if (!rpc2.valid)
    {
        warning = "右/下影像的RPC模型无效";
        return false;
    }

    double minLon1, maxLon1, minLat1, maxLat1;
    double minLon2, maxLon2, minLat2, maxLat2;

    rpc1.computeGeoBounds(0.0, minLon1, maxLon1, minLat1, maxLat1);
    rpc2.computeGeoBounds(0.0, minLon2, maxLon2, minLat2, maxLat2);

    double overlapLon = std::max(0.0, std::min(maxLon1, maxLon2) - std::max(minLon1, minLon2));
    double overlapLat = std::max(0.0, std::min(maxLat1, maxLat2) - std::max(minLat1, minLat2));

    if (overlapLon <= 0.0 && overlapLat <= 0.0)
    {
        warning = "两幅影像的地理范围无重叠区域，无法拼接";
        return false;
    }

    return true;
}

static double haversineDistanceMeters(double lon1, double lat1, double lon2, double lat2)
{
    double R = 6371000.0;
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;
    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1 * PI / 180.0) * std::cos(lat2 * PI / 180.0) *
               std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
}

StitchValidationResult validateStitchAccuracy(const MultiBandImage& img1, const MultiBandImage& img2,
                                               const RPCModel& rpc1, const RPCModel& rpc2,
                                               const Homography& H, int outW, int outH,
                                               int offsetX, int offsetY,
                                               int sampleCount, double thresholdMeters)
{
    StitchValidationResult result;
    result.thresholdMeters = thresholdMeters;

    if (!rpc1.valid || !rpc2.valid || !img1.isValid() || !img2.isValid())
        return result;

    int w1 = img1.width, h1 = img1.height;
    int hh = 0;

    std::mt19937 rng(static_cast<unsigned>(42));
    std::uniform_int_distribution<int> distX(0, outW - 1);
    std::uniform_int_distribution<int> distY(0, outH - 1);

    for (int i = 0; i < sampleCount; ++i)
    {
        int ox = distX(rng);
        int oy = distY(rng);

        int px1 = ox - offsetX;
        int py1 = oy - offsetY;
        bool in1 = (px1 >= 0 && px1 < w1 && py1 >= 0 && py1 < h1);

        double ox2, oy2;
        applyHomography(H, static_cast<double>(px1), static_cast<double>(py1), ox2, oy2);
        int px2 = static_cast<int>(std::round(ox2));
        int py2 = static_cast<int>(std::round(oy2));
        bool in2 = (px2 >= 0 && px2 < img2.width && py2 >= 0 && py2 < img2.height);

        if (!in1 || !in2) continue;

        double lon1, lat1, lon2, lat2;
        rpc1.forward(py1, px1, lon1, lat1, 0.0);
        rpc2.forward(py2, px2, lon2, lat2, 0.0);

        double dist = haversineDistanceMeters(lon1, lat1, lon2, lat2);

        result.point1Line.push_back(static_cast<double>(py1));
        result.point1Samp.push_back(static_cast<double>(px1));
        result.point1Lon.push_back(lon1);
        result.point1Lat.push_back(lat1);
        result.point2Line.push_back(static_cast<double>(py2));
        result.point2Samp.push_back(static_cast<double>(px2));
        result.point2Lon.push_back(lon2);
        result.point2Lat.push_back(lat2);
        result.geoDistanceMeters.push_back(dist);

        hh++;
    }

    result.samplePointCount = hh;
    if (hh == 0) return result;

    double sum = 0.0, sumSq = 0.0;
    result.maxErrorMeters = 0.0;
    for (double d : result.geoDistanceMeters)
    {
        sum += d;
        sumSq += d * d;
        if (d > result.maxErrorMeters) result.maxErrorMeters = d;
    }
    result.meanErrorMeters = sum / hh;
    result.rmsErrorMeters = std::sqrt(sumSq / hh);
    result.passed = (result.rmsErrorMeters <= thresholdMeters);

    return result;
}

MultiBandImage stitchImagesRPC(const MultiBandImage& img1, const MultiBandImage& img2,
                                const RPCModel& rpc1, const RPCModel& rpc2,
                                const StitchConfig& config, StitchOutputMetadata* outMeta,
                                StitchValidationResult* validation,
                                double* elapsedMs)
{
    QElapsedTimer timer;
    timer.start();

    MultiBandImage result;
    if (!img1.isValid() || !img2.isValid()) return result;
    if (!rpc1.valid || !rpc2.valid) return result;

    int minBands = std::min(img1.bands, img2.bands);
    int w1 = img1.width, h1 = img1.height;
    int w2 = img2.width, h2 = img2.height;

    double height = config.rpcHeight;
    double outputGSD = config.rpcOutputResolution;

    int outW, outH;
    double outMinLon, outMaxLon, outMinLat, outMaxLat;
    Homography H = computeHomographyFromRPC(rpc1, rpc2, w1, h1, w2, h2,
                                             height, outputGSD,
                                             outW, outH,
                                             outMinLon, outMaxLon,
                                             outMinLat, outMaxLat);

    std::vector<double> dummy(static_cast<size_t>(w1) * h1, 0.0);
    std::vector<double> dummy2(static_cast<size_t>(w2) * h2, 0.0);
    int outW2, outH2, offsetX, offsetY;
    computeOutputBounds(dummy, w1, h1, dummy2, w2, h2, H, outW2, outH2, offsetX, offsetY);

    if (outW2 <= 0) outW2 = outW;
    if (outH2 <= 0) outH2 = outH;
    outW = outW2;
    outH = outH2;

    result.create(outW, outH, minBands);

    for (int b = 0; b < minBands; ++b)
    {
        std::vector<double> bandOut(static_cast<size_t>(outW) * outH, 0.0);
        blendLinearRamp(bandOut, outW, outH,
                         const_cast<MultiBandImage&>(img1).bandData[b], w1, h1,
                         const_cast<MultiBandImage&>(img2).bandData[b], w2, h2,
                         offsetX, offsetY, H, config.blendWidth);
        result.bandData[b] = bandOut;
    }

    if (outMeta)
    {
        outMeta->hasRPC = true;
        outMeta->rpcModel = rpc1;
        outMeta->rpcModel.imageWidth = outW;
        outMeta->rpcModel.imageHeight = outH;
        outMeta->rpcModel.norm.lineOffset = static_cast<double>(outH) / 2.0;
        outMeta->rpcModel.norm.sampOffset = static_cast<double>(outW) / 2.0;
        outMeta->rpcModel.norm.lineScale = static_cast<double>(outH) / 2.0;
        outMeta->rpcModel.norm.sampScale = static_cast<double>(outW) / 2.0;
        outMeta->rpcModel.norm.latOffset = (outMinLat + outMaxLat) / 2.0;
        outMeta->rpcModel.norm.longOffset = (outMinLon + outMaxLon) / 2.0;
        outMeta->rpcModel.norm.latScale = (outMaxLat - outMinLat) / 2.0;
        outMeta->rpcModel.norm.longScale = (outMaxLon - outMinLon) / 2.0;

        outMeta->minLon = outMinLon;
        outMeta->maxLon = outMaxLon;
        outMeta->minLat = outMinLat;
        outMeta->maxLat = outMaxLat;
        outMeta->outputWidth = outW;
        outMeta->outputHeight = outH;
    }

    if (validation)
    {
        *validation = validateStitchAccuracy(img1, img2, rpc1, rpc2,
                                              H, outW, outH, offsetX, offsetY, 25, 10.0);
    }

    if (elapsedMs) *elapsedMs = timer.elapsed();
    return result;
}

// ============================================================================
// Deep stitching pipeline (multi-scale + edge-aware)
// ============================================================================

MultiBandImage stitchImagesDeep(const MultiBandImage& img1, const MultiBandImage& img2,
                                 const StitchConfig& config, double* elapsedMs)
{
    QElapsedTimer timer;
    timer.start();

    MultiBandImage result;
    if (!img1.isValid() || !img2.isValid()) return result;

    int minBands = std::min(img1.bands, img2.bands);
    int w1 = img1.width, h1 = img1.height;
    int w2 = img2.width, h2 = img2.height;

    std::vector<double> gray1(static_cast<size_t>(w1) * h1), gray2(static_cast<size_t>(w2) * h2);
    size_t np1 = gray1.size(), np2 = gray2.size();
    for (size_t i = 0; i < np1; ++i)
    {
        double sum = 0.0;
        for (int b = 0; b < minBands; ++b) sum += img1.bandData[b][i];
        gray1[i] = sum / minBands;
    }
    for (size_t i = 0; i < np2; ++i)
    {
        double sum = 0.0;
        for (int b = 0; b < minBands; ++b) sum += img2.bandData[b][i];
        gray2[i] = sum / minBands;
    }

    auto edges1 = computeEdgeMap(gray1, w1, h1);
    auto edges2 = computeEdgeMap(gray2, w2, h2);

    auto feature1 = gray1;
    auto feature2 = gray2;
    for (size_t i = 0; i < feature1.size(); ++i)
        feature1[i] = 0.5 * gray1[i] + 0.5 * edges1[i];
    for (size_t i = 0; i < feature2.size(); ++i)
        feature2[i] = 0.5 * gray2[i] + 0.5 * edges2[i];

    std::vector<double> working1 = feature1, working2 = feature2;
    int ww1 = w1, hh1 = h1, ww2 = w2, hh2 = h2;

    if (config.downsampleScale < 1.0)
    {
        gaussianPyramidDownsample(working1, ww1, hh1, working1, ww1, hh1);
        gaussianPyramidDownsample(working2, ww2, hh2, working2, ww2, hh2);
    }

    auto kp1 = detectKeypoints(working1, ww1, hh1, config.featureDetector, config);
    auto kp2 = detectKeypoints(working2, ww2, hh2, config.featureDetector, config);

    auto desc1 = computePatchDescriptors(working1, ww1, hh1, kp1, 11);
    auto desc2 = computePatchDescriptors(working2, ww2, hh2, kp2, 11);

    auto matches = matchDescriptors(desc1, desc2, config.matchMethod, 0.75);

    double scaleFactor = 1.0 / config.downsampleScale;
    for (auto& kp : kp1) { kp.x *= scaleFactor; kp.y *= scaleFactor; }
    for (auto& kp : kp2) { kp.x *= scaleFactor; kp.y *= scaleFactor; }

    double ransacThresh = config.ransacThreshold * 1.5;
    Homography H = estimateHomographyRANSAC(kp1, kp2, matches, ransacThresh, config.maxIterations);

    int outW, outH, offsetX, offsetY;
    computeOutputBounds(gray1, w1, h1, gray2, w2, h2, H, outW, outH, offsetX, offsetY);

    result.create(outW, outH, minBands);

    for (int b = 0; b < minBands; ++b)
    {
        std::vector<double> bandOut(static_cast<size_t>(outW) * outH, 0.0);
        blendLinearRamp(bandOut, outW, outH,
                         const_cast<MultiBandImage&>(img1).bandData[b], w1, h1,
                         const_cast<MultiBandImage&>(img2).bandData[b], w2, h2,
                         offsetX, offsetY, H, config.blendWidth);
        result.bandData[b] = bandOut;
    }

    if (elapsedMs) *elapsedMs = timer.elapsed();
    return result;
}

// ============================================================================
// Unified stitch entry
// ============================================================================

MultiBandImage stitchImages(const MultiBandImage& img1, const MultiBandImage& img2,
                             const StitchConfig& config, double* elapsedMs)
{
    if (config.algorithmType == 1)
        return stitchImagesDeep(img1, img2, config, elapsedMs);
    else
        return stitchImagesTraditional(img1, img2, config, elapsedMs);
}

// ============================================================================
// Geo-coordinate based stitching
// ============================================================================

Homography computeHomographyFromGeo(const GeoMetadata& meta1, const GeoMetadata& meta2)
{
    Homography H;
    for (int i = 0; i < 9; ++i) H.H[i] = (i == 0 || i == 4 || i == 8) ? 1.0 : 0.0;

    if (!meta1.valid || !meta2.valid)
        return H;

    const double* t1 = meta1.geoTransform.transform;
    const double* t2 = meta2.geoTransform.transform;

    double pw1 = t1[1], rx1 = t1[2];
    double ry1 = t1[4], ph1 = t1[5];
    double o1x = t1[0], o1y = t1[3];

    double pw2 = t2[1], rx2 = t2[2];
    double ry2 = t2[4], ph2 = t2[5];
    double o2x = t2[0], o2y = t2[3];

    double det2 = pw2 * ph2 - rx2 * ry2;
    if (std::abs(det2) < 1e-15)
    {
        double scaleX = (std::abs(pw2) > 1e-15) ? pw1 / pw2 : 1.0;
        double scaleY = (std::abs(ph2) > 1e-15) ? ph1 / ph2 : 1.0;
        H.H[0] = scaleX;
        H.H[1] = 0.0;
        H.H[2] = (std::abs(pw2) > 1e-15) ? (o1x - o2x) / pw2 : 0.0;
        H.H[3] = 0.0;
        H.H[4] = scaleY;
        H.H[5] = (std::abs(ph2) > 1e-15) ? (o1y - o2y) / ph2 : 0.0;
        H.H[6] = 0.0;
        H.H[7] = 0.0;
        H.H[8] = 1.0;
        return H;
    }

    double dx = o2x - o1x;
    double dy = o2y - o1y;

    H.H[0] = (ph2 * pw1 - rx2 * ry1) / det2;
    H.H[1] = (ph2 * rx1 - rx2 * ph1) / det2;
    H.H[2] = (-ph2 * dx + rx2 * dy) / det2;
    H.H[3] = (-ry2 * pw1 + pw2 * ry1) / det2;
    H.H[4] = (-ry2 * rx1 + pw2 * ph1) / det2;
    H.H[5] = (ry2 * dx - pw2 * dy) / det2;
    H.H[6] = 0.0;
    H.H[7] = 0.0;
    H.H[8] = 1.0;

    return H;
}

bool validateGeoMetadataForStitch(const GeoMetadata& meta1, const GeoMetadata& meta2, std::string& warning)
{
    warning.clear();

    if (!meta1.valid)
    {
        warning = "左/上影像缺少有效的地理坐标元数据";
        return false;
    }

    if (!meta2.valid)
    {
        warning = "右/下影像缺少有效的地理坐标元数据";
        return false;
    }

    if (!meta1.geoTransform.isValid())
    {
        warning = "左/上影像的GeoTransform无效（像素宽度或高度为零）";
        return false;
    }

    if (!meta2.geoTransform.isValid())
    {
        warning = "右/下影像的GeoTransform无效（像素宽度或高度为零）";
        return false;
    }

    if (meta1.epsgCode > 0 && meta2.epsgCode > 0 && meta1.epsgCode != meta2.epsgCode)
    {
        warning = "两幅影像使用不同的坐标参考系统 (EPSG:" +
                  std::to_string(meta1.epsgCode) + " vs EPSG:" +
                  std::to_string(meta2.epsgCode) +
                  ")，拼接可能存在精度损失";
    }

    return true;
}

MultiBandImage stitchImagesGeo(const MultiBandImage& img1, const MultiBandImage& img2,
                                const GeoMetadata& meta1, const GeoMetadata& meta2,
                                const StitchConfig& config, GeoMetadata* outMeta,
                                double* elapsedMs)
{
    QElapsedTimer timer;
    timer.start();

    MultiBandImage result;
    if (!img1.isValid() || !img2.isValid()) return result;
    if (!meta1.valid || !meta2.valid) return result;

    int minBands = std::min(img1.bands, img2.bands);
    int w1 = img1.width, h1 = img1.height;
    int w2 = img2.width, h2 = img2.height;

    Homography H = computeHomographyFromGeo(meta1, meta2);

    std::vector<double> dummy(static_cast<size_t>(w1) * h1, 0.0);
    std::vector<double> dummy2(static_cast<size_t>(w2) * h2, 0.0);
    int outW, outH, offsetX, offsetY;
    computeOutputBounds(dummy, w1, h1, dummy2, w2, h2, H, outW, outH, offsetX, offsetY);

    result.create(outW, outH, minBands);

    for (int b = 0; b < minBands; ++b)
    {
        std::vector<double> bandOut(static_cast<size_t>(outW) * outH, 0.0);
        blendLinearRamp(bandOut, outW, outH,
                         const_cast<MultiBandImage&>(img1).bandData[b], w1, h1,
                         const_cast<MultiBandImage&>(img2).bandData[b], w2, h2,
                         offsetX, offsetY, H, config.blendWidth);
        result.bandData[b] = bandOut;
    }

    if (outMeta)
    {
        const double* t1 = meta1.geoTransform.transform;
        double o1x = t1[0], pw1 = t1[1];
        double o1y = t1[3], ph1 = t1[5];

        outMeta->geoTransform.transform[0] = o1x - offsetX * pw1;
        outMeta->geoTransform.transform[1] = pw1;
        outMeta->geoTransform.transform[2] = 0.0;
        outMeta->geoTransform.transform[3] = o1y - offsetY * ph1;
        outMeta->geoTransform.transform[4] = 0.0;
        outMeta->geoTransform.transform[5] = ph1;
        outMeta->epsgCode = meta1.epsgCode;
        outMeta->projectionWKT = meta1.projectionWKT;
        outMeta->imageWidth = outW;
        outMeta->imageHeight = outH;
        outMeta->valid = true;
    }

    if (elapsedMs) *elapsedMs = timer.elapsed();
    return result;
}
