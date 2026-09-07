#pragma once

#include <QString>
#include <QVector>
#include <QPointF>

/**
 * @brief 同名点对数据结构
 *
 * 存储一对在源影像和参考影像上人工刺取的"同名点"坐标，
 * 以及配准解算后计算得到的残差信息。
 * 源影像坐标 (srcX, srcY) 指向待配准影像上的点，
 * 参考影像坐标 (refX, refY) 指向参考影像上的同名点。
 */
struct ControlPoint
{
    int    id;       // 控制点编号（从 0 开始递增）
    double srcX;     // 源影像像平面坐标 X (子像素精度)
    double srcY;     // 源影像像平面坐标 Y (子像素精度)
    double refX;     // 参考影像像平面坐标 X (子像素精度)
    double refY;     // 参考影像像平面坐标 Y (子像素精度)
    double resX;     // X方向残差 vx = x'_计算 - x'_观测 (配准后计算)
    double resY;     // Y方向残差 vy = y'_计算 - y'_观测 (配准后计算)

    ControlPoint() : id(0), srcX(0), srcY(0), refX(0), refY(0), resX(0), resY(0) {}
    ControlPoint(int i, double sx, double sy, double rx, double ry)
        : id(i), srcX(sx), srcY(sy), refX(rx), refY(ry), resX(0), resY(0) {}
};

/**
 * @brief 平差解算结果结构体
 *
 * 存储最小二乘平差解算后得到的各项精度指标，
 * 包括模型参数、单位权中误差、均方根误差等统计量。
 */
struct AdjustmentResult
{
    QVector<double> parameters;    // 解算得到的模型参数向量 X
    double          sigma0;        // 单位权中误差 σ₀ = sqrt(VᵀPV / r)
    double          rmseX;         // X方向均方根误差 (Root Mean Square Error)
    double          rmseY;         // Y方向均方根误差
    double          rmseTotal;     // 总体均方根误差 = sqrt((rmseX² + rmseY²) / 2)
    double          maxErrorX;     // X方向最大残差绝对值
    double          maxErrorY;     // Y方向最大残差绝对值
    QVector<ControlPoint> points;  // 带残差的控制点列表（解算后回填残差值）
    bool            valid;         // 解算是否成功

    AdjustmentResult() : sigma0(0), rmseX(0), rmseY(0), rmseTotal(0),
                         maxErrorX(0), maxErrorY(0), valid(false) {}
};

/**
 * @brief 几何变换模型类型枚举
 *
 * 定义配准可选的数学变换模型，用户通过 UI 下拉框切换。
 */
enum class GeoModelType
{
    Affine,            // 仿射变换 (6参数, 最少3个控制点)
    Polynomial2nd      // 二次多项式 (12参数, 最少6个控制点)
};
