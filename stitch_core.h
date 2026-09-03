#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>
#include <cstdint>

#include <QtGui/QImage>

#include "fusion_core.h"
#include "geo_metadata.h"
#include "rpc_model.h"

struct KeyPoint
{
    double x, y;
    double response;
    std::vector<double> descriptor;
};

struct StitchMatch
{
    int srcIdx;
    int dstIdx;
    double distance;
};

struct Homography
{
    double H[9];
};

struct StitchConfig
{
    int algorithmType;          // 0=traditional, 1=deep, 2=geoCoordinate
    int featureDetector;        // 0=FAST, 1=Harris, 2=SIFT-like
    int matchMethod;            // 0=SSD, 1=NCC
    double ransacThreshold;     // pixel distance threshold
    int maxIterations;          // RANSAC iterations
    int blendMethod;            // 0=average, 1=linear ramp, 2=multi-band
    int blendWidth;             // seam blend width in pixels
    double downsampleScale;     // preprocessing downscale
    bool useGeoCoordinates;     // use geo metadata for direct alignment
    bool enableRPCMode;         // use RPC-based precise alignment
    double rpcHeight;           // reference height for RPC (meters)
    double rpcOutputResolution; // output GSD in degrees

    StitchConfig();
    void setDefaults();
    std::string serialize() const;
    bool deserialize(const std::string& json);
};

struct StitchValidationResult
{
    int samplePointCount;
    std::vector<double> point1Line;
    std::vector<double> point1Samp;
    std::vector<double> point1Lon;
    std::vector<double> point1Lat;
    std::vector<double> point2Line;
    std::vector<double> point2Samp;
    std::vector<double> point2Lon;
    std::vector<double> point2Lat;
    std::vector<double> geoDistanceMeters;
    double meanErrorMeters;
    double maxErrorMeters;
    double rmsErrorMeters;
    bool passed;
    double thresholdMeters;

    StitchValidationResult();
    void clear();
    std::string toReportCSV() const;
    std::string toReportSummary() const;
};

struct StitchIntegrityReport
{
    int emptyPixelCount;
    int totalPixels;
    double emptyRatio;
    int deadColumnCount;
    int edgeMismatchCount;
    bool hasValidData;
    bool passed;
    std::string summary;

    StitchIntegrityReport();
};

struct StitchOutputMetadata
{
    GeoMetadata geoMeta;
    RPCModel rpcModel;
    bool hasRPC;
    double minLon, maxLon, minLat, maxLat;
    int outputWidth, outputHeight;

    StitchOutputMetadata();
};

std::vector<KeyPoint> detectFASTCorners(const std::vector<double>& image, int width, int height,
                                         double threshold = 40.0, int maxCorners = 500);
std::vector<KeyPoint> detectHarrisCorners(const std::vector<double>& image, int width, int height,
                                           double k = 0.04, int maxCorners = 500);
std::vector<KeyPoint> detectSiftLike(const std::vector<double>& image, int width, int height,
                                      int maxKeypoints = 500);

std::vector<KeyPoint> detectKeypoints(const std::vector<double>& image, int width, int height,
                                       int detectorType, const StitchConfig& config);

std::vector<std::vector<double>> computePatchDescriptors(
    const std::vector<double>& image, int width, int height,
    const std::vector<KeyPoint>& keypoints, int patchSize = 9);

std::vector<StitchMatch> matchDescriptors(
    const std::vector<std::vector<double>>& desc1,
    const std::vector<std::vector<double>>& desc2,
    int matchMethod, double ratioThreshold = 0.8);

Homography estimateHomographyRANSAC(
    const std::vector<KeyPoint>& kp1,
    const std::vector<KeyPoint>& kp2,
    const std::vector<StitchMatch>& matches,
    double threshold, int maxIterations);

void applyHomography(const Homography& H, double x, double y, double& ox, double& oy);
void invertHomography(const Homography& H, Homography& Hinv);
void computeOutputBounds(const std::vector<double>& img1, int w1, int h1,
                          const std::vector<double>& img2, int w2, int h2,
                          const Homography& H,
                          int& outW, int& outH, int& offsetX, int& offsetY);

MultiBandImage stitchImagesTraditional(const MultiBandImage& img1, const MultiBandImage& img2,
                                        const StitchConfig& config, double* elapsedMs = nullptr);
MultiBandImage stitchImagesDeep(const MultiBandImage& img1, const MultiBandImage& img2,
                                 const StitchConfig& config, double* elapsedMs = nullptr);

MultiBandImage stitchImages(const MultiBandImage& img1, const MultiBandImage& img2,
                             const StitchConfig& config, double* elapsedMs = nullptr);

Homography computeHomographyFromGeo(const GeoMetadata& meta1, const GeoMetadata& meta2);

bool validateGeoMetadataForStitch(const GeoMetadata& meta1, const GeoMetadata& meta2, std::string& warning);

MultiBandImage stitchImagesGeo(const MultiBandImage& img1, const MultiBandImage& img2,
                                const GeoMetadata& meta1, const GeoMetadata& meta2,
                                const StitchConfig& config, GeoMetadata* outMeta,
                                double* elapsedMs = nullptr);

Homography computeHomographyFromRPC(const RPCModel& rpc1, const RPCModel& rpc2,
                                     int w1, int h1, int w2, int h2,
                                     double height, double outputGSD,
                                     int& outW, int& outH,
                                     double& outMinLon, double& outMaxLon,
                                     double& outMinLat, double& outMaxLat);

bool validateRPCMetadataForStitch(const RPCModel& rpc1, const RPCModel& rpc2, std::string& warning);

MultiBandImage stitchImagesRPC(const MultiBandImage& img1, const MultiBandImage& img2,
                                const RPCModel& rpc1, const RPCModel& rpc2,
                                const StitchConfig& config, StitchOutputMetadata* outMeta,
                                StitchValidationResult* validation,
                                double* elapsedMs = nullptr);

StitchValidationResult validateStitchAccuracy(const MultiBandImage& img1, const MultiBandImage& img2,
                                               const RPCModel& rpc1, const RPCModel& rpc2,
                                               const Homography& H, int outW, int outH,
                                               int offsetX, int offsetY,
                                               int sampleCount = 25, double thresholdMeters = 10.0);

StitchIntegrityReport validateStitchIntegrity(const MultiBandImage& stitched,
                                                int srcW1, int srcH1,
                                                int srcW2, int srcH2,
                                                int offsetX, int offsetY);

void blendLinearRamp(std::vector<double>& output, int outW, int outH,
                      const std::vector<double>& img1, int w1, int h1,
                      const std::vector<double>& img2, int w2, int h2,
                      int offsetX, int offsetY, const Homography& H, int blendWidth);

void gaussianPyramidDownsample(const std::vector<double>& src, int srcW, int srcH,
                               std::vector<double>& dst, int& dstW, int& dstH);

std::vector<double> computeEdgeMap(const std::vector<double>& image, int width, int height);
double computeNCC(const std::vector<double>& patch1, const std::vector<double>& patch2);
