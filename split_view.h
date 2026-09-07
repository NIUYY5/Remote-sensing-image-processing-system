#pragma once

#include <QtWidgets/QWidget>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QLabel>
#include <QtCore/QString>

// 分屏对比视图 - 左右分屏同时显示原始图像和处理结果
class SplitViewWidget : public QWidget
{
    Q_OBJECT

public:
    enum ViewMode {
        SingleView,   // 仅显示原始图像
        SideBySide,   // 左右并排对比
        Overlay       // 叠加对比（半透明）
    };

    explicit SplitViewWidget(QWidget *parent = nullptr);
    ~SplitViewWidget();

    // Set images for comparison
    void setOriginalImage(const QPixmap &pixmap, const QString &label = QString::fromUtf8("原始图像"));
    void setResultImage(const QPixmap &pixmap, const QString &label = QString::fromUtf8("处理结果"));

    // Switch view mode
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }

    // Zoom operations (applied synchronously to both views)
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomOriginal();

    // Get current zoom level
    double zoomLevel() const { return m_zoomLevel; }

    // Access graphics views
    QGraphicsView* originalView() { return m_originalView; }
    QGraphicsView* resultView() { return m_resultView; }

    // Clear all content
    void clear();

signals:
    void zoomChanged(double level);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void applyZoom();
    void synchronizeViews();

    ViewMode m_viewMode;
    double m_zoomLevel;

    QSplitter *m_splitter;
    QWidget *m_originalContainer;
    QWidget *m_resultContainer;
    QLabel *m_originalLabel;
    QLabel *m_resultLabel;
    QGraphicsScene *m_originalScene;
    QGraphicsScene *m_resultScene;
    QGraphicsView *m_originalView;
    QGraphicsView *m_resultView;
};
