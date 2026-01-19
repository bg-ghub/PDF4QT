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
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfstreamingdocumentwriter.h"
#include "pdfconstants.h"
#include "pdfvisitor.h"
#include "pdfparser.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentbuilder.h"
#include "pdfobjectutils.h"

#include <QFile>
#include <QSaveFile>

#include "pdfdbgheap.h"

namespace pdf
{

// Reuse the write visitor from pdfdocumentwriter.cpp
class PDFStreamingWriteObjectVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFStreamingWriteObjectVisitor(QIODevice* device) :
        m_device(device)
    {
    }

    virtual void visitNull() override { m_device->write("null "); }
    virtual void visitBool(bool value) override { m_device->write(value ? "true " : "false "); }
    virtual void visitInt(PDFInteger value) override { m_device->write(QString::number(value).toLatin1()); m_device->write(" "); }
    virtual void visitReal(PDFReal value) override { m_device->write(QString::number(value, 'f', 5).toLatin1()); m_device->write(" "); }
    
    virtual void visitString(PDFStringRef string) override
    {
        QByteArray data = string.getString();
        if (data.indexOf('(') != -1 || data.indexOf(')') != -1 || data.indexOf('\\') != -1)
        {
            m_device->write("<");
            m_device->write(data.toHex());
            m_device->write(">");
        }
        else
        {
            m_device->write("(");
            m_device->write(data);
            m_device->write(")");
        }
        m_device->write(" ");
    }
    
    virtual void visitName(PDFStringRef name) override
    {
        m_device->write("/");
        for (const char character : name.getString())
        {
            if (PDFLexicalAnalyzer::isRegular(character))
            {
                m_device->write(&character, 1);
            }
            else
            {
                m_device->write("#");
                m_device->write(QByteArray(&character, 1).toHex());
            }
        }
        m_device->write(" ");
    }
    
    virtual void visitArray(const PDFArray* array) override
    {
        m_device->write("[ ");
        acceptArray(array);
        m_device->write("] ");
    }
    
    virtual void visitDictionary(const PDFDictionary* dictionary) override
    {
        m_device->write("<< ");
        for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
        {
            visitName(PDFStringRef(dictionary->getKey(i)));
            dictionary->getValue(i).accept(this);
        }
        m_device->write(">> ");
    }
    
    virtual void visitStream(const PDFStream* stream) override
    {
        visitDictionary(stream->getDictionary());
        m_device->write("stream");
        m_device->write("\x0D\x0A");
        m_device->write(*stream->getContent());
        m_device->write("\x0D\x0A");
        m_device->write("endstream");
        m_device->write("\x0D\x0A");
    }
    
    virtual void visitReference(const PDFObjectReference reference) override
    {
        visitInt(reference.objectNumber);
        visitInt(reference.generation);
        m_device->write("R ");
    }

private:
    QIODevice* m_device;
};

// ============================================================================
// PDFStreamingDocumentWriter implementation
// ============================================================================

PDFStreamingDocumentWriter::PDFStreamingDocumentWriter(QIODevice* device, PDFProgress* progress) :
    m_device(device),
    m_progress(progress),
    m_version(1, 7),
    m_isOpen(false)
{
    // Reserve object 0 (always free)
    m_objectOffsets.push_back({-1, 65535, false});
}

PDFStreamingDocumentWriter::~PDFStreamingDocumentWriter()
{
    // Note: We don't close the device here - that's the caller's responsibility
}

bool PDFStreamingDocumentWriter::beginDocument(PDFVersion version)
{
    if (!m_device || !m_device->isWritable())
    {
        return false;
    }

    m_version = version;
    m_isOpen = true;

    // Write PDF header
    m_device->write(QString("%PDF-%1.%2").arg(version.major).arg(version.minor).toLatin1());
    writeCRLF();
    m_device->write("% PDF producer: ");
    m_device->write(PDF_LIBRARY_NAME);
    m_device->write(" (PDF4QT-Opus Streaming Writer)");
    writeCRLF();
    // Write binary marker (recommended for files with binary content)
    m_device->write("%");
    m_device->write("\xE2\xE3\xCF\xD3");
    writeCRLF();
    writeCRLF();

    return true;
}

PDFObjectReference PDFStreamingDocumentWriter::writeObject(const PDFObject& object, PDFInteger generation)
{
    if (!m_isOpen)
    {
        return PDFObjectReference();
    }

    PDFInteger objectNumber = static_cast<PDFInteger>(m_objectOffsets.size());
    PDFObjectReference reference(objectNumber, generation);

    // Record offset before writing
    ObjectEntry entry;
    entry.offset = m_device->pos();
    entry.generation = generation;
    entry.isReserved = false;
    m_objectOffsets.push_back(entry);

    // Write the object
    writeObjectHeader(reference);
    writeObject(object);
    writeObjectFooter();

    return reference;
}

PDFObjectReference PDFStreamingDocumentWriter::reserveObject(PDFInteger generation)
{
    PDFInteger objectNumber = static_cast<PDFInteger>(m_objectOffsets.size());
    
    ObjectEntry entry;
    entry.offset = -1;  // Not yet written
    entry.generation = generation;
    entry.isReserved = true;
    m_objectOffsets.push_back(entry);

    return PDFObjectReference(objectNumber, generation);
}

bool PDFStreamingDocumentWriter::writeReservedObject(PDFObjectReference reference, const PDFObject& object)
{
    if (!m_isOpen || reference.objectNumber < 0 || 
        static_cast<size_t>(reference.objectNumber) >= m_objectOffsets.size())
    {
        return false;
    }

    ObjectEntry& entry = m_objectOffsets[reference.objectNumber];
    if (!entry.isReserved || entry.offset != -1)
    {
        return false;  // Not reserved or already written
    }

    // Record offset and write
    entry.offset = m_device->pos();
    entry.isReserved = false;

    writeObjectHeader(reference);
    writeObject(object);
    writeObjectFooter();

    return true;
}

void PDFStreamingDocumentWriter::addPage(PDFObjectReference pageReference)
{
    m_pages.push_back(pageReference);
}

void PDFStreamingDocumentWriter::setCatalogReference(PDFObjectReference catalogReference)
{
    m_catalogReference = catalogReference;
}

void PDFStreamingDocumentWriter::setInfoReference(PDFObjectReference infoReference)
{
    m_infoReference = infoReference;
}

PDFObjectReference PDFStreamingDocumentWriter::createPageTree()
{
    // Create the page tree root with all pages
    PDFObjectFactory factory;
    
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Pages");
    factory.endDictionaryItem();
    
    factory.beginDictionaryItem("Kids");
    factory.beginArray();
    for (const PDFObjectReference& pageRef : m_pages)
    {
        factory << pageRef;
    }
    factory.endArray();
    factory.endDictionaryItem();
    
    factory.beginDictionaryItem("Count");
    factory << PDFInteger(m_pages.size());
    factory.endDictionaryItem();
    
    factory.endDictionary();

    return writeObject(factory.takeObject());
}

PDFObjectReference PDFStreamingDocumentWriter::createCatalog(PDFObjectReference pageTreeRoot)
{
    PDFObjectFactory factory;
    
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Catalog");
    factory.endDictionaryItem();
    
    factory.beginDictionaryItem("Pages");
    factory << pageTreeRoot;
    factory.endDictionaryItem();
    
    factory.endDictionary();

    return writeObject(factory.takeObject());
}

PDFOperationResult PDFStreamingDocumentWriter::endDocument()
{
    if (!m_isOpen)
    {
        return tr("Document is not open.");
    }

    // Check for unwritten reserved objects
    for (size_t i = 1; i < m_objectOffsets.size(); ++i)
    {
        if (m_objectOffsets[i].isReserved && m_objectOffsets[i].offset == -1)
        {
            return tr("Reserved object %1 was never written.").arg(i);
        }
    }

    // Create catalog if not set
    if (!m_catalogReference.isValid())
    {
        PDFObjectReference pageTreeRoot = createPageTree();
        m_catalogReference = createCatalog(pageTreeRoot);
    }

    // Write cross-reference table
    PDFInteger xrefOffset = m_device->pos();
    m_device->write("xref");
    writeCRLF();
    m_device->write(QString("0 %1").arg(m_objectOffsets.size()).toLatin1());
    writeCRLF();

    for (size_t i = 0; i < m_objectOffsets.size(); ++i)
    {
        const ObjectEntry& entry = m_objectOffsets[i];
        PDFInteger generation = entry.generation;
        PDFInteger offset = entry.offset;

        if (offset == -1)
        {
            offset = 0;
        }

        QString offsetString = QString::number(offset).rightJustified(10, QChar('0'), true);
        QString generationString = QString::number(generation).rightJustified(5, QChar('0'), true);

        m_device->write(offsetString.toLatin1());
        m_device->write(" ");
        m_device->write(generationString.toLatin1());
        m_device->write(" ");
        m_device->write((i == 0 || entry.offset == -1) ? "f" : "n");
        writeCRLF();
    }

    // Write trailer
    PDFObjectFactory factory;
    factory.beginDictionary();
    
    factory.beginDictionaryItem("Size");
    factory << PDFInteger(m_objectOffsets.size());
    factory.endDictionaryItem();
    
    factory.beginDictionaryItem("Root");
    factory << m_catalogReference;
    factory.endDictionaryItem();
    
    if (m_infoReference.isValid())
    {
        factory.beginDictionaryItem("Info");
        factory << m_infoReference;
        factory.endDictionaryItem();
    }
    
    factory.endDictionary();

    m_device->write("trailer");
    writeCRLF();
    
    PDFStreamingWriteObjectVisitor visitor(m_device);
    factory.takeObject().accept(&visitor);
    
    writeCRLF();
    m_device->write("startxref");
    writeCRLF();
    m_device->write(QString::number(xrefOffset).toLatin1());
    writeCRLF();
    m_device->write("%%EOF");

    m_isOpen = false;
    return true;
}

void PDFStreamingDocumentWriter::writeCRLF()
{
    m_device->write("\x0D\x0A");
}

void PDFStreamingDocumentWriter::writeObjectHeader(PDFObjectReference reference)
{
    QString objectHeader = QString("%1 %2 obj").arg(QString::number(reference.objectNumber), QString::number(reference.generation));
    m_device->write(objectHeader.toLatin1());
    writeCRLF();
}

void PDFStreamingDocumentWriter::writeObjectFooter()
{
    m_device->write("endobj");
    writeCRLF();
}

void PDFStreamingDocumentWriter::writeObject(const PDFObject& object)
{
    PDFStreamingWriteObjectVisitor visitor(m_device);
    object.accept(&visitor);
}

// ============================================================================
// PDFStreamingMerger implementation
// ============================================================================

PDFStreamingMerger::PDFStreamingMerger(const QString& outputPath, PDFProgress* progress) :
    m_outputPath(outputPath),
    m_progress(progress),
    m_totalPages(0),
    m_totalDocuments(0)
{
}

PDFStreamingMerger::~PDFStreamingMerger()
{
    // Ensure cleanup
    m_writer.reset();
    m_device.reset();
}

bool PDFStreamingMerger::begin()
{
    auto file = std::make_unique<QSaveFile>(m_outputPath);
    file->setDirectWriteFallback(true);
    
    if (!file->open(QFile::WriteOnly | QFile::Truncate))
    {
        return false;
    }

    m_device = std::move(file);
    m_writer = std::make_unique<PDFStreamingDocumentWriter>(m_device.get(), m_progress);
    
    return m_writer->beginDocument();
}

bool PDFStreamingMerger::addDocument(const PDFDocument& document, int documentIndex, bool namespaceFields)
{
    Q_UNUSED(documentIndex);
    Q_UNUSED(namespaceFields);
    
    if (!m_writer || !m_writer->isOpen())
    {
        return false;
    }

    // Get the document's object storage
    const PDFObjectStorage& storage = document.getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();

    // Create a mapping from old references to new references
    std::map<PDFObjectReference, PDFObjectReference> referenceMapping;

    // First pass: reserve all object numbers
    for (size_t i = 1; i < objects.size(); ++i)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        if (!entry.object.isNull())
        {
            PDFObjectReference oldRef(i, entry.generation);
            PDFObjectReference newRef = m_writer->reserveObject(0);
            referenceMapping[oldRef] = newRef;
        }
    }

    // Second pass: write all objects with updated references
    for (size_t i = 1; i < objects.size(); ++i)
    {
        const PDFObjectStorage::Entry& entry = objects[i];
        if (!entry.object.isNull())
        {
            PDFObjectReference oldRef(i, entry.generation);
            PDFObjectReference newRef = referenceMapping[oldRef];
            
            // Replace references in the object
            PDFObject updatedObject = PDFObjectUtils::replaceReferences(entry.object, referenceMapping);
            
            m_writer->writeReservedObject(newRef, updatedObject);
        }
    }

    // Add pages from this document
    const PDFCatalog* catalog = document.getCatalog();
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const PDFPage* page = catalog->getPage(i);
        if (page)
        {
            PDFObjectReference oldPageRef = page->getPageReference();
            auto it = referenceMapping.find(oldPageRef);
            if (it != referenceMapping.end())
            {
                m_writer->addPage(it->second);
                ++m_totalPages;
            }
        }
    }

    ++m_totalDocuments;
    return true;
}

PDFOperationResult PDFStreamingMerger::finish()
{
    if (!m_writer)
    {
        return tr("Merger is not initialized.");
    }

    PDFOperationResult result = m_writer->endDocument();
    
    if (result)
    {
        // Commit the file
        QSaveFile* saveFile = dynamic_cast<QSaveFile*>(m_device.get());
        if (saveFile && !saveFile->commit())
        {
            result = tr("Failed to save file: %1").arg(saveFile->errorString());
        }
    }
    
    m_writer.reset();
    m_device.reset();
    
    return result;
}

}   // namespace pdf
