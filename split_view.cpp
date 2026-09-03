#include "split_view.h"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QFrame>
#include <QtGui/QPixmap>
#include <QtCore/QSize>

static const double ZoomStep = 1.25;
static const double MinZoom = 0.01;
static const double MaxZoom = 50.0;

SplitViewWidget::SplitViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_viewMode(SingleView)
    , m_zoomLevel(1.0)
    , m_splitter(nullptr)
    , m_originalContainer(nullptr)
    , m_resultContainer(nullptr)
    , m_originalLabel(nullptr)
    , m_resultLabel(nullptr)
    , m_originalScene(nullptr)
    , m_originalView(nullptr)
    , m_resultScene(nullptr)
    , m_resultView(nullptr)
{
    setupUi();
}

SplitViewWidget::~SplitViewWidget()
{
}

// 构建分屏 UI（左右 QGraphicsView + 标签）
void SplitViewWidget::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(3);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background-color: #007ACC; width: 2px; }"
    );

    // Original image panel
    m_originalContainer = new QWidget(this);
    QVBoxLayout *origLayout = new QVBoxLayout(m_originalContainer);
    origLayout->setContentsMargins(0, 0, 0, 0);
    origLayout->setSpacing(0);

    m_originalLabel = new QLabel(QString::fromUtf8("原始图像"), m_originalContainer);
    m_originalLabel->setAlignment(Qt::AlignCenter);
    m_originalLabel->setFixedHeight(28);
    m_originalLabel->setStyleSheet(
        "QLabel { background-color: #3C3C40; color: #E0E0E0; font-size: 13px; "
        "font-weight: bold; padding: 4px; border-bottom: 1px solid #505050; }"
    );

    m_originalScene = new QGraphicsScene(this);
    m_originalScene->setBackgroundBrush(QColor("#2D2D30"));

    m_originalView = new QGraphicsView(m_originalScene, m_originalContainer);
    m_originalView->setRenderHints(QPainter::Antialiasing);
    m_originalView->setOptimizationFlags(QGraphicsView::DontAdjustForAntialiasing);
    m_originalView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_originalView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_originalView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    m_originalView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_originalView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_originalView->setFrameShape(QFrame::NoFrame);

    origLayout->addWidget(m_originalLabel);
    origLayout->addWidget(m_originalView);

    // Result image panel
    m_resultContainer = new QWidget(this);
    QVBoxLayout *resLayout = new QVBoxLayout(m_resultContainer);
    resLayout->setContentsMargins(0, 0, 0, 0);
    resLayout->setSpacing(0);

    m_resultLabel = new QLabel(QString::fromUtf8("处理结果"), m_resultContainer);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    m_resultLabel->setFixedHeight(28);
    m_resultLabel->setStyleSheet(
        "QLabel { background-color: #3C3C40; color: #E0E0E0; font-size: 13px; "
        "font-weight: bold; padding: 4px; border-bottom: 1px solid #505050; }"
    );

    m_resultScene = new QGraphicsScene(this);
    m_resultScene->setBackgroundBrush(QColor("#2D2D30"));

    m_resultView = new QGraphicsView(m_resultScene, m_resultContainer);
    m_resultView->setRenderHints(QPainter::Antialiasing);
    m_resultView->setOptimizationFlags(QGraphicsView::DontAdjustForAntialiasing);
    m_resultView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_resultView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_resultView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    m_resultView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_resultView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_resultView->setFrameShape(QFrame::NoFrame);

    resLayout->addWidget(m_resultLabel);
    resLayout->addWidget(m_resultView);

    m_splitter->addWidget(m_originalContainer);
    m_splitter->addWidget(m_resultContainer);

    mainLayout->addWidget(m_splitter);

    // Initial state: side by side
    setViewMode(SideBySide);
}

// 设置左侧原始图像
void SplitViewWidget::setOriginalImage(const QPixmap &pixmap, const QString &label)
{
    m_originalScene->clear();
    m_originalScene->addPixmap(pixmap);
    m_originalScene->setSceneRect(pixmap.rect());
    m_originalLabel->setText(label);
    m_originalView->fitInView(m_originalScene->sceneRect(), Qt::KeepAspectRatio);
}

// 设置右侧处理结果图像
void SplitViewWidget::setResultImage(const QPixmap &pixmap, const QString &label)
{
    m_resultScene->clear();
    m_resultScene->addPixmap(pixmap);
    m_resultScene->setSceneRect(pixmap.rect());
    m_resultLabel->setText(label);
    m_resultView->fitInView(m_resultScene->sceneRect(), Qt::KeepAspectRatio);
}

// 切换视图模式（单图/并排/叠加）
void SplitViewWidget::setViewMode(ViewMode mode)
{
    m_viewMode = mode;

    switch (mode)
    {
    case SingleView:
        m_originalContainer->show();
        m_resultContainer->hide();
        break;
    case SideBySide:
        m_originalContainer->show();
        m_resultContainer->show();
        {
            QList<int> sizes;
            int half = m_splitter->width() / 2;
            sizes << half << half;
            m_splitter->setSizes(sizes);
        }
        break;
    case Overlay:
        m_originalContainer->show();
        m_resultContainer->show();
        break;
    }

    synchronizeViews();
}

// 分屏 - 放大（两视图同步）
void SplitViewWidget::zoomIn()
{
    if (m_zoomLevel >= MaxZoom)
        return;

    m_zoomLevel *= ZoomStep;
    applyZoom();
}

// 分屏 - 缩小（两视图同步）
void SplitViewWidget::zoomOut()
{
    if (m_zoomLevel <= MinZoom)
        return;

    m_zoomLevel /= ZoomStep;
    applyZoom();
}

// 分屏 - 适用窗口
void SplitViewWidget::zoomFit()
{
    m_originalView->fitInView(m_originalScene->sceneRect(), Qt::KeepAspectRatio);
    m_zoomLevel = m_originalView->transform().m11();
    m_resultView->fitInView(m_resultScene->sceneRect(), Qt::KeepAspectRatio);
    emit zoomChanged(m_zoomLevel);
}

// 分屏 - 原始比例（1:1）
void SplitViewWidget::zoomOriginal()
{
    m_originalView->resetTransform();
    m_resultView->resetTransform();
    m_zoomLevel = 1.0;
    emit zoomChanged(m_zoomLevel);
}

// 清空所有视图内容
void SplitViewWidget::clear()
{
    m_originalScene->clear();
    m_resultScene->clear();
    m_zoomLevel = 1.0;
}

// 将缩放级别应用到两个视图
void SplitViewWidget::applyZoom()
{
    m_originalView->resetTransform();
    m_originalView->scale(m_zoomLevel, m_zoomLevel);

    m_resultView->resetTransform();
    m_resultView->scale(m_zoomLevel, m_zoomLevel);

    emit zoomChanged(m_zoomLevel);
}

// 同步两个视图的滚动位置
void SplitViewWidget::synchronizeViews()
{
    // Sync scroll positions and zoom
    if (m_viewMode != SingleView)
    {
        m_resultView->setTransform(m_originalView->transform());

        // Sync horizontal scroll
        connect(m_originalView->horizontalScrollBar(), &QScrollBar::valueChanged,
                m_resultView->horizontalScrollBar(), &QScrollBar::setValue);
        connect(m_resultView->horizontalScrollBar(), &QScrollBar::valueChanged,
                m_originalView->horizontalScrollBar(), &QScrollBar::setValue);

        // Sync vertical scroll
        connect(m_originalView->verticalScrollBar(), &QScrollBar::valueChanged,
                m_resultView->verticalScrollBar(), &QScrollBar::setValue);
        connect(m_resultView->verticalScrollBar(), &QScrollBar::valueChanged,
                m_originalView->verticalScrollBar(), &QScrollBar::setValue);
    }
}

// 窗口大小改变时重新适配
void SplitViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_viewMode == SideBySide)
    {
        QList<int> sizes;
        int half = m_splitter->width() / 2;
        sizes << half << half;
        m_splitter->setSizes(sizes);
    }
}
