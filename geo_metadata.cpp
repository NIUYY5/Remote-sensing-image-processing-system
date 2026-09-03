#include "geo_metadata.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegExp>
#include <QtCore/QTextStream>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <cstring>
#include <cmath>

// ============================================================================
// GeoTransform
// ============================================================================

GeoTransform::GeoTransform()
{
    for (int i = 0; i < 6; ++i)
        transform[i] = 0.0;
}

bool GeoTransform::isValid() const
{
    return std::abs(transform[1]) > 1e-15 && std::abs(transform[5]) > 1e-15;
}

void GeoTransform::setIdentity(int width, int height)
{
    Q_UNUSED(width);
    Q_UNUSED(height);
    transform[0] = 0.0;
    transform[1] = 1.0;
    transform[2] = 0.0;
    transform[3] = 0.0;
    transform[4] = 0.0;
    transform[5] = 1.0;
}

void GeoTransform::pixelFromGeo(double geoX, double geoY, double& pixelX, double& pixelY) const
{
    double det = transform[1] * transform[5] - transform[2] * transform[4];
    if (std::abs(det) < 1e-15)
    {
        pixelX = geoX;
        pixelY = geoY;
        return;
    }
    double dx = geoX - transform[0];
    double dy = geoY - transform[3];
    pixelX = ( transform[5] * dx - transform[2] * dy) / det;
    pixelY = (-transform[4] * dx + transform[1] * dy) / det;
}

// ============================================================================
// GeoMetadata
// ============================================================================

GeoMetadata::GeoMetadata()
    : epsgCode(0)
    , valid(false)
    , hasRPC(false)
    , imageWidth(0)
    , imageHeight(0)
{
}

double GeoMetadata::minX() const
{
    return geoTransform.originX();
}

double GeoMetadata::minY() const
{
    return geoTransform.originY() + pixelHeight() * imageHeight;
}

// ============================================================================
// TIFF binary parsing helpers
// ============================================================================

enum TiffTagId
{
    TIFFTAG_IMAGEWIDTH = 256,
    TIFFTAG_IMAGELENGTH = 257,
    TIFFTAG_MODELPIXELSCALE = 33550,
    TIFFTAG_MODELTIEPOINT = 33922,
    TIFFTAG_GEOKEYDIRECTORY = 34735,
    TIFFTAG_GEODOUBLEPARAMS = 34736,
    TIFFTAG_GEOASCIIPARAMS = 34737
};

enum GeoKeyId
{
    GTModelTypeGeoKey = 1024,
    GTRasterTypeGeoKey = 1025,
    GTCitationGeoKey = 1026,
    GeographicTypeGeoKey = 2048,
    GeogCitationGeoKey = 2049,
    GeogGeodeticDatumGeoKey = 2050,
    GeogPrimeMeridianGeoKey = 2051,
    GeogAngularUnitsGeoKey = 2054,
    GeogEllipsoidGeoKey = 2056,
    GeogAzimuthUnitsGeoKey = 2060,
    ProjectedCSTypeGeoKey = 3072,
    ProjectionGeoKey = 3074,
    ProjCoordTransGeoKey = 3075,
    ProjLinearUnitsGeoKey = 3076,
    ProjStdParallel1GeoKey = 3078,
    ProjStdParallel2GeoKey = 3079,
    ProjNatOriginLongGeoKey = 3080,
    ProjNatOriginLatGeoKey = 3081,
    ProjFalseEastingGeoKey = 3082,
    ProjFalseNorthingGeoKey = 3083,
    ProjFalseOriginLongGeoKey = 3084,
    ProjFalseOriginLatGeoKey = 3085,
    ProjCenterLongGeoKey = 3090,
    VerticalCSTypeGeoKey = 4096
};

static uint16_t readUint16LE(const unsigned char* buf)
{
    return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
}

static uint16_t readUint16BE(const unsigned char* buf)
{
    return (static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]);
}

static uint32_t readUint32LE(const unsigned char* buf)
{
    return static_cast<uint32_t>(buf[0])
        | (static_cast<uint32_t>(buf[1]) << 8)
        | (static_cast<uint32_t>(buf[2]) << 16)
        | (static_cast<uint32_t>(buf[3]) << 24);
}

static uint32_t readUint32BE(const unsigned char* buf)
{
    return (static_cast<uint32_t>(buf[0]) << 24)
        | (static_cast<uint32_t>(buf[1]) << 16)
        | (static_cast<uint32_t>(buf[2]) << 8)
        | static_cast<uint32_t>(buf[3]);
}

static uint64_t readUint64LE(const unsigned char* buf)
{
    uint64_t lo = readUint32LE(buf);
    uint64_t hi = readUint32LE(buf + 4);
    return lo | (hi << 32);
}

static uint64_t readUint64BE(const unsigned char* buf)
{
    uint64_t hi = readUint32BE(buf);
    uint64_t lo = readUint32BE(buf + 4);
    return (hi << 32) | lo;
}

static double readDoubleLE(const unsigned char* buf)
{
    uint64_t bits = readUint64LE(buf);
    double result;
    std::memcpy(&result, &bits, sizeof(double));
    return result;
}

static double readDoubleBE(const unsigned char* buf)
{
    uint64_t bits = readUint64BE(buf);
    double result;
    std::memcpy(&result, &bits, sizeof(double));
    return result;
}

struct TiffReader
{
    std::vector<unsigned char> data;
    bool bigEndian;
    size_t pos;

    TiffReader() : bigEndian(false), pos(0) {}

    bool load(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;

        qint64 size = file.size();
        if (size < 8)
            return false;

        data.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), size);
        file.close();
        pos = 0;
        return true;
    }

    bool readHeader()
    {
        if (data.size() < 8)
            return false;

        unsigned char b0 = data[0];
        unsigned char b1 = data[1];

        if (b0 == 0x49 && b1 == 0x49)
            bigEndian = false;
        else if (b0 == 0x4D && b1 == 0x4D)
            bigEndian = true;
        else
            return false;

        uint16_t magic = readUint16At(2);
        if (magic != 42)
            return false;

        uint32_t ifdOffset = readUint32At(4);
        pos = ifdOffset;
        return true;
    }

    uint16_t readUint16At(size_t offset) const
    {
        if (offset + 2 > data.size()) return 0;
        return bigEndian ? readUint16BE(&data[offset]) : readUint16LE(&data[offset]);
    }

    uint32_t readUint32At(size_t offset) const
    {
        if (offset + 4 > data.size()) return 0;
        return bigEndian ? readUint32BE(&data[offset]) : readUint32LE(&data[offset]);
    }

    double readDoubleAt(size_t offset) const
    {
        if (offset + 8 > data.size()) return 0.0;
        return bigEndian ? readDoubleBE(&data[offset]) : readDoubleLE(&data[offset]);
    }

    struct TiffTagValue
    {
        uint16_t tag;
        uint16_t type;
        uint32_t count;
        size_t valueOffset;
        uint32_t inlineValue;
    };

    bool readNextIfd(std::vector<TiffTagValue>& tags)
    {
        tags.clear();

        if (pos + 2 > data.size())
            return false;

        uint16_t numEntries = readUint16At(pos);
        pos += 2;

        for (uint16_t i = 0; i < numEntries; ++i)
        {
            if (pos + 12 > data.size())
                return false;

            TiffTagValue tv;
            tv.tag = readUint16At(pos);
            tv.type = readUint16At(pos + 2);
            tv.count = readUint32At(pos + 4);

            uint32_t byteCount = typeSize(tv.type) * tv.count;
            if (byteCount <= 4)
            {
                tv.valueOffset = pos + 8;
                tv.inlineValue = readUint32At(pos + 8);
            }
            else
            {
                tv.valueOffset = readUint32At(pos + 8);
                tv.inlineValue = 0;
            }

            tags.push_back(tv);
            pos += 12;
        }

        return true;
    }

    bool readModelPixelScale(const TiffTagValue& tag, GeoTransform& gt)
    {
        size_t off = tag.valueOffset;

        if (off + 24 > data.size())
            return false;

        gt.transform[0] = 0.0;
        gt.transform[1] = readDoubleAt(off);
        gt.transform[2] = 0.0;
        gt.transform[3] = 0.0;
        gt.transform[4] = 0.0;
        gt.transform[5] = -readDoubleAt(off + 8);

        return true;
    }

    bool readModelTiepoint(const TiffTagValue& tag, GeoTransform& gt)
    {
        size_t off = tag.valueOffset;

        if (off + 48 > data.size())
            return false;

        double tieI = readDoubleAt(off);
        double tieJ = readDoubleAt(off + 8);
        double tieX = readDoubleAt(off + 24);
        double tieY = readDoubleAt(off + 32);

        gt.transform[0] = tieX - tieI * gt.transform[1];
        gt.transform[3] = tieY - tieJ * gt.transform[5];

        return true;
    }

    int readGeoKeys(size_t offset)
    {
        if (offset + 8 > data.size())
            return 0;

        uint16_t numKeys = readUint16At(offset + 6);
        int epsg = 0;

        for (uint16_t i = 0; i < numKeys && (offset + 8 + i * 8 + 8) <= data.size(); ++i)
        {
            size_t koff = offset + 8 + i * 8;
            uint16_t keyId = readUint16At(koff);
            uint16_t tiffTagLoc = readUint16At(koff + 2);
            uint16_t valueOffset = readUint16At(koff + 6);

            if (keyId == ProjectedCSTypeGeoKey || keyId == GeographicTypeGeoKey)
            {
                int code = static_cast<int>(valueOffset);
                if (code > 0)
                    epsg = code;
            }
        }

        return epsg;
    }

private:
    static uint32_t typeSize(uint16_t type)
    {
        switch (type)
        {
        case 1:  return 1;
        case 2:  return 1;
        case 3:  return 2;
        case 4:  return 4;
        case 5:  return 8;
        case 6:  return 1;
        case 7:  return 1;
        case 8:  return 2;
        case 9:  return 4;
        case 10: return 8;
        case 11: return 4;
        case 12: return 8;
        default: return 1;
        }
    }
};

// ============================================================================
// parseGeoTiffTags
// ============================================================================

GeoMetadata parseGeoTiffTags(const QString& filePath)
{
    GeoMetadata meta;

    TiffReader reader;
    if (!reader.load(filePath))
        return meta;

    if (!reader.readHeader())
        return meta;

    std::vector<TiffReader::TiffTagValue> tags;
    if (!reader.readNextIfd(tags))
        return meta;

    bool hasPixelScale = false;

    for (const auto& tag : tags)
    {
        switch (tag.tag)
        {
        case TIFFTAG_IMAGEWIDTH:
            meta.imageWidth = static_cast<int>(tag.inlineValue);
            break;
        case TIFFTAG_IMAGELENGTH:
            meta.imageHeight = static_cast<int>(tag.inlineValue);
            break;
        case TIFFTAG_MODELPIXELSCALE:
            hasPixelScale = reader.readModelPixelScale(tag, meta.geoTransform);
            break;
        case TIFFTAG_GEOKEYDIRECTORY:
        {
            size_t gkOffset = tag.valueOffset;
            meta.epsgCode = reader.readGeoKeys(gkOffset);
            break;
        }
        default:
            break;
        }
    }

    if (hasPixelScale)
    {
        for (const auto& tag : tags)
        {
            if (tag.tag == TIFFTAG_MODELTIEPOINT)
            {
                reader.readModelTiepoint(tag, meta.geoTransform);
                break;
            }
        }
    }

    meta.valid = meta.geoTransform.isValid() && meta.imageWidth > 0 && meta.imageHeight > 0;
    return meta;
}

// ============================================================================
// World file parsing
// ============================================================================

static QString worldFileExtension(const QString& imagePath)
{
    QFileInfo fi(imagePath);
    QString ext = fi.suffix().toLower();
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();

    if (ext == "tif" || ext == "tiff")
        return base + ".tfw";
    if (ext == "jpg" || ext == "jpeg")
        return base + ".jgw";
    if (ext == "png")
        return base + ".pgw";
    if (ext == "bmp")
        return base + ".bpw";

    return base + ".tfw";
}

GeoMetadata parseWorldFile(const QString& imagePath)
{
    GeoMetadata meta;

    QString wfPath = worldFileExtension(imagePath);
    if (!QFileInfo::exists(wfPath))
        return meta;

    QFile file(wfPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return meta;

    QTextStream stream(&file);
    QStringList lines;
    while (!stream.atEnd())
        lines << stream.readLine().trimmed();

    file.close();

    if (lines.size() < 6)
        return meta;

    bool ok = true;
    double a = lines[0].toDouble(&ok);
    double d = lines[1].toDouble(&ok);
    double b = lines[2].toDouble(&ok);
    double e = lines[3].toDouble(&ok);
    double c = lines[4].toDouble(&ok);
    double f = lines[5].toDouble(&ok);

    if (!ok)
        return meta;

    meta.geoTransform.transform[0] = c - a * 0.5;
    meta.geoTransform.transform[1] = a;
    meta.geoTransform.transform[2] = b;
    meta.geoTransform.transform[3] = f - e * 0.5;
    meta.geoTransform.transform[4] = d;
    meta.geoTransform.transform[5] = e;

    meta.valid = meta.geoTransform.isValid();
    return meta;
}

// ============================================================================
// PRJ file parsing
// ============================================================================

static QString findPrjFile(const QString& imagePath)
{
    QFileInfo fi(imagePath);
    QString dir = fi.absolutePath();
    QString base = fi.completeBaseName();

    QString prjPath = dir + "/" + base + ".prj";
    if (QFileInfo::exists(prjPath))
        return prjPath;

    return QString();
}

GeoMetadata parsePrjFile(const QString& prjPath)
{
    GeoMetadata meta;

    QFile file(prjPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return meta;

    QTextStream stream(&file);
    QString wkt = stream.readAll();
    file.close();

    meta.projectionWKT = wkt.toStdString();

    QRegExp epsgRegex("AUTHORITY\\[\"EPSG\",\"(\\d+)\"\\]", Qt::CaseInsensitive);
    if (epsgRegex.indexIn(wkt) != -1)
    {
        meta.epsgCode = epsgRegex.cap(1).toInt();
    }
    else
    {
        QRegExp idRegex("ID\\[\"EPSG\",(\\d+)\\]", Qt::CaseInsensitive);
        if (idRegex.indexIn(wkt) != -1)
        {
            meta.epsgCode = idRegex.cap(1).toInt();
        }
    }

    return meta;
}

// ============================================================================
// loadImageGeoMetadata - main entry point
// ============================================================================

GeoMetadata loadImageGeoMetadata(const QString& imagePath)
{
    GeoMetadata meta = parseGeoTiffTags(imagePath);

    if (meta.valid)
    {
        QString prjPath = findPrjFile(imagePath);
        if (!prjPath.isEmpty())
        {
            GeoMetadata prjMeta = parsePrjFile(prjPath);
            if (prjMeta.epsgCode > 0)
            {
                meta.epsgCode = prjMeta.epsgCode;
                meta.projectionWKT = prjMeta.projectionWKT;
            }
        }
        return meta;
    }

    meta = parseWorldFile(imagePath);
    if (meta.valid)
    {
        QString prjPath = findPrjFile(imagePath);
        if (!prjPath.isEmpty())
        {
            GeoMetadata prjMeta = parsePrjFile(prjPath);
            if (prjMeta.epsgCode > 0)
            {
                meta.epsgCode = prjMeta.epsgCode;
                meta.projectionWKT = prjMeta.projectionWKT;
            }
        }

        QImageReader reader(imagePath);
        if (reader.canRead())
        {
            QSize sz = reader.size();
            meta.imageWidth = sz.width();
            meta.imageHeight = sz.height();
        }
        else
        {
            QImage img(imagePath);
            if (!img.isNull())
            {
                meta.imageWidth = img.width();
                meta.imageHeight = img.height();
            }
        }

        return meta;
    }

    return meta;
}

GeoMetadata loadImageGeoMetadataWithRPC(const QString& imagePath)
{
    GeoMetadata meta = loadImageGeoMetadata(imagePath);

    RPCModel rpc = findAndLoadRPCModel(imagePath);
    if (rpc.valid)
    {
        meta.hasRPC = true;
        meta.rpcModel = rpc;

        if (rpc.imageWidth > 0)  meta.imageWidth  = rpc.imageWidth;
        if (rpc.imageHeight > 0) meta.imageHeight = rpc.imageHeight;
    }

    return meta;
}
