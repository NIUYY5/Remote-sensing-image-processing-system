# 遥感图像处理系统 (RSImageProcess)

基于 C++ / Qt / GDAL 的桌面端遥感影像综合处理系统，集成几何配准、影像融合、影像镶嵌、特征提取与分类、精度评定等完整遥感处理链路。所有核心算法（最小二乘平差、PCA/HIS 融合、K-Means/ISODATA、最大似然分类、RANSAC 单应性估计等）均为自主实现，不依赖第三方数学库。

- 应用名称：遥感图像处理系统
- 组织：RSImageProcess
- 版本：1.0.0
- 主程序入口：`main.cpp`

## 目录

- [功能特性](#功能特性)
- [技术栈](#技术栈)
- [项目结构](#项目结构)
- [核心模块说明](#核心模块说明)
- [环境要求](#环境要求)
- [构建方法](#构建方法)
- [运行说明](#运行说明)
- [输出格式](#输出格式)
- [许可证](#许可证)

## 功能特性

### 主框架
- 登录界面（`login_dialog.h`）：支持 GIF 动态背景、卡片式登录、无边框拖拽
- 主窗口（`moban.h`）：项目式管理（新建/打开/保存/另存）、最近项目列表（最多 5 项）、数据树面板、输出日志面板、状态栏坐标显示、明暗双主题切换、分屏对比视图
- GDAL 影像加载（`gdal_loader.h`）：支持多格式（GeoTIFF 等）、多波段选择、缩略图预览、元数据读取

### 几何配准（Processing → 几何配准）
入口对话框：`jihepeizhun.h`
- 双视图布局（源影像 + 参考影像），左右分屏刺点
- `PickManager` 状态机管理人工刺点流程（Idle → WaitSrc → WaitRef）
- `automatch.h`：自动匹配（Harris 角点 + NCC + RANSAC）
- `geomodel.h`：自编最小二乘平差，支持仿射变换（6 参数）与二次多项式（12 参数），高斯列主元消去法解法方程
- 控制点表格管理、导入/导出、重叠区域计算与导出
- 正射校正对话框（`orthodialog.h`）

### 影像融合（Processing → 影像融合）
核心：`fusion_core.h`，对话框：`fusion_dialog.h`
- PCA 主成分分析融合
- HIS（IHS）色彩空间融合
- 自实现矩阵运算（特征值/特征向量、对称矩阵特征分解、直方图匹配）
- 多种上采样插值（最近邻 / 双线性 / 双三次）
- 评价体系：光谱扭曲度、空间细节保持度、信息熵、PSNR、RMSE，支持 CSV / HTML 报告输出

### 影像镶嵌（独立对话框）
核心：`stitch_core.h`，对话框：`stitch_dialog.h`
- 特征检测：FAST、Harris、SIFT-Like
- 描述子匹配：SSD / NCC，比值测试筛选
- 单应性矩阵 RANSAC 鲁棒估计
- 三种对齐模式：传统特征匹配 / 深度风格 / 地理坐标直接对齐
- RPC 精确对齐模式（基于 `rpc_model.h`）
- 接缝融合：平均 / 线性渐变 / 多频段（拉普拉斯金字塔）
- 几何精度验证（采样点 RMS 误差，单位米）、镶嵌完整性报告、输出元数据

### 特征提取与分类（Processing → 分类）
主界面：`FeatureExtraction4.h`
- **聚类分析**（`ClusteringEngine.h`）：K-Means 与 ISODATA，扁平化存储 + 平方距离 + 并行分块，支持空间位置特征加权
- **监督分类**（`ClassificationEngine.h`）：最大似然法（Mahalanobis 距离，自实现矩阵求逆与行列式），支持网格采样 / 随机采样 / 人工 ROI 采样
- **特征计算**（`FeatureExtractor.h`）：GLCM 纹理特征（对比度、同质性、能量、相关性等）、光谱指数（NDVI / NDWI / NDBI / MNDWI）
- **地物提取**（`LandCoverExtractor.h`）：水体 / 植被 / 建筑 / 道路 / 裸土 / 阴影分类掩膜，形态学后处理
- **精度评定**（`AccuracyAssessment.h`）：混淆矩阵、总体精度、生产者/用户精度、F1、Kappa 系数；人工判读会话（标注、备注、批量确认、版本对比）
- **结果导出**（`ExportManager.h`）：GeoTIFF / Shapefile / CSV / ENVI BSQ，含 world file

### 影像视图
- `imageview.h`：自定义影像视图，支持缩放、平移、坐标拾取
- `split_view.h`：原图与处理结果分屏对比，独立缩放/平移

### 地理元数据
- `geo_metadata.h`：GeoTIFF 标签解析、World File 解析、PRJ 投影解析
- `rpc_model.h`：RPC（Rational Polynomial Coefficients）20 系数模型与归一化参数

## 技术栈

| 组件 | 版本 / 说明 |
| --- | --- |
| 语言 | C++ (C++17) |
| GUI 框架 | Qt 5.15.x / Qt 6.x（兼容） |
| 影像 I/O | GDAL（动态链接，运行时由 `gdal_loader.cpp` 加载） |
| 构建工具 | Visual Studio 2022 + MSBuild |
| 目标平台 | Windows x64 |
| 数学实现 | 全部自编（矩阵、特征分解、最小二乘、RANSAC、K-Means 等），不依赖 Eigen/Armadillo |
| 并行 | STL 并行分块（聚类标签分配） |

## 项目结构

```
SoftwareProject/
├── main.cpp                      程序入口，启动登录与主窗口
├── moban.h/.cpp/.ui/.qrc/.vcxproj  主窗口框架（项目/菜单/工具栏/Dock/主题）
├── login_dialog.h/.cpp           登录对话框
├── gdal_loader.h/.cpp            GDAL 影像加载与元数据
├── imageview.h/.cpp              自定义影像视图
├── split_view.h/.cpp             分屏对比视图
│
├── ── 几何配准模块 ──
├── jihepeizhun.h/.cpp            几何配准主对话框
├── geomodel.h/.cpp               几何变换模型 + 最小二乘平差
├── controlpoint.h                控制点数据结构
├── pickmanager.h/.cpp            刺点交互状态机
├── automatch.h/.cpp              Harris+NCC+RANSAC 自动匹配
├── orthodialog.h/.cpp            正射校正对话框
│
├── ── 影像融合模块 ──
├── fusion_core.h/.cpp            PCA / HIS 融合算法与评价
├── fusion_dialog.h/.cpp/.ui      融合参数与结果对话框
│
├── ── 影像镶嵌模块 ──
├── stitch_core.h/.cpp            特征检测 / 匹配 / 单应性 / 接缝融合
├── stitch_dialog.h/.cpp/.ui      镶嵌参数与结果对话框
│
├── ── 特征提取与分类模块 ──
├── FeatureExtraction4.h/.cpp/.ui/.qrc  分类主窗口
├── GeoImageData.h/.cpp               多波段影像数据结构
├── ClusteringEngine.h/.cpp           K-Means / ISODATA
├── ClassificationEngine.h/.cpp       最大似然分类
├── FeatureExtractor.h/.cpp           GLCM 纹理 + 光谱指数
├── LandCoverExtractor.h/.cpp          地物类型掩膜提取
├── AccuracyAssessment.h/.cpp          混淆矩阵 / Kappa / 人工判读
├── ExportManager.h/.cpp               多格式结果导出
│
├── ── 地理元数据 ──
├── geo_metadata.h/.cpp           GeoTIFF / WorldFile / PRJ
├── rpc_model.h/.cpp              RPC 系数模型
│
├── icons/                        SVG 工具栏图标 + 应用图标
├── moban/                        构建中间目录
├── x64/                          构建输出（Debug/Release）
├── moban.vcxproj                 VS 工程文件
├── moban.sln                     VS 解决方案
└── package_release.ps1           一键发布打包脚本
```

## 核心模块说明

### 几何配准工作流

1. 打开源影像与参考影像 → 2. 人工刺点或自动匹配生成同名点 → 3. 选择几何模型（仿射 / 二次多项式）→ 4. 最小二乘平差解算 → 5. 查看 RMSE 与残差 → 6. 影像重采样输出。详见 `jihepeizhun.cpp`。

### 影像融合工作流

输入 PAN（全色）+ MS（多光谱）→ 上采样 MS → 选择 PCA / HIS → 执行融合 → 输出融合影像 + 评价报告（熵、PSNR、光谱扭曲度）。详见 `fusion_core.cpp`。

### 影像镶嵌工作流

加载两幅影像 → 选择对齐模式（特征 / 地理坐标 / RPC）→ 特征检测与匹配 → RANSAC 单应性 → 接缝融合（平均 / 线性 / 多频段）→ 输出镶嵌图 + 几何精度验证报告。详见 `stitch_core.cpp`。

### 分类与精度评定工作流

打开多波段影像 → 选择波段 → 聚类（K-Means/ISODATA）或监督分类（最大似然）→ 可选地物提取与光谱指数计算 → 精度评定（混淆矩阵 + Kappa）→ 人工判读修正 → 导出 GeoTIFF/Shapefile/CSV/ENVI + PDF/Excel 报告。详见 `FeatureExtraction4.cpp`。

## 环境要求

- Windows 10 / 11 (x64)
- Visual Studio 2022（含 MSBuild、C++ 工作负载）
- Qt 5.15.2 或 Qt 6.x（含对应版本 windeployqt）
- GDAL 运行时库（bin 目录需包含 `gdal*.dll` 及插件）
- （可选）Steam 客户端：用于登录界面的动态壁纸背景

## 构建方法

### 方式一：Visual Studio 2022 IDE

1. 打开 `moban.sln` 或 `moban.vcxproj`
2. 配置为 `Release | x64`
3. 在项目属性中正确配置 Qt 与 GDAL 的头文件、库文件、依赖 DLL 路径
4. 生成解决方案，输出至 `x64/Release/moban.exe`

### 方式二：一键打包脚本（推荐用于发布）

从开始菜单运行 "x64 Native Tools Command Prompt for VS 2022"，进入项目目录：

```powershell
powershell -ExecutionPolicy Bypass -File package_release.ps1
```

脚本将依次：定位 MSBuild → 编译 Release → 调用 windeployqt 收集 Qt 依赖 → 拷贝 GDAL 运行时 → 打包到 `../release_package`。详见 `package_release.ps1`。

> 注意：脚本中的 `$GDAL_BINDIR`（默认 `D:\gdl\x64-windows\bin`）需根据本机 GDAL 安装路径修改。

## 运行说明

1. 启动 `moban.exe`，进入登录界面（任意用户名/密码即可进入演示用途）
2. 主窗口通过 **文件** 菜单新建或打开工程（`.rsip` JSON 工程文件）
3. 通过 **文件 → 导入影像** 添加待处理影像到数据树
4. 通过 **处理** 菜单进入：几何配准 / 影像融合 / 分类与特征提取
5. 影像镶嵌通过独立对话框（`stitch_dialog`）启动
6. 处理结果自动保存到工程目录的 `results/` 子目录，并加入数据树
7. **视图** 菜单支持缩放、适应窗口、分屏对比、主题切换

## 输出格式

| 类型 | 格式 | 说明 |
| --- | --- | --- |
| 影像 | GeoTIFF | 配准/融合/镶嵌/分类结果，含地理变换与投影 |
| 矢量 | Shapefile | 分类与地物提取结果矢量导出 |
| 表格 | CSV | 控制点、分类标签、镶嵌验证报告 |
| ENVI | ENVI BSQ | 分类标签图 + 头文件 |
| 报告 | HTML / PDF / Excel | 融合评价、精度评定专业报告 |
| 辅助 | World File (.tfw) | 与 GeoTIFF 配套的地理坐标文件 |

## 许可证

本项目为课程教学/第七组遥感程序项目工程，所有源代码与算法均为小组成员自主实现，仅供学习交流使用。
