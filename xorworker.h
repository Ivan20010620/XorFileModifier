#ifndef XORWORKER_H
#define XORWORKER_H

#include <QObject>
#include <QStringList>
#include <atomic>

class XorWorker : public QObject
{
    Q_OBJECT
public:
    explicit XorWorker(QObject *parent = nullptr);

    void setFiles(const QStringList &files);
    void setXorKey(quint64 key);
    void setOutputDir(const QString &dir);
    void setDeleteInput(bool del);
    void setOverwriteMode(bool overwrite);

public slots:
    void process();
    void cancel();

signals:
    void progressChanged(int percent);
    void statusMessage(const QString &msg);
    void finished();
    void errorOccurred(const QString &error);

private:
    QString resolveOutputPath(const QString &inputFilePath);
    bool xorFile(const QString &inputPath, const QString &outputPath,
                 int fileIndex, int totalFiles);

    QStringList m_files;
    quint64 m_xorKey = 0;
    QString m_outputDir;
    bool m_deleteInput = false;
    bool m_overwrite = true;
    std::atomic<bool> m_cancelled{false};
};

#endif // XORWORKER_H
