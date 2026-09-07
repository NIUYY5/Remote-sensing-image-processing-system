// ============================================================================
// 文件: geomodel.cpp
// 功能: 几何变换模型 — 自编最小二乘平差解算
// 包含: 仿射变换、二次多项式模型的 正/逆变换 和 最小二乘参数解算
// 数学原理: L + V = A·X,  VᵀPV = min, 法方程 AᵀAX = AᵀL
// 特点: 纯手写矩阵运算, 不依赖任何第三方线性代数库
// ============================================================================

#include "geomodel.h"
#include <cmath>
#include <algorithm>

GeoModel::GeoModel() : m_type(GeoModelType::Affine) {}

GeoModel::~GeoModel() {}

void GeoModel::setModelType(GeoModelType type)
{
    m_type = type;
    m_params.clear();
}

int GeoModel::parameterCount() const
{
    // 仿射: 6参数 (a0,a1,a2,b0,b1,b2)
    // 二次多项式: 12参数 (a0~a5,b0~b5)
    return (m_type == GeoModelType::Affine) ? 6 : 12;
}

int GeoModel::minControlPoints() const
{
    // 仿射变换: 6个参数, 每个控制点提供2个方程 → 最少3个点
    // 二次多项式: 12个参数 → 最少6个点
    return parameterCount() / 2;
}

void GeoModel::setParameters(const QVector<double>& params)
{
    m_params = params;
}

// ========== 前向变换（源点坐标 → 参考点坐标） ==========
// 调用对应模型的正算函数, 将源影像上的点映射到参考影像坐标系
//   仿射模型: forwardPointAffine  — 线性变换（平移+旋转+缩放+剪切）
//   二次多项式模型: forwardPointPoly2 — 非线性变换（可处理影像畸变）
QPointF GeoModel::forwardTransform(double sx, double sy) const
{
    if (m_params.size() < parameterCount())
        return QPointF(sx, sy);

    switch (m_type) {
    case GeoModelType::Affine:
        return forwardPointAffine(sx, sy);
    case GeoModelType::Polynomial2nd:
        return forwardPointPoly2(sx, sy);
    default:
        return QPointF(sx, sy);
    }
}

// ========== 逆向变换（参考点坐标 → 源点坐标） ==========
// 仿射模型: 采用解析求逆, 利用 3×3 增广矩阵求逆公式直接计算
//   将仿射变换写为齐次形式 [R;1] = M·[S;1], 其中 M = [a1 a2 a0; b1 b2 b0; 0 0 1]
//   逆变换: S = M⁻¹·R, 即对 2×2 线性部分求逆后平移回代
//
// 二次多项式模型: 采用牛顿迭代法求解非线性方程组
//   方程组: f1(sx,sy) = Fx(sx,sy) - rx = 0
//           f2(sx,sy) = Fy(sx,sy) - ry = 0
//   迭代公式: S_{k+1} = S_k - J⁻¹·F(S_k), 其中 J 为雅可比矩阵
//   初值取参考点坐标(中心化后), 迭代至收敛或达到最大次数(20次)
QPointF GeoModel::inverseTransform(double rx, double ry) const
{
    if (m_params.size() < parameterCount())
        return QPointF(rx, ry);

    switch (m_type) {
    case GeoModelType::Affine:
    {
        // 仿射矩阵 A_3x3 = [a1 a2 a0; b1 b2 b0; 0 0 1]
        // 求逆: inv(A_3x3) * [rx; ry; 1]
        double a1 = m_params[1], a2 = m_params[2], a0 = m_params[0];
        double b1 = m_params[4], b2 = m_params[5], b0 = m_params[3];

        // 行列式: det = a1*b2 - a2*b1
        double det = a1 * b2 - a2 * b1;
        if (std::abs(det) < 1e-15)
            return QPointF(rx, ry);

        double invDet = 1.0 / det;
        // inv_A_2x2 = [b2/det, -a2/det; -b1/det, a1/det]
        // inv_translation = -inv_A_2x2 * [a0; b0]
        double sx =  invDet * ( b2 * (rx - a0) - a2 * (ry - b0));
        double sy =  invDet * (-b1 * (rx - a0) + a1 * (ry - b0));
        return QPointF(sx, sy);
    }
    case GeoModelType::Polynomial2nd:
    {
        double sx = rx, sy = ry;
        if (m_meanSX != 0.0 || m_meanSY != 0.0) {
            sx -= m_meanSX;
            sy -= m_meanSY;
        }
        for (int iter = 0; iter < 20; ++iter) {
            double sx2 = sx * sx, sy2 = sy * sy, sxy = sx * sy;
            // 计算函数值
            double f1 = m_params[0] + m_params[1]*sx + m_params[2]*sy
                      + m_params[3]*sx2 + m_params[4]*sxy + m_params[5]*sy2 - rx;
            double f2 = m_params[6] + m_params[7]*sx + m_params[8]*sy
                      + m_params[9]*sx2 + m_params[10]*sxy + m_params[11]*sy2 - ry;
            // 雅可比矩阵 J = [∂f1/∂sx, ∂f1/∂sy; ∂f2/∂sx, ∂f2/∂sy]
            // ∂f1/∂sx = a1 + 2*a3*sx + a4*sy
            // ∂f1/∂sy = a2 + a4*sx + 2*a5*sy
            double j11 = m_params[1] + 2.0*m_params[3]*sx + m_params[4]*sy;
            double j12 = m_params[2] + m_params[4]*sx + 2.0*m_params[5]*sy;
            double j21 = m_params[7] + 2.0*m_params[9]*sx + m_params[10]*sy;
            double j22 = m_params[8] + m_params[10]*sx + 2.0*m_params[11]*sy;
            // 求解 J * Δ = -F   →   Δ = J⁻¹ * (-F)
            double detJ = j11 * j22 - j12 * j21;
            if (std::abs(detJ) < 1e-15) break;
            double invDet = 1.0 / detJ;
            double dsx = invDet * (-j22 * f1 + j12 * f2);
            double dsy = invDet * ( j21 * f1 - j11 * f2);
            sx += dsx;
            sy += dsy;
            // NaN/Inf 保护: 雅可比病态时迭代发散, 立即终止
            if (!std::isfinite(sx) || !std::isfinite(sy) ||
                std::abs(sx) > 1e10 || std::abs(sy) > 1e10)
                return QPointF(rx, ry);
            if (std::abs(dsx) < 1e-8 && std::abs(dsy) < 1e-8)
                break;
        }
        if (m_meanSX != 0.0 || m_meanSY != 0.0)
            return QPointF(sx + m_meanSX, sy + m_meanSY);
        return QPointF(sx, sy);
    }
    default:
        return QPointF(rx, ry);
    }
}

// ========== 前向仿射变换 ==========
// 仿射变换是最常用的几何变换模型, 包含6个参数:
//   rx = a0 + a1·sx + a2·sy     (x 方向: 平移 a0 + 线性组合 a1,a2)
//   ry = b0 + b1·sx + b2·sy     (y 方向: 平移 b0 + 线性组合 b1,b2)
// 矩阵形式: [rx] = [a0] + [a1 a2]·[sx]
//           [ry]   [b0]   [b1 b2] [sy]
// 几何含义: a0/b0 = 平移量, a1/b2 = 缩放, a2/b1 = 旋转+剪切
QPointF GeoModel::forwardPointAffine(double sx, double sy) const
{
    double rx = m_params[0] + m_params[1] * sx + m_params[2] * sy;
    double ry = m_params[3] + m_params[4] * sx + m_params[5] * sy;
    return QPointF(rx, ry);
}

// ========== 前向二次多项式变换 ==========
// 二次多项式包含12个参数, 能描述更复杂的几何畸变:
//   rx = a0 + a1·sx + a2·sy + a3·sx² + a4·sx·sy + a5·sy²
//   ry = b0 + b1·sx + b2·sy + b3·sx² + b4·sx·sy + b5·sy²
// 坐标中心化说明:
//   当 m_meanSX/m_meanSY 非零时, 先将源坐标减去均值再代入多项式,
//   目的是改善法方程的条件数, 提高数值稳定性
QPointF GeoModel::forwardPointPoly2(double sx, double sy) const
{
    double sx2, sy2, sxy;
    if (m_meanSX != 0.0 || m_meanSY != 0.0) {
        sx -= m_meanSX;
        sy -= m_meanSY;
    }
    sx2 = sx * sx; sy2 = sy * sy; sxy = sx * sy;
    double rx = m_params[0]  + m_params[1]*sx  + m_params[2]*sy
              + m_params[3]*sx2 + m_params[4]*sxy + m_params[5]*sy2;
    double ry = m_params[6]  + m_params[7]*sx  + m_params[8]*sy
              + m_params[9]*sx2 + m_params[10]*sxy + m_params[11]*sy2;
    return QPointF(rx, ry);
}

// ========== 构建设计矩阵 A (行优先存储) ==========
// 平差模型: L + V = A·X
//   每行对应一个观测方程, 每列对应一个待求参数
//   每个控制点提供2个方程 (X 和 Y 方向), 因此矩阵共 2n 行、m 列
//
// 仿射模型 (m=6), 第 i 个控制点贡献的 2 行:
//   [1, sx_i, sy_i,    0,    0,    0]   ← X 方程: rx = a0 + a1·sx + a2·sy
//   [0,    0,    0,    1, sx_i, sy_i]   ← Y 方程: ry = b0 + b1·sx + b2·sy
//
// 二次多项式模型 (m=12), 第 i 个控制点贡献的 2 行:
//   [1, sx, sy, sx², sxy, sy²,  0,  0,  0,   0,    0,   0]   ← X 方程
//   [0,  0,  0,   0,    0,   0,  1, sx, sy, sx², sxy, sy²]   ← Y 方程
//   注意: 二次多项式使用中心化坐标 (sx - meanSX, sy - meanSY)
QVector<double> GeoModel::buildDesignMatrix(const QVector<ControlPoint>& points)
{
    int n = points.size();
    int m = parameterCount();
    // 矩阵按行优先存储: 2n 行, m 列
    QVector<double> A(2 * n * m, 0.0);

    for (int i = 0; i < n; ++i) {
        double sx = points[i].srcX;
        double sy = points[i].srcY;
        if (m_type == GeoModelType::Polynomial2nd) {
            sx -= m_meanSX;
            sy -= m_meanSY;
        }
        int row0 = 2 * i;       // 第i个点的 X 方程行
        int row1 = 2 * i + 1;   // 第i个点的 Y 方程行

        if (m_type == GeoModelType::Affine) {
            // X 方程: rx = a0 + a1*sx + a2*sy
            A[row0 * m + 0] = 1.0;
            A[row0 * m + 1] = sx;
            A[row0 * m + 2] = sy;
            // Y 方程: ry = b0 + b1*sx + b2*sy
            A[row1 * m + 3] = 1.0;
            A[row1 * m + 4] = sx;
            A[row1 * m + 5] = sy;
        } else {
            double sx2 = sx * sx, sy2 = sy * sy, sxy = sx * sy;
            // X 方程: 参数 a0~a5
            A[row0 * m + 0] = 1.0;
            A[row0 * m + 1] = sx;
            A[row0 * m + 2] = sy;
            A[row0 * m + 3] = sx2;
            A[row0 * m + 4] = sxy;
            A[row0 * m + 5] = sy2;
            // Y 方程: 参数 b0~b5
            A[row1 * m + 6]  = 1.0;
            A[row1 * m + 7]  = sx;
            A[row1 * m + 8]  = sy;
            A[row1 * m + 9]  = sx2;
            A[row1 * m + 10] = sxy;
            A[row1 * m + 11] = sy2;
        }
    }
    return A;
}

// ========== 构建观测向量 L ==========
// 将控制点的参考影像坐标按顺序排列为列向量
// 排列方式: L = [rx₀, ry₀, rx₁, ry₁, ..., rx_{n-1}, ry_{n-1}]ᵀ
// 与设计矩阵 A 的行排列一一对应, 即第 i 个控制点的 X/Y 观测值对应 A 的第 2i / 2i+1 行
QVector<double> GeoModel::buildObservationVector(const QVector<ControlPoint>& points)
{
    int n = points.size();
    QVector<double> L(2 * n, 0.0);
    for (int i = 0; i < n; ++i) {
        L[2 * i]     = points[i].refX;
        L[2 * i + 1] = points[i].refY;
    }
    return L;
}

// ========== 矩阵乘法 C = A × B ==========
// A: m×n, B: n×p → C: m×p, 所有矩阵均按行优先 (row-major) 存储
// 三重循环优化策略:
//   外层循环 i 遍历 A 的行, 中层循环 k 遍历 A 的列 (同时也是 B 的行),
//   内层循环 j 遍历 B 的列。
// 优化: 当 A[i][k] 接近零时跳过内层循环, 减少浮点乘加运算次数。
//   因为设计矩阵 A 含有大量零元素 (每个控制点只有 m/2 个非零列),
//   这种 IKJ 循环顺序配合零值跳过能显著加速法方程构建。
QVector<double> GeoModel::matrixMultiply(
    const QVector<double>& A, int m, int n,
    const QVector<double>& B, int p)
{
    QVector<double> C(m * p, 0.0);
    for (int i = 0; i < m; ++i) {
        for (int k = 0; k < n; ++k) {
            double aik = A[i * n + k];
            if (std::abs(aik) < 1e-20) continue;
            for (int j = 0; j < p; ++j) {
                C[i * p + j] += aik * B[k * p + j];
            }
        }
    }
    return C;
}

// ========== 矩阵转置 B = Aᵀ ==========
// A: m×n 矩阵 (行优先存储) → B: n×m 矩阵 (行优先存储)
// 转置规则: B[j][i] = A[i][j], 即 B[j * m + i] = A[i * n + j]
// 用于法方程构建: AᵀA 需要先对 A 转置再做乘法
QVector<double> GeoModel::matrixTranspose(const QVector<double>& A, int m, int n)
{
    QVector<double> B(n * m, 0.0);
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) {
            B[j * m + i] = A[i * n + j];
        }
    }
    return B;
}

// ========== 高斯列主元消去法 ==========
// 求解线性方程组 A·X = B, A 为 n×n 方阵
// 原理: 选主元 → 行交换 → 消元 → 回代, 将 A 化为上三角矩阵后求解
// 步骤说明:
//   1.【列主元选择】对第 k 列, 在第 k~n-1 行中找出 |A[i][k]| 最大的行 pivot
//      目的是避免小主元导致数值不稳定, 提高求解精度
//   2.【行交换】将第 k 行与第 pivot 行交换 (A 和 B 同时交换), 使主元移至对角线
//   3.【消元】对第 i=k+1..n-1 行, 计算消元因子 factor = A[i][k]/A[k][k]
//      然后: A[i][j] -= factor·A[k][j], j=k+1..n-1
//            B[i]    -= factor·B[k]
//      将第 k 列对角线以下的元素归零
//   4.【回代】从最后一行开始反向求解:
//      X[n-1] = B[n-1] / A[n-1][n-1]
//      X[i] = (B[i] - Σ_{j=i+1}^{n-1} A[i][j]·X[j]) / A[i][i], i=n-2..0
bool GeoModel::gaussianElimination(QVector<double>& A, int n,
                                    QVector<double>& B, QVector<double>& X)
{
    X.resize(n);
    // ---- 消元过程 ----
    for (int k = 0; k < n; ++k) {
        // 列主元选择: 找第k列中绝对值最大的行
        int pivot = k;
        double maxVal = std::abs(A[k * n + k]);
        for (int i = k + 1; i < n; ++i) {
            double val = std::abs(A[i * n + k]);
            if (val > maxVal) {
                maxVal = val;
                pivot = i;
            }
        }
        // 矩阵奇异判断
        if (maxVal < 1e-15)
            return false;
        // 交换行
        if (pivot != k) {
            for (int j = k; j < n; ++j) {
                std::swap(A[k * n + j], A[pivot * n + j]);
            }
            std::swap(B[k], B[pivot]);
        }
        // 消元: 将第k列 k+1 行以下归零
        for (int i = k + 1; i < n; ++i) {
            double factor = A[i * n + k] / A[k * n + k];
            A[i * n + k] = 0.0;  // 显式归零
            for (int j = k + 1; j < n; ++j) {
                A[i * n + j] -= factor * A[k * n + j];
            }
            B[i] -= factor * B[k];
        }
    }
    // ---- 回代过程 ----
    for (int i = n - 1; i >= 0; --i) {
        double sum = B[i];
        for (int j = i + 1; j < n; ++j) {
            sum -= A[i * n + j] * X[j];
        }
        X[i] = sum / A[i * n + i];
    }
    return true;
}

// ========== 最小二乘平差（等权版本） ==========
// 委托给加权平差, 传入空权重即退化为等权 P = I
AdjustmentResult GeoModel::solveAdjustment(const QVector<ControlPoint>& points)
{
    return solveWeightedAdjustment(points, {});
}

// ========== 加权最小二乘平差 ==========
// 在等权平差的基础上引入逐点权重矩阵 P = diag(w₀,w₀, w₁,w₁, ...)
//
// 加权法方程:
//   (AᵀPA)·X = AᵀPL
//
// 实现方式（等价变换）:
//   将 A 的每行和 L 的每行乘以 sqrt(w_i), 再用等权法方程解算
//   A_w[i, :] = sqrt(w_i) * A[i, :]
//   L_w[i]    = sqrt(w_i) * L[i]
//   此时 A_wᵀA_w = AᵀPA,  A_wᵀL_w = AᵀPL
//
// 权重策略:
//   · 边缘点 (w=5): 5倍约束力, 配准精度优先拟合边缘区域
//   · 非边缘点 (w=1): 基准约束力, 保证整体几何一致性
//   · 空权重: 退化为等权平差 (P = I)
//
// 平差流程与 solveAdjustment 完全一致, 仅额外增加了 A/L 的逐行加权步骤
// ============================================================================
AdjustmentResult GeoModel::solveWeightedAdjustment(const QVector<ControlPoint>& points,
                                                    const QVector<double>& weights)
{
    AdjustmentResult result;
    int n = points.size();
    int m = parameterCount();

    if (n < minControlPoints()) {
        result.valid = false;
        return result;
    }

    // 等权退化: 空向量或长度不匹配时, 全部置为 1.0
    bool hasWeights = (weights.size() == n);

    // ---- 坐标中心化 (仅二次多项式模型) ----
    m_meanSX = 0.0; m_meanSY = 0.0;
    if (m_type == GeoModelType::Polynomial2nd) {
        for (int i = 0; i < n; ++i) {
            m_meanSX += points[i].srcX;
            m_meanSY += points[i].srcY;
        }
        m_meanSX /= n;
        m_meanSY /= n;
    }

    // ---- 步骤1: 构建设计矩阵 A 和 观测向量 L ----
    QVector<double> A_flat = buildDesignMatrix(points);
    int rows = 2 * n;
    QVector<double> L = buildObservationVector(points);

    // ---- 步骤2: 权重应用 —— 将 A 和 L 逐行缩放 ----
    // 对第 i 个控制点的 X/Y 两行同时乘以 sqrt(w_i)
    // 等价于最小化 VᵀPV, 其中 P 为对角权矩阵
    if (hasWeights) {
        for (int i = 0; i < n; ++i) {
            double w = weights[i];
            if (w <= 0.0) w = 1.0;        // 保护: 非正权重退化为 1
            double sw = std::sqrt(w);
            int r0 = 2 * i;                // X 方程行
            int r1 = 2 * i + 1;            // Y 方程行
            // 缩放 A 矩阵的对应两行 (行优先存储)
            for (int j = 0; j < m; ++j) {
                A_flat[r0 * m + j] *= sw;
                A_flat[r1 * m + j] *= sw;
            }
            // 缩放 L 向量的对应两个元素
            L[r0] *= sw;
            L[r1] *= sw;
        }
    }

    // ---- 步骤3: 法方程 AᵀA (m×m) 和 AᵀL (m×1) ----
    QVector<double> AT = matrixTranspose(A_flat, rows, m);
    QVector<double> ATA = matrixMultiply(AT, m, rows, A_flat, m);
    QVector<double> ATL = matrixMultiply(AT, m, rows, L, 1);

    // ---- 步骤4: 高斯消元求解 ----
    QVector<double> X;
    if (!gaussianElimination(ATA, m, ATL, X)) {
        result.valid = false;
        return result;
    }

    m_params = X;
    result.parameters = X;

    // ---- 步骤5: 残差计算 V = AX - L ----
    QVector<ControlPoint> ptsWithResidual = points;
    double sumVx2 = 0.0, sumVy2 = 0.0;
    double maxErrX = 0.0, maxErrY = 0.0;

    for (int i = 0; i < n; ++i) {
        QPointF predicted = forwardTransform(points[i].srcX, points[i].srcY);
        double vx = predicted.x() - points[i].refX;
        double vy = predicted.y() - points[i].refY;
        ptsWithResidual[i].resX = vx;
        ptsWithResidual[i].resY = vy;

        // 加权残差: 高权重点的残差对精度指标贡献更大
        double wi = hasWeights ? weights[i] : 1.0;
        sumVx2 += wi * vx * vx;
        sumVy2 += wi * vy * vy;
        maxErrX = std::max(maxErrX, std::abs(vx));
        maxErrY = std::max(maxErrY, std::abs(vy));
    }
    result.points = ptsWithResidual;

    // ---- 步骤6: 精度评定 ----
    int nObs = 2 * n;
    int nParams = m;
    int redundancy = nObs - nParams;

    double VTV = sumVx2 + sumVy2;
    result.sigma0 = (redundancy > 0) ? std::sqrt(VTV / redundancy) : 0.0;
    result.rmseX = std::sqrt(sumVx2 / n);
    result.rmseY = std::sqrt(sumVy2 / n);
    result.rmseTotal = std::sqrt((result.rmseX * result.rmseX +
                                   result.rmseY * result.rmseY) / 2.0);
    result.maxErrorX = maxErrX;
    result.maxErrorY = maxErrY;
    result.valid = true;

    return result;
}

// ============================================================================
// detectEdgeWeights — Sobel 边缘权重检测（二值硬权重）
//
// 原理:
//   在遥感影像中, 边缘（道路交叉口、建筑转角、河流交汇等）是纹理
//   最丰富、定位最精确的位置。对这些位置的控制点赋予更高权重,
//   可以在最小二乘平差中强制优化向边缘区域倾斜, 提升配准精度。
//
// 算法步骤:
//   1. 对输入灰度图, 用 3×3 Sobel 核计算每像素梯度幅值:
//        Gx = [-1 0 +1]     Gy = [-1 -2 -1]
//             [-2 0 +2]           [ 0  0  0]
//             [-1 0 +1]           [+1 +2 +1]
//        G(x,y) = √(Gx² + Gy²)
//
//   2. 对每个控制点, 在其周围 windowSize×windowSize 窗口内求平均 G
//
//   3. 平均 G ≥ edgeThreshold → 边缘点 → weight = edgeWeight(5)
//      平均 G <  edgeThreshold → 非边缘 → weight = 1
//
// 参数建议:
//   · windowSize=7: 平衡计算效率和局部统计稳定性
//   · edgeThreshold=30: 对8-bit灰度图, 区分边缘/平坦区域的合理阈值
//   · edgeWeight=5:  边缘:非边缘 = 5:1, 足够显著但不至于过度约束
// ============================================================================
QVector<double> GeoModel::detectEdgeWeights(const QImage& image,
                                             const QVector<ControlPoint>& points,
                                             double edgeWeight,
                                             int windowSize,
                                             double edgeThreshold)
{
    int n = points.size();
    QVector<double> weights(n, 1.0);  // 默认全部非边缘, weight = 1

    int iw = image.width();
    int ih = image.height();
    if (iw < 3 || ih < 3 || n == 0) return weights;

    const int halfW = windowSize / 2;

    // 转为 8-bit 灰度 (若源图为彩色则取亮度分量)
    QImage gray;
    if (image.format() == QImage::Format_Grayscale8 ||
        image.format() == QImage::Format_Indexed8) {
        gray = image;
    } else {
        gray = image.convertToFormat(QImage::Format_Grayscale8);
    }

    const uchar* bits = gray.constBits();
    int bytesPerLine = gray.bytesPerLine();

    // ---- Sobel 梯度计算 (逐像素, 只对控制点邻域计算以节省时间) ----
    // 使用 uchar 范围 [0,255] 直接计算, 无需归一化
    for (int p = 0; p < n; ++p) {
        int cx = static_cast<int>(points[p].srcX + 0.5);
        int cy = static_cast<int>(points[p].srcY + 0.5);

        // 边界检查
        if (cx - halfW < 1 || cx + halfW >= iw - 1 ||
            cy - halfW < 1 || cy + halfW >= ih - 1)
            continue;  // 影像边缘的控制点保持权重 1

        double sumG = 0.0;
        int count = 0;

        for (int dy = -halfW; dy <= halfW; ++dy) {
            for (int dx = -halfW; dx <= halfW; ++dx) {
                int x = cx + dx;
                int y = cy + dy;

                const uchar* row0 = bits + (y - 1) * bytesPerLine;
                const uchar* row1 = bits +  y      * bytesPerLine;
                const uchar* row2 = bits + (y + 1) * bytesPerLine;

                int p00 = row0[x - 1], p01 = row0[x], p02 = row0[x + 1];
                int p10 = row1[x - 1], p11 = row1[x], p12 = row1[x + 1];
                int p20 = row2[x - 1], p21 = row2[x], p22 = row2[x + 1];

                int gx = (-p00 + p02) + 2 * (-p10 + p12) + (-p20 + p22);
                int gy = (+p00 + 2*p01 + p02) + (-p20 - 2*p21 - p22);

                sumG += std::sqrt(static_cast<double>(gx * gx + gy * gy));
                count++;
            }
        }

        double avgG = sumG / count;
        if (avgG >= edgeThreshold) {
            weights[p] = edgeWeight;
            // 边缘点: weight = 5（默认）
        }
        // 非边缘点保持默认 weight = 1
    }

    return weights;
}
