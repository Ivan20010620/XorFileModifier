#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class XorWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onBrowseInput();
    void onBrowseOutput();
    void onStartOnce();
    void onCancel();
    void onTimerToggle();
    void onTimerTick();
    void onWorkerFinished();
    void onProgressChanged(int pct);
    void onStatusMessage(const QString &msg);
    void onError(const QString &err);

private:
    void startProcessingFiles(const QStringList &files);
    QStringList collectInputFiles();
    bool validateInputs();
    quint64 parseXorKey(bool *ok) const;
    void setProcessingState(bool running);
    void logMessage(const QString &msg);

    Ui::MainWindow *ui;

    QThread m_workerThread;
    XorWorker *m_worker = nullptr;
    bool m_processing = false;
    QTimer *m_timer;
};

#endif // MAINWINDOW_H
