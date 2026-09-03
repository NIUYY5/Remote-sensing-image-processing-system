#pragma once

#include "controlpoint.h"
#include <QPointF>
#include <QVector>
#include <QImage>

/**
 * @brief 几何变换模型类（自编最小二乘平差实现）
 *
 * 实现影像几何配准的核心解算功能，支持仿射变换和二次多项式两种模型。
 * 采用自编的高斯列主元消去法解算法方程，完全自主实现，不依赖任何
 * 第三方数学库（如 Eigen、Armadillo 等）。
 *
 * 数学模型：
 * - 仿射变换 (6参数):   x' = a0 + a1·x + a2·y,  y' = b0 + b1·x + b2·y
 * - 二次多项式 (12参数): x' = a0 + a1·x + a2·y + a3·x² + a4·x·y + a5·y²
 *                         y' = b0 + b1·x + b2·y + b3·x² + b4·x·y + b5·y²
 */
class GeoModel
{
public:
    GeoModel();
    ~GeoModel();

    void setModelType(GeoModelType type);    // 设置几何变换模型类型
    GeoModelType modelType() const { return m_type; }  // 获取当前模型类型
    int parameterCount() const;              // 获取当前模型的参数个数
    int minControlPoints() const;            // 获取当前模型所需的最少控制点数

    /**
     * @brief 前向变换：源→参考
     *
     * 将源影像坐标 (sx,sy) 通过已解算的变换模型映射到参考影像坐标系，
     * 得到变换后的坐标 (rx,ry)。用于影像重采样时的坐标映射。
     */
    QPointF forwardTransform(double sx, double sy) const;

    /**
     * @brief 逆向变换：参考→源
     *
     * 将参考影像坐标 (rx,ry) 逆映射回源影像坐标 (sx,sy)。
     * 对于多项式模型，逆变换通过迭代近似求解。
     */
    QPointF inverseTransform(double rx, double ry) const;

    /**
     * @brief 核心解算方法：最小二乘平差
     *
     * 利用控制点观测值，通过最小二乘原理求解变换模型参数。
     * 步骤：
     *   1. 构建设计矩阵 A (2n × m)
     *   2. 构建观测向量 L (2n × 1)
     *   3. 组成法方程: (AᵀA)X = AᵀL
     *   4. 高斯列主元消去法求解法方程
     *   5. 计算残差和精度指标
     * 完全自编实现，不调用任何第三方库。
     *
     * @param points 控制点列表
     * @return 平差结果（含参数、残差、精度指标）
     */
    AdjustmentResult solveAdjustment(const QVector<ControlPoint>& points);

    /**
     * @brief 加权最小二乘平差（边缘加权）
     *
     * 与 solveAdjustment 相同的解算流程，但引入逐点权重矩阵 P:
     *   法方程: (AᵀPA)·X = AᵀPL
     *
     * 每个控制点 i 的权重 w_i 作用于其 X/Y 两个观测方程:
     *   P = diag(w₀,w₀, w₁,w₁, ..., w_{n-1},w_{n-1})
     *
     * 等价于将设计矩阵 A 和 观测向量 L 的每一行乘以 sqrt(w_i),
     * 从而在最小二乘准则 VᵀPV = min 中放大边缘点的约束力。
     *
     * @param points 控制点列表
     * @param weights 逐点权重向量（长度 n，缺省为空则退化为等权）
     * @return 平差结果
     */
    AdjustmentResult solveWeightedAdjustment(const QVector<ControlPoint>& points,
                                              const QVector<double>& weights = {});

    /**
     * @brief 边缘权重检测（Sobel 算子）
     *
     * 对源影像进行 Sobel 梯度检测，在每个控制点位置计算局部窗口内的
     * 平均梯度幅值，超过阈值的视为边缘点，赋予高权重。
     *
     * 算法: 3×3 Sobel 核 → 梯度幅值 G = √(Gx² + Gy²)
     *       控制点局部窗口(windowSize×windowSize)内平均 G 与阈值比较
     *
     * 权重分配策略（二值硬权重）:
     *   · 边缘点: weight = edgeWeight (默认 5)
     *   · 非边缘点: weight = 1
     *
     * 设计意图: 边缘是影像中纹理最丰富、匹配最可靠的位置,
     *   增大边缘点的权重可显著提升配准精度。
     *
     * @param image 源影像（灰度图）
     * @param points 控制点列表（源影像坐标）
     * @param edgeWeight 边缘点权重（默认 5）
     * @param nmsRadius 非极大值抑制半径（像素），避免角点聚集
     * @return 逐点权重向量
     */
    static QVector<double> detectEdgeWeights(const QImage& image,
                                              const QVector<ControlPoint>& points,
                                              double edgeWeight = 5.0,
                                              int windowSize = 7,
                                              double edgeThreshold = 30.0);

    /**
     * @brief 设置已解算的参数
     *
     * 用于从文件或缓存中恢复之前保存的变换参数。
     * @param params 变换参数向量
     */
    void setParameters(const QVector<double>& params);
    const QVector<double>& parameters() const { return m_params; }  // 获取当前变换参数

private:
    GeoModelType  m_type;             // 几何变换模型类型
    QVector<double> m_params;         // 变换参数向量（由平差解算或外部加载得到）
    double m_meanSX = 0.0;            // 多项式模型的源坐标 X 中心化均值（提高数值稳定性）
    double m_meanSY = 0.0;            // 多项式模型的源坐标 Y 中心化均值

    // ==================== 辅助方法 ====================

    /**
     * @brief 构建设计矩阵 A
     *
     * 设计矩阵大小为 2n × m（n=控制点数, m=参数个数）。
     * 行排列方式: [方程1_x, 方程1_y, 方程2_x, 方程2_y, ...]
     * 每两行对应一个控制点，分别对应 X 和 Y 方向的观测方程系数。
     *
     * @param points 控制点列表
     * @return 行优先存储的设计矩阵向量
     */
    QVector<double> buildDesignMatrix(const QVector<ControlPoint>& points);

    /**
     * @brief 构建观测向量 L
     *
     * 观测向量大小为 2n × 1，排列方式与设计矩阵对应。
     * 元素为控制点在参考影像上的坐标 (refX, refY) 交替排列。
     *
     * @param points 控制点列表
     * @return 观测向量
     */
    QVector<double> buildObservationVector(const QVector<ControlPoint>& points);

    /**
     * @brief 矩阵乘法
     *
     * C = A × B，其中 A(m×n), B(n×p), 结果 C(m×p)
     * 矩阵均按行优先存储（row-major order）。
     */
    static QVector<double> matrixMultiply(
        const QVector<double>& A, int m, int n,
        const QVector<double>& B, int p);

    /**
     * @brief 矩阵转置
     *
     * B = Aᵀ，其中 A(m×n)，结果 B(n×m)
     */
    static QVector<double> matrixTranspose(const QVector<double>& A, int m, int n);

    /**
     * @brief 高斯列主元消去法解线性方程组
     *
     * 求解 AX = B，其中 A 为 n×n 方阵，B 为 n×1 向量。
     * 采用列主元选取策略以提高数值稳定性。
     * 注意：传入的 A 和 B 会被修改（消元过程就地操作）。
     *
     * @param[in,out] A 系数矩阵（n×n，会被修改）
     * @param[in] n 矩阵阶数
     * @param[in,out] B 右端向量（n×1，会被修改）
     * @param[out] X 解向量（n×1）
     * @return true 解算成功，false 矩阵奇异
     */
    static bool gaussianElimination(QVector<double>& A, int n,
                                    QVector<double>& B, QVector<double>& X);

    // 计算单个控制点的前向变换坐标（仿射模型专用）
    QPointF forwardPointAffine(double sx, double sy) const;
    // 计算单个控制点的前向变换坐标（二次多项式模型专用）
    QPointF forwardPointPoly2(double sx, double sy) const;
};
