// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
// Streaming optimizations (c) 2026 PDF4QT-Opus Contributors
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

#ifndef PDFSTREAMINGDOCUMENTWRITER_H
#define PDFSTREAMINGDOCUMENTWRITER_H

#include "pdfdocument.h"
#include "pdfprogress.h"
#include "pdfutils.h"

#include <QIODevice>
#include <memory>
#include <vector>


namespace pdf {

/// Streaming document writer for creating large PDFs without buffering all
/// objects in memory. This class allows incremental building and writing of PDF
/// documents, which is essential for merging many large PDF files without
/// running out of memory.
///
/// Usage:
/// 1. Call beginDocument() to start writing
/// 2. Call writeObject() for each object (returns the object reference)
/// 3. Call addPage() for each page
/// 4. Call endDocument() to finalize (writes xref and trailer)
///
/// Objects are written immediately to the output device, and only their offsets
/// are tracked in memory.
class PDF4QTLIBCORESHARED_EXPORT PDFStreamingDocumentWriter {
  Q_DECLARE_TR_FUNCTIONS(pdf::PDFStreamingDocumentWriter)

public:
  explicit PDFStreamingDocumentWriter(QIODevice *device,
                                      PDFProgress *progress = nullptr);
  ~PDFStreamingDocumentWriter();

  /// Begins writing a new PDF document. Must be called before any writeObject
  /// calls.
  /// @param version PDF version (default 1.7)
  /// @return true if successful
  bool beginDocument(PDFVersion version = PDFVersion(1, 7));

  /// Writes an object to the document and returns its reference.
  /// Objects are written immediately to the output device.
  /// @param object The object to write
  /// @param generation Generation number (usually 0)
  /// @return Reference to the written object
  PDFObjectReference writeObject(const PDFObject &object,
                                 PDFInteger generation = 0);

  /// Reserves an object number for later use.
  /// Useful when you need to reference an object before writing it.
  /// @param generation Generation number (usually 0)
  /// @return Reference that will be used when the object is written
  PDFObjectReference reserveObject(PDFInteger generation = 0);

  /// Writes a previously reserved object.
  /// @param reference The reference returned by reserveObject()
  /// @param object The object to write
  /// @return true if successful
  bool writeReservedObject(PDFObjectReference reference,
                           const PDFObject &object);

  /// Adds a page reference to the document.
  /// Pages will be added to the page tree when endDocument() is called.
  /// @param pageReference Reference to a page object
  void addPage(PDFObjectReference pageReference);

  /// Sets the catalog reference. If not set, a default catalog will be created.
  /// @param catalogReference Reference to the catalog object
  void setCatalogReference(PDFObjectReference catalogReference);

  /// Sets the document info reference.
  /// @param infoReference Reference to the info dictionary
  void setInfoReference(PDFObjectReference infoReference);

  /// Finalizes the document by writing the cross-reference table and trailer.
  /// @return Operation result
  PDFOperationResult endDocument();

  /// Returns the current object count.
  size_t getObjectCount() const { return m_objectOffsets.size(); }

  /// Returns the total bytes written so far.
  qint64 getBytesWritten() const { return m_device ? m_device->pos() : 0; }

  /// Checks if the writer is in a valid state for writing.
  bool isOpen() const { return m_isOpen; }

  /// Creates a simple page tree containing all added pages.
  /// @return Reference to the page tree root
  PDFObjectReference createPageTree();

  /// Creates a default catalog with the given page tree root.
  /// @param pageTreeRoot Reference to the page tree root
  /// @return Reference to the catalog object
  PDFObjectReference createCatalog(PDFObjectReference pageTreeRoot);

private:
  void writeCRLF();
  void writeObjectHeader(PDFObjectReference reference);
  void writeObjectFooter();
  void writeObjectContent(const PDFObject &object);

  struct ObjectEntry {
    PDFInteger offset = -1; // -1 means not yet written
    PDFInteger generation = 0;
    bool isReserved = false;
  };

  QIODevice *m_device;
  PDFProgress *m_progress;
  PDFVersion m_version;
  bool m_isOpen = false;

  std::vector<ObjectEntry> m_objectOffsets;
  std::vector<PDFObjectReference> m_pages;
  PDFObjectReference m_catalogReference;
  PDFObjectReference m_infoReference;
};

/// Helper class for streaming merge operations.
/// Allows merging multiple documents without loading all of them into memory at
/// once.
class PDF4QTLIBCORESHARED_EXPORT PDFStreamingMerger {
  Q_DECLARE_TR_FUNCTIONS(pdf::PDFStreamingMerger)

public:
  explicit PDFStreamingMerger(const QString &outputPath,
                              PDFProgress *progress = nullptr);
  ~PDFStreamingMerger();

  /// Begins the merge operation.
  /// @return true if successful
  bool begin();

  /// Adds all pages from a document to the merged output.
  /// The document is processed and then can be released from memory.
  /// @param document Document to add
  /// @param documentIndex Index for field namespacing (if enabled)
  /// @param namespaceFields Whether to prefix field names
  /// @return true if successful
  bool addDocument(const PDFDocument &document, int documentIndex = 0,
                   bool namespaceFields = false);

  /// Finalizes the merge and closes the output file.
  /// @return Operation result
  PDFOperationResult finish();

  /// Returns the total pages added.
  size_t getTotalPages() const { return m_totalPages; }

  /// Returns the total documents added.
  size_t getTotalDocuments() const { return m_totalDocuments; }

private:
  QString m_outputPath;
  PDFProgress *m_progress;
  std::unique_ptr<QIODevice> m_device;
  std::unique_ptr<PDFStreamingDocumentWriter> m_writer;
  size_t m_totalPages = 0;
  size_t m_totalDocuments = 0;
};

} // namespace pdf

#endif // PDFSTREAMINGDOCUMENTWRITER_H
