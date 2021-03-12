//
// Created by ebeuque on 11/03/2021.
//

#ifndef ALEAKD_QAPPLICATIONWINDOWCONTROLLER_H
#define ALEAKD_QAPPLICATIONWINDOWCONTROLLER_H

#include <QObject>
#include <QSharedPointer>
#include <QTimer>
#include <QReadWriteLock>

#include "MemoryOperation.h"
#include "IMemOpRcptServerHandler.h"

class QApplicationWindow;
class QMemoryOperationModel;

class QApplicationWindowController : public QObject, public IMemOpRcptServerHandler {
	Q_OBJECT
public:
	QApplicationWindowController();
	virtual ~QApplicationWindowController();

	bool init(QApplicationWindow* pApplicationWindow);

	void addMemoryOperation(const QSharedPointer<MemoryOperation>& pMemoryOperation);
	void clearMemoryOperation();

	// IMemOpRcptServerHandler
	void onMemoryOperationReceived(const MemoryOperationSharedPtr& pMemoryOperation);
	void onNewConnection();

private slots:
	void onSearchButtonClicked();
	void onTimerUpdate();

private:
	QApplicationWindow* m_pApplicationWindow;

	QTimer m_timerUpdate;

	QReadWriteLock m_lockListMemoryOperation;
	MemoryOperationList m_listMemoryOperation;

	// Search display
	MemoryOperationList m_listFilterMemoryOperation;
	QMemoryOperationModel* m_pModels;
};


#endif //ALEAKD_QAPPLICATIONWINDOWCONTROLLER_H
