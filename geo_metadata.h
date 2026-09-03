#pragma once

#include <string>
#include <cstdint>
#include <QtCore/QString>

#include "rpc_model.h"

struct GeoTransform {
    double transform[6];

    GeoTransform();
    bool isValid() const;
    void setIdentity(int width, int height);

    double originX() const { return transform[0]; }
    double pixelWidth() const { return transform[1]; }
    double rotationX() const { return transform[2]; }
    double originY() const { return transform[3]; }
    double rotationY() const { return transform[4]; }
    double pixelHeight() const { return transform[5]; }

    double geoX(int pixelX, int pixelY) const
    {
        return transform[0] + pixelX * transform[1] + pixelY * transform[2];
    }
    double geoY(int pixelX, int pixelY) const
    {
        return transform[3] + pixelX * transform[4] + pixelY * transform[5];
    }

    void pixelFromGeo(double geoX, double geoY, double& pixelX, double& pixelY) const;
};

struct GeoMetadata {
    GeoTransform geoTransform;
    int epsgCode;
    std::string projectionWKT;
    bool valid;

    bool hasRPC;
    RPCModel rpcModel;

    GeoMetadata();

    double minX() const;
    double maxX() const { return minX() + pixelWidth() * imageWidth; }
    double minY() const;
    double maxY() const { return minY() + pixelHeight() * imageHeight; }

    double pixelWidth() const { return geoTransform.pixelWidth(); }
    double pixelHeight() const { return geoTransform.pixelHeight(); }

    int imageWidth;
    int imageHeight;
};

GeoMetadata parseGeoTiffTags(const QString& filePath);

GeoMetadata parseWorldFile(const QString& imagePath);

GeoMetadata parsePrjFile(const QString& prjPath);

GeoMetadata loadImageGeoMetadata(const QString& imagePath);

GeoMetadata loadImageGeoMetadataWithRPC(const QString& imagePath);
