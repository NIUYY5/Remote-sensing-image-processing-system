// ============================================================================
// 文件: imageview.cpp
// 功能: 遥感影像显示控件 — 支持缩放、平移、刺点、控制点标记
// 基于 GDAL 读取各种格式的遥感影像，支持地理参考信息显示
// ============================================================================

#include "imageview.h"

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QtMath>
#include <QDebug>
#include <QApplication>
#include <algorithm>

#include "gdal_priv.h"
#include "cpl_conv.h"
#include "ogr_srs_api.h"

// ============================================================================
// 构造函数：初始化影像显示控件的所有成员变量
// ============================================================================
ImageView::ImageView(QWidget* parent)
    : QWidget(parent)
    , m_scale(1.0)           // 默认缩放比为 1.0（原尺寸显示）
    , m_offset(0, 0)         // 初始偏移量为零，影像左上角对齐窗口左上角
    , m_isPanning(false)     // 初始不处于拖拽平移状态
    , m_pickMode(Normal)     // 默认处于浏览模式（非刺点模式）
    , m_hasPickPoint(false)  // 尚未设置刺点
    , m_crosshairColor(255, 0, 0)  // 十字丝颜色：红色
    , m_markerColor(0, 0, 255)     // 控制点标记颜色：蓝色
    , m_hasGeoRef(false)     // 尚未加载影像，无地理参考信息
{
    // 初始化六参数仿射变换系数为零
    std::fill(std::begin(m_geoTransform), std::end(m_geoTransform), 0.0);
    setMouseTracking(true);                // 开启鼠标追踪，无需按下按键即可接收 mouseMoveEvent
    setFocusPolicy(Qt::StrongFocus);       // 支持通过 Tab 键和点击获取焦点
    setMinimumSize(200, 150);              // 设置控件最小尺寸
}

ImageView::~ImageView()
{
}

// ============================================================================
// loadImage：影像加载核心函数
// 功能：使用 GDAL 打开指定路径的遥感影像文件，读取像素数据并转换为 QImage
// ============================================================================
bool ImageView::loadImage(const QString& filePath)
{
    // ---- GDAL 初始化与配置 ----
    // 注册所有 GDAL 驱动，使程序支持各种栅格格式（GeoTIFF、IMG、JPEG 等）
    GDALAllRegister();
    // 设置 GDAL 文件名编码为 UTF-8，确保中文路径正常识别
    CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "YES");
    // 压制 GDAL 内部错误信息输出，由程序自行处理错误提示
    CPLPushErrorHandler((CPLErrorHandler)CPLQuietErrorHandler);

    // ---- GDAL 打开文件 ----
    QByteArray pathUtf8 = filePath.toUtf8();
    GDALDataset* dataset = static_cast<GDALDataset*>(
        GDALOpenEx(pathUtf8.constData(), GA_ReadOnly, nullptr, nullptr, nullptr));
    // 打开失败时，获取错误信息并返回 false
    if (!dataset) {
        m_lastError = QString::fromUtf8(CPLGetLastErrorMsg());
        CPLPopErrorHandler();
        qDebug() << "GDALOpenEx failed:" << filePath << "\n" << m_lastError;
        return false;
    }
    CPLPopErrorHandler();

    // ---- 获取影像尺寸与波段数 ----
    int width  = dataset->GetRasterXSize();
    int height = dataset->GetRasterYSize();
    int bands  = dataset->GetRasterCount();

    // 校验影像尺寸合法性
    if (width <= 0 || height <= 0 || bands <= 0) {
        GDALClose(dataset);
        m_lastError = QString::fromUtf8("Invalid image dimensions: %1x%2, bands=%3")
            .arg(width).arg(height).arg(bands);
        return false;
    }

    qDebug() << "Image loaded:" << width << "x" << height << "bands:" << bands;

    m_bandCount = bands;

    // ---- 地理参考信息读取 ----
    // 读取 GeoTransform 六参数仿射变换系数，用于影像坐标 ↔ 地理坐标 的转换
    // m_geoTransform[0] = 左上角 X 坐标（经度/投影X）
    // m_geoTransform[1] = 像元宽度（X 方向分辨率）
    // m_geoTransform[2] = X 方向旋转参数
    // m_geoTransform[3] = 左上角 Y 坐标（纬度/投影Y）
    // m_geoTransform[4] = Y 方向旋转参数
    // m_geoTransform[5] = 像元高度（Y 方向分辨率，通常为负值）
    if (dataset->GetGeoTransform(m_geoTransform) == CE_None) {
        m_hasGeoRef = true;
        // 读取投影参考系信息（WKT 格式）
        const char* projRef = dataset->GetProjectionRef();
        m_projectionWkt = projRef ? QString::fromUtf8(projRef) : QString();
        qDebug() << "GeoTransform:" << m_geoTransform[0] << m_geoTransform[1]
                 << m_geoTransform[2] << m_geoTransform[3] << m_geoTransform[4] << m_geoTransform[5];
        qDebug() << "Projection:" << m_projectionWkt.left(80);
    } else {
        // 无地理参考信息时，清空相关状态
        m_hasGeoRef = false;
        m_projectionWkt.clear();
    }

    // 读取波段 NoData 值（用于识别背景/无数据区域 → 透明显示）
    bool hasNoData = false;
    double noDataVal = 0.0;
    {
        int success = FALSE;
        double nd = dataset->GetRasterBand(1)->GetNoDataValue(&success);
        if (success) { hasNoData = true; noDataVal = nd; }
    }

    // 创建目标 QImage（ARGB32 格式，支持 Alpha 通道透明）
    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    // ---- 三分量彩色影像加载 ----
    if (bands >= 3) {
        // 获取 R/G/B 三个波段
        GDALRasterBand* rBand = dataset->GetRasterBand(1);
        GDALRasterBand* gBand = dataset->GetRasterBand(2);
        GDALRasterBand* bBand = dataset->GetRasterBand(3);
        GDALDataType dt = rBand->GetRasterDataType();

        if (dt == GDT_Byte) {
            // 【Byte 类型直接读取】
            // 8 位无符号整型，像素值范围 0-255，可直接赋值到 QImage
            QVector<quint8> rBuf(width), gBuf(width), bBuf(width);
            for (int y = 0; y < height; ++y) {
                // 每 50 行发射一次进度信号，供主界面进度条更新
                if (y % 50 == 0 || y == height - 1) {
                    emit loadProgress(y * 100 / height);
                    QApplication::processEvents();
                }
                // 逐行读取 R/G/B 三个波段数据
                rBand->RasterIO(GF_Read, 0, y, width, 1,
                    rBuf.data(), width, 1, GDT_Byte, 0, 0);
                gBand->RasterIO(GF_Read, 0, y, width, 1,
                    gBuf.data(), width, 1, GDT_Byte, 0, 0);
                bBand->RasterIO(GF_Read, 0, y, width, 1,
                    bBuf.data(), width, 1, GDT_Byte, 0, 0);
                for (int x = 0; x < width; ++x) {
                    // 检查是否为 NoData 像素（背景），若是则设为透明
                    if (hasNoData && rBuf[x] == noDataVal &&
                        gBuf[x] == noDataVal && bBuf[x] == noDataVal)
                        img.setPixel(x, y, qRgba(0, 0, 0, 0));
                    else
                        img.setPixel(x, y, qRgb(rBuf[x], gBuf[x], bBuf[x]));
                }
            }
        } else {
            // 【Float 类型自动拉伸】
            // 对于浮点型（GDT_Float32 / GDT_UInt16 等）高动态范围数据，
            // 先计算各波段像素值的最小/最大值，再线性拉伸至 0-255 用于显示
            double rMin, rMax, gMin, gMax, bMin, bMax;
            double adfMinMax[2];
            rBand->ComputeRasterMinMax(1, adfMinMax); rMin = adfMinMax[0]; rMax = adfMinMax[1];
            gBand->ComputeRasterMinMax(1, adfMinMax); gMin = adfMinMax[0]; gMax = adfMinMax[1];
            bBand->ComputeRasterMinMax(1, adfMinMax); bMin = adfMinMax[0]; bMax = adfMinMax[1];
            // 防止除以零：若最大值等于最小值，则令最大值 = 最小值 + 1
            if (rMax <= rMin) rMax = rMin + 1;
            if (gMax <= gMin) gMax = gMin + 1;
            if (bMax <= bMin) bMax = bMin + 1;

            QVector<float> rBuf(width), gBuf(width), bBuf(width);
            for (int y = 0; y < height; ++y) {
                // 每 50 行发射一次进度信号
                if (y % 50 == 0 || y == height - 1) {
                    emit loadProgress(y * 100 / height);
                    QApplication::processEvents();
                }
                // 逐行读取浮点数据
                rBand->RasterIO(GF_Read, 0, y, width, 1,
                    rBuf.data(), width, 1, GDT_Float32, 0, 0);
                gBand->RasterIO(GF_Read, 0, y, width, 1,
                    gBuf.data(), width, 1, GDT_Float32, 0, 0);
                bBand->RasterIO(GF_Read, 0, y, width, 1,
                    bBuf.data(), width, 1, GDT_Float32, 0, 0);
                for (int x = 0; x < width; ++x) {
                    // 线性拉伸： (value - min) / (max - min) * 255，并限幅到 [0, 255]
                    int rv = qBound(0, static_cast<int>(255.0 * (rBuf[x] - rMin) / (rMax - rMin)), 255);
                    int gv = qBound(0, static_cast<int>(255.0 * (gBuf[x] - gMin) / (gMax - gMin)), 255);
                    int bv = qBound(0, static_cast<int>(255.0 * (bBuf[x] - bMin) / (bMax - bMin)), 255);
                    // 检查是否为 NoData 像素（浮点比较带容差），若是则设为透明
                    if (hasNoData &&
                        std::abs(rBuf[x] - noDataVal) < 0.5 &&
                        std::abs(gBuf[x] - noDataVal) < 0.5 &&
                        std::abs(bBuf[x] - noDataVal) < 0.5)
                        img.setPixel(x, y, qRgba(0, 0, 0, 0));
                    else
                        img.setPixel(x, y, qRgb(rv, gv, bv));
                }
            }
        }
    // ---- 单波段灰度影像加载 ----
    } 
    else if (bands == 1) {
        GDALRasterBand* band = dataset->GetRasterBand(1);
        GDALDataType dt = band->GetRasterDataType();

        if (dt == GDT_Byte) {
            // Byte 类型，直接读取后复制到 RGB 三通道（灰度 = R=G=B）
            QVector<quint8> buf(width);
            for (int y = 0; y < height; ++y) {
                if (y % 50 == 0 || y == height - 1) {
                    emit loadProgress(y * 100 / height);
                    QApplication::processEvents();
                }
                band->RasterIO(GF_Read, 0, y, width, 1,
                    buf.data(), width, 1, GDT_Byte, 0, 0);
                for (int x = 0; x < width; ++x) {
                    quint8 v = buf[x];
                    // 检查是否为 NoData 像素，若是则设为透明
                    if (hasNoData && buf[x] == noDataVal)
                        img.setPixel(x, y, qRgba(0, 0, 0, 0));
                    else
                        img.setPixel(x, y, qRgb(v, v, v));
                }
            }
        } else {
            // 浮点型，计算最小/最大值后进行线性拉伸
            double adfMinMax[2];
            band->ComputeRasterMinMax(1, adfMinMax);
            double dMin = adfMinMax[0], dMax = adfMinMax[1];
            if (dMax <= dMin) dMax = dMin + 1;

            QVector<float> buf(width);
            for (int y = 0; y < height; ++y) {
                if (y % 50 == 0 || y == height - 1) {
                    emit loadProgress(y * 100 / height);
                    QApplication::processEvents();
                }
                band->RasterIO(GF_Read, 0, y, width, 1,
                    buf.data(), width, 1, GDT_Float32, 0, 0);
                for (int x = 0; x < width; ++x) {
                    int v = qBound(0, static_cast<int>(255.0 * (buf[x] - dMin) / (dMax - dMin)), 255);
                    // 检查是否为 NoData 像素（浮点比较带容差），若是则设为透明
                    if (hasNoData && std::abs(buf[x] - noDataVal) < 0.5)
                        img.setPixel(x, y, qRgba(0, 0, 0, 0));
                    else
                        img.setPixel(x, y, qRgb(v, v, v));
                }
            }
        }
    }

    // 发射 100% 完成信号，确保进度条到达终点
    emit loadProgress(100);
    QApplication::processEvents();

    // 关闭 GDAL 数据集，释放资源
    GDALClose(dataset);

    // 保存 QImage 和文件路径
    m_image = img;
    m_imagePath = filePath;
    // 加载新影像后清空之前遗留的控制点和刺点
    m_controlPoints.clear();
    m_hasPickPoint = false;

    // 自动缩放影像以完整显示在窗口中
    fitToWindow();
    update();   // 触发重绘
    return true;
}

// ============================================================================
// checkGeoReference — 深度检测影像地理参考信息
//
// 多层级检测:
//   1. 存在性检测: GeoTransform 是否从 GDAL 成功读取 (CE_None)
//   2. 数值合法性: 六参数是否全部有限（非 NaN/Inf）
//   3. 分辨率合理性: gt[1]/gt[5] 非零且在合理量级内
//      - 地理坐标系(WGS84等): 典型值 0.00001~0.01 度
//      - 投影坐标系(UTM等): 典型值 0.3~30 米
//      - 容忍范围: 1e-6 ~ 1e5
//   4. 坐标范围合理性: 左上角坐标在典型地学范围内
//      - 地理坐标: 纬度 -90~90, 经度 -180~180
//   5. CRS 检测: 是否包含有效的投影定义字符串
//
// 返回 GeoRefStatus 结构体, 包含有效性标志、各项参数和诊断问题列表
// ============================================================================
ImageView::GeoRefStatus ImageView::checkGeoReference() const
{
    GeoRefStatus status;

    if (!m_hasGeoRef) {
        status.issues.append(QString::fromUtf8("无 GeoTransform: 影像未包含地理仿射变换参数"));
        status.issues.append(QString::fromUtf8("  后果: 自动匹配无法计算重叠区域, 将退化为全图搜索"));
        status.issues.append(QString::fromUtf8("  建议: 使用正射校正后的影像, 或手动添加地理参考"));
        return status;
    }

    const double* gt = m_geoTransform;

    // ---- 1. 数值合法性: 检查是否存在 NaN 或 Inf ----
    for (int i = 0; i < 6; ++i) {
        if (!std::isfinite(gt[i])) {
            status.issues.append(QString::fromUtf8(
                "GeoTransform 参数包含非法数值 (NaN/Inf): GT[%1]=%2")
                .arg(i).arg(gt[i]));
            return status;
        }
    }

    status.pixelWidth  = gt[1];
    status.pixelHeight = gt[5];
    status.topLeftX    = gt[0];
    status.topLeftY    = gt[3];

    // ---- 2. 分辨率合理性 ----
    bool resOK = true;
    if (std::abs(gt[1]) < 1e-12) {
        status.issues.append(QString::fromUtf8(
            "像元宽度为零 (GT[1]=%1): 无法进行坐标换算").arg(gt[1]));
        resOK = false;
    }
    if (std::abs(gt[5]) < 1e-12) {
        status.issues.append(QString::fromUtf8(
            "像元高度为零 (GT[5]=%1): 无法进行坐标换算").arg(gt[5]));
        resOK = false;
    }

    // 分辨率数量级检查 (按地理坐标和投影坐标两种典型值设置不同阈值)
    double absResX = std::abs(gt[1]);
    double absResY = std::abs(gt[5]);
    bool isGeographic = (m_projectionWkt.contains("GEOGCS", Qt::CaseInsensitive) ||
                          m_projectionWkt.contains("WGS", Qt::CaseInsensitive)    ||
                          m_projectionWkt.contains("NAD83", Qt::CaseInsensitive)  ||
                          m_projectionWkt.contains("NAD27", Qt::CaseInsensitive)  ||
                          (m_projectionWkt.isEmpty() && absResX < 0.05));
    QString unit = isGeographic ? QString::fromUtf8("度") : QString::fromUtf8("米");

    if (absResX > 1e5 || absResY > 1e5) {
        status.issues.append(QString::fromUtf8(
            "像元分辨率异常大 (%1 %3, %2 %3): GeoTransform 可能错误")
            .arg(absResX).arg(absResY).arg(unit));
        resOK = false;
    }
    if (isGeographic && (absResX > 0.1 || absResY > 0.1)) {
        status.issues.append(QString::fromUtf8(
            "地理坐标系下分辨率过大 (%1°, %2°): 可能不是有效的遥感影像")
            .arg(absResX).arg(absResY));
        // 不严重，仅提示
    }

    // ---- 3. 坐标范围合理性 ----
    if (isGeographic) {
        // 地理坐标: 纬度在 -90~90, 经度在 -180~180（或 0~360）
        if (gt[0] < -180.0 || gt[0] > 360.0) {
            status.issues.append(QString::fromUtf8(
                "左上角经度异常 (%1): 不在有效地理经度范围内").arg(gt[0]));
        }
        if (gt[3] < -90.0 || gt[3] > 90.0) {
            status.issues.append(QString::fromUtf8(
                "左上角纬度异常 (%1): 不在有效地理纬度范围内").arg(gt[3]));
        }
    } else {
        // 投影坐标: 容忍范围 -1e7 ~ 1e7 (UTM 等投影坐标系)
        if (std::abs(gt[0]) > 1e7 || std::abs(gt[3]) > 1e7) {
            status.issues.append(QString::fromUtf8(
                "左上角投影坐标异常 (%1, %2): 数值量级过大").arg(gt[0]).arg(gt[3]));
        }
    }

    // ---- 4. CRS 检测 ----
    if (!m_projectionWkt.isEmpty()) {
        status.hasCRS = true;

        // 尝试解析 CRS 获取友好描述
        OGRSpatialReference srs;
        QByteArray wkt = m_projectionWkt.toUtf8();
        char* tmp = wkt.data();
        if (srs.importFromWkt(&tmp) == OGRERR_NONE) {
            // 优先获取 EPSG 代码
            const char* authority = srs.GetAuthorityName(nullptr);
            const char* code = srs.GetAuthorityCode(nullptr);
            if (authority && code && strcmp(authority, "EPSG") == 0) {
                status.crsDescription = QString("EPSG:%1").arg(code);
            }

            // 获取投影名称
            const char* projName = srs.GetAttrValue("PROJCS");
            if (!projName) projName = srs.GetAttrValue("GEOGCS");
            if (projName)
                status.crsDescription += status.crsDescription.isEmpty()
                    ? QString::fromUtf8(projName)
                    : QString(" (%1)").arg(QString::fromUtf8(projName));
        }
    }

    status.valid = resOK;
    return status;
}

// ============================================================================
// clear：清除影像和相关状态
// 功能：重置所有成员变量到初始状态，释放影像资源
// ============================================================================
void ImageView::clear()
{
    m_image = QImage();             // 释放影像数据
    m_imagePath.clear();            // 清空文件路径
    m_controlPoints.clear();         // 清空控制点列表
    m_hasPickPoint = false;          // 清除刺点状态
    m_scale = 1.0;                   // 恢复默认缩放比
    m_offset = QPointF(0, 0);       // 重置偏移量
    m_hasGeoRef = false;             // 清除地理参考信息标志
    m_bandCount = 3;                 // 恢复默认波段数
    m_projectionWkt.clear();         // 清空投影参考系字符串
    std::fill(std::begin(m_geoTransform), std::end(m_geoTransform), 0.0); // 清零仿射变换系数
    update();                        // 触发重绘，显示空白背景
}

// ============================================================================
// zoomIn / zoomOut：缩放操作
// zoomIn  — 放大 1.25 倍（缩小系数 1/1.25 = 0.8）
// zoomOut — 缩小至 1/1.25（即原图的 0.8 倍）
// ============================================================================
void ImageView::zoomIn()
{
    zoomToScale(m_scale * 1.25);
}//放大用

void ImageView::zoomOut()
{
    zoomToScale(m_scale / 1.25);
}//缩小用

// ============================================================================
// fitToWindow：适应窗口
// 功能：计算缩放比例，使影像完整显示在控件范围内，并居中放置
// ============================================================================
void ImageView::fitToWindow()
{
    if (m_image.isNull()) return;
    // 分别计算水平方向和垂直方向的缩放比，取较小值确保影像完整显示
    double sx = (double)width()  / m_image.width();
    double sy = (double)height() / m_image.height();
    m_scale = qMin(sx, sy);
    // 计算居中偏移量，使影像位于窗口正中央
    m_offset = QPointF(
        (width()  - m_image.width()  * m_scale) / 2.0,
        (height() - m_image.height() * m_scale) / 2.0);
    update();
}

// ============================================================================
// centerOn：中心定位到指定影像坐标
// 功能：将影像上的指定点移至控件视口中心
// 参数 imagePt — 影像坐标系中的目标点（像素坐标）
// 效果体现为选中控制点之后跳转到相应的控制点
// ============================================================================
void ImageView::centerOn(const QPointF& imagePt)
{
    if (m_image.isNull()) return;
    // 计算偏移量，使 (imagePt.x, imagePt.y) 映射到控件中心
    m_offset = QPointF(
        width()  * 0.5 - imagePt.x() * m_scale,
        height() * 0.5 - imagePt.y() * m_scale);
    update();
}

// ============================================================================
// zoomToScale：以窗口中心为锚点缩放到指定比例
// 功能：将缩放比设置为指定值，并调整偏移量使窗口中心点对应的影像位置不变
// 参数 scale — 目标缩放比，限制在 [0.01, 50.0] 范围内
// ============================================================================
void ImageView::zoomToScale(double scale)
{
    if (m_image.isNull()) return;
    // 限制缩放比范围，防止过度放大或缩小
    scale = qBound(0.01, scale, 50.0);
    if (qAbs(scale - m_scale) < 1e-6) return;

    // 以窗口中心为锚点缩放：先记录窗口中心对应的影像坐标，
    // 缩放后再将该影像坐标重新定位到窗口中心，从而实现"锚定"效果
    double cx = width()  / 2.0;
    double cy = height() / 2.0;
    QPointF imgPt = screenToImage(QPointF(cx, cy));

    m_scale = scale;
    QPointF newScreen = imageToScreen(imgPt);
    // 调整偏移量，补偿因缩放导致的中心点偏移
    m_offset += QPointF(cx - newScreen.x(), cy - newScreen.y());

    update();
}

// ============================================================================
// screenToImage / imageToScreen：屏幕坐标 ↔ 影像坐标 双向转换
// screenToImage — 将控件屏幕坐标转换为影像像素坐标（减去偏移后除以缩放比）
// imageToScreen — 将影像像素坐标转换为控件屏幕坐标（乘以缩放比后加上偏移）
// ============================================================================
QPointF ImageView::screenToImage(const QPointF& screenPt) const
{
    return QPointF(
        (screenPt.x() - m_offset.x()) / m_scale,
        (screenPt.y() - m_offset.y()) / m_scale);
}

QPointF ImageView::imageToScreen(const QPointF& imagePt) const
{
    return QPointF(
        m_offset.x() + imagePt.x() * m_scale,
        m_offset.y() + imagePt.y() * m_scale);
}

// ============================================================================
// paintEvent：绘制事件 — 控件的主绘制入口
// 功能：
//   1. 填充深灰色背景
//   2. 绘制影像（仅绘制可见区域，提高性能）
//   3. 绘制控制点标记（绿色十字 + 圆形）
//   4. 刺点模式下绘制十字丝及坐标标签
// ============================================================================
void ImageView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // 开启平滑缩放，使缩放后的影像边缘更平滑
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // ---- 背景填充 ----
    // 使用深灰色填充整个控件背景，未覆盖影像的区域显示为深灰色
    painter.fillRect(rect(), QColor(50, 50, 50));

    // 无影像时显示提示文字
    if (m_image.isNull()) {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter,
            QString::fromUtf8("拖放影像文件到此处或使用文件→打开"));
        return;
    }

    // ---- 影像绘制（仅绘制可见区域） ----
    // 计算当前视口在影像坐标系中的覆盖范围，仅复制并绘制这部分，
    // 避免将整幅大影像缩放到 QPainter 中绘制，显著提升性能
    QPointF topLeft     = screenToImage(QPointF(0, 0));
    QPointF bottomRight = screenToImage(QPointF(width(), height()));
    int srcX = qMax(0, static_cast<int>(topLeft.x()));
    int srcY = qMax(0, static_cast<int>(topLeft.y()));
    int srcW = qMin(m_image.width()  - srcX,
                    static_cast<int>(bottomRight.x()) - srcX + 2);
    int srcH = qMin(m_image.height() - srcY,
                    static_cast<int>(bottomRight.y()) - srcY + 2);

    if (srcW > 0 && srcH > 0) {
        QImage visible = m_image.copy(srcX, srcY, srcW, srcH);
        QPointF dst = imageToScreen(QPointF(srcX, srcY));
        QRectF targetRect(dst.x(), dst.y(), srcW * m_scale, srcH * m_scale);
        painter.drawImage(targetRect, visible);
    }

    // ---- 控制点标记绘制（绿色十字 + 圆） ----
    // 在影像上已标记的控制点位置绘制绿色十字线和外接圆
    painter.setPen(QPen(m_markerColor, 2));
    for (const QPointF& pt : m_controlPoints) {
        QPointF sp = imageToScreen(pt);
        int cx = static_cast<int>(sp.x());
        int cy = static_cast<int>(sp.y());
        int r = 6;
        painter.drawLine(cx - r, cy, cx + r, cy);
        painter.drawLine(cx, cy - r, cx, cy + r);
        painter.drawEllipse(QPointF(cx, cy), r, r);
    }

    // ---- 特征点绘制（红色圆点）----
    // 用于可视化 Harris 角点检测结果，在自动匹配过程中调用
    // 绘制为直径 5px 的实心红色圆点
    for (const auto& fp : m_featurePoints) {
        QPointF sp = imageToScreen(fp);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 0, 0, 180));
        painter.drawEllipse(sp, 2.5, 2.5);
    }

    // ---- 重叠区覆盖绘制（绿色半透明）----
    // 计算重叠区域后，在源影像上以绿色半透明矩形标识重叠范围
    if (m_hasOverlay && m_overlayRect.isValid()) {
        QPointF tl = imageToScreen(m_overlayRect.topLeft());
        QPointF br = imageToScreen(m_overlayRect.bottomRight());
        QRectF screenRect(tl.x(), tl.y(),
                          br.x() - tl.x(), br.y() - tl.y());
        painter.fillRect(screenRect, QColor(0, 255, 0, 40));
        painter.setPen(QPen(QColor(0, 200, 0, 200), 2, Qt::DashLine));
        painter.drawRect(screenRect);
    }

    // ---- 十字丝绘制（刺点模式）----
    // 刺点模式下，在鼠标左键选定的位置绘制红色十字丝，并显示影像坐标标签
    if (m_pickMode == Picking && m_hasPickPoint) {
        QPointF sp = imageToScreen(m_pickPoint);
        int cx = static_cast<int>(sp.x());
        int cy = static_cast<int>(sp.y());
        int len = 30;
        painter.setPen(QPen(m_crosshairColor, 1));
        painter.drawLine(cx - len, cy, cx + len, cy);
        painter.drawLine(cx, cy - len, cx, cy + len);
        // 在十字丝右上方绘制坐标文本（浮点数格式，保留两位小数）
        painter.setPen(Qt::white);
        painter.drawText(cx + 10, cy - 10,
            QString("(%1, %2)")
                .arg(m_pickPoint.x(), 0, 'f', 2)
                .arg(m_pickPoint.y(), 0, 'f', 2));
    }
}

// ============================================================================
// wheelEvent：滚轮缩放
// 功能：滚动鼠标滚轮时以鼠标位置为锚点进行缩放，
//       保持鼠标指针下方的影像位置不发生偏移
// 说明：上滚放大（×1.15），下滚缩小（÷1.15）
// ============================================================================
void ImageView::wheelEvent(QWheelEvent* event)
{
    if (m_image.isNull()) return;

    // 根据滚轮方向确定缩放系数，正值（上滚）放大，负值（下滚）缩小
    double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);

    // 以鼠标位置为锚点缩放，保持鼠标下方的图像位置不变
    QPointF mousePos = event->position();
    QPointF imgPt = screenToImage(mousePos);

    m_scale *= factor;
    m_scale = qBound(0.01, m_scale, 50.0);

    // 缩放后重新计算偏移量，使鼠标位置对应的影像坐标保持不变
    QPointF newScreen = imageToScreen(imgPt);
    m_offset += QPointF(mousePos.x() - newScreen.x(),
                        mousePos.y() - newScreen.y());

    update();
}

// ============================================================================
// mousePressEvent：鼠标按下事件
// 功能：
//   - 刺点模式：左键在影像上定位十字丝位置；右键确认当前十字丝为刺点
//   - 浏览模式：左键按下时开启拖拽平移
// ============================================================================
void ImageView::mousePressEvent(QMouseEvent* event)
{
    if (m_image.isNull()) return;

    if (m_pickMode == Picking) {
        // 刺点模式：
        //   左键 — 将十字丝定位到鼠标点击处的影像坐标
        //   右键 — 发出 pointPicked 信号，将当前十字丝位置作为刺点提交
        QPointF imgPt = screenToImage(event->pos());
        if (event->button() == Qt::LeftButton) {
            m_pickPoint = imgPt;
            m_hasPickPoint = true;
            update();
        } else if (event->button() == Qt::RightButton && m_hasPickPoint) {
            emit pointPicked(m_pickPoint);
        }
    } else {
        // 浏览模式：左键按下时记录起始位置，开始拖拽平移
        if (event->button() == Qt::LeftButton) {
            m_isPanning = true;
            m_lastMousePos = event->localPos();
            setCursor(Qt::ClosedHandCursor);  // 切换为抓取手型光标
        }
    }
}

// ============================================================================
// mouseMoveEvent：鼠标移动事件
// 功能：
//   - 拖拽平移：当处于平移状态时，根据鼠标位移更新偏移量
//   - 实时发射 mouseMoved 信号（含影像坐标），供状态栏等显示
// ============================================================================
void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_image.isNull()) return;

    if (m_isPanning) {
        // 计算鼠标拖拽位移量，累加到偏移量中实现影像平移
        QPointF delta = event->localPos() - m_lastMousePos;
        m_offset += delta;
        m_lastMousePos = event->localPos();
        update();
    }

    // 实时计算鼠标在影像上的坐标并发出信号，可用于在状态栏显示坐标值
    QPointF imgPt = screenToImage(event->localPos());
    emit mouseMoved(imgPt);
}

// ============================================================================
// mouseReleaseEvent：鼠标释放事件
// 功能：左键释放时结束拖拽平移状态，恢复默认光标样式
// ============================================================================
void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isPanning && event->button() == Qt::LeftButton) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);  // 恢复默认箭头光标
    }
}

// ============================================================================
// resizeEvent：窗口大小改变事件
// 功能：控件尺寸变化时触发重绘，使影像在新的尺寸下正确显示
// ============================================================================
void ImageView::resizeEvent(QResizeEvent*)
{
    update();
}

// ============================================================================
// setPickMode：切换刺点 / 浏览模式
// 功能：
//   - Picking（刺点模式）：光标变为十字准星，左键定位、右键确认刺点
//   - Normal（浏览模式）：光标恢复为箭头，支持拖拽平移
// ============================================================================
void ImageView::setPickMode(PickMode mode)
{
    m_pickMode = mode;
    if (mode == Picking) {
        setCursor(Qt::CrossCursor);    // 刺点模式使用十字准星光标
    } else {
        setCursor(Qt::ArrowCursor);    // 浏览模式恢复箭头光标
    }
    update();
}

// ============================================================================
// setControlPoints / addControlPoint / clearControlPoints：控制点管理
// setControlPoints   — 批量设置控制点列表（替换原有所有控制点）
// addControlPoint    — 向列表中添加单个控制点
// clearControlPoints — 清除所有控制点及刺点状态
// ============================================================================
void ImageView::setControlPoints(const QVector<QPointF>& pts)
{
    m_controlPoints = pts;
    update();
}

void ImageView::addControlPoint(const QPointF& pt)
{
    m_controlPoints.append(pt);
    update();
}

void ImageView::clearControlPoints()
{
    m_controlPoints.clear();
    m_hasPickPoint = false;
    update();
}

// setFeaturePoints — 设置特征点列表（红色圆点）
// 用于在自动匹配后显示提取到的 Harris 角点位置
void ImageView::setFeaturePoints(const QVector<QPointF>& pts)
{
    m_featurePoints = pts;
    update();
}

// clearFeaturePoints — 清除特征点标记
void ImageView::clearFeaturePoints()
{
    m_featurePoints.clear();
    update();
}

// ============================================================================
// 重叠区覆盖 — 在影像上绘制绿色半透明矩形指示重叠范围
// 用于计算重叠区域后在源影像上可视化显示重叠区位置
// ============================================================================

// setOverlayRect — 设置重叠区矩形，用绿色半透明填充标记
// imageRect: 影像坐标下的重叠区矩形（由 OverlapResult.srcROI 提供）
void ImageView::setOverlayRect(const QRectF& imageRect)
{
    m_overlayRect = imageRect;
    m_hasOverlay  = true;
    update();
}

// clearOverlayRect — 清除重叠区覆盖
void ImageView::clearOverlayRect()
{
    m_hasOverlay = false;
    update();
}
