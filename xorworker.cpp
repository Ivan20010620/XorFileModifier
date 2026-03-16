#include "xorworker.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>

using namespace Qt::StringLiterals;

constexpr qint64 BUFFER_SIZE = 64 * 1024;

XorWorker::XorWorker(QObject *parent) : QObject(parent) {}

void XorWorker::setFiles(const QStringList &files)
{
    m_files = files;
}

void XorWorker::setXorKey(quint64 key)
{
    m_xorKey = key;
}

void XorWorker::setOutputDir(const QString &dir)
{
    m_outputDir = dir;
}

void XorWorker::setDeleteInput(bool del)
{
    m_deleteInput = del;
}

void XorWorker::setOverwriteMode(bool overwrite)
{
    m_overwrite = overwrite;
}

void XorWorker::cancel()
{
    m_cancelled.store(true);
}

QString XorWorker::resolveOutputPath(const QString &inputFilePath)
{
    QFileInfo fi(inputFilePath);
    QString baseName = fi.completeBaseName();
    QString suffix = fi.suffix();
    QDir outDir(m_outputDir);
    QString outPath = outDir.filePath(fi.fileName());

    if (m_overwrite)
        return outPath;

    if (QFile::exists(outPath)) {
        for (int n = 1; ; ++n) {
            QString newName = baseName + u"_%1"_s.arg(n)
                + (suffix.isEmpty() ? QString() : (u"." + suffix));
            QString candidate = outDir.filePath(newName);
            if (!QFile::exists(candidate))
                return candidate;
        }
    }
    return outPath;
}

bool XorWorker::xorFile(const QString &inputPath, const QString &outputPath,
                        int fileIndex, int totalFiles)
{
    QFile inFile(inputPath);
    if (!inFile.open(QIODevice::ReadOnly)) {
        emit errorOccurred(u"Не удалось открыть входной файл: "_s + inputPath);
        return false;
    }

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(u"Не удалось открыть выходной файл: "_s + outputPath);
        inFile.close();
        return false;
    }

    const qint64 totalSize = inFile.size();
    qint64 processed = 0;

    quint8 keyBytes[8];
    for (int i = 0; i < 8; ++i)
        keyBytes[i] = static_cast<quint8>((m_xorKey >> (56 - i * 8)) & 0xFF);

    qsizetype keyOffset = 0;
    QByteArray buffer;
    bool writeOk = true;

    while (!inFile.atEnd()) {
        if (m_cancelled.load()) {
            outFile.close();
            QFile::remove(outputPath);
            inFile.close();
            return false;
        }

        buffer = inFile.read(BUFFER_SIZE);
        if (buffer.isEmpty())
            break;

        char *data = buffer.data();
        const qsizetype len = buffer.size();
        for (qsizetype i = 0; i < len; ++i)
            data[i] ^= keyBytes[(keyOffset + i) % 8];
        keyOffset = (keyOffset + len) % 8;

        if (outFile.write(buffer) != len) {
            emit errorOccurred(u"Ошибка записи: "_s + outputPath + u" (нет места на диске?)"_s);
            writeOk = false;
            break;
        }

        processed += len;

        if (totalFiles > 0) {
            int pct;
            if (totalSize > 0)
                pct = static_cast<int>((fileIndex * 100 + processed * 100 / totalSize) / totalFiles);
            else
                pct = static_cast<int>((fileIndex + 1) * 100 / totalFiles);
            emit progressChanged(pct);
        }
    }

    if (writeOk && totalSize == 0 && totalFiles > 0)
        emit progressChanged(static_cast<int>((fileIndex + 1) * 100 / totalFiles));

    inFile.close();
    outFile.close();

    if (!writeOk) {
        QFile::remove(outputPath);
        return false;
    }
    return true;
}

void XorWorker::process()
{
    m_cancelled.store(false);
    const int total = m_files.size();
    int succeeded = 0;

    for (int i = 0; i < total; ++i) {
        if (m_cancelled.load()) {
            emit statusMessage(u"Отменено."_s);
            emit finished();
            return;
        }

        const QString &path = m_files.at(i);
        emit statusMessage(u"Обработка [%1/%2]: %3"_s
            .arg(i + 1).arg(total).arg(QFileInfo(path).fileName()));

        QString outPath = resolveOutputPath(path);
        if (xorFile(path, outPath, i, total)) {
            ++succeeded;
            if (m_deleteInput
                && QFileInfo(path).absoluteFilePath() != QFileInfo(outPath).absoluteFilePath())
                QFile::remove(path);
        }
    }

    emit progressChanged(100);
    emit statusMessage(u"Готово. Обработано файлов: %1 из %2."_s.arg(succeeded).arg(total));
    emit finished();
}
