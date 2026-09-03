#include "rpc_model.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtCore/QDir>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

// ============================================================================
// RPCModel
// ============================================================================

RPCModel::RPCModel()
    : errBias(0)
    , errRand(0)
    , lineOffset_px(0)
    , sampOffset_px(0)
    , imageWidth(0)
    , imageHeight(0)
    , valid(false)
{
    std::memset(coeffs.lineNum, 0, sizeof(coeffs.lineNum));
    std::memset(coeffs.lineDen, 0, sizeof(coeffs.lineDen));
    std::memset(coeffs.sampNum, 0, sizeof(coeffs.sampNum));
    std::memset(coeffs.sampDen, 0, sizeof(coeffs.sampDen));
}

double RPCModel::evaluatePolynomial(const double* coeffs,
                                     double P, double L, double H) const
{
    double result =
        coeffs[0] +
        coeffs[1] * L + coeffs[2] * P + coeffs[3] * H +
        coeffs[4] * L * P + coeffs[5] * L * H + coeffs[6] * P * H +
        coeffs[7] * L * L + coeffs[8] * P * P + coeffs[9] * H * H +
        coeffs[10] * P * L * H +
        coeffs[11] * L * L * L +
        coeffs[12] * L * P * P + coeffs[13] * L * H * H + coeffs[14] * L * L * P +
        coeffs[15] * P * P * P +
        coeffs[16] * P * H * H + coeffs[17] * L * L * H + coeffs[18] * P * P * H +
        coeffs[19] * H * H * H;
    return result;
}

void RPCModel::forward(int pixelLine, int pixelSamp,
                        double& lon, double& lat, double height) const
{
    double L = (static_cast<double>(pixelLine) - norm.lineOffset) / norm.lineScale;
    double P = (static_cast<double>(pixelSamp) - norm.sampOffset) / norm.sampScale;
    double H = (height - norm.heightOffset) / norm.heightScale;

    double lineNum = evaluatePolynomial(coeffs.lineNum, P, L, H);
    double lineDen = evaluatePolynomial(coeffs.lineDen, P, L, H);
    double sampNum = evaluatePolynomial(coeffs.sampNum, P, L, H);
    double sampDen = evaluatePolynomial(coeffs.sampDen, P, L, H);

    if (std::abs(lineDen) < 1e-15) lineDen = 1e-15;
    if (std::abs(sampDen) < 1e-15) sampDen = 1e-15;

    lat = lineNum / lineDen * norm.latScale + norm.latOffset;
    lon = sampNum / sampDen * norm.longScale + norm.longOffset;
}

void RPCModel::inverse(double lon, double lat, double height,
                        double& pixelLine, double& pixelSamp,
                        int maxIterations, double tolerance) const
{
    double Ln = 0.0, Pn = 0.0;

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        double lineNum = evaluatePolynomial(coeffs.lineNum, Pn, Ln, (height - norm.heightOffset) / norm.heightScale);
        double lineDen = evaluatePolynomial(coeffs.lineDen, Pn, Ln, (height - norm.heightOffset) / norm.heightScale);
        double sampNum = evaluatePolynomial(coeffs.sampNum, Pn, Ln, (height - norm.heightOffset) / norm.heightScale);
        double sampDen = evaluatePolynomial(coeffs.sampDen, Pn, Ln, (height - norm.heightOffset) / norm.heightScale);

        if (std::abs(lineDen) < 1e-15) lineDen = 1e-15;
        if (std::abs(sampDen) < 1e-15) sampDen = 1e-15;

        double F_line = lineNum / lineDen * norm.latScale + norm.latOffset - lat;
        double F_samp = sampNum / sampDen * norm.longScale + norm.longOffset - lon;

        if (std::abs(F_line) < tolerance && std::abs(F_samp) < tolerance)
        {
            pixelLine = Ln * norm.lineScale + norm.lineOffset;
            pixelSamp = Pn * norm.sampScale + norm.sampOffset;
            return;
        }

        double eps = 1e-4;
        double Ln1 = Ln + eps;
        double lineNum1 = evaluatePolynomial(coeffs.lineNum, Pn, Ln1, (height - norm.heightOffset) / norm.heightScale);
        double lineDen1 = evaluatePolynomial(coeffs.lineDen, Pn, Ln1, (height - norm.heightOffset) / norm.heightScale);
        double sampNum1 = evaluatePolynomial(coeffs.sampNum, Pn, Ln1, (height - norm.heightOffset) / norm.heightScale);
        double sampDen1 = evaluatePolynomial(coeffs.sampDen, Pn, Ln1, (height - norm.heightOffset) / norm.heightScale);

        double dF_line_dL = (lineNum1 / lineDen1 - lineNum / lineDen) / eps * norm.latScale;
        double dF_samp_dL = (sampNum1 / sampDen1 - sampNum / sampDen) / eps * norm.longScale;

        double Pn1 = Pn + eps;
        double lineNum2 = evaluatePolynomial(coeffs.lineNum, Pn1, Ln, (height - norm.heightOffset) / norm.heightScale);
        double lineDen2 = evaluatePolynomial(coeffs.lineDen, Pn1, Ln, (height - norm.heightOffset) / norm.heightScale);
        double sampNum2 = evaluatePolynomial(coeffs.sampNum, Pn1, Ln, (height - norm.heightOffset) / norm.heightScale);
        double sampDen2 = evaluatePolynomial(coeffs.sampDen, Pn1, Ln, (height - norm.heightOffset) / norm.heightScale);

        double dF_line_dP = (lineNum2 / lineDen2 - lineNum / lineDen) / eps * norm.latScale;
        double dF_samp_dP = (sampNum2 / sampDen2 - sampNum / sampDen) / eps * norm.longScale;

        double det = dF_line_dL * dF_samp_dP - dF_line_dP * dF_samp_dL;
        if (std::abs(det) < 1e-15)
        {
            Ln -= 0.1 * F_line;
            Pn -= 0.1 * F_samp;
        }
        else
        {
            double dL = (-dF_samp_dP * F_line + dF_line_dP * F_samp) / det;
            double dP = ( dF_samp_dL * F_line - dF_line_dL * F_samp) / det;
            Ln -= dL;
            Pn -= dP;
        }
    }

    pixelLine = Ln * norm.lineScale + norm.lineOffset;
    pixelSamp = Pn * norm.sampScale + norm.sampOffset;
}

bool RPCModel::computeGeoBounds(double height, double& minLon, double& maxLon,
                                 double& minLat, double& maxLat) const
{
    if (!valid) return false;

    minLon = 1e30; maxLon = -1e30;
    minLat = 1e30; maxLat = -1e30;

    const int stepsX = 8;
    const int stepsY = 8;

    for (int iy = 0; iy <= stepsY; ++iy)
    {
        for (int ix = 0; ix <= stepsX; ++ix)
        {
            int col = (ix * imageWidth) / stepsX;
            int row = (iy * imageHeight) / stepsY;
            if (col >= imageWidth) col = imageWidth - 1;
            if (row >= imageHeight) row = imageHeight - 1;

            double lon, lat;
            forward(row, col, lon, lat, height);

            if (lon < minLon) minLon = lon;
            if (lon > maxLon) maxLon = lon;
            if (lat < minLat) minLat = lat;
            if (lat > maxLat) maxLat = lat;
        }
    }

    return true;
}

std::string RPCModel::toRPBText() const
{
    std::ostringstream oss;
    oss << std::setprecision(16);

    oss << "LINE_OFF: " << std::showpos << norm.lineOffset << " pixels" << std::noshowpos << std::endl;
    oss << "SAMP_OFF: " << std::showpos << norm.sampOffset << " pixels" << std::noshowpos << std::endl;
    oss << "LAT_OFF: " << std::showpos << norm.latOffset << " degrees" << std::noshowpos << std::endl;
    oss << "LONG_OFF: " << std::showpos << norm.longOffset << " degrees" << std::noshowpos << std::endl;
    oss << "HEIGHT_OFF: " << std::showpos << norm.heightOffset << " meters" << std::noshowpos << std::endl;
    oss << "LINE_SCALE: " << std::showpos << norm.lineScale << " pixels" << std::noshowpos << std::endl;
    oss << "SAMP_SCALE: " << std::showpos << norm.sampScale << " pixels" << std::noshowpos << std::endl;
    oss << "LAT_SCALE: " << std::showpos << norm.latScale << " degrees" << std::noshowpos << std::endl;
    oss << "LONG_SCALE: " << std::showpos << norm.longScale << " degrees" << std::noshowpos << std::endl;
    oss << "HEIGHT_SCALE: " << std::showpos << norm.heightScale << " meters" << std::noshowpos << std::endl;

    for (int i = 1; i <= RPC_COEFF_COUNT; ++i)
        oss << "LINE_NUM_COEFF_" << i << ": " << std::showpos << coeffs.lineNum[i - 1] << std::noshowpos << std::endl;
    for (int i = 1; i <= RPC_COEFF_COUNT; ++i)
        oss << "LINE_DEN_COEFF_" << i << ": " << std::showpos << coeffs.lineDen[i - 1] << std::noshowpos << std::endl;
    for (int i = 1; i <= RPC_COEFF_COUNT; ++i)
        oss << "SAMP_NUM_COEFF_" << i << ": " << std::showpos << coeffs.sampNum[i - 1] << std::noshowpos << std::endl;
    for (int i = 1; i <= RPC_COEFF_COUNT; ++i)
        oss << "SAMP_DEN_COEFF_" << i << ": " << std::showpos << coeffs.sampDen[i - 1] << std::noshowpos << std::endl;

    return oss.str();
}

// ============================================================================
// RPC file parsing
// ============================================================================

static QString findRPBFile(const QString& imagePath)
{
    QFileInfo fi(imagePath);
    QString dir = fi.absolutePath();
    QString base = fi.completeBaseName();

    QStringList candidates;
    candidates << dir + "/" + base + ".RPB";
    candidates << dir + "/" + base + ".rpb";
    candidates << dir + "/" + base + ".RPC";
    candidates << dir + "/" + base + ".rpc";
    candidates << dir + "/" + base + "_RPC.TXT";
    candidates << dir + "/" + base + "_rpc.txt";
    candidates << dir + "/" + base + "_RPC.txt";
    candidates << dir + "/RPC_" + base + ".TXT";
    candidates << dir + "/RPC_" + base + ".txt";
    candidates << dir + "/rpc_" + base + ".txt";
    candidates << dir + "/" + base + "_metadata.txt";
    candidates << dir + "/" + base + ".rpc.txt";
    candidates << dir + "/" + base + ".RPC.txt";
    candidates << dir + "/" + base + "_rpc.TXT";
    candidates << dir + "/" + base + ".txt";

    QDir rpcDir(dir + "/rpc");
    if (rpcDir.exists())
    {
        candidates << dir + "/rpc/" + base + ".RPB";
        candidates << dir + "/rpc/" + base + ".rpb";
        candidates << dir + "/rpc/" + base + ".rpc";
        candidates << dir + "/rpc/" + base + ".txt";
        candidates << dir + "/rpc/" + base + "_rpc.txt";
    }

    for (const QString& c : candidates)
    {
        if (QFileInfo::exists(c))
            return c;
    }

    QDir dirObj(dir);
    QStringList txtFiles = dirObj.entryList({"*rpc*.txt", "*RPC*.txt", "*RPC*.TXT", "*RPB*"}, QDir::Files);
    for (const QString& f : txtFiles)
        return dir + "/" + f;

    return QString();
}

static void trimLine(QString& line)
{
    line = line.trimmed();
    if (line.endsWith(';'))
        line.chop(1);
    line = line.trimmed();
}

RPCModel parseRPCFile(const QString& filePath)
{
    RPCModel model;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return model;

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    content.replace("\r\n", "\n");
    content.replace('\r', '\n');

    double* coeffArrays[4] = {
        model.coeffs.lineNum,
        model.coeffs.lineDen,
        model.coeffs.sampNum,
        model.coeffs.sampDen
    };

    static const struct { const char* name; int arrIdx; } coeffMap[] = {
        {"LINE_NUM_COEFF_", 0}, {"LINENUMCOEF_", 0}, {"LINENUMCOEFF_", 0}, {"LINENUMCOEF", 0},
        {"LINE_DEN_COEFF_", 1}, {"LINEDENCOEF_", 1}, {"LINEDENCOEFF_", 1}, {"LINEDENCOEF", 1},
        {"SAMP_NUM_COEFF_", 2}, {"SAMPNUMCOEF_", 2}, {"SAMPNUMCOEFF_", 2}, {"SAMPNUMCOEF", 2},
        {"SAMP_DEN_COEFF_", 3}, {"SAMPDENCOEF_", 3}, {"SAMPDENCOEFF_", 3}, {"SAMPDENCOEF", 3},
    };
    static const int coeffMapSize = sizeof(coeffMap) / sizeof(coeffMap[0]);

    static const struct { const char* name; int* target; bool isDouble; } normMap[] = {
        {"LINE_OFF",       nullptr,    true},
        {"LINEOFFSET",     nullptr,    true},
        {"SAMP_OFF",       nullptr,    true},
        {"SAMPOFFSET",     nullptr,    true},
        {"LAT_OFF",        nullptr,    true},
        {"LATOFFSET",      nullptr,    true},
        {"LONG_OFF",       nullptr,    true},
        {"LONGOFFSET",     nullptr,    true},
        {"LON_OFF",        nullptr,    true},
        {"HEIGHT_OFF",     nullptr,    true},
        {"HEIGHTOFFSET",   nullptr,    true},
        {"LINE_SCALE",     nullptr,    true},
        {"LINESCALE",      nullptr,    true},
        {"SAMP_SCALE",     nullptr,    true},
        {"SAMPSCALE",      nullptr,    true},
        {"LAT_SCALE",      nullptr,    true},
        {"LATSCALE",       nullptr,    true},
        {"LONG_SCALE",     nullptr,    true},
        {"LONGSCALE",      nullptr,    true},
        {"LON_SCALE",      nullptr,    true},
        {"HEIGHT_SCALE",   nullptr,    true},
        {"HEIGHTSCALE",    nullptr,    true},
        {"ERRBIAS",        nullptr,    false},
        {"ERR_BIAS",       nullptr,    false},
        {"ERRRAND",        nullptr,    false},
        {"ERR_RAND",       nullptr,    false},
        {"IMAGEWIDTH",     nullptr,    false},
        {"IMAGEHEIGHT",    nullptr,    false},
        {"NROWS",          nullptr,    false},
        {"NCOLS",          nullptr,    false},
        {"NLINES",         nullptr,    false},
    };
    static const int normMapSize = sizeof(normMap) / sizeof(normMap[0]);

    int lineNum = 0;
    QStringList lines = content.split('\n');

    auto extractKeyValue = [](const QString& rawLine, QString& key, QString& val) -> bool {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("//"))
            return false;

        int brkOpen = line.indexOf('[');
        int brkClose = line.indexOf(']');
        int colonPos = line.indexOf(':');
        int eqPos = line.indexOf('=');

        int sepPos = -1;
        if (colonPos >= 0 && eqPos >= 0)
            sepPos = std::min(colonPos, eqPos);
        else if (colonPos >= 0)
            sepPos = colonPos;
        else if (eqPos >= 0)
            sepPos = eqPos;
        else
            return false;

        key = line.left(sepPos).trimmed().toUpper();
        val = line.mid(sepPos + 1).trimmed();

        if (val.endsWith(';'))
            val.chop(1);

        if (brkOpen >= 0 && brkClose > brkOpen && brkOpen < sepPos)
        {
            QString idxStr = line.mid(brkOpen + 1, brkClose - brkOpen - 1).trimmed();
            key = key.left(brkOpen).trimmed().toUpper() + "_" + idxStr;
        }

        key.replace(' ', "_");
        key.replace('\t', "_");
        while (key.contains("__"))
            key.replace("__", "_");

        val = val.trimmed();

        static QRegExp unitRe("(\\s+(pixels|degrees|meters|meter|pixel|deg|m)\\s*)?$", Qt::CaseInsensitive);
        unitRe.setMinimal(true);
        val.remove(unitRe);

        return true;
    };

    for (const QString& rawLine : lines)
    {
        lineNum++;
        QString key, val;
        if (!extractKeyValue(rawLine, key, val))
            continue;

        bool ok = false;
        double dv = val.toDouble(&ok);

        if (key == "LINE_OFF" || key == "LINEOFFSET")      { model.norm.lineOffset   = dv; model.valid = true; }
        else if (key == "SAMP_OFF" || key == "SAMPOFFSET")   model.norm.sampOffset   = dv;
        else if (key == "LAT_OFF"  || key == "LATOFFSET")    model.norm.latOffset    = dv;
        else if (key == "LONG_OFF" || key == "LONGOFFSET" || key == "LON_OFF")
                                                              model.norm.longOffset   = dv;
        else if (key == "HEIGHT_OFF" || key == "HEIGHTOFFSET" || key == "HEIGHT_OFF")
                                                              model.norm.heightOffset = dv;
        else if (key == "LINE_SCALE"   || key == "LINESCALE")  model.norm.lineScale     = dv;
        else if (key == "SAMP_SCALE"   || key == "SAMPSCALE")  model.norm.sampScale     = dv;
        else if (key == "LAT_SCALE"    || key == "LATSCALE")   model.norm.latScale      = dv;
        else if (key == "LONG_SCALE"   || key == "LONGSCALE" || key == "LON_SCALE")
                                                              model.norm.longScale     = dv;
        else if (key == "HEIGHT_SCALE" || key == "HEIGHTSCALE" || key == "HEIGHT_SCALE")
                                                              model.norm.heightScale   = dv;
        else if (key == "ERRBIAS"   || key == "ERR_BIAS")   model.errBias       = static_cast<int>(dv);
        else if (key == "ERRRAND"   || key == "ERR_RAND")   model.errRand       = static_cast<int>(dv);
        else if (key == "IMAGEWIDTH"   || key == "NCOLS")   model.imageWidth    = static_cast<int>(dv);
        else if (key == "IMAGEHEIGHT"  || key == "NROWS" || key == "NLINES")
                                                              model.imageHeight   = static_cast<int>(dv);

        else if (ok)
        {
            for (int m = 0; m < coeffMapSize; ++m)
            {
                const char* prefix = coeffMap[m].name;
                int prefixLen = static_cast<int>(std::strlen(prefix));
                if (key.length() <= prefixLen)
                    continue;

                QString keyPrefix = key.left(prefixLen);
                if (keyPrefix != QString(prefix) && keyPrefix != QString(prefix).chopped(1))
                    continue;

                QString rest = key.mid(prefixLen);
                if (rest.startsWith('_'))
                    rest = rest.mid(1);

                bool numOk = false;
                int idx = rest.toInt(&numOk) - 1;
                if (!numOk)
                {
                    QString cleaned;
                    for (QChar ch : rest)
                    {
                        if (ch.isDigit()) cleaned += ch;
                        else break;
                    }
                    if (!cleaned.isEmpty())
                        idx = cleaned.toInt(&numOk) - 1;
                }
                if (numOk && idx >= 0 && idx < RPC_COEFF_COUNT)
                {
                    coeffArrays[coeffMap[m].arrIdx][idx] = dv;
                }
                break;
            }
        }
    }

    if (model.norm.lineScale  == 0.0) model.norm.lineScale   = 1.0;
    if (model.norm.sampScale  == 0.0) model.norm.sampScale   = 1.0;
    if (model.norm.latScale   == 0.0) model.norm.latScale    = 1.0;
    if (model.norm.longScale  == 0.0) model.norm.longScale   = 1.0;
    if (model.norm.heightScale == 0.0) model.norm.heightScale = 500.0;

    if (model.imageWidth  <= 0) model.imageWidth  = static_cast<int>(std::round(model.norm.sampScale  * 2.0));
    if (model.imageHeight <= 0) model.imageHeight = static_cast<int>(std::round(model.norm.lineScale  * 2.0));

    return model;
}

RPCModel parseRPBFile(const QString& filePath)
{
    return parseRPCFile(filePath);
}

RPCModel findAndLoadRPCModel(const QString& imagePath)
{
    QString extPath = findRPBFile(imagePath);
    if (!extPath.isEmpty())
    {
        RPCModel model = parseRPCFile(extPath);
        if (model.valid)
        {
            model.setSourceFilePath(extPath);
            return model;
        }
    }

    return RPCModel();
}
