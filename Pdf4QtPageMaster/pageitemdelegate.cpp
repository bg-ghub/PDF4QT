// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pageitemdelegate.h"
#include "pageitemmodel.h"
#include "pdfcompiler.h"
#include "pdfconstants.h"
#include "pdfimage.h"
#include "pdfpainterutils.h"
#include "pdfrenderer.h"
#include "pdfwidgetutils.h"

#include <QAbstractItemView>
#include <QFutureWatcher>
#include <QPainter>
#include <QPixmapCache>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

namespace pdfpagemaster {

// PDF4QT-Opus: Global thread pool for thumbnail rendering, limited to ideal
// thread count
static QThreadPool *getThumbnailThreadPool() {
  static QThreadPool pool;
  static bool initialized = false;
  if (!initialized) {
    // Limit to ideal thread count to prevent thrashing
    pool.setMaxThreadCount(QThread::idealThreadCount());
    initialized = true;
  }
  return &pool;
}

PageItemDelegate::PageItemDelegate(PageItemModel *model, QObject *parent)
    : BaseClass(parent), m_model(model), m_rasterizer(nullptr),
      m_updatePending(false) {
  m_rasterizer = new pdf::PDFRasterizer(this);
  m_rasterizer->reset(pdf::RendererEngine::Blend2D_SingleThread);

  // PDF4QT-Opus: Setup update coalescing timer - batches repaint requests
  m_updateTimer = new QTimer(this);
  m_updateTimer->setSingleShot(true);
  m_updateTimer->setInterval(50); // 50ms coalescing window
  connect(m_updateTimer, &QTimer::timeout, this, [this]() {
    m_updatePending = false;
    Q_EMIT sizeHintChanged(QModelIndex());
  });
}

PageItemDelegate::~PageItemDelegate() {}

void PageItemDelegate::paint(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
  const PageGroupItem *item = m_model->getItem(index);

  if (!item) {
    return;
  }

  QRect rect = option.rect;

  m_dpiScaleRatio = option.widget->devicePixelRatioF();
  QSize scaledSize =
      pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
  int verticalSpacing =
      pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());
  int horizontalSpacing =
      pdf::PDFWidgetUtils::scaleDPI_x(option.widget, getHorizontalSpacing());

  QRect pageBoundingRect =
      QRect(QPoint(rect.left() + (rect.width() - scaledSize.width()) / 2,
                   rect.top() + verticalSpacing),
            scaledSize);

  // Draw page preview
  if (!item->groups.empty()) {
    const PageGroupItem::GroupItem &groupItem = item->groups.front();
    QSizeF rotatedPageSize =
        pdf::PDFPage::getRotatedBox(
            QRectF(QPointF(0, 0), groupItem.rotatedPageDimensionsMM),
            groupItem.pageAdditionalRotation)
            .size();
    QSize pageImageSize =
        rotatedPageSize.scaled(pageBoundingRect.size(), Qt::KeepAspectRatio)
            .toSize();
    QRect pageImageRect(
        pageBoundingRect.topLeft() +
            QPoint((pageBoundingRect.width() - pageImageSize.width()) / 2,
                   (pageBoundingRect.height() - pageImageSize.height()) / 2),
        pageImageSize);

    painter->setBrush(Qt::white);
    painter->drawRect(pageImageRect);

    QPixmap pageImagePixmap = getPageImagePixmap(item, pageImageRect);
    if (!pageImagePixmap.isNull()) {
      painter->drawPixmap(pageImageRect, pageImagePixmap);
    }

    painter->setPen(QPen(Qt::black));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(pageImageRect);
  }

  int textOffset = pageBoundingRect.bottom() + verticalSpacing;
  QRect textRect = option.rect;
  textRect.setTop(textOffset);
  textRect.setHeight(option.fontMetrics.lineSpacing());
  painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
  painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine,
                    m_model->getItemDisplayText(item));
  textRect.translate(0, textRect.height());
  painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine,
                    item->pagesCaption);

  if (option.state.testFlag(QStyle::State_Selected)) {
    QColor selectedColor =
        option.palette.color(QPalette::Active, QPalette::Highlight);
    selectedColor.setAlphaF(0.3f);
    painter->fillRect(rect, selectedColor);
  }

  QPoint tagPoint =
      rect.topRight() + QPoint(-horizontalSpacing, verticalSpacing);
  for (const QString &tag : item->tags) {
    QStringList splitted = tag.split('@', Qt::KeepEmptyParts);
    if (splitted.size() != 2 || splitted.back().isEmpty()) {
      continue;
    }

    QColor color = QColor::fromString(splitted.front());
    QRect bubbleRect = pdf::PDFPainterHelper::drawBubble(
        painter, tagPoint, color, splitted.back(),
        Qt::AlignLeft | Qt::AlignBottom);
    tagPoint.ry() += bubbleRect.height() + verticalSpacing;
  }
}

QSize PageItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const {
  Q_UNUSED(index);

  QSize scaledSize =
      pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
  int height =
      scaledSize.height() + option.fontMetrics.lineSpacing() * 2 +
      2 * pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());
  int width =
      qMax(pdf::PDFWidgetUtils::scaleDPI_x(option.widget, 40),
           scaledSize.width() + 2 * pdf::PDFWidgetUtils::scaleDPI_x(
                                        option.widget, getHorizontalSpacing()));
  return QSize(width, height);
}

QSize PageItemDelegate::getPageImageSize() const { return m_pageImageSize; }

void PageItemDelegate::setPageImageSize(QSize pageImageSize) {
  if (m_pageImageSize != pageImageSize) {
    m_pageImageSize = pageImageSize;
    // PDF4QT-Opus: Clear pending renders since they are for old size
    m_pendingRenders.clear();
    Q_EMIT sizeHintChanged(QModelIndex());
  }
}

QPixmap PageItemDelegate::getPageImagePixmap(const PageGroupItem *item,
                                             QRect rect) const {
  QPixmap pixmap;

  Q_ASSERT(item);
  if (item->groups.empty()) {
    return pixmap;
  }

  const PageGroupItem::GroupItem &groupItem = item->groups.front();
  if (groupItem.pageType == PT_Empty) {
    return pixmap;
  }

  // Generate cache key
  QString key = QString("%1#%2#%3#%4#%5@%6x%7")
                    .arg(groupItem.documentIndex)
                    .arg(groupItem.imageIndex)
                    .arg(int(groupItem.pageAdditionalRotation))
                    .arg(groupItem.pageIndex)
                    .arg(groupItem.pageType)
                    .arg(rect.width())
                    .arg(rect.height());

  if (!QPixmapCache::find(key, &pixmap)) {
    // PDF4QT-Opus: Check if we're already rendering this thumbnail
    if (m_pendingRenders.contains(key)) {
      // Return empty pixmap - placeholder shown, render in progress
      return pixmap;
    }

    // Mark as pending
    m_pendingRenders.insert(key);

    // PDF4QT-Opus: Launch background render using QtConcurrent
    // Capture necessary data for thread-safe rendering
    RenderRequest request;
    request.key = key;
    request.rect = rect;
    request.dpiScaleRatio = m_dpiScaleRatio;
    request.groupItem = groupItem;

    // Get document/image data needed for rendering
    if (groupItem.pageType == PT_DocumentPage) {
      const auto &documents = m_model->getDocuments();
      auto it = documents.find(groupItem.documentIndex);
      if (it != documents.cend()) {
        request.hasDocument = true;
        // Store a copy of document pointer for thread-safe access
        request.documentPtr = &it->second.document;
      }
    } else if (groupItem.pageType == PT_Image) {
      const auto &images = m_model->getImages();
      auto it = images.find(groupItem.imageIndex);
      if (it != images.cend()) {
        request.hasImage = true;
        request.image = it->second.image; // Copy the image
      }
    }

    // PDF4QT-Opus: Launch in limited thread pool for better CPU utilization
    QFuture<QImage> future =
        QtConcurrent::run(getThumbnailThreadPool(), [this, request]() {
          return renderInBackground(request);
        });

    // Create watcher to handle result on main thread
    auto *watcher =
        new QFutureWatcher<QImage>(const_cast<PageItemDelegate *>(this));
    connect(watcher, &QFutureWatcher<QImage>::finished,
            const_cast<PageItemDelegate *>(this), [this, watcher, key]() {
              QImage image = watcher->result();
              if (!image.isNull()) {
                QPixmapCache::insert(key, QPixmap::fromImage(image));
              }
              const_cast<PageItemDelegate *>(this)->m_pendingRenders.remove(
                  key);

              // PDF4QT-Opus: Use coalesced updates - start timer if not already
              // pending This batches multiple thumbnail completions into a
              // single repaint
              if (!m_updatePending) {
                m_updatePending = true;
                m_updateTimer->start();
              }
              watcher->deleteLater();
            });
    watcher->setFuture(future);

    // Return empty for now - placeholder will be shown
    return pixmap;
  }

  return pixmap;
}

QImage
PageItemDelegate::renderInBackground(const RenderRequest &request) const {
  QImage result;

  if (request.groupItem.pageType == PT_DocumentPage && request.hasDocument &&
      request.documentPtr) {
    const pdf::PDFDocument &document = *request.documentPtr;
    const pdf::PDFInteger pageIndex = request.groupItem.pageIndex - 1;

    if (pageIndex >= 0 &&
        pageIndex < pdf::PDFInteger(document.getCatalog()->getPageCount())) {
      const pdf::PDFPage *page = document.getCatalog()->getPage(pageIndex);
      if (!page)
        return result;

      // PDF4QT-Opus: FAST PATH - Try embedded thumbnail first (instant)
      // Many PDFs contain pre-rendered thumbnails, extracting these is much
      // faster than rendering the full page
      QSize targetSize = request.rect.size() * request.dpiScaleRatio;
      pdf::PDFObject thumbnailObj = page->getThumbnail(&document.getStorage());
      if (thumbnailObj.isStream()) {
        try {
          // Create minimal resources for thumbnail decoding
          pdf::PDFCMSManager cmsManager(nullptr);
          cmsManager.setDocument(&document);
          pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();

          // Decode embedded thumbnail image
          pdf::PDFImage thumbnailImage = pdf::PDFImage::createImage(
              &document, thumbnailObj.getStream(), nullptr, false,
              pdf::RenderingIntent::Perceptual, nullptr);

          QImage thumbResult =
              thumbnailImage.getImage(cms.data(), nullptr, nullptr);
          if (!thumbResult.isNull()) {
            // Scale to target size and return (VERY fast)
            result = thumbResult.scaled(targetSize, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
            return result;
          }
        } catch (...) {
          // Thumbnail extraction failed, fall through to full rendering
        }
      }

      // SLOW PATH: Full page rendering (fallback when no embedded thumbnail)
      // Create per-thread rendering resources
      pdf::PDFPrecompiledPage compiledPage;
      pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT,
                                  pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
      pdf::PDFCMSManager cmsManager(nullptr);
      pdf::PDFOptionalContentActivity optionalContentActivity(
          &document, pdf::OCUsage::View, nullptr);

      fontCache.setDocument(pdf::PDFModifiedDocument(
          const_cast<pdf::PDFDocument *>(&document), &optionalContentActivity));
      cmsManager.setDocument(&document);

      pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
      pdf::PDFRenderer renderer(&document, &fontCache, cms.data(),
                                &optionalContentActivity,
                                pdf::PDFRenderer::getDefaultFeatures(),
                                pdf::PDFMeshQualitySettings());
      renderer.compile(&compiledPage, pageIndex);

      // PDF4QT-Opus: Use 1/4 resolution for faster rendering (still looks good
      // for thumbnails)
      QSize imageSize = request.rect.size() * request.dpiScaleRatio;
      QSize previewSize = imageSize / 4; // Quarter resolution for speed
      previewSize = previewSize.expandedTo(QSize(80, 80));

      // Create thread-local rasterizer
      pdf::PDFRasterizer rasterizer(nullptr);
      rasterizer.reset(pdf::RendererEngine::Blend2D_SingleThread);

      result = rasterizer.render(pageIndex, page, &compiledPage, previewSize,
                                 pdf::PDFRenderer::getDefaultFeatures(),
                                 nullptr, cms.data(),
                                 request.groupItem.pageAdditionalRotation);

      // Scale up to target size (fast bilinear is fine for thumbnails)
      if (!result.isNull() && result.size() != imageSize) {
        result = result.scaled(imageSize, Qt::IgnoreAspectRatio,
                               Qt::FastTransformation);
      }
    }
  } else if (request.groupItem.pageType == PT_Image && request.hasImage) {
    const QImage &image = request.image;
    if (!image.isNull()) {
      QSize targetSize = request.rect.size();
      QImage pixmap(targetSize, QImage::Format_ARGB32_Premultiplied);
      pixmap.fill(Qt::transparent);

      QRect drawRect(QPoint(0, 0), targetSize);
      QRect mediaBox(QPoint(0, 0), image.size());
      QRectF rotatedMediaBox = pdf::PDFPage::getRotatedBox(
          mediaBox, request.groupItem.pageAdditionalRotation);
      QTransform matrix = pdf::PDFRenderer::createMediaBoxToDevicePointMatrix(
          rotatedMediaBox, drawRect, request.groupItem.pageAdditionalRotation);

      QPainter painter(&pixmap);
      painter.setWorldTransform(QTransform(matrix));
      painter.translate(0, image.height());
      painter.scale(1.0, -1.0);
      painter.drawImage(0, 0, image);

      result = pixmap;
    }
  }

  return result;
}

} // namespace pdfpagemaster
