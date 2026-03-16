#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "xorworker.h"

#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QTime>
#include <QRegularExpressionValidator>
#include <QMetaObject>

using namespace Qt::StringLiterals;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_timer(new QTimer(this))
{
    ui->setupUi(this);

    connect(ui->btnBrowseInput, &QPushButton::clicked, this, &MainWindow::onBrowseInput);
    connect(ui->btnBrowseOutput, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    connect(ui->btnRunOnce, &QPushButton::clicked, this, &MainWindow::onStartOnce);
    connect(ui->btnCancel, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(ui->btnTimerToggle, &QPushButton::clicked, this, &MainWindow::onTimerToggle);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTimerTick);

    ui->edtXorKey->setValidator(
        new QRegularExpressionValidator(QRegularExpression(u"[0-9A-Fa-f]{0,16}"_s), this));

    m_workerThread.start();
}

MainWindow::~MainWindow()
{
    m_timer->stop();
    if (m_worker) m_worker->cancel();
    m_workerThread.quit();
    m_workerThread.wait();
    delete ui;
}

void MainWindow::logMessage(const QString &msg)
{
    QString ts = QTime::currentTime().toString(u"HH:mm:ss");
    ui->txtLog->appendPlainText(u"[%1] %2"_s.arg(ts, msg));
}

void MainWindow::setProcessingState(bool running)
{
    m_processing = running;

    ui->grpInput->setEnabled(!running);
    ui->grpOutput->setEnabled(!running);
    ui->grpKey->setEnabled(!running);
    ui->btnRunOnce->setEnabled(!running);
    ui->spnInterval->setEnabled(!running);
    ui->btnCancel->setEnabled(running);
    if (running && !m_timer->isActive())
        ui->btnTimerToggle->setEnabled(false);
    else
        ui->btnTimerToggle->setEnabled(true);

    if (running)
        ui->progressBar->setValue(0);
}

void MainWindow::onBrowseInput()
{
    QString dir = QFileDialog::getExistingDirectory(this, u"Выберите каталог входных файлов"_s);
    if (!dir.isEmpty())
        ui->edtInputDir->setText(dir);
}

void MainWindow::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(this, u"Выберите каталог для результатов"_s);
    if (!dir.isEmpty())
        ui->edtOutputDir->setText(dir);
}

QStringList MainWindow::collectInputFiles()
{
    QDir dir(ui->edtInputDir->text().trimmed());
    if (!dir.exists()) return {};

    QString mask = ui->edtFileMask->text().trimmed();
    if (mask.isEmpty())
        return {};

    static const QRegularExpression sep(u"[;\\s]+"_s);
    QStringList filters = mask.split(sep, Qt::SkipEmptyParts);

    QStringList result;
    for (const auto &fi : dir.entryInfoList(filters, QDir::Files))
        result << fi.absoluteFilePath();
    return result;
}

quint64 MainWindow::parseXorKey(bool *ok) const
{
    QString hex = ui->edtXorKey->text().trimmed();

    if (hex.isEmpty()) {
        *ok = false;
        return 0;
    }

    while (hex.size() < 16)
        hex.append(u'0');
    return hex.toULongLong(ok, 16);
}

bool MainWindow::validateInputs()
{
    if (ui->edtInputDir->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, u"Ошибка"_s, u"Укажите каталог входных файлов."_s);
        return false;
    }

    if (ui->edtOutputDir->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, u"Ошибка"_s, u"Укажите каталог для сохранения результатов."_s);
        return false;
    }

    if (ui->edtFileMask->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, u"Ошибка"_s, u"Укажите маску файлов."_s);
        ui->edtFileMask->setFocus();
        return false;
    }

    bool keyOk = false;
    parseXorKey(&keyOk);
    if (!keyOk) {
        QMessageBox::warning(this, u"Ошибка"_s, u"Введите XOR-ключ."_s);
        ui->edtXorKey->setFocus();
        return false;
    }

    QDir inDir(ui->edtInputDir->text().trimmed());
    QDir outDir(ui->edtOutputDir->text().trimmed());
    if (inDir.absolutePath() == outDir.absolutePath()
        && ui->cmbDuplicate->currentIndex() == 0) {
        auto btn = QMessageBox::warning(this, u"Внимание"_s,
            u"Каталог входных и выходных файлов совпадает, "
            u"а режим — «Перезаписать».\n\n"
            u"Оригиналы файлов будут утеряны!\nПродолжить?"_s,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes)
            return false;
    }

    return true;
}

void MainWindow::onStartOnce()
{
    if (m_processing) {
        logMessage(u"Обработка уже выполняется..."_s);
        return;
    }
    if (!validateInputs()) return;
    startProcessingFiles(collectInputFiles());
}

void MainWindow::onCancel()
{
    if (m_worker) {
        m_worker->cancel();
        logMessage(u"Отправлен запрос на отмену..."_s);
    }
}

void MainWindow::onTimerToggle()
{
    if (m_timer->isActive()) {
        m_timer->stop();
        ui->btnTimerToggle->setText(u"Запустить таймер"_s);
        logMessage(u"Таймер остановлен."_s);
    } else {
        if (!validateInputs()) return;

        int sec = ui->spnInterval->value();
        m_timer->start(sec * 1000);
        ui->btnTimerToggle->setText(u"Остановить таймер"_s);
        logMessage(u"Таймер запущен — каждые %1 сек."_s.arg(sec));
        onTimerTick();
    }
}

void MainWindow::onTimerTick()
{
    logMessage(u"Таймер: опрос каталога..."_s);

    if (m_processing) {
        logMessage(u"Таймер: пропуск — обработка ещё выполняется."_s);
        return;
    }

    QStringList files = collectInputFiles();
    if (files.isEmpty()) {
        logMessage(u"Таймер: файлы по маске не найдены. Ожидание."_s);
        return;
    }

    logMessage(u"Таймер: найдено %1 файл(ов), запуск."_s.arg(files.size()));
    startProcessingFiles(files);
}

void MainWindow::startProcessingFiles(const QStringList &files)
{
    if (files.isEmpty()) {
        logMessage(u"Файлы по заданной маске не найдены."_s);
        return;
    }

    QString outDir = ui->edtOutputDir->text().trimmed();
    QDir().mkpath(outDir);
    setProcessingState(true);

    bool keyOk = false;
    quint64 key = parseXorKey(&keyOk);
    if (!keyOk) {
        logMessage(u"Ошибка: некорректный XOR-ключ, обработка отменена."_s);
        setProcessingState(false);
        return;
    }

    m_worker = new XorWorker();
    m_worker->setFiles(files);
    m_worker->setXorKey(key);
    m_worker->setOutputDir(outDir);
    m_worker->setDeleteInput(ui->chkDeleteInput->isChecked());
    m_worker->setOverwriteMode(ui->cmbDuplicate->currentIndex() == 0);
    m_worker->moveToThread(&m_workerThread);

    connect(m_worker, &XorWorker::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_worker, &XorWorker::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_worker, &XorWorker::errorOccurred, this, &MainWindow::onError);
    connect(m_worker, &XorWorker::finished, this, &MainWindow::onWorkerFinished);

    logMessage(u"Начало обработки: %1 файл(ов)..."_s.arg(files.size()));
    QMetaObject::invokeMethod(m_worker, &XorWorker::process, Qt::QueuedConnection);
}

void MainWindow::onProgressChanged(int pct)
{
    ui->progressBar->setValue(pct);
}

void MainWindow::onStatusMessage(const QString &msg)
{
    logMessage(msg);
    ui->statusbar->showMessage(msg);
}

void MainWindow::onError(const QString &err)
{
    logMessage(u"ОШИБКА: "_s + err);
}

void MainWindow::onWorkerFinished()
{
    m_worker->deleteLater();
    m_worker = nullptr;
    setProcessingState(false);
}
