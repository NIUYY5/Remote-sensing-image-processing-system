#pragma once

#include <QImage>
#include <QPointF>
#include <QVector>
#include <QPair>
#include <QRect>
#include <QString>
#include "controlpoint.h"

/**
 * @brief 自动匹配候选点结构体
 *
 * 存储一对自动匹配得到的同名点候选，附带 NCC 归一化互相关得分。
 */
struct MatchPoint {
    QPointF srcPt;      // 源影像上的特征点坐标
    QPointF refPt;      // 参考影像上的同名候选点坐标
    double  nccScore;   // NCC 归一化互相关匹配得分（越大越相似）
};

/**
 * @brief 重叠区域计算结果结构体
 *
 * 存储两幅影像的空间重叠分析结果，包括像素坐标和地理坐标两个层级的范围。
 * 可用于导出重叠信息、约束特征点提取范围、或作为独立计算的输出结果。
 */
struct OverlapResult {
    bool   valid = false;       // 是否存在有效重叠
    // --- 像素坐标重叠区域 ---
    QRect  srcROI;              // 源影像上的重叠区域（原始像素坐标）
    QRect  refROI;              // 参考影像上的重叠区域（原始像素坐标）
    // --- 地理坐标范围 ---
    double geoXmin = 0, geoXmax = 0;  // 重叠区地理范围 X
    double geoYmin = 0, geoYmax = 0;  // 重叠区地理范围 Y
    double srcGeoXmin=0, srcGeoXmax=0, srcGeoYmin=0, srcGeoYmax=0;  // 源影像地理四点
    double refGeoXmin=0, refGeoXmax=0, refGeoYmin=0, refGeoYmax=0;  // 参考影像地理四点
};

/**
 * @brief 自动匹配类
 *
 * 基于 Harris 角点检测 + NCC 归一化互相关匹配 + RANSAC 单应矩阵验证的
 * 全自动同名点匹配实现。采用单应矩阵（8参数透视变换）替代仿射变换，
 * 能够更好地适应原始卫星影像与 DOM 之间的复杂几何关系。
 *
 * 整体处理流程：
 *
 * 1. 将源/参考影像转为灰度图
 * 2. 降采样至最长边 ≤1500px 以加速处理
 * 3. 对源/参考影像分别提取 Harris 角点作为待匹配特征点
 * 4. 通过地理重叠区域约束搜索范围（若存在 GeoTransform）
 * 5. 前向 NCC 匹配：源角点 → 参考角点搜索最佳 NCC（含比率测试）
 * 6. 反向 NCC 匹配：参考角点 → 源角点搜索最佳 NCC（含比率测试）
 * 7. 交叉验证：保留前向/反向一致的同名点对
 * 8. 亚像素精度优化（二次曲线拟合法）
 * 9. RANSAC 单应矩阵验证，通过 DLT（直接线性变换）估计 8 参数透视模型
 * 10. 最小二乘重拟合 + 内点筛选
 * 11. 去重处理 + 坐标缩放回原始分辨率
 */
class AutoMatch
{
public:
    AutoMatch();

    /**
     * @brief 自动匹配参数配置结构体
     */
    struct Config {
        int    maxCorners        = 2000;   // Harris 角点最大提取数量
        int    patchRadius       = 15;     // NCC 匹配模板窗口半径（像素）
        int    searchRadius      = 300;    // 参考影像搜索窗口半径（像素）
        double nccThreshold      = 0.25;   // NCC 匹配阈值，低于此值的匹配被剔除
        double nccRatioThreshold = 1.00;   // NCC 比值检验阈值（最优/次优），剔除模糊匹配
        double harrisK           = 0.04;   // Harris 角点检测参数 κ（通常 0.04~0.06）
        double harrisTopFraction = 15.0;   // 保留 Harris 响应值最高的前百分之多少的角点
        double nmsRadius         = 2.5;    // 非极大值抑制半径（像素），避免角点聚集
        double ransacDist        = 8.0;    // RANSAC 内点距离阈值（像素）
    };

    void setConfig(const Config& cfg) { m_cfg = cfg; }  // 设置匹配参数
    const Config& config() const { return m_cfg; }       // 获取当前匹配参数

    QString lastError() const { return m_lastError; }    // 获取最后一条错误信息

    /**
     * @brief 执行自动匹配
     *
     * @param srcImage 源影像（待配准影像）
     * @param refImage 参考影像（基准影像）
     * @param srcGT 源影像地理仿射变换参数（可选，用于约束搜索范围），可为 nullptr
     * @param refGT 参考影像地理仿射变换参数（可选），可为 nullptr
     * @return 匹配成功的控制点列表（已验证的内点）
     */
    QVector<ControlPoint> match(const QImage& srcImage, const QImage& refImage,
                                const double* srcGT = nullptr,
                                const double* refGT = nullptr);

    /**
     * @brief 独立计算两幅影像的重叠区域
     * @return OverlapResult 包含重叠区域在像素和地理坐标下的范围
     *
     * 与 match() 内部调用的 computeGeoOverlap 功能相同，
     * 但提供了完整的返回值结构体，支持导出和分析。
     */
    OverlapResult computeOverlap(int srcW, int srcH, const double* srcGT,
                                  int refW, int refH, const double* refGT);

    /**
     * @brief 独立提取影像 Harris 角点特征
     * @param gray 输入灰度影像
     * @param maxCorners 最大角点数量
     * @param roi 感兴趣区域（空矩形=全图）
     * @return 角点坐标列表
     *
     * 此方法将私有 extractHarrisCorners 暴露为公共接口，
     * 调用者可直接获取特征点坐标并在影像上标注显示。
     */
    QVector<QPointF> extractFeatures(const QImage& gray, int maxCorners = 2000,
                                      const QRect& roi = QRect());

    /**
     * @brief 计算地理重叠区域
     *
     * 利用两幅影像的地理仿射变换参数，计算它们在投影坐标系下的
     * 空间重叠范围，从而将搜索范围限定在重叠区域内。
     *
     * @return true 如果存在重叠区域
     */
    bool computeGeoOverlap(int srcW, int srcH, const double* srcGT,
                           int refW, int refH, const double* refGT,
                           QRect& srcROI, QRect& refROI);

    /**
     * @brief 提取 Harris 角点
     *
     * 计算每个像素的 Harris 响应值 R = det(M) - κ · trace(M)²，
     * 进行非极大值抑制（NMS），保留响应值最高的 Top 角点。
     *
     * @param gray 输入灰度影像
     * @param[out] corners 检测到的角点坐标列表
     * @param skipBorder 跳过影像边框像素宽度
     * @param roi 感兴趣区域（仅在区域内检测），空矩形表示全图
     */
    void extractHarrisCorners(const QImage& gray, QVector<QPointF>& corners,
                              int skipBorder, const QRect& roi);

private:
    /**
     * @brief 计算归一化互相关 (NCC)
     *
     * 在灰度影像 grayA 的 ptA 位置取模板窗口，
     * 与灰度影像 grayB 的 ptB 位置的同样大小窗口计算 NCC 系数。
     * NCC = Σ((A-Ā)(B-B̄)) / sqrt(Σ(A-Ā)² · Σ(B-B̄)²)
     * 结果范围 [-1, 1]，越接近 1 表示越相似。
     */
    double computeNCC(const QImage& grayA, QPointF ptA,
                      const QImage& grayB, QPointF ptB);

    /**
     * @brief 亚像素精度匹配优化
     *
     * 在整数像素最佳匹配位置附近，使用二次曲面拟合法拟合 NCC 响应曲面，
     * 求取曲面顶点作为亚像素精度的匹配位置。
     *
     * @param ptB [in,out] 输入为整数像素最佳匹配位置，输出为亚像素优化后的位置
     * @return 优化后的 NCC 得分
     */
    double refineMatchSubPixel(const QImage& grayA, const QPointF& ptA,
                                const QImage& grayB, QPointF& ptB);

    /**
     * @brief 最小二乘单应矩阵参数估计
     *
     * 利用 RANSAC 确认的内点，通过 DLT（直接线性变换）求解单应矩阵的 8 个参数。
     * 单应矩阵模型（h33=1 归一化）:
     *   x' = (h11·x + h12·y + h13) / (h31·x + h32·y + 1)
     *   y' = (h21·x + h22·y + h23) / (h31·x + h32·y + 1)
     *
     * 每个匹配点贡献 2 个方程，至少需要 4 个点求解。
     * 构建法方程 AᵀA·h = Aᵀb，用列主元高斯消去法求解 8×8 系统。
     * 相比仿射变换，单应矩阵能描述透视变形，更适合原始卫星影像↔DOM 等场景。
     */
    void leastSquaresHomography(const QVector<MatchPoint>& inliers,
                                QVector<double>& params);

    /**
     * @brief RANSAC 验证与精化
     *
     * 对初步匹配点进行 RANSAC 随机抽样一致性验证，使用单应矩阵（8参数透视模型）:
     * 1. 随机选取 4 对同名点，通过 DLT 求解单应矩阵 H
     * 2. 计算所有匹配点经 H 投影后的残差，残差 < ransacDist 的记为内点
     * 3. 迭代 N 次后取内点数最多的模型
     * 4. 用所有内点通过最小二乘重算单应矩阵
     * 5. 用精化后的模型再次筛选内点，返回最终结果
     */
    QVector<MatchPoint> validateAndRefine(const QVector<MatchPoint>& matches);

    /**
     * @brief 彩色影像转灰度
     *
     * 将 QImage 转为 8 位灰度图。如果输入已是灰度图则直接返回。
     * 使用标准亮度公式: Gray = 0.299·R + 0.587·G + 0.114·B
     */
    QImage toGray(const QImage& img);

    Config  m_cfg;          // 匹配参数配置
    QString m_lastError;    // 最后一条错误信息
};