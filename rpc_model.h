#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <QtCore/QString>

static const int RPC_COEFF_COUNT = 20;

struct RPCCoefficients
{
    double lineNum[RPC_COEFF_COUNT];
    double lineDen[RPC_COEFF_COUNT];
    double sampNum[RPC_COEFF_COUNT];
    double sampDen[RPC_COEFF_COUNT];
};

struct RPCNormalization
{
    double lineOffset;
    double sampOffset;
    double latOffset;
    double longOffset;
    double heightOffset;
    double lineScale;
    double sampScale;
    double latScale;
    double longScale;
    double heightScale;

    double lineNumMin;
    double lineNumMax;
    double lineDenMin;
    double lineDenMax;
    double sampNumMin;
    double sampNumMax;
    double sampDenMin;
    double sampDenMax;
};

struct RPCModel
{
    RPCCoefficients coeffs;
    RPCNormalization norm;
    int errBias;
    int errRand;
    int lineOffset_px;
    int sampOffset_px;
    int imageWidth;
    int imageHeight;
    bool valid;

    RPCModel();

    double evaluatePolynomial(const double* coeffs,
                              double P, double L, double H) const;

    void forward(int pixelLine, int pixelSamp,
                 double& lon, double& lat, double height = 0.0) const;

    void inverse(double lon, double lat, double height,
                 double& pixelLine, double& pixelSamp,
                 int maxIterations = 30, double tolerance = 1e-8) const;

    bool computeGeoBounds(double height, double& minLon, double& maxLon,
                          double& minLat, double& maxLat) const;

    std::string toRPBText() const;

    void setImageDimensions(int w, int h) { imageWidth = w; imageHeight = h; }
    const QString& sourceFilePath() const { return m_sourceFile; }
    void setSourceFilePath(const QString& p) { m_sourceFile = p; }

private:
    QString m_sourceFile;
};

RPCModel parseRPCFile(const QString& filePath);

RPCModel parseRPBFile(const QString& filePath);

RPCModel findAndLoadRPCModel(const QString& imagePath);
