#include "RealTimeTranslator.h"
#include <qpushbutton.h>
#include "characterrecognize.h"
#include "qdialog.h"
#include "QtConcurrent/QtConcurrent"
#include "AppSelectDialog.h"
#include "LanguageMapping.h"

RealTimeTranslator::RealTimeTranslator(QWidget* parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

	createLanguageMenu();

	m_selectAppButton = findChild<QPushButton*>("selectAppButton");
	connect(m_selectAppButton, &QPushButton::clicked, this, &RealTimeTranslator::selectApp);
	m_captureButton = findChild<QPushButton*>("captureButton");
	connect(m_captureButton, &QPushButton::clicked, this,
		[this](bool clicked) { QtConcurrent::run(this, &RealTimeTranslator::captureAndTranslate, clicked); });
	m_roiButton = findChild<QPushButton*>("ROIButton");
	connect(m_roiButton, &QPushButton::clicked, this, &RealTimeTranslator::selectRoi);
	m_translateButton = findChild<QPushButton*>("translateButton");
	connect(m_translateButton, &QPushButton::clicked, this, &RealTimeTranslator::translate);
	m_glossaryButton = findChild<QPushButton*>("glossaryButton");
	m_glossaryButton->setHidden(true);
	connect(m_glossaryButton, &QPushButton::clicked, &m_glossary, &GlossaryManager::showDialog);
	m_fontColorButton = findChild<QPushButton*>("fontColorButton");
	connect(m_fontColorButton, &QPushButton::clicked, this, &RealTimeTranslator::selectFontColor);
	m_originalTextEdit = findChild<QTextEdit*>("textEdit");
	connect(this, &RealTimeTranslator::setOriginalText, m_originalTextEdit, &QTextEdit::setText);
	m_translateTextEdit = findChild<QTextEdit*>("textEdit_2");
	connect(this, &RealTimeTranslator::setTranslateText, m_translateTextEdit, &QTextEdit::setText);
	m_fontColorCheckBox = findChild<QCheckBox*>("checkBox");
	m_srcLanguageComboBox = findChild<QComboBox*>("srcLanguageComboBox");
	m_tgtLanguageComboBox = findChild<QComboBox*>("tgtLanguageComboBox");
	for (int idx = 0; idx <= QOnlineTranslator::Language::Zulu; idx++)
	{
		QString language = QVariant::fromValue(QOnlineTranslator::Language(idx)).toString();
		m_srcLanguageComboBox->addItem(language);
		m_tgtLanguageComboBox->addItem(language);
	}
	m_srcLanguageComboBox->setCurrentIndex(m_sourceLanguage);
	m_tgtLanguageComboBox->setCurrentIndex(m_targetLanguage);
	connect(m_srcLanguageComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(setSourceLanguage(int)));
	connect(m_tgtLanguageComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(setTargetLanguage(int)));

	m_roi.left = 0;
	m_roi.right = 0;
	m_roi.top = 0;
	m_roi.bottom = 0;

	connect(this, &RealTimeTranslator::beginTranslate, this, &RealTimeTranslator::translate);
}

void RealTimeTranslator::selectApp(bool clicked)
{
	auto appList = m_watcher.getAppInfoList();
	auto appSelectDialog = new AppSelectDialog(appList);
	appSelectDialog->show();
	connect(appSelectDialog, &AppSelectDialog::selectApp, this, [this](QString str) {this->m_watcher.setApplication(str); });
}

void RealTimeTranslator::captureAndTranslate(bool clicked)
{
	PIX* pix = m_watcher.capture(m_roi);
	BOX* roi = new Box();
	roi->x = m_roi.left;
	roi->y = m_roi.top;
	roi->w = m_roi.right-m_roi.left;
	roi->h = m_roi.bottom - m_roi.top;
	PIX* processed;
	if (roi->w == 0 || roi->h == 0)
	{
		processed = pix;
	}
	else
	{
		processed = pixClipRectangle(pix, roi, NULL);
	}
	pixWrite("D:/capture.png", processed, IFF_PNG);
	emit setOriginalText("Recognizing");
	if (m_fontColorCheckBox->isChecked())
	{
		pixWrite("D:/TestFile_Pre_Process.png", processed, IFF_PNG);
		thresholdByFontColor(processed);
		pixWrite("D:/TestFile_Post_Process.png", processed, IFF_PNG);
	}
	QString language = languageMapping::qtToTesseract[m_sourceLanguage];
	QString capture = ocr(processed, language);
	QStringList list1 = capture.split('\n');
	QString simplified; 
	for (auto str : list1)
	{
		if (str.size() == 0)
		{
			continue;
		}
		if (str == 32)
		{
			continue;
		}
		simplified.append(str);
		simplified.append('\n');
	}
	emit setOriginalText(simplified);
	emit beginTranslate(true);
}

void RealTimeTranslator::translate(bool clicked)
{
	QString original = m_originalTextEdit->toPlainText();
	std::map<QString, QString> dict;
	auto encoded = m_glossary.encode(original, dict);
	emit setTranslateText("Translating");
	m_translator.translate(encoded, QOnlineTranslator::Google, m_targetLanguage);
	QObject::connect(&m_translator, &QOnlineTranslator::finished, [=] {
		if (this->m_translator.error() == QOnlineTranslator::NoError)
		{
			auto translation = this->m_translator.translation();
			auto decoded = m_glossary.decode(translation, dict);
			emit setTranslateText(decoded);
		}
		else
		{
			emit setTranslateText(this->m_translator.errorString());
		}
		});
}

void RealTimeTranslator::selectRoi(bool clicked)
{
	auto windowRect = m_watcher.getWindowSize();
	RECT emptyRect;
	emptyRect.left = 0;
	emptyRect.right = 0;
	emptyRect.top = 0;
	emptyRect.bottom = 0;
	PIX* img = m_watcher.capture(emptyRect);
	auto canvas = new InvisibleCanvas(InvisibleCanvas::Mode::ROI);
	connect(canvas, &InvisibleCanvas::setROI, this, [this](RECT rect) {this->m_roi = rect; });
	canvas->showCanvas(img, windowRect);
}

void RealTimeTranslator::selectFontColor(bool clicked)
{
	auto windowRect = m_watcher.getWindowSize();
	RECT emptyRect;
	emptyRect.left = 0;
	emptyRect.right = 0;
	emptyRect.top = 0;
	emptyRect.bottom = 0;
	PIX* img = m_watcher.capture(emptyRect);
	auto canvas = new InvisibleCanvas(InvisibleCanvas::Mode::Color);
	connect(canvas, &InvisibleCanvas::setColor, this, [this](QColor color) {this->m_color = color; });
	canvas->showCanvas(img, windowRect);
}

void RealTimeTranslator::setSourceLanguage(int idx)
{
	m_sourceLanguage = QOnlineTranslator::Language(idx);
}

void RealTimeTranslator::setTargetLanguage(int idx)
{
	m_targetLanguage = QOnlineTranslator::Language(idx);
}


void RealTimeTranslator::thresholdByFontColor(PIX* pix)
{
	unsigned char r1 = m_color.redF() * 255;
	unsigned char g1 = m_color.greenF() * 255;
	unsigned char b1 = m_color.blueF() * 255;
	const auto isSimilarColor = [=](l_int32 r, l_int32 g, l_int32 b)
	{
		if ((r - r1) * (r - r1) + (g - g1) * (g - g1) + (b - b1) * (b - b1) <= 1000)
		{
			return true;
		}
		else
		{
			return false;
		}
	};
	for (int i = 0; i < pix->w; i++)
	{
		for (int j = 0; j < pix->h; j++)
		{
			l_int32 r = 0;
			l_int32 g = 0;
			l_int32 b = 0;
			pixGetRGBPixel(pix, i, j, &r, &g, &b);
			if (isSimilarColor(r, g, b))
			{
				pixSetRGBPixel(pix, i, j, 255, 255, 255);
			}
			else
			{
				pixSetRGBPixel(pix, i, j, 0, 0, 0);
			}
		}
	}
}

void RealTimeTranslator::switchTranslator(QTranslator& translator, const QString& filename)
{
	// remove the old translator
	qApp->removeTranslator(&translator);
	 
	// load the new translator
	QString path = QApplication::applicationDirPath();
	path.append("/");
	if (translator.load(path + filename)) //Here Path and Filename has to be entered because the system didn't find the QM Files else
		qApp->installTranslator(&translator);
}

void RealTimeTranslator::loadLanguage(const QString& rLanguage)
{
	if (m_currUILang != rLanguage) {
		m_currUILang = rLanguage;
		QLocale locale = QLocale(m_currUILang);
		QLocale::setDefault(locale);
		QString languageName = QLocale::languageToString(locale.language());
		switchTranslator(m_uiTranslator, QString("realtimetranslator_%1.qm").arg(rLanguage));
		//ui.statusBar->showMessage(tr("Current Language changed to %1").arg(languageName));
	}
}

void RealTimeTranslator::createLanguageMenu(void)
{
	QActionGroup* langGroup = new QActionGroup(ui.menuLanguage);
	langGroup->setExclusive(true);

	connect(langGroup, SIGNAL(triggered(QAction*)), this, SLOT(slotLanguageChanged(QAction*)));

	// format systems language
	QString defaultLocale = QLocale::system().name(); // e.g. "de_DE"
	defaultLocale.truncate(defaultLocale.lastIndexOf('_')); // e.g. "de"

	QDir dir("");
	QStringList fileNames = dir.entryList(QStringList("realtimetranslator_*.qm"));

	for (int i = 0; i < fileNames.size(); ++i) {
		// get locale extracted by filename
		QString locale;
		locale = fileNames[i]; // "TranslationExample_de.qm"
		locale.truncate(locale.lastIndexOf('.')); // "TranslationExample_de"
		locale.remove(0, locale.indexOf('_') + 1); // "de"

		QString lang = QLocale::languageToString(QLocale(locale).language());

		QAction* action = new QAction(lang, this);
		action->setCheckable(true);
		action->setData(locale);

		ui.menuLanguage->addAction(action);
		langGroup->addAction(action);

		// set default translators and language checked
		if (defaultLocale == locale)
		{
			action->setChecked(true);
			slotLanguageChanged(action);
		}
	}
}


void RealTimeTranslator::changeEvent(QEvent* event)
{
	if (0 != event) {
		switch (event->type()) {
			// this event is send if a translator is loaded
		case QEvent::LanguageChange:
			ui.retranslateUi(this);
			break;

			// this event is send, if the system, language changes
		case QEvent::LocaleChange:
		{
			QString locale = QLocale::system().name();
			locale.truncate(locale.lastIndexOf('_'));
			loadLanguage(locale);
		}
		break;
		}
	}
	QMainWindow::changeEvent(event);
}

void RealTimeTranslator::slotLanguageChanged(QAction* action)
{
	if (0 != action) {
		// load the language dependant on the action content
		loadLanguage(action->data().toString());
	}
}