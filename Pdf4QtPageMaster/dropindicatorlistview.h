// PDF4QT-Opus: Custom QListView with visible drop indicator
// Paints a clear visual indicator during drag-drop operations

#ifndef DROPINDICATORLISTVIEW_H
#define DROPINDICATORLISTVIEW_H

#include <QDragMoveEvent>
#include <QListView>
#include <QPainter>


namespace pdfpagemaster {

/// Custom QListView that paints a highly visible drop indicator
class DropIndicatorListView : public QListView {
  Q_OBJECT

public:
  explicit DropIndicatorListView(QWidget *parent = nullptr)
      : QListView(parent), m_dropRow(-1) {
    setAcceptDrops(true);
    setDropIndicatorShown(false); // We'll paint our own
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
  }

protected:
  void dragMoveEvent(QDragMoveEvent *event) override {
    QListView::dragMoveEvent(event);

    // Find drop position
    QModelIndex index = indexAt(event->position().toPoint());
    if (index.isValid()) {
      QRect rect = visualRect(index);
      // Determine if dropping before or after
      if (event->position().y() < rect.center().y()) {
        m_dropRow = index.row();
      } else {
        m_dropRow = index.row() + 1;
      }
    } else {
      m_dropRow = model() ? model()->rowCount() : 0;
    }
    viewport()->update();
  }

  void dragLeaveEvent(QDragLeaveEvent *event) override {
    QListView::dragLeaveEvent(event);
    m_dropRow = -1;
    viewport()->update();
  }

  void dropEvent(QDropEvent *event) override {
    QListView::dropEvent(event);
    m_dropRow = -1;
    viewport()->update();
  }

  void paintEvent(QPaintEvent *event) override {
    QListView::paintEvent(event);

    // Paint custom drop indicator
    if (m_dropRow >= 0) {
      QPainter painter(viewport());
      painter.setRenderHint(QPainter::Antialiasing);

      // Calculate indicator position
      QRect indicatorRect;
      if (m_dropRow < model()->rowCount()) {
        QModelIndex idx = model()->index(m_dropRow, 0);
        QRect itemRect = visualRect(idx);
        indicatorRect = QRect(0, itemRect.top() - 3, viewport()->width(), 6);
      } else if (model()->rowCount() > 0) {
        QModelIndex idx = model()->index(model()->rowCount() - 1, 0);
        QRect itemRect = visualRect(idx);
        indicatorRect = QRect(0, itemRect.bottom() + 1, viewport()->width(), 6);
      }

      // Draw bright indicator bar
      painter.fillRect(indicatorRect, QColor(255, 102, 0)); // Orange
      painter.setPen(QPen(QColor(255, 0, 0), 2));           // Red border
      painter.drawRect(indicatorRect);
    }
  }

private:
  int m_dropRow;
};

} // namespace pdfpagemaster

#endif // DROPINDICATORLISTVIEW_H
