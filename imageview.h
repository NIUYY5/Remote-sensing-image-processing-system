#pragma once

#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QVector>
#include <QPoint>

/**
 * @brief 影像视图组件
 *
 * 基于 QWidget 的自绘影像显示控件，支持：
 * - 影像加载与缩放/平移/自适应显示
 * - 鼠标刺点交互（点击选取同名点）
 * - 控制点标记绘制（十字丝 + 编号圆圈）
 * - 滚轮缩放 + 拖拽平移
 * - 从 GeoTIFF 读取地理参考信息
 *
 * 坐标体系：
 * - 屏幕坐标 (screen coordinates): 控件像素坐标
 * - 影像坐标 (image coordinates): 原始影像像素坐标，左上角为原点
 */
class ImageView : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 视图交互模式枚举
     */
    enum PickMode {
        Normal,   // 普通模式：支持拖拽平移和滚轮缩放
        Picking   // 刺点模式：点击影像选取同名点，发射 pointPicked 信号
    };

    explicit ImageView(QWidget* parent = nullptr);
    ~ImageView();

    bool loadImage(const QString& filePath);  // 加载影像文件（支持常见栅格格式）
    bool isLoaded() const { return !m_image.isNull(); }  // 是否已加载影像
    void clear();                             // 清空影像和所有标记

    void zoomIn();                            // 放大（比例尺增大）
    void zoomOut();                           // 缩小（比例尺减小）
    void fitToWindow();                       // 自适应缩放至控件窗口大小
    void zoomToScale(double scale);           // 缩放到指定比例尺
    double currentScale() const { return m_scale; }  // 获取当前缩放比例
    void centerOn(const QPointF& imagePt);    // 将影像的某点居中显示

    void setPickMode(PickMode mode);          // 设置交互模式（普通/刺点）
    PickMode pickMode() const { return m_pickMode; }  // 获取当前交互模式

    QPointF currentPickPoint() const { return m_pickPoint; }  // 获取当前刺点位置
    bool hasPickPoint() const { return m_hasPickPoint; }      // 是否已刺取当前点

    void setControlPoints(const QVector<QPointF>& pts);  // 批量设置要显示的控制点
    void addControlPoint(const QPointF& pt);              // 添加一个控制点标记
    void clearControlPoints();                            // 清除所有控制点标记
    int  controlPointCount() const { return m_controlPoints.size(); }  // 控制点数量

    void setFeaturePoints(const QVector<QPointF>& pts);  // 设置特征点列表（红色标注）
    void clearFeaturePoints();                            // 清除特征点标记
    QVector<QPointF> featurePoints() const { return m_featurePoints; }  // 获取特征点列表

    // ==================== 重叠区覆盖 ====================
    void setOverlayRect(const QRectF& imageRect);   // 设置重叠区矩形（绿色半透明覆盖）
    void clearOverlayRect();                         // 清除重叠区覆盖

    int imageWidth() const { return m_image.width(); }    // 获取影像宽度（像素）
    int imageHeight() const { return m_image.height(); }  // 获取影像高度（像素）

    const QImage& getImage() const { return m_image; }    // 获取原始 QImage
    QString imagePath() const { return m_imagePath; }     // 获取影像文件路径
    QString lastError() const { return m_lastError; }     // 获取最后一次错误信息

    // ==================== 地理参考 ====================
    bool hasGeoTransform() const { return m_hasGeoRef; }           // 是否包含地理仿射变换参数
    const double* geoTransform() const { return m_geoTransform; }  // 获取 GT(2×3) 仿射变换参数
    QString projectionWkt() const { return m_projectionWkt; }      // 获取投影坐标系 WKT 字符串
    int  bandCount() const { return m_bandCount; }                 // 获取影像波段数

    /**
     * @brief 检测影像是否包含有效的地理参考信息
     *
     * 深度学习检测，不只是 GDAL 读取成功，还验证：
     * 1. GeoTransform 六参数全部合法（非 NaN/Inf）
     * 2. 像元分辨率非零（gt[1]≠0, gt[5]≠0）
     * 3. 像元大小合理（0.000001 ~ 100000 范围）
     * 4. 左上角坐标在合理经纬度/投影范围
     *
     * @return 检测结果结构体，包含状态和详细诊断信息
     */
    struct GeoRefStatus {
        bool   valid   = false;     // 地理参考是否有效
        bool   hasCRS  = false;     // 是否包含投影坐标系定义
        double pixelWidth  = 0.0;   // X 方向像元分辨率
        double pixelHeight = 0.0;   // Y 方向像元分辨率
        double topLeftX    = 0.0;   // 左上角 X 坐标
        double topLeftY    = 0.0;   // 左上角 Y 坐标
        QString crsDescription;     // 投影坐标系描述（供显示）
        QStringList issues;         // 检测到的问题列表
    };
    GeoRefStatus checkGeoReference() const;

signals:
    // 刺点完成信号：在 Picking 模式下点击影像时发射，传递影像像素坐标（浮点，左上角为原点）
    // 通过滚轮放大影像后点击可获得亚像素精度
    void pointPicked(QPointF imageCoord);

    // 鼠标移动信号：鼠标在影像上移动时持续发射当前影像坐标，用于状态栏坐标显示
    void mouseMoved(QPointF imageCoord);

    // 影像加载进度信号：0~100
    void loadProgress(int percent);

protected:
    void paintEvent(QPaintEvent* event) override;     // 自绘影像、控制点标记和十字丝
    void wheelEvent(QWheelEvent* event) override;     // 滚轮缩放
    void mousePressEvent(QMouseEvent* event) override;  // 鼠标按下：刺点/拖拽开始
    void mouseMoveEvent(QMouseEvent* event) override;   // 鼠标移动：拖拽平移/坐标更新
    void mouseReleaseEvent(QMouseEvent* event) override; // 鼠标释放：拖拽结束
    void resizeEvent(QResizeEvent* event) override;     // 控件大小变化

private:
    // 坐标转换：屏幕坐标 → 影像坐标（考虑缩放偏移）
    QPointF screenToImage(const QPointF& screenPt) const;
    // 坐标转换：影像坐标 → 屏幕坐标（考虑缩放偏移）
    QPointF imageToScreen(const QPointF& imagePt) const;

private:
    QImage    m_image;           // 当前显示的影像数据
    QString   m_imagePath;       // 影像文件路径
    double    m_scale;           // 当前缩放比例（1.0 表示原始大小）
    QPointF   m_offset;          // 显示偏移量（像素），用于平移控制
    QPointF   m_lastMousePos;    // 上一次鼠标位置（用于拖拽平移计算）
    bool      m_isPanning;       // 是否正在拖拽平移中

    PickMode  m_pickMode;        // 当前交互模式（Normal / Picking）
    QPointF   m_pickPoint;       // 当前刺取的影像坐标
    bool      m_hasPickPoint;    // 是否已刺取到当前点

    QVector<QPointF> m_controlPoints;  // 控制点标记列表，用于在影像上绘制十字丝
    QVector<QPointF> m_featurePoints;   // 特征点标记列表（红色圆点），用于自动匹配时显示 Harris 角点
    QRectF    m_overlayRect;            // 重叠区矩形（影像坐标），绘制绿色半透明覆盖
    bool      m_hasOverlay = false;     // 是否有重叠区需要显示

    QColor    m_crosshairColor;  // 刺点十字丝颜色
    QColor    m_markerColor;     // 控制点标记颜色

    QString   m_lastError;       // 最后一条错误信息

    // ==================== 地理参考 (从 GeoTIFF 读取) ====================
    double    m_geoTransform[6]; // GDAL 仿射变换六参数 (GT(0)~GT(5))
    QString   m_projectionWkt;   // 投影坐标系 Well-Known Text 描述
    bool      m_hasGeoRef;       // 是否已成功读取地理参考信息
    int       m_bandCount = 3;   // 影像波段数（默认3波段RGB）
};
