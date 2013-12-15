#include "QtWebKitWebWidget.h"
#include "QtWebKitWebPage.h"
#include "../../../windows/web/ImagePropertiesDialog.h"
#include "../../../../core/ActionsManager.h"
#include "../../../../core/NetworkAccessManager.h"
#include "../../../../core/SearchesManager.h"
#include "../../../../core/SessionsManager.h"
#include "../../../../core/SettingsManager.h"
#include "../../../../core/TransfersManager.h"
#include "../../../../core/Utils.h"
#include "../../../../ui/ContentsWidget.h"

#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtGui/QMovie>
#include <QtNetwork/QAbstractNetworkCache>
#include <QtWebKit/QWebHistory>
#include <QtWebKit/QWebElement>
#include <QtWebKitWidgets/QWebFrame>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QVBoxLayout>

namespace Otter
{

QtWebKitWebWidget::QtWebKitWebWidget(bool privateWindow, ContentsWidget *parent, QtWebKitWebPage *page) : WebWidget(parent),
	m_parent(parent),
	m_webView(new QWebView(this)),
	m_inspector(NULL),
	m_networkAccessManager(NULL),
	m_splitter(new QSplitter(Qt::Vertical, this)),
	m_searchEngine(SettingsManager::getValue("Browser/DefaultSearch").toString()),
	m_isLinkHovered(false),
	m_isLoading(false)
{
	m_splitter->addWidget(m_webView);
	m_splitter->setChildrenCollapsible(false);
	m_splitter->setContentsMargins(0, 0, 0, 0);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(m_splitter);
	layout->setContentsMargins(0, 0, 0, 0);

	setLayout(layout);

	if (page)
	{
		page->setParent(this);
	}
	else
	{
		page = new QtWebKitWebPage(this);
	}

	m_networkAccessManager = new NetworkAccessManager(privateWindow, true, parent);
	m_networkAccessManager->setParent(page);

	page->setNetworkAccessManager(m_networkAccessManager);
	page->setForwardUnsupportedContent(true);

	m_webView->installEventFilter(this);
	m_webView->setPage(page);
	m_webView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_webView->settings()->setAttribute(QWebSettings::PrivateBrowsingEnabled, privateWindow);

	ActionsManager::setupLocalAction(getAction(CutAction), "Cut");
	ActionsManager::setupLocalAction(getAction(CopyAction), "Copy");
	ActionsManager::setupLocalAction(getAction(PasteAction), "Paste");
	ActionsManager::setupLocalAction(getAction(DeleteAction), "Delete");
	ActionsManager::setupLocalAction(getAction(SelectAllAction), "SelectAll");
	ActionsManager::setupLocalAction(getAction(UndoAction), "Undo");
	ActionsManager::setupLocalAction(getAction(RedoAction), "Redo");
	ActionsManager::setupLocalAction(getAction(GoBackAction), "GoBack");
	ActionsManager::setupLocalAction(getAction(GoForwardAction), "GoForward");
	ActionsManager::setupLocalAction(getAction(ReloadAction), "Reload");
	ActionsManager::setupLocalAction(getAction(StopAction), "Stop");
	ActionsManager::setupLocalAction(getAction(OpenLinkInThisTabAction), "OpenLinkInThisTab");
	ActionsManager::setupLocalAction(getAction(OpenLinkInNewWindowAction), "OpenLinkInNewWindow");
	ActionsManager::setupLocalAction(getAction(OpenFrameInNewTabAction), "OpenFrameInNewTab");
	ActionsManager::setupLocalAction(getAction(SaveLinkToDiskAction), "SaveLinkToDisk");
	ActionsManager::setupLocalAction(getAction(CopyLinkToClipboardAction), "CopyLinkToClipboard");
	ActionsManager::setupLocalAction(getAction(OpenImageInNewTabAction), "OpenImageInNewTab");
	ActionsManager::setupLocalAction(getAction(SaveImageToDiskAction), "SaveImageToDisk");
	ActionsManager::setupLocalAction(getAction(CopyImageToClipboardAction), "CopyImageToClipboard");
	ActionsManager::setupLocalAction(getAction(CopyImageUrlToClipboardAction), "CopyImageUrlToClipboard");

	getAction(ReloadAction)->setEnabled(true);
	getAction(OpenLinkInThisTabAction)->setIcon(Utils::getIcon("document-open"));

	connect(SearchesManager::getInstance(), SIGNAL(searchAdded(SearchInformation*)), this, SLOT(updateSearchActions()));
	connect(SearchesManager::getInstance(), SIGNAL(searchModified(SearchInformation*)), this, SLOT(updateSearchActions()));
	connect(SearchesManager::getInstance(), SIGNAL(searchRemoved(SearchInformation*)), this, SLOT(updateSearchActions()));
	connect(page, SIGNAL(requestedNewWindow(WebWidget*)), this, SIGNAL(requestedNewWindow(WebWidget*)));
	connect(page, SIGNAL(microFocusChanged()), this, SIGNAL(actionsChanged()));
	connect(page, SIGNAL(selectionChanged()), this, SIGNAL(actionsChanged()));
	connect(page, SIGNAL(loadStarted()), this, SLOT(loadStarted()));
	connect(page, SIGNAL(loadFinished(bool)), this, SLOT(loadFinished(bool)));
	connect(page, SIGNAL(statusBarMessage(QString)), this, SIGNAL(statusMessageChanged(QString)));
	connect(page, SIGNAL(linkHovered(QString,QString,QString)), this, SLOT(linkHovered(QString,QString)));
	connect(page, SIGNAL(saveFrameStateRequested(QWebFrame*,QWebHistoryItem*)), this, SLOT(saveState(QWebFrame*,QWebHistoryItem*)));
	connect(page, SIGNAL(restoreFrameStateRequested(QWebFrame*)), this, SLOT(restoreState(QWebFrame*)));
	connect(page, SIGNAL(downloadRequested(QNetworkRequest)), this, SLOT(downloadFile(QNetworkRequest)));
	connect(page, SIGNAL(unsupportedContent(QNetworkReply*)), this, SLOT(downloadFile(QNetworkReply*)));
	connect(m_webView, SIGNAL(titleChanged(const QString)), this, SLOT(notifyTitleChanged()));
	connect(m_webView, SIGNAL(urlChanged(const QUrl)), this, SLOT(notifyUrlChanged(const QUrl)));
	connect(m_webView, SIGNAL(iconChanged()), this, SLOT(notifyIconChanged()));
	connect(m_webView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
	connect(m_networkAccessManager, SIGNAL(statusChanged(int,int,qint64,qint64,qint64)), this, SIGNAL(loadStatusChanged(int,int,qint64,qint64,qint64)));
	connect(m_networkAccessManager, SIGNAL(documentLoadProgressChanged(int)), this, SIGNAL(loadProgress(int)));
}

void QtWebKitWebWidget::search(QAction *action)
{
	const QString engine = ((!action || action->data().toString().isEmpty()) ? m_searchEngine : action->data().toString());

	if (SearchesManager::getSearches().contains(engine))
	{
		updateSearchActions(engine);

		emit requestedSearch(m_webView->selectedText(), m_searchEngine);
	}
}

void QtWebKitWebWidget::search(const QString &query, const QString &engine)
{
	QNetworkRequest request;
	QNetworkAccessManager::Operation method;
	QByteArray body;

	if (SearchesManager::setupQuery(query, engine, &request, &method, &body))
	{
		m_webView->page()->mainFrame()->load(request, method, body);
	}
}

void QtWebKitWebWidget::print(QPrinter *printer)
{
	m_webView->print(printer);
}

void QtWebKitWebWidget::loadStarted()
{
	m_isLoading = true;

	m_thumbnail = QPixmap();

	if (m_actions.contains(RewindBackAction))
	{
		getAction(RewindBackAction)->setEnabled(getAction(GoBackAction)->isEnabled());
	}

	if (m_actions.contains(RewindForwardAction))
	{
		getAction(RewindForwardAction)->setEnabled(getAction(GoForwardAction)->isEnabled());
	}

	if (m_actions.contains(ReloadOrStopAction))
	{
		QAction *action = getAction(ReloadOrStopAction);

		ActionsManager::setupLocalAction(action, "Stop");

		action->setShortcut(QKeySequence());
	}

	if (!isPrivate())
	{
		SessionsManager::markSessionModified();
	}

	emit loadingChanged(true);
	emit statusMessageChanged(QString());
}

void QtWebKitWebWidget::loadFinished(bool ok)
{
	m_isLoading = false;

	m_thumbnail = QPixmap();

	m_networkAccessManager->resetStatistics();

	if (m_actions.contains(ReloadOrStopAction))
	{
		QAction *action = getAction(ReloadOrStopAction);

		ActionsManager::setupLocalAction(action, "Reload");

		action->setShortcut(QKeySequence());
	}

	if (ok && !isPrivate())
	{
		SessionsManager::markSessionModified();
	}

	emit loadingChanged(false);
}

void QtWebKitWebWidget::downloadFile(const QNetworkRequest &request)
{
	TransfersManager::startTransfer(request, QString(), isPrivate());
}

void QtWebKitWebWidget::downloadFile(QNetworkReply *reply)
{
	TransfersManager::startTransfer(reply, QString(), isPrivate());
}

void QtWebKitWebWidget::linkHovered(const QString &link, const QString &title)
{
	QString text;

	if (!link.isEmpty())
	{
		text = (title.isEmpty() ? tr("Address: %1").arg(link) : tr("Title: %1\nAddress: %2").arg(title).arg(link));
	}

	m_isLinkHovered = !text.isEmpty();

	QToolTip::showText(QCursor::pos(), text, m_webView);

	emit statusMessageChanged(link, 0);
}

void QtWebKitWebWidget::saveState(QWebFrame *frame, QWebHistoryItem *item)
{
	if (frame == m_webView->page()->mainFrame())
	{
		QVariantHash data;
		data["position"] = m_webView->page()->mainFrame()->scrollPosition();
		data["zoom"] = getZoom();

		item->setUserData(data);
	}
}

void QtWebKitWebWidget::restoreState(QWebFrame *frame)
{
	if (frame == m_webView->page()->mainFrame())
	{
		setZoom(m_webView->history()->currentItem().userData().toHash().value("zoom", getZoom()).toInt());

		if (m_webView->page()->mainFrame()->scrollPosition() == QPoint(0, 0))
		{
			m_webView->page()->mainFrame()->setScrollPosition(m_webView->history()->currentItem().userData().toHash().value("position").toPoint());
		}
	}
}

void QtWebKitWebWidget::searchMenuAboutToShow()
{
	QAction *searchMenuAction = getAction(SearchMenuAction);

	if (searchMenuAction->isEnabled() && searchMenuAction->menu()->actions().isEmpty())
	{
		const QStringList engines = SearchesManager::getSearches();

		for (int i = 0; i < engines.count(); ++i)
		{
			SearchInformation *search = SearchesManager::getSearch(engines.at(i));

			if (search)
			{
				QAction *searchAction = searchMenuAction->menu()->addAction(search->icon, search->title);
				searchAction->setData(search->identifier);
				searchAction->setToolTip(search->description);
			}
		}
	}
}

void QtWebKitWebWidget::notifyTitleChanged()
{
	emit titleChanged(getTitle());
}

void QtWebKitWebWidget::notifyUrlChanged(const QUrl &url)
{
	if (m_actions.contains(RewindBackAction))
	{
		getAction(RewindBackAction)->setEnabled(getAction(GoBackAction)->isEnabled());
	}

	if (m_actions.contains(RewindForwardAction))
	{
		getAction(RewindForwardAction)->setEnabled(getAction(GoForwardAction)->isEnabled());
	}

	emit urlChanged(url);
}

void QtWebKitWebWidget::notifyIconChanged()
{
	emit iconChanged(getIcon());
}

void QtWebKitWebWidget::updateSearchActions(const QString &engine)
{
	if (sender() == SearchesManager::getInstance())
	{
		getAction(SearchMenuAction)->menu()->clear();
	}

	QAction *defaultSearchAction = getAction(SearchAction);
	SearchInformation *search = SearchesManager::getSearch(engine.isEmpty() ? m_searchEngine : engine);

	if (!search)
	{
		search = SearchesManager::getSearch(SearchesManager::getSearches().first());
	}

	if (search)
	{
		defaultSearchAction->setEnabled(true);
		defaultSearchAction->setIcon(search->icon.isNull() ? Utils::getIcon("edit-find") : search->icon);
		defaultSearchAction->setText(search->title);
		defaultSearchAction->setToolTip(search->description);

		m_searchEngine = search->identifier;
	}
	else
	{
		defaultSearchAction->setEnabled(false);
		defaultSearchAction->setIcon(QIcon());
		defaultSearchAction->setText(tr("Search"));
		defaultSearchAction->setToolTip(tr("No search engines defined"));
	}

	getAction(SearchMenuAction)->setEnabled(SearchesManager::getSearches().count() > 1);
}

void QtWebKitWebWidget::showDialog(QWidget *dialog)
{
	m_parent->showDialog(dialog);
}

void QtWebKitWebWidget::hideDialog(QWidget *dialog)
{
	m_parent->hideDialog(dialog);
}

void QtWebKitWebWidget::triggerAction(WindowAction action, bool checked)
{
	const QWebPage::WebAction webAction = mapAction(action);

	if (webAction != QWebPage::NoWebAction)
	{
		m_webView->triggerPageAction(webAction, checked);

		return;
	}

	switch (action)
	{
		case RewindBackAction:
			m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(0));

			break;
		case RewindForwardAction:
			m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(m_webView->page()->history()->count() - 1));

			break;
		case CopyAddressAction:
			QApplication::clipboard()->setText(getUrl().toString());

			break;
		case ZoomInAction:
			setZoom(qMin((getZoom() + 10), 10000));

			break;
		case ZoomOutAction:
			setZoom(qMax((getZoom() - 10), 10));

			break;
		case ZoomOriginalAction:
			setZoom(100);

			break;
		case ReloadOrStopAction:
			if (isLoading())
			{
				triggerAction(StopAction);
			}
			else
			{
				triggerAction(ReloadAction);
			}

			break;
		case OpenLinkInNewTabAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), false, false);
			}

			break;
		case OpenLinkInNewTabBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), true, false);
			}

			break;
		case OpenLinkInNewWindowAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), false, true);
			}

			break;
		case OpenLinkInNewWindowBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedOpenUrl(m_hitResult.linkUrl(), true, true);
			}

			break;
		case BookmarkLinkAction:
			if (m_hitResult.linkUrl().isValid())
			{
				emit requestedAddBookmark(m_hitResult.linkUrl());
			}

			break;
		case OpenSelectionAsLinkAction:
			emit requestedOpenUrl(m_webView->selectedText(), false, false);

			break;
		case ImagePropertiesAction:
			{
				ImagePropertiesDialog dialog(m_hitResult.imageUrl(), m_hitResult.element().attribute("alt"), m_hitResult.element().attribute("longdesc"), m_hitResult.pixmap(), (m_networkAccessManager->cache() ? m_networkAccessManager->cache()->data(m_hitResult.imageUrl()) : NULL), this);
				QEventLoop eventLoop;

				m_parent->showDialog(&dialog);

				connect(&dialog, SIGNAL(finished(int)), &eventLoop, SLOT(quit()));
				connect(this, SIGNAL(destroyed()), &eventLoop, SLOT(quit()));

				eventLoop.exec();

				m_parent->hideDialog(&dialog);
			}

			break;
		case InspectPageAction:
			if (!m_inspector)
			{
				m_inspector = new QWebInspector(this);
				m_inspector->setPage(m_webView->page());
				m_inspector->setContextMenuPolicy(Qt::NoContextMenu);
				m_inspector->setMinimumHeight(200);

				m_splitter->addWidget(m_inspector);
			}

			m_inspector->setVisible(checked);

			getAction(InspectPageAction)->setChecked(checked);

			emit actionsChanged();

			break;
		case SaveLinkToDownloadsAction:
			TransfersManager::startTransfer(m_hitResult.linkUrl().toString(), QString(), isPrivate(), true);

			break;
		case OpenFrameInThisTabAction:
			if (m_hitResult.frame())
			{
				setUrl(m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl());
			}

			break;
		case OpenFrameInNewTabBackgroundAction:
			if (m_hitResult.frame())
			{
				emit requestedOpenUrl((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()), true, false);
			}

			break;
		case CopyFrameLinkToClipboardAction:
			if (m_hitResult.frame())
			{
				QGuiApplication::clipboard()->setText((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()).toString());
			}

			break;
		case ReloadFrameAction:
			if (m_hitResult.frame())
			{
				const QUrl url = (m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl());

				m_hitResult.frame()->setUrl(QUrl());
				m_hitResult.frame()->setUrl(url);
			}

			break;
		case SearchAction:
			search(getAction(SearchAction));

			break;
		default:
			break;
	}
}

void QtWebKitWebWidget::setDefaultTextEncoding(const QString &encoding)
{
	m_webView->settings()->setDefaultTextEncoding(encoding);
	m_webView->reload();
}

void QtWebKitWebWidget::setHistory(const HistoryInformation &history)
{
	Q_UNUSED(history)
///TODO
}

void QtWebKitWebWidget::setZoom(int zoom)
{
	if (zoom != getZoom())
	{
		m_webView->setZoomFactor(qBound(0.1, ((qreal) zoom / 100), (qreal) 100));

		emit zoomChanged(zoom);
	}
}

void QtWebKitWebWidget::setUrl(const QUrl &url)
{
	if (url.scheme() == "javascript")
	{
		evaluateJavaScript(url.path());

		return;
	}

	if (!url.fragment().isEmpty() && url.matches(getUrl(), (QUrl::RemoveFragment | QUrl::StripTrailingSlash | QUrl::NormalizePathSegments)))
	{
		m_webView->page()->mainFrame()->scrollToAnchor(url.fragment());

		return;
	}

	if (url.isValid() && url.scheme().isEmpty() && !url.path().startsWith('/'))
	{
		QUrl httpUrl = url;
		httpUrl.setScheme("http");

		m_webView->setUrl(httpUrl);
	}
	else if (url.isValid() && (url.scheme().isEmpty() || url.scheme() == "file"))
	{
		QUrl localUrl = url;
		localUrl.setScheme("file");

		m_webView->setUrl(localUrl);
	}
	else
	{
		m_webView->setUrl(url);
	}

	notifyTitleChanged();
	notifyIconChanged();
}

void QtWebKitWebWidget::showContextMenu(const QPoint &position)
{
	m_hitResult = m_webView->page()->frameAt(position)->hitTestContent(position);
	MenuFlags flags = NoMenu;

	if (m_hitResult.element().tagName().toLower() == "textarea" || (m_hitResult.element().tagName().toLower() == "input" && (m_hitResult.element().attribute("type").isEmpty() || m_hitResult.element().attribute("type").toLower() == "text")))
	{
		flags |= FormMenu;
	}

	if (m_hitResult.pixmap().isNull() && m_hitResult.isContentSelected() && !m_webView->selectedText().isEmpty())
	{
		updateSearchActions(m_searchEngine);

		flags |= SelectionMenu;
	}

	if (m_hitResult.linkUrl().isValid())
	{
		flags |= LinkMenu;
	}

	if (!m_hitResult.pixmap().isNull())
	{
		flags |= ImageMenu;

		const bool isImageOpened = getUrl().matches(m_hitResult.imageUrl(), (QUrl::NormalizePathSegments | QUrl::RemoveFragment | QUrl::StripTrailingSlash));

		getAction(OpenImageInNewTabAction)->setEnabled(!isImageOpened);
		getAction(InspectElementAction)->setEnabled(!isImageOpened);
	}

	if (m_hitResult.isContentEditable())
	{
		flags |= EditMenu;
	}

	if (flags == NoMenu)
	{
		flags = StandardMenu;

		if (m_hitResult.frame() != m_webView->page()->mainFrame())
		{
			flags |= FrameMenu;
		}
	}

	WebWidget::showContextMenu(position, flags);
}

WebWidget* QtWebKitWebWidget::clone(ContentsWidget *parent)
{
	WebWidget *widget = new QtWebKitWebWidget(isPrivate(), parent);
	widget->setDefaultTextEncoding(getDefaultTextEncoding());
	widget->setUrl(getUrl());
	widget->setZoom(getZoom());
	widget->setHistory(getHistory());

	return widget;
}

QAction *QtWebKitWebWidget::getAction(WindowAction action)
{
	const QWebPage::WebAction webAction = mapAction(action);

	if (webAction != QWebPage::NoWebAction)
	{
		return m_webView->page()->action(webAction);
	}

	if (action == NoAction)
	{
		return NULL;
	}

	if (m_actions.contains(action))
	{
		return m_actions[action];
	}

	QAction *actionObject = new QAction(this);
	actionObject->setData(action);

	connect(actionObject, SIGNAL(triggered()), this, SLOT(triggerAction()));

	switch (action)
	{
		case OpenLinkInNewTabAction:
			ActionsManager::setupLocalAction(actionObject, "OpenLinkInNewTab", true);

			break;
		case OpenLinkInNewTabBackgroundAction:
			ActionsManager::setupLocalAction(actionObject, "OpenLinkInNewTabBackground", true);

			break;
		case OpenLinkInNewWindowAction:
			ActionsManager::setupLocalAction(actionObject, "OpenLinkInNewWindow", true);

			break;
		case OpenLinkInNewWindowBackgroundAction:
			ActionsManager::setupLocalAction(actionObject, "OpenLinkInNewWindowBackground", true);

			break;
		case OpenFrameInThisTabAction:
			ActionsManager::setupLocalAction(actionObject, "OpenFrameInThisTab", true);

			break;
		case OpenFrameInNewTabBackgroundAction:
			ActionsManager::setupLocalAction(actionObject, "OpenFrameInNewTabBackground", true);

			break;
		case CopyFrameLinkToClipboardAction:
			ActionsManager::setupLocalAction(actionObject, "CopyFrameLinkToClipboard", true);

			break;
		case ViewSourceFrameAction:
			ActionsManager::setupLocalAction(actionObject, "ViewSourceFrame", true);

			actionObject->setEnabled(false);

			break;
		case ReloadFrameAction:
			ActionsManager::setupLocalAction(actionObject, "ReloadFrame", true);

			break;
		case SaveLinkToDownloadsAction:
			ActionsManager::setupLocalAction(actionObject, "SaveLinkToDownloads");

			break;
		case RewindBackAction:
			ActionsManager::setupLocalAction(actionObject, "RewindBack", true);

			actionObject->setEnabled(getAction(GoBackAction)->isEnabled());

			break;
		case RewindForwardAction:
			ActionsManager::setupLocalAction(actionObject, "RewindForward", true);

			actionObject->setEnabled(getAction(GoForwardAction)->isEnabled());

			break;
		case ReloadTimeAction:
			ActionsManager::setupLocalAction(actionObject, "ReloadTime", true);

			actionObject->setMenu(new QMenu(this));
			actionObject->setEnabled(false);

			break;
		case PrintAction:
			ActionsManager::setupLocalAction(actionObject, "Print", true);

			break;
		case BookmarkAction:
			ActionsManager::setupLocalAction(actionObject, "AddBookmark", true);

			break;
		case BookmarkLinkAction:
			ActionsManager::setupLocalAction(actionObject, "BookmarkLink", true);

			break;
		case CopyAddressAction:
			ActionsManager::setupLocalAction(actionObject, "CopyAddress", true);

			break;
		case ViewSourceAction:
			ActionsManager::setupLocalAction(actionObject, "ViewSource", true);

			actionObject->setEnabled(false);

			break;
		case ValidateAction:
			ActionsManager::setupLocalAction(actionObject, "Validate", true);

			actionObject->setMenu(new QMenu(this));
			actionObject->setEnabled(false);

			break;
		case ContentBlockingAction:
			ActionsManager::setupLocalAction(actionObject, "ContentBlocking", true);

			actionObject->setEnabled(false);

			break;
		case WebsitePreferencesAction:
			ActionsManager::setupLocalAction(actionObject, "WebsitePreferences", true);

			actionObject->setEnabled(false);

			break;
		case FullScreenAction:
			ActionsManager::setupLocalAction(actionObject, "FullScreen", true);

			actionObject->setEnabled(false);

			break;
		case ZoomInAction:
			ActionsManager::setupLocalAction(actionObject, "ZoomIn");

			break;
		case ZoomOutAction:
			ActionsManager::setupLocalAction(actionObject, "ZoomOut", true);

			break;
		case ZoomOriginalAction:
			ActionsManager::setupLocalAction(actionObject, "ZoomOriginal", true);

			break;
		case SearchAction:
			ActionsManager::setupLocalAction(actionObject, "Search", true);

			actionObject->setEnabled(false);

			break;
		case SearchMenuAction:
			ActionsManager::setupLocalAction(actionObject, "SearchMenu", true);

			actionObject->setMenu(new QMenu(this));
			actionObject->setEnabled(false);

			connect(actionObject->menu(), SIGNAL(aboutToShow()), this, SLOT(searchMenuAboutToShow()));
			connect(actionObject->menu(), SIGNAL(triggered(QAction*)), this, SLOT(search(QAction*)));

			break;
		case OpenSelectionAsLinkAction:
			ActionsManager::setupLocalAction(actionObject, "OpenSelectionAsLink", true);

			break;
		case ClearAllAction:
			ActionsManager::setupLocalAction(actionObject, "ClearAll", true);

			actionObject->setEnabled(false);

			break;
		case SpellCheckAction:
			ActionsManager::setupLocalAction(actionObject, "SpellCheck", true);

			actionObject->setEnabled(false);

			break;
		case ImagePropertiesAction:
			ActionsManager::setupLocalAction(actionObject, "ImageProperties", true);

			break;
		case CreateSearchAction:
			ActionsManager::setupLocalAction(actionObject, "CreateSearch", true);

			actionObject->setEnabled(false);

			break;
		case ReloadOrStopAction:
			ActionsManager::setupLocalAction(actionObject, "Reload");

			actionObject->setShortcut(QKeySequence());

			break;
		case InspectPageAction:
			ActionsManager::setupLocalAction(actionObject, "InspectPage");

			actionObject->setEnabled(true);
			actionObject->setShortcut(QKeySequence());

			break;
		default:
			actionObject->deleteLater();
			actionObject = NULL;

			break;
	}

	if (actionObject)
	{
		m_actions[action] = actionObject;
	}

	return actionObject;
}

QUndoStack *QtWebKitWebWidget::getUndoStack()
{
	return m_webView->page()->undoStack();
}

QString QtWebKitWebWidget::getDefaultTextEncoding() const
{
	return m_webView->settings()->defaultTextEncoding();
}

QString QtWebKitWebWidget::getTitle() const
{
	const QString title = m_webView->title();

	if (title.isEmpty())
	{
		const QUrl url = getUrl();

		if (url.scheme() == "about" && (url.path().isEmpty() || url.path() == "blank"))
		{
			return tr("New Tab");
		}

		if (url.isLocalFile())
		{
			return QFileInfo(url.toLocalFile()).canonicalFilePath();
		}

		return tr("(Untitled)");
	}

	return title;
}

QVariant QtWebKitWebWidget::evaluateJavaScript(const QString &script)
{
	return m_webView->page()->mainFrame()->evaluateJavaScript(script);
}

QUrl QtWebKitWebWidget::getUrl() const
{
	return m_webView->url();
}

QIcon QtWebKitWebWidget::getIcon() const
{
	if (isPrivate())
	{
		return QIcon(":/icons/tab-private.png");
	}

	const QIcon icon = m_webView->icon();

	return (icon.isNull() ? QIcon(":/icons/tab.png") : icon);
}

QPixmap QtWebKitWebWidget::getThumbnail()
{
	if (!m_thumbnail.isNull() && !isLoading())
	{
		return m_thumbnail;
	}

	const QSize thumbnailSize = QSize(260, 170);
	const QSize oldViewportSize = m_webView->page()->viewportSize();
	const qreal zoom = m_webView->page()->mainFrame()->zoomFactor();
	QSize contentsSize = m_webView->page()->mainFrame()->contentsSize();
	QWidget *newView = new QWidget();
	QWidget *oldView = m_webView->page()->view();

	m_webView->page()->setView(newView);
	m_webView->page()->setViewportSize(contentsSize);
	m_webView->page()->mainFrame()->setZoomFactor(1);

	if (contentsSize.width() > 2000)
	{
		contentsSize.setWidth(2000);
	}

	contentsSize.setHeight(thumbnailSize.height() * (qreal(contentsSize.width()) / thumbnailSize.width()));

	QPixmap pixmap(contentsSize);
	pixmap.fill(Qt::white);

	QPainter painter(&pixmap);

	m_webView->page()->mainFrame()->render(&painter, QWebFrame::ContentsLayer, QRegion(QRect(QPoint(0, 0), contentsSize)));
	m_webView->page()->mainFrame()->setZoomFactor(zoom);
	m_webView->page()->setView(oldView);
	m_webView->page()->setViewportSize(oldViewportSize);

	painter.end();

	pixmap = pixmap.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	newView->deleteLater();

	m_thumbnail = pixmap;

	return pixmap;
}

HistoryInformation QtWebKitWebWidget::getHistory() const
{
	QVariantHash data;
	data["position"] = m_webView->page()->mainFrame()->scrollPosition();
	data["zoom"] = getZoom();

	m_webView->history()->currentItem().setUserData(data);

	QWebHistory *history = m_webView->history();
	HistoryInformation information;
	information.index = history->currentItemIndex();

	for (int i = 0; i < history->count(); ++i)
	{
		const QWebHistoryItem item = history->itemAt(i);
		HistoryEntry entry;
		entry.url = item.url().toString();
		entry.title = item.title();
		entry.position = item.userData().toHash().value("position", QPoint(0, 0)).toPoint();
		entry.zoom = item.userData().toHash().value("zoom").toInt();

		information.entries.append(entry);
	}

	return information;
}

QWebPage::WebAction QtWebKitWebWidget::mapAction(WindowAction action) const
{
	switch (action)
	{
		case OpenLinkAction:
			return QWebPage::OpenLink;
		case OpenLinkInThisTabAction:
			return QWebPage::OpenLinkInThisWindow;
		case OpenFrameInNewTabAction:
			return QWebPage::OpenFrameInNewWindow;
		case SaveLinkToDiskAction:
			return QWebPage::DownloadLinkToDisk;
		case CopyLinkToClipboardAction:
			return QWebPage::CopyLinkToClipboard;
		case OpenImageInNewTabAction:
			return QWebPage::OpenImageInNewWindow;
		case SaveImageToDiskAction:
			return QWebPage::DownloadImageToDisk;
		case CopyImageToClipboardAction:
			return QWebPage::CopyImageToClipboard;
		case CopyImageUrlToClipboardAction:
			return QWebPage::CopyImageUrlToClipboard;
		case GoBackAction:
			return QWebPage::Back;
		case GoForwardAction:
			return QWebPage::Forward;
		case StopAction:
			return QWebPage::Stop;
		case StopScheduledPageRefreshAction:
			return QWebPage::StopScheduledPageRefresh;
		case ReloadAction:
			return QWebPage::Reload;
		case ReloadAndBypassCacheAction:
			return QWebPage::ReloadAndBypassCache;
		case CutAction:
			return QWebPage::Cut;
		case CopyAction:
			return QWebPage::Copy;
		case PasteAction:
			return QWebPage::Paste;
		case DeleteAction:
			return QWebPage::DeleteEndOfWord;
		case SelectAllAction:
			return QWebPage::SelectAll;
		case UndoAction:
			return QWebPage::Undo;
		case RedoAction:
			return QWebPage::Redo;
		case InspectElementAction:
			return QWebPage::InspectElement;
		default:
			return QWebPage::NoWebAction;
	}

	return QWebPage::NoWebAction;
}

void QtWebKitWebWidget::triggerAction()
{
	QAction *action = qobject_cast<QAction*>(sender());

	if (action)
	{
		triggerAction(static_cast<WindowAction>(action->data().toInt()));
	}
}

int QtWebKitWebWidget::getZoom() const
{
	return (m_webView->zoomFactor() * 100);
}

bool QtWebKitWebWidget::isLoading() const
{
	return m_isLoading;
}

bool QtWebKitWebWidget::isPrivate() const
{
	return m_webView->settings()->testAttribute(QWebSettings::PrivateBrowsingEnabled);
}

bool QtWebKitWebWidget::find(const QString &text, FindFlags flags)
{
	QWebPage::FindFlags nativeFlags = (QWebPage::FindWrapsAroundDocument | QWebPage::FindBeginsInSelection);

	if (flags & BackwardFind)
	{
		nativeFlags |= QWebPage::FindBackward;
	}

	if (flags & CaseSensitiveFind)
	{
		nativeFlags |= QWebPage::FindCaseSensitively;
	}

	if (flags & HighlightAllFind)
	{
		nativeFlags |= QWebPage::HighlightAllOccurrences;
	}

	return m_webView->findText(text, nativeFlags);
}

bool QtWebKitWebWidget::eventFilter(QObject *object, QEvent *event)
{
	if (object == m_webView)
	{
		if (event->type() == QEvent::ToolTip && m_isLinkHovered)
		{
			event->accept();

			return true;
		}
		else if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

			if (mouseEvent->button() == Qt::MiddleButton)
			{
				QWebHitTestResult result = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (result.linkUrl().isValid())
				{
					emit requestedOpenUrl(result.linkUrl(), true, false);

					event->accept();

					return true;
				}
			}

			if (mouseEvent->button() == Qt::BackButton)
			{
				triggerAction(GoBackAction);

				event->accept();

				return true;
			}

			if (mouseEvent->button() == Qt::ForwardButton)
			{
				triggerAction(GoForwardAction);

				event->accept();

				return true;
			}
		}
		else if (event->type() == QEvent::Wheel)
		{
			QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);

			if (wheelEvent->modifiers() & Qt::CTRL || wheelEvent->buttons() & Qt::LeftButton)
			{
				setZoom(getZoom() + (wheelEvent->delta() / 16));

				event->accept();

				return true;
			}

		}
	}

	return QObject::eventFilter(object, event);
}

}
