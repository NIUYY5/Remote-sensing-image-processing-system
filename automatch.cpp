// ============================================================================
// 文件: automatch.cpp
// 功能: 自动同名点匹配 — Harris 角点检测 + NCC 归一化互相关 + RANSAC 几何验证
// 流程: 降采样 → Harris 角点提取 → 双向 NCC 匹配 → 交叉验证 → 亚像素精化 → RANSAC 提纯
// ============================================================================

#include "automatch.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstring>
#include <QMap>

AutoMatch::AutoMatch()
{
}

// ----------------------------------------------------------------------------
// toGray — 将任意格式图像转为 8 位灰度图
// 转换公式(加权亮度法): Gray = (299·R + 587·G + 114·B) / 1000
// 系数 299/587/114 对应 ITU-R BT.601 标准的人眼亮度感知权重
// 若输入已是 Format_Grayscale8 则直接返回，避免不必要的拷贝
// ----------------------------------------------------------------------------
QImage AutoMatch::toGray(const QImage& img)
{
    if (img.format() == QImage::Format_Grayscale8)
        return img;
    QImage gray(img.width(), img.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* srcLine = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        uchar* dstLine = gray.scanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            QRgb p = srcLine[x];
            dstLine[x] = static_cast<uchar>(
                (299 * qRed(p) + 587 * qGreen(p) + 114 * qBlue(p)) / 1000);
        }
    }
    return gray;
}

// ----------------------------------------------------------------------------
// downsampleIfLarge — 降采样策略
// 当图像最长边超过 maxDim(默认 1500px) 时，等比缩放使最长边等于 maxDim
// 使用 Qt::SmoothTransformation(双线性插值) 保持图像质量
// 降采样大幅减少角点检测和 NCC 匹配的计算量，同时保留足够的结构信息
// ----------------------------------------------------------------------------
static QImage downsampleIfLarge(const QImage& gray, int maxDim)
{
    int w = gray.width(), h = gray.height();
    int maxSide = qMax(w, h);
    if (maxSide <= maxDim)
        return gray;

    double scale = static_cast<double>(maxDim) / maxSide;
    int nw = static_cast<int>(w * scale);
    int nh = static_cast<int>(h * scale);
    return gray.scaled(nw, nh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// ----------------------------------------------------------------------------
// extractHarrisCorners — Harris 角点提取
//
// 算法步骤:
//   1. 用 Sobel 算子(3×3 窗口)计算梯度 Ix, Iy
//   2. 构建积分图(Integral Image)加速 Ix², Iy², Ixy 的窗口求和
//      积分图使得任意半径窗口的求和操作降为 O(1)
//   3. 对每个像素计算 Harris 响应 R = det(M) - k·trace²(M)
//      其中 M = [∑Ix²  ∑Ixy; ∑Ixy  ∑Iy²] 为 2×2 结构张量
//   4. 仅保留 R > 0 的候选点
//   5. 按 R 值降序排序，截取 topFraction · maxCorners 的候选
//   6. 非极大值抑制 NMS: 在 nmsRadius 邻域内保留 R 最大的点
//   7. 可选的 ROI 约束: 只提取指定矩形区域内的角点(用于地理重叠区域优先)
//
// 参数:
//   gray       — 输入灰度图
//   corners    — 输出角点列表(QPointF)
//   skipBorder — 跳过图像边界的像素数
//   roi        — 感兴趣区域(QRect)，为空时提取全图角点
// ----------------------------------------------------------------------------
void AutoMatch::extractHarrisCorners(const QImage& gray,
                                      QVector<QPointF>& corners,
                                      int skipBorder, const QRect& roi)
{
    corners.clear();
    int w = gray.width();
    int h = gray.height();
    if (w < 25 || h < 25) return;

    int winRadius = 3;

    int stride = w + 1;
    QVector<double> intIx2(stride * (h + 1), 0.0);
    QVector<double> intIy2(stride * (h + 1), 0.0);
    QVector<double> intIxy(stride * (h + 1), 0.0);

    for (int y = 1; y < h - 1; ++y) {
        const uchar* row0 = gray.constScanLine(y - 1);
        const uchar* row1 = gray.constScanLine(y);
        const uchar* row2 = gray.constScanLine(y + 1);
        int iy = y + 1;
        for (int x = 1; x < w - 1; ++x) {
            double ix = (row0[x+1] - row0[x-1])
                      + 2.0 * (row1[x+1] - row1[x-1])
                      + (row2[x+1] - row2[x-1]);
            double iy_v = (row0[x-1] - row2[x-1])
                        + 2.0 * (row0[x]   - row2[x])
                        + (row0[x+1] - row2[x+1]);
            double vx2 = ix * ix;
            double vy2 = iy_v * iy_v;
            double vxy = ix * iy_v;

            int ix_pos = x + 1;
            int pid  = iy * stride + ix_pos;
            int up   = (iy - 1) * stride + ix_pos;
            int left = iy * stride + (ix_pos - 1);
            int ul   = (iy - 1) * stride + (ix_pos - 1);

            intIx2[pid] = vx2 + intIx2[up] + intIx2[left] - intIx2[ul];
            intIy2[pid] = vy2 + intIy2[up] + intIy2[left] - intIy2[ul];
            intIxy[pid] = vxy + intIxy[up] + intIxy[left] - intIxy[ul];
        }
    }

    struct Cand { double R; int idx; };
    QVector<Cand> allPositive;
    int estSize = m_cfg.maxCorners * 20;
    if (!roi.isNull()) estSize = qMin(estSize, roi.width() * roi.height());
    allPositive.reserve(estSize);

    int b = skipBorder + winRadius + 1;
    for (int y = b; y <= h - skipBorder - 1; ++y) {
        for (int x = b; x <= w - skipBorder - 1; ++x) {
            int x2 = x + winRadius, y2 = y + winRadius;
            int x1 = x - winRadius - 1, y1 = y - winRadius - 1;
            int p22 = y2 * stride + x2, p21 = y2 * stride + x1;
            int p12 = y1 * stride + x2, p11 = y1 * stride + x1;

            double a = intIx2[p22] - intIx2[p21] - intIx2[p12] + intIx2[p11];
            double c = intIy2[p22] - intIy2[p21] - intIy2[p12] + intIy2[p11];
            double b_xy = intIxy[p22] - intIxy[p21] - intIxy[p12] + intIxy[p11];

            double det = a * c - b_xy * b_xy;
            double trace = a + c;
            double R = det - m_cfg.harrisK * trace * trace;
            if (R <= 0) continue;

            int origX = x - 1, origY = y - 1;
            if (!roi.isNull() && !roi.contains(origX, origY)) continue;
            allPositive.append({R, origY * w + origX});
        }
    }

    int topLimit = qMin(allPositive.size(), m_cfg.maxCorners * (int)m_cfg.harrisTopFraction);
    if (topLimit < 10) return;

    std::nth_element(allPositive.begin(), allPositive.begin() + topLimit, allPositive.end(),
        [](const Cand& a, const Cand& b) { return a.R > b.R; });
    allPositive.resize(topLimit);

    QVector<QPair<double, int>> candidates;
    candidates.reserve(topLimit);
    for (const auto& c : allPositive)
        candidates.append({c.R, c.idx});

    std::sort(candidates.begin(), candidates.end(),
              [](const QPair<double,int>& a, const QPair<double,int>& b) {
                  return a.first > b.first;
              });

    double minDistSq = m_cfg.nmsRadius * m_cfg.nmsRadius;
    QVector<QPair<double,int>> selected;
    selected.reserve(m_cfg.maxCorners);
    for (const auto& c : candidates) {
        double cx = c.second % w;
        double cy = c.second / w;
        bool tooClose = false;
        for (const auto& s : selected) {
            double sx = s.second % w;
            double sy = s.second / w;
            double dx = cx - sx, dy = cy - sy;
            if (dx * dx + dy * dy < minDistSq) { tooClose = true; break; }
        }
        if (!tooClose) {
            selected.append(c);
            if (selected.size() >= m_cfg.maxCorners) break;
        }
    }

    for (const auto& s : selected)
        corners.append(QPointF(s.second % w, s.second / w));
}

// ----------------------------------------------------------------------------
// computeNCC — 归一化互相关(Normalized Cross-Correlation)
// 计算参考图像中点 ptA 与源图像中点 ptB 的 (2r+1)×(2r+1) 窗口的 NCC
//
// 公式: NCC = Σ[(Aᵢ - μA)·(Bᵢ - μB)] / √[Σ(Aᵢ - μA)² · Σ(Bᵢ - μB)²]
//       = (ΣAB·invCount - μA·μB) / √(varA · varB)
//
// 返回值范围: [-1, 1]，越接近 1 表示越相似
// 当有效像素数 < 30 时返回 -1.0(无效匹配)
// 当窗口方差过小时返回 0.0(纹理不足以区分)
// ----------------------------------------------------------------------------
double AutoMatch::computeNCC(const QImage& grayA, QPointF ptA,
                              const QImage& grayB, QPointF ptB)
{
    int wA = grayA.width(), hA = grayA.height();
    int wB = grayB.width(), hB = grayB.height();
    int r = m_cfg.patchRadius;

    int ax = static_cast<int>(ptA.x()), ay = static_cast<int>(ptA.y());
    int bx = static_cast<int>(ptB.x()), by = static_cast<int>(ptB.y());

    double sumA = 0, sumB = 0, sumA2 = 0, sumB2 = 0, sumAB = 0;
    int count = 0;

    for (int dy = -r; dy <= r; ++dy) {
        int ay2 = ay + dy, by2 = by + dy;
        if (ay2 < 0 || ay2 >= hA || by2 < 0 || by2 >= hB) continue;
        const uchar* lineA = grayA.constScanLine(ay2);
        const uchar* lineB = grayB.constScanLine(by2);
        for (int dx = -r; dx <= r; ++dx) {
            int ax2 = ax + dx, bx2 = bx + dx;
            if (ax2 < 0 || ax2 >= wA || bx2 < 0 || bx2 >= wB) continue;
            double va = static_cast<double>(lineA[ax2]);
            double vb = static_cast<double>(lineB[bx2]);
            sumA  += va;
            sumB  += vb;
            sumA2 += va * va;
            sumB2 += vb * vb;
            sumAB += va * vb;
            count++;
        }
    }

    if (count < 30) return -1.0;

    double invCount = 1.0 / count;
    double meanA = sumA * invCount, meanB = sumB * invCount;
    double varA  = sumA2 * invCount - meanA * meanA;
    double varB  = sumB2 * invCount - meanB * meanB;
    if (varA < 1e-6 || varB < 1e-6) return 0.0;

    double cov = sumAB * invCount - meanA * meanB;
    return cov / std::sqrt(varA * varB);
}

// ----------------------------------------------------------------------------
// refineMatchSubPixel — 二次曲线拟合亚像素精化
// 在整数像素最佳匹配点的左右/上下 ±1px 邻域内，用二次抛物线拟合 NCC 响应
//
// 水平方向: 取 leftNCC, centerNCC, rightNCC 三点
//   拟合抛物线 y = a·x² + b·x + c，顶点在 -b/(2a)
//   其中 a = (left + right)/2 - center, b = (right - left)/2
//   若顶点偏移在 [-0.5, 0.5] 内则更新 dx
// 垂直方向同理，独立计算 dy
//
// 返回精化后的 NCC 值；若未发生偏移则返回原始 centerNCC
// ----------------------------------------------------------------------------
double AutoMatch::refineMatchSubPixel(const QImage& grayA, const QPointF& ptA,
                                       const QImage& grayB, QPointF& ptB)
{
    int bx = static_cast<int>(ptB.x()), by = static_cast<int>(ptB.y());
    double centerNCC = computeNCC(grayA, ptA, grayB, QPointF(bx, by));

    double leftNCC  = computeNCC(grayA, ptA, grayB, QPointF(bx - 1, by));
    double rightNCC = computeNCC(grayA, ptA, grayB, QPointF(bx + 1, by));
    double dx = 0.0;
    {
        double a = (leftNCC + rightNCC) / 2.0 - centerNCC;
        double b = (rightNCC - leftNCC) / 2.0;
        if (std::abs(a) > 1e-10) {
            double delta = -b / (2.0 * a);
            if (delta > -0.5 && delta < 0.5) dx = delta;
        }
    }

    double upNCC   = computeNCC(grayA, ptA, grayB, QPointF(bx, by - 1));
    double downNCC = computeNCC(grayA, ptA, grayB, QPointF(bx, by + 1));
    double dy = 0.0;
    {
        double a = (upNCC + downNCC) / 2.0 - centerNCC;
        double b = (downNCC - upNCC) / 2.0;
        if (std::abs(a) > 1e-10) {
            double delta = -b / (2.0 * a);
            if (delta > -0.5 && delta < 0.5) dy = delta;
        }
    }

    ptB.setX(bx + dx);
    ptB.setY(by + dy);
    if (dx != 0.0 || dy != 0.0)
        return computeNCC(grayA, ptA, grayB, ptB);
    return centerNCC;
}

// ----------------------------------------------------------------------------
// applyHomography — 应用单应矩阵进行透视变换
// 单应矩阵模型（8参数，h33=1归一化）:
//   x' = (h11·x + h12·y + h13) / (h31·x + h32·y + 1)
//   y' = (h21·x + h22·y + h23) / (h31·x + h32·y + 1)
// 分母不可忽略时使用透视除法，否则直接返回仿射结果
// ----------------------------------------------------------------------------
static void applyHomography(const QVector<double>& H, double x, double y,
                             double& xp, double& yp)
{
    double w = H[6] * x + H[7] * y + 1.0;
    if (std::abs(w) > 1e-12) {
        xp = (H[0] * x + H[1] * y + H[2]) / w;
        yp = (H[3] * x + H[4] * y + H[5]) / w;
    } else {
        // 退化为仿射
        xp = H[0] * x + H[1] * y + H[2];
        yp = H[3] * x + H[4] * y + H[5];
    }
}

// ----------------------------------------------------------------------------
// leastSquaresHomography — 最小二乘单应矩阵参数估计 (DLT 算法)
//
// 单应矩阵 H 描述两个平面之间的透视变换关系:
//   x' ~ H·x  即  (x', y', 1)ᵀ ~ H·(x, y, 1)ᵀ
//
// 展开为 8 参数模型（h33 = 1 归一化）:
//   x' = (h11·x + h12·y + h13) / (h31·x + h32·y + 1)
//   y' = (h21·x + h22·y + h23) / (h31·x + h32·y + 1)
//
// DLT（直接线性变换）将非线性方程线性化:
//   每个同名点对 (x,y)→(x',y') 贡献 2 个方程:
//     h11·x + h12·y + h13 - h31·x·x' - h32·y·x' = x'
//     h21·x + h22·y + h23 - h31·x·y' - h32·y·y' = y'
//
// N ≥ 4 个点构建 2N×8 的矩阵 A 和右端项 b，求解超定方程 A·h = b
// 解法: 法方程 AᵀA·h = Aᵀb，列主元 Gauss 消去
//
// 相比仿射变换(6参数)，单应矩阵增加了透视分量(h31, h32)，
// 能描述投影畸变、倾斜平面的透视变形，更适合:
//   - 原始卫星影像 ↔ 正射影像(DOM) 的匹配
//   - 不同视角拍摄的地面影像匹配
//   - 存在地形起伏引起投影差的场景
// ----------------------------------------------------------------------------
void AutoMatch::leastSquaresHomography(const QVector<MatchPoint>& inliers,
                                        QVector<double>& params)
{
    int n = inliers.size();
    int m = 8;
    params.resize(m);
    std::fill(params.begin(), params.end(), 0.0);
    if (n < 4) return;

    int rows = 2 * n;          // 每个匹配点贡献 2 个方程

    // ---- 构建 A (2N×8) 和 b (2N) ----
    QVector<double> A(rows * m, 0.0);
    QVector<double> b(rows, 0.0);

    for (int i = 0; i < n; ++i) {
        double x  = inliers[i].srcPt.x();
        double y  = inliers[i].srcPt.y();
        double xp = inliers[i].refPt.x();
        double yp = inliers[i].refPt.y();

        // 第 1 行: h11·x + h12·y + h13 - h31·x·x' - h32·y·x' = x'
        int r1 = 2 * i;
        A[r1 * m + 0] =  x;
        A[r1 * m + 1] =  y;
        A[r1 * m + 2] =  1.0;
        A[r1 * m + 6] = -x * xp;
        A[r1 * m + 7] = -y * xp;
        b[r1]          =  xp;

        // 第 2 行: h21·x + h22·y + h23 - h31·x·y' - h32·y·y' = y'
        int r2 = 2 * i + 1;
        A[r2 * m + 3] =  x;
        A[r2 * m + 4] =  y;
        A[r2 * m + 5] =  1.0;
        A[r2 * m + 6] = -x * yp;
        A[r2 * m + 7] = -y * yp;
        b[r2]          =  yp;
    }

    // ---- 法方程 AᵀA·h = Aᵀb ----
    QVector<double> AtA(m * m, 0.0);
    QVector<double> Atb(m, 0.0);
    for (int j = 0; j < m; ++j) {
        for (int k = 0; k < m; ++k) {
            double sum = 0.0;
            for (int i = 0; i < rows; ++i)
                sum += A[i * m + j] * A[i * m + k];
            AtA[j * m + k] = sum;
        }
        double sum = 0.0;
        for (int i = 0; i < rows; ++i)
            sum += A[i * m + j] * b[i];
        Atb[j] = sum;
    }

    // ---- 列主元 Gauss 消去 ----
    for (int col = 0; col < m; ++col) {
        int maxRow = col;
        double maxVal = std::abs(AtA[col * m + col]);
        for (int row = col + 1; row < m; ++row) {
            double v = std::abs(AtA[row * m + col]);
            if (v > maxVal) { maxVal = v; maxRow = row; }
        }
        if (maxVal < 1e-15) { std::fill(params.begin(), params.end(), 0.0); return; }
        if (maxRow != col) {
            for (int k = col; k < m; ++k)
                std::swap(AtA[col * m + k], AtA[maxRow * m + k]);
            std::swap(Atb[col], Atb[maxRow]);
        }
        double pivot = AtA[col * m + col];
        for (int k = col; k < m; ++k) AtA[col * m + k] /= pivot;
        Atb[col] /= pivot;
        for (int row = 0; row < m; ++row) {
            if (row == col) continue;
            double factor = AtA[row * m + col];
            for (int k = col; k < m; ++k)
                AtA[row * m + k] -= factor * AtA[col * m + k];
            Atb[row] -= factor * Atb[col];
        }
    }

    params = Atb;
}

// ----------------------------------------------------------------------------
// validateAndRefine — RANSAC 验证 + 最小二乘精化（单应矩阵模型）
//
// 使用单应矩阵（8参数透视变换）替代仿射变换进行几何一致性验证。
// 单应矩阵比仿射多了透视分量，能更好地描述原始卫星影像与 DOM
// 之间的透视变形关系。
//
// 阶段 1 — RANSAC 随机采样一致性:
//   1. 从匹配集中随机抽取 4 个点，调用 leastSquaresHomography 求解单应矩阵 H
//   2. 用 H 对所有匹配点计算投影残差，残差 < ransacDist 的视为内点
//   3. 重复最多 800 次迭代(或 n×30 次)，保留内点数最多的模型
//   4. 若最佳内点数 < 4，认为无有效几何模型，返回原始匹配
//
// 阶段 2 — 最小二乘精化:
//   1. 用 RANSAC 最优模型的所有内点，调用 leastSquaresHomography 重新拟合
//   2. 用精化后的 H 再次筛选内点(降低噪声点的影响)
// ----------------------------------------------------------------------------
QVector<MatchPoint> AutoMatch::validateAndRefine(const QVector<MatchPoint>& matches)
{
    int n = matches.size();
    if (n < 4) return matches;

    int bestInliers = 0;
    QVector<double> bestParams(8, 0.0);
    std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()
        ^ std::random_device{}()));
    int maxIter = qMin(800, n * 30);

    // ---- 阶段 1: RANSAC 随机采样 ----
    // 每次随机选取 4 个匹配点，通过 DLT 求解单应矩阵
    // 统计该矩阵下的内点数，保留内点数最多的模型
    for (int iter = 0; iter < maxIter; ++iter) {
        int i0 = rng() % n;
        int i1 = rng() % n; while (i1 == i0) i1 = rng() % n;
        int i2 = rng() % n; while (i2 == i0 || i2 == i1) i2 = rng() % n;
        int i3 = rng() % n; while (i3 == i0 || i3 == i1 || i3 == i2) i3 = rng() % n;

        QVector<MatchPoint> sample = {
            matches[i0], matches[i1], matches[i2], matches[i3]
        };

        QVector<double> H;
        leastSquaresHomography(sample, H);

        // 检查 H 是否退化（全零或奇异）
        bool degenerate = false;
        for (int k = 0; k < 8; ++k)
            if (std::isnan(H[k]) || std::isinf(H[k])) { degenerate = true; break; }
        if (degenerate) continue;
        // 检查 h31/h32 是否过大导致分母接近零（数值不稳定）
        if (std::abs(H[6]) > 1e3 || std::abs(H[7]) > 1e3) continue;

        double thr2 = m_cfg.ransacDist * m_cfg.ransacDist;
        int inliers = 0;
        for (int j = 0; j < n; ++j) {
            double px, py;
            applyHomography(H, matches[j].srcPt.x(), matches[j].srcPt.y(), px, py);
            double dx = px - matches[j].refPt.x();
            double dy = py - matches[j].refPt.y();
            if (dx * dx + dy * dy < thr2) inliers++;
        }
        if (inliers > bestInliers) {
            bestInliers = inliers;
            bestParams = H;
        }
    }

    if (bestInliers < 4) return matches;

    // ---- 收集内点 ----
    QVector<MatchPoint> rawInliers;
    double thr2 = m_cfg.ransacDist * m_cfg.ransacDist;
    for (int j = 0; j < n; ++j) {
        double px, py;
        applyHomography(bestParams, matches[j].srcPt.x(), matches[j].srcPt.y(), px, py);
        double dx = px - matches[j].refPt.x(), dy = py - matches[j].refPt.y();
        if (dx * dx + dy * dy < thr2)
            rawInliers.append(matches[j]);
    }

    if (rawInliers.size() < 4) return rawInliers;

    // ---- 阶段 2: 最小二乘精化 ----
    // 用所有内点重新拟合单应矩阵，提高参数估计精度
    QVector<double> lsParams;
    leastSquaresHomography(rawInliers, lsParams);

    QVector<MatchPoint> result;
    for (int j = 0; j < n; ++j) {
        double px, py;
        applyHomography(lsParams, matches[j].srcPt.x(), matches[j].srcPt.y(), px, py);
        double dx = px - matches[j].refPt.x(), dy = py - matches[j].refPt.y();
        if (dx * dx + dy * dy < thr2)
            result.append(matches[j]);
    }
    return result;
}

// ----------------------------------------------------------------------------
// computeOverlap — 独立计算两幅影像的重叠区域
// 返回完整的 OverlapResult 结构体，包含像素坐标和地理坐标两个层级的范围。
// ----------------------------------------------------------------------------
OverlapResult AutoMatch::computeOverlap(int srcW, int srcH, const double* srcGT,
                                         int refW, int refH, const double* refGT)
{
    OverlapResult result;
    if (!srcGT || !refGT) return result;
    QRect srcROI, refROI;
    result.valid = computeGeoOverlap(srcW, srcH, srcGT, refW, refH, refGT, srcROI, refROI);
    result.srcROI = srcROI;
    result.refROI = refROI;

    // 计算地理坐标范围
    auto calcGeoExtent = [](int w, int h, const double* gt,
                             double& xmin, double& xmax, double& ymin, double& ymax) {
        double cx[4], cy[4];
        for (int i = 0; i < 4; ++i) {
            double px = (i == 1 || i == 2) ? double(w) : 0.0;
            double py = (i == 2 || i == 3) ? double(h) : 0.0;
            double gx = gt[0] + px * gt[1] + py * gt[2];
            double gy = gt[3] + px * gt[4] + py * gt[5];
            cx[i] = gx; cy[i] = gy;
        }
        xmin = *std::min_element(cx, cx+4);
        xmax = *std::max_element(cx, cx+4);
        ymin = *std::min_element(cy, cy+4);
        ymax = *std::max_element(cy, cy+4);
    };

    calcGeoExtent(srcW, srcH, srcGT, result.srcGeoXmin, result.srcGeoXmax, result.srcGeoYmin, result.srcGeoYmax);
    calcGeoExtent(refW, refH, refGT, result.refGeoXmin, result.refGeoXmax, result.refGeoYmin, result.refGeoYmax);

    if (result.valid) {
        result.geoXmin = qMax(result.srcGeoXmin, result.refGeoXmin);
        result.geoXmax = qMin(result.srcGeoXmax, result.refGeoXmax);
        result.geoYmin = qMax(result.srcGeoYmin, result.refGeoYmin);
        result.geoYmax = qMin(result.srcGeoYmax, result.refGeoYmax);
    }
    return result;
}

// ----------------------------------------------------------------------------
// extractFeatures — 独立提取影像 Harris 角点特征
// 将私有 extractHarrisCorners 暴露为公共接口，支持 ROI 约束和角点数量控制。
// ----------------------------------------------------------------------------
QVector<QPointF> AutoMatch::extractFeatures(const QImage& gray, int maxCorners,
                                              const QRect& roi)
{
    int savedMax = m_cfg.maxCorners;
    m_cfg.maxCorners = maxCorners;
    QVector<QPointF> corners;
    extractHarrisCorners(gray, corners, 10, roi);
    m_cfg.maxCorners = savedMax;
    return corners;
}

// ----------------------------------------------------------------------------
// geoFromPixel / pixelFromGeo — 影像像素坐标与地理坐标之间的双向变换
// GeoTransform 是 6 参数仿射模型:
//   gx = gt[0] + px·gt[1] + py·gt[2]
//   gy = gt[3] + px·gt[4] + py·gt[5]
// pixelFromGeo 通过逆矩阵求解，需保证行列式 |gt[1]·gt[5] - gt[2]·gt[4]| > 0
// ----------------------------------------------------------------------------
static void geoFromPixel(const double* gt, double px, double py,
                          double& gx, double& gy)
{
    gx = gt[0] + px * gt[1] + py * gt[2];
    gy = gt[3] + px * gt[4] + py * gt[5];
}

static void pixelFromGeo(const double* gt, double gx, double gy,
                          double& px, double& py)
{
    double det = gt[1] * gt[5] - gt[2] * gt[4];
    if (std::abs(det) < 1e-20) { px = 0; py = 0; return; }
    double invDet = 1.0 / det;
    px = (gt[5] * (gx - gt[0]) - gt[2] * (gy - gt[3])) * invDet;
    py = (gt[1] * (gy - gt[3]) - gt[4] * (gx - gt[0])) * invDet;
}

// ----------------------------------------------------------------------------
// computeGeoOverlap — 地理重叠区域计算
// 根据两幅影像的 GeoTransform 参数，计算它们在物理空间中的重叠区域
//
// 算法:
//   1. 将源影像和参考影像的四个角点分别通过 geoFromPixel 变换到地理坐标
//   2. 计算两幅影像在地理坐标系中的包围框交集(olXmin~olXmax, olYmin~olYmax)
//   3. 若交集为空(olXmin≥olXmax 或 olYmin≥olYmax)，返回 false
//   4. 将交集区域的四个角点分别通过 pixelFromGeo 反投影回两幅影像的像素坐标
//   5. 由此得到 srcROI 和 refROI，即两幅影像各自对应重叠区域的像素矩形
//
// 返回值: true 表示存在有效重叠区域，false 表示无重叠
// ----------------------------------------------------------------------------
bool AutoMatch::computeGeoOverlap(int srcW, int srcH, const double* srcGT,
                                   int refW, int refH, const double* refGT,
                                   QRect& srcROI, QRect& refROI)
{
    double srcGxMin = 1e30, srcGxMax = -1e30;
    double srcGyMin = 1e30, srcGyMax = -1e30;
    double refGxMin = 1e30, refGxMax = -1e30;
    double refGyMin = 1e30, refGyMax = -1e30;

    struct Corner { double px, py; };
    Corner srcC[4] = {{0,0},{double(srcW),0},{double(srcW),double(srcH)},{0,double(srcH)}};
    for (int i = 0; i < 4; ++i) {
        double gx, gy;
        geoFromPixel(srcGT, srcC[i].px, srcC[i].py, gx, gy);
        srcGxMin = qMin(srcGxMin, gx); srcGxMax = qMax(srcGxMax, gx);
        srcGyMin = qMin(srcGyMin, gy); srcGyMax = qMax(srcGyMax, gy);
    }

    Corner refC[4] = {{0,0},{double(refW),0},{double(refW),double(refH)},{0,double(refH)}};
    for (int i = 0; i < 4; ++i) {
        double gx, gy;
        geoFromPixel(refGT, refC[i].px, refC[i].py, gx, gy);
        refGxMin = qMin(refGxMin, gx); refGxMax = qMax(refGxMax, gx);
        refGyMin = qMin(refGyMin, gy); refGyMax = qMax(refGyMax, gy);
    }

    double olXmin = qMax(srcGxMin, refGxMin);
    double olXmax = qMin(srcGxMax, refGxMax);
    double olYmin = qMax(srcGyMin, refGyMin);
    double olYmax = qMin(srcGyMax, refGyMax);
    if (olXmin >= olXmax || olYmin >= olYmax)
        return false;

    double olC[4][2] = {
        {olXmin, olYmax}, {olXmax, olYmax},
        {olXmax, olYmin}, {olXmin, olYmin}
    };

    double sPxMin = 1e30, sPxMax = -1e30, sPyMin = 1e30, sPyMax = -1e30;
    for (int i = 0; i < 4; ++i) {
        double px, py;
        pixelFromGeo(srcGT, olC[i][0], olC[i][1], px, py);
        sPxMin = qMin(sPxMin, px); sPxMax = qMax(sPxMax, px);
        sPyMin = qMin(sPyMin, py); sPyMax = qMax(sPyMax, py);
    }

    double rPxMin = 1e30, rPxMax = -1e30, rPyMin = 1e30, rPyMax = -1e30;
    for (int i = 0; i < 4; ++i) {
        double px, py;
        pixelFromGeo(refGT, olC[i][0], olC[i][1], px, py);
        rPxMin = qMin(rPxMin, px); rPxMax = qMax(rPxMax, px);
        rPyMin = qMin(rPyMin, py); rPyMax = qMax(rPyMax, py);
    }

    int sx0 = qMax(0, (int)std::floor(sPxMin));
    int sy0 = qMax(0, (int)std::floor(sPyMin));
    int sx1 = qMin(srcW - 1, (int)std::ceil(sPxMax));
    int sy1 = qMin(srcH - 1, (int)std::ceil(sPyMax));
    if (sx0 >= sx1 || sy0 >= sy1) return false;
    srcROI = QRect(sx0, sy0, sx1 - sx0, sy1 - sy0);

    int rx0 = qMax(0, (int)std::floor(rPxMin));
    int ry0 = qMax(0, (int)std::floor(rPyMin));
    int rx1 = qMin(refW - 1, (int)std::ceil(rPxMax));
    int ry1 = qMin(refH - 1, (int)std::ceil(rPyMax));
    if (rx0 >= rx1 || ry0 >= ry1) return false;
    refROI = QRect(rx0, ry0, rx1 - rx0, ry1 - ry0);
    return true;
}

// ----------------------------------------------------------------------------
// match — 自动同名点匹配主函数
//
// 完整流程(共 12 步):
//
//   1. 灰度化: 将源影像和参考影像转为 8 位灰度图
//   2. 降采样: 若边长超过 1500px，等比缩小以加速后续处理
//      同时计算缩放系数 srcSx/srcSy/refSx/refSy，用于最后坐标恢复
//   3. 地理重叠约束: 若提供了 GeoTransform，计算两幅影像的重叠区域
//      srcROI/refROI 分别对应重叠区域在各自降采样图中的像素范围
//   4. 自适应搜索半径: 若重叠区域占比 < 15%，按比例扩大搜索半径
//      (最小 0.15 占比对应最大 250px 半径)
//   5. Harris 角点提取: 优先在重叠区域内提取角点(extractHarrisCorners)
//      若重叠区域角点不足(< 10)，降级为全图提取
//   6. 前向 NCC 匹配: 遍历源影像角点，在参考影像角点中搜索 NCC 最高匹配
//      同时跟踪第二高 NCC，用于 Lowe 比率测试(剔除模糊匹配)
//      搜索受地理变换约束: 源角点通过 refToSrcSpace 投影到参考空间后限距
//   7. 反向 NCC 匹配: 遍历参考影像角点，在源影像角点中搜索最佳匹配
//      同样使用比率测试和搜索半径约束
//   8. 交叉验证: crossValidate 检查前向/反向匹配的一致性
//      若正反向匹配的 srcPt 和 refPt 差异均 < tol(8px)，则认为是可靠匹配
//   9. 无重叠降级(fallback): 若有地理约束但交叉验证结果为 0，
//      降级为无约束全图匹配(搜索半径 200px)，重新提取角点和匹配
//   10. 亚像素精化: 对每个一致匹配对，refineMatchSubPixel 做二次曲线拟合
//   11. RANSAC 提纯: validateAndRefine 用随机采样一致性检验几何一致性
//       并用最小二乘精化模型参数
//   12. 去重 + 坐标缩放: 分别在参考空间和源空间去重(约整到像素，保留
//       NCC 更高的匹配)，最后将坐标乘以降采样系数恢复至原始分辨率
//
// 返回:
//   QVector<ControlPoint> — 每个元素包含 ID、源坐标(x,y)、参考坐标(x,y)
//   失败时返回空列表，并通过 m_lastError 记录错误原因
// ----------------------------------------------------------------------------
QVector<ControlPoint> AutoMatch::match(const QImage& srcImage, const QImage& refImage,
                                        const double* srcGT, const double* refGT)
{
    QVector<ControlPoint> result;
    m_lastError.clear();

    if (srcImage.isNull() || refImage.isNull()) {
        m_lastError = QString::fromUtf8("源影像或参考影像为空");
        return result;
    }

    QImage srcGray = toGray(srcImage);
    QImage refGray = toGray(refImage);

    int srcW = srcGray.width(), srcH = srcGray.height();
    int refW = refGray.width(), refH = refGray.height();

    const int MAX_SIDE = 3000;
    QImage srcSmall = downsampleIfLarge(srcGray, MAX_SIDE);
    QImage refSmall = downsampleIfLarge(refGray, MAX_SIDE);
    double srcSx = static_cast<double>(srcW) / srcSmall.width();
    double srcSy = static_cast<double>(srcH) / srcSmall.height();
    double refSx = static_cast<double>(refW) / refSmall.width();
    double refSy = static_cast<double>(refH) / refSmall.height();

    int sW = srcSmall.width(), sH = srcSmall.height();
    int rW = refSmall.width(), rH = refSmall.height();

    QRect srcROI, refROI;
    bool hasOverlap = false;
    if (srcGT && refGT) {
        hasOverlap = computeGeoOverlap(srcW, srcH, srcGT, refW, refH, refGT, srcROI, refROI);
    }

    QRect srcROI_ds, refROI_ds;
    if (hasOverlap) {
        int dsx0 = qMax(0, static_cast<int>(srcROI.x() / srcSx));
        int dsy0 = qMax(0, static_cast<int>(srcROI.y() / srcSy));
        int dsx1 = qMin(sW, static_cast<int>(std::ceil((srcROI.x() + srcROI.width()) / srcSx)));
        int dsy1 = qMin(sH, static_cast<int>(std::ceil((srcROI.y() + srcROI.height()) / srcSy)));
        int drx0 = qMax(0, static_cast<int>(refROI.x() / refSx));
        int dry0 = qMax(0, static_cast<int>(refROI.y() / refSy));
        int drx1 = qMin(rW, static_cast<int>(std::ceil((refROI.x() + refROI.width()) / refSx)));
        int dry1 = qMin(rH, static_cast<int>(std::ceil((refROI.y() + refROI.height()) / refSy)));

        srcROI_ds = QRect(dsx0, dsy0, qMax(1, dsx1 - dsx0), qMax(1, dsy1 - dsy0));
        refROI_ds = QRect(drx0, dry0, qMax(1, drx1 - drx0), qMax(1, dry1 - dry0));
    }

    double srcGT_ds[6] = {0,1,0,0,0,1};
    double refGT_ds[6] = {0,1,0,0,0,1};
    if (srcGT) {
        std::memcpy(srcGT_ds, srcGT, 6 * sizeof(double));
        srcGT_ds[1] *= srcSx; srcGT_ds[2] *= srcSy;
        srcGT_ds[4] *= srcSx; srcGT_ds[5] *= srcSy;
    }
    if (refGT) {
        std::memcpy(refGT_ds, refGT, 6 * sizeof(double));
        refGT_ds[1] *= refSx; refGT_ds[2] *= refSy;
        refGT_ds[4] *= refSx; refGT_ds[5] *= refSy;
    }

    // 构建地理→像素的投影函数: 将参考影像坐标通过地理坐标映射到源影像坐标
    // 用于约束 NCC 匹配的搜索范围，只在地理重叠区域附近搜索
    auto refToSrcSpace = [&](QPointF rp) -> QPointF {
        if (!srcGT || !refGT) return rp;
        double gx, gy, px, py;
        geoFromPixel(refGT_ds, rp.x(), rp.y(), gx, gy);
        pixelFromGeo(srcGT_ds, gx, gy, px, py);
        return QPointF(px, py);
    };

    double adaptiveSR = m_cfg.searchRadius;
    if (hasOverlap && srcROI_ds.width() > 0) {
        double srcArea = static_cast<double>(sW) * sH;
        double roiArea = static_cast<double>(srcROI_ds.width()) * srcROI_ds.height();
        double olRatio = roiArea / srcArea;
        if (olRatio < 0.15)
            adaptiveSR = qMin(250.0, m_cfg.searchRadius * (1.0 + 3.0 * (0.15 - olRatio) / 0.15));
    }
    double sr2 = adaptiveSR * adaptiveSR;

    double savedNms = m_cfg.nmsRadius;
    if (hasOverlap && srcROI_ds.width() > 0 &&
        srcROI_ds.width() * srcROI_ds.height() < 300 * 300)
        m_cfg.nmsRadius = qMax(1.5, m_cfg.nmsRadius * 0.5);

    QVector<QPointF> srcCorners, refCorners;
    extractHarrisCorners(srcSmall, srcCorners, 10, hasOverlap ? srcROI_ds : QRect());
    extractHarrisCorners(refSmall, refCorners, 10, hasOverlap ? refROI_ds : QRect());
    m_cfg.nmsRadius = savedNms;

    if (hasOverlap && (srcCorners.size() < 10 || refCorners.size() < 10)) {
        srcCorners.clear(); refCorners.clear();
        extractHarrisCorners(srcSmall, srcCorners, 10, QRect());
        extractHarrisCorners(refSmall, refCorners, 10, QRect());
        hasOverlap = false;
    }

    if (srcCorners.isEmpty() || refCorners.isEmpty()) {
        m_lastError = QString::fromUtf8("未检测到足够的角点特征 (源:%1, 参考:%2)")
            .arg(srcCorners.size()).arg(refCorners.size());
        return result;
    }

    int srcLimit = qMin(srcCorners.size(), m_cfg.maxCorners / 2);
    int refLimit = qMin(refCorners.size(), m_cfg.maxCorners / 2);

    // ---- 前向 NCC 匹配: 源影像角点 → 参考影像角点 ----
    // 对每个源角点，在参考角点中搜索 NCC 最高的匹配
    // 同时记录第二高 NCC，用于 Lowe 比率测试:
    //   最佳NCC > nccThreshold 且 最佳/次佳 ≥ nccRatioThreshold
    // 搜索范围受地理约束: 源角点经 refToSrcSpace 投影后的距离 ≤ adaptiveSR
    QVector<MatchPoint> forwardMatches;
    forwardMatches.reserve(srcLimit / 2);
    for (int i = 0; i < srcLimit; ++i) {
        QPointF sp = srcCorners[i];
        double bestNCC = -1.0, secondNCC = -1.0;
        QPointF bestRP;
        for (int j = 0; j < refLimit; ++j) {
            QPointF rp = refCorners[j];
            QPointF rm = refToSrcSpace(rp);
            double dx = sp.x() - rm.x(), dy = sp.y() - rm.y();
            if (dx * dx + dy * dy > sr2) continue;
            double ncc = computeNCC(srcSmall, sp, refSmall, rp);
            if (ncc > bestNCC) {
                secondNCC = bestNCC; bestNCC = ncc; bestRP = rp;
            } else if (ncc > secondNCC) {
                secondNCC = ncc;
            }
        }
        if (bestNCC > m_cfg.nccThreshold &&
            (secondNCC < -0.5 || bestNCC / qMax(secondNCC, 0.001) >= m_cfg.nccRatioThreshold)) {
            forwardMatches.append({sp, bestRP, bestNCC});
        }
    }

    // ---- 反向 NCC 匹配: 参考影像角点 → 源影像角点 ----
    // 与前向对称，遍历参考角点在源角点中搜索最佳 NCC
    // 同样使用比率测试和地理搜索半径约束
    QVector<MatchPoint> backwardMatches;
    backwardMatches.reserve(refLimit / 2);
    for (int i = 0; i < refLimit; ++i) {
        QPointF rp = refCorners[i];
        QPointF rm = refToSrcSpace(rp);
        double bestNCC = -1.0, secondNCC = -1.0;
        QPointF bestSP;
        for (int j = 0; j < srcLimit; ++j) {
            QPointF sp = srcCorners[j];
            double dx = sp.x() - rm.x(), dy = sp.y() - rm.y();
            if (dx * dx + dy * dy > sr2) continue;
            double ncc = computeNCC(srcSmall, sp, refSmall, rp);
            if (ncc > bestNCC) {
                secondNCC = bestNCC; bestNCC = ncc; bestSP = sp;
            } else if (ncc > secondNCC) {
                secondNCC = ncc;
            }
        }
        if (bestNCC > m_cfg.nccThreshold &&
            (secondNCC < -0.5 || bestNCC / qMax(secondNCC, 0.001) >= m_cfg.nccRatioThreshold)) {
            backwardMatches.append({bestSP, rp, bestNCC});
        }
    }

    // ---- 交叉验证: 检查前向/反向匹配的一致性 ----
    // 同时满足 srcPt 和 refPt 差异均 < tol 的匹配对才被认为是可靠的
    // 这消除了大量"一对多"或"多对一"的模糊匹配
    auto crossValidate = [](const QVector<MatchPoint>& fwd,
                             const QVector<MatchPoint>& bwd,
                             double tol,
                             QVector<MatchPoint>& out)
    {
        out.clear();
        out.reserve(qMin(fwd.size(), bwd.size()));
        for (const auto& fw : fwd) {
            for (const auto& bw : bwd) {
                if (std::abs(fw.srcPt.x() - bw.srcPt.x()) < tol &&
                    std::abs(fw.srcPt.y() - bw.srcPt.y()) < tol &&
                    std::abs(fw.refPt.x() - bw.refPt.x()) < tol &&
                    std::abs(fw.refPt.y() - bw.refPt.y()) < tol) {
                    out.append(fw);
                    break;
                }
            }
        }
    };

    QVector<MatchPoint> consistent;
    crossValidate(forwardMatches, backwardMatches, 8.0, consistent);

    // ---- 无重叠降级(fallback) ----
    // 有地理约束但交叉验证结果为 0，说明重叠区域角点或匹配不足
    // 降级策略: 去除地理约束，使用固定搜索半径 200px，在全图重新匹配
    if (consistent.isEmpty() && hasOverlap) {
        hasOverlap = false;
        adaptiveSR = 200.0;
        sr2 = adaptiveSR * adaptiveSR;

        srcCorners.clear(); refCorners.clear();
        extractHarrisCorners(srcSmall, srcCorners, 10, QRect());
        extractHarrisCorners(refSmall, refCorners, 10, QRect());
        if (srcCorners.isEmpty() || refCorners.isEmpty()) {
            m_lastError = QString::fromUtf8("GT匹配失败后全图角点不足 (源:%1, 参考:%2)")
                .arg(srcCorners.size()).arg(refCorners.size());
            return result;
        }

        srcLimit = qMin(srcCorners.size(), m_cfg.maxCorners / 2);
        refLimit = qMin(refCorners.size(), m_cfg.maxCorners / 2);

        forwardMatches.clear(); backwardMatches.clear();
        forwardMatches.reserve(srcLimit / 2);
        for (int i = 0; i < srcLimit; ++i) {
            QPointF sp = srcCorners[i];
            double bestNCC = -1.0, secondNCC = -1.0;
            QPointF bestRP;
            for (int j = 0; j < refLimit; ++j) {
                QPointF rp = refCorners[j];
                double dx = sp.x() - rp.x(), dy = sp.y() - rp.y();
                if (dx * dx + dy * dy > sr2) continue;
                double ncc = computeNCC(srcSmall, sp, refSmall, rp);
                if (ncc > bestNCC) {
                    secondNCC = bestNCC; bestNCC = ncc; bestRP = rp;
                } else if (ncc > secondNCC) {
                    secondNCC = ncc;
                }
            }
            if (bestNCC > m_cfg.nccThreshold &&
                (secondNCC < -0.5 || bestNCC / qMax(secondNCC, 0.001) >= m_cfg.nccRatioThreshold))
                forwardMatches.append({sp, bestRP, bestNCC});
        }

        backwardMatches.reserve(refLimit / 2);
        for (int i = 0; i < refLimit; ++i) {
            QPointF rp = refCorners[i];
            double bestNCC = -1.0, secondNCC = -1.0;
            QPointF bestSP;
            for (int j = 0; j < srcLimit; ++j) {
                QPointF sp = srcCorners[j];
                double dx = sp.x() - rp.x(), dy = sp.y() - rp.y();
                if (dx * dx + dy * dy > sr2) continue;
                double ncc = computeNCC(srcSmall, sp, refSmall, rp);
                if (ncc > bestNCC) {
                    secondNCC = bestNCC; bestNCC = ncc; bestSP = sp;
                } else if (ncc > secondNCC) {
                    secondNCC = ncc;
                }
            }
            if (bestNCC > m_cfg.nccThreshold &&
                (secondNCC < -0.5 || bestNCC / qMax(secondNCC, 0.001) >= m_cfg.nccRatioThreshold))
                backwardMatches.append({bestSP, rp, bestNCC});
        }

        crossValidate(forwardMatches, backwardMatches, 10.0, consistent);
    }

    if (consistent.isEmpty()) {
        m_lastError = QString::fromUtf8("交叉验证未通过 (前向:%1 反向:%2 一致:0)")
            .arg(forwardMatches.size()).arg(backwardMatches.size());
        return result;
    }

    // ---- 亚像素精化 ----
    // 对每个交叉验证通过的匹配对，在整数坐标邻域内做二次曲线拟合
    // 将匹配点坐标精度提升到亚像素级别
    for (int i = 0; i < consistent.size(); ++i) {
        refineMatchSubPixel(srcSmall, consistent[i].srcPt, refSmall, consistent[i].refPt);
    }

    // ---- RANSAC 几何验证 + 最小二乘精化（单应矩阵模型） ----
    // 用随机采样一致性剔除错误匹配，单应矩阵（8参数透视模型）比仿射
    // 多了透视分量，能更好地描述原始卫星影像与 DOM 之间的几何变形。
    // 保留满足单应投影模型的内点，再用内点做最小二乘重拟合。
    QVector<MatchPoint> refined = validateAndRefine(consistent);
    if (refined.isEmpty()) {
        m_lastError = QString::fromUtf8("几何一致性未通过 (交叉验证通过:%1)")
            .arg(consistent.size());
        return result;
    }

    // ---- 在参考空间中去重 ----
    // 多个匹配可能指向同一参考像素位置，只保留 NCC 分数最高的那个
    {
        QMap<QPair<int,int>, int> best;
        for (int i = 0; i < refined.size(); ++i) {
            QPair<int,int> key(
                static_cast<int>(std::round(refined[i].refPt.x())),
                static_cast<int>(std::round(refined[i].refPt.y())));
            auto it = best.find(key);
            if (it == best.end())
                best.insert(key, i);
            else if (refined[i].nccScore > refined[it.value()].nccScore)
                it.value() = i;
        }
        QVector<MatchPoint> deduped;
        deduped.reserve(best.size());
        for (auto it = best.begin(); it != best.end(); ++it)
            deduped.append(refined[it.value()]);
        refined = deduped;
    }
    // ---- 在源空间中去重 ----
    // 同理，多个匹配也不应指向同一源像素位置
    {
        QMap<QPair<int,int>, int> best;
        for (int i = 0; i < refined.size(); ++i) {
            QPair<int,int> key(
                static_cast<int>(std::round(refined[i].srcPt.x())),
                static_cast<int>(std::round(refined[i].srcPt.y())));
            auto it = best.find(key);
            if (it == best.end())
                best.insert(key, i);
            else if (refined[i].nccScore > refined[it.value()].nccScore)
                it.value() = i;
        }
        QVector<MatchPoint> deduped;
        deduped.reserve(best.size());
        for (auto it = best.begin(); it != best.end(); ++it)
            deduped.append(refined[it.value()]);
        refined = deduped;
    }

    // ---- 坐标缩放回原始分辨率 ----
    // 降采样时的匹配坐标乘以缩放系数 srcSx/srcSy/refSx/refSy
    // 恢复为原始影像分辨率下的坐标值
    result.reserve(refined.size());
    for (int i = 0; i < refined.size(); ++i) {
        double sx = refined[i].srcPt.x() * srcSx;
        double sy = refined[i].srcPt.y() * srcSy;
        double rx = refined[i].refPt.x() * refSx;
        double ry = refined[i].refPt.y() * refSy;
        result.append(ControlPoint(i + 1, sx, sy, rx, ry));
    }

    return result;
}