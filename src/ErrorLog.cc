#include "ErrorLog.h"
#include "printutils.h"
#include "MainWindow.h"
#include <boost/filesystem.hpp>

ErrorLog::ErrorLog(QWidget *parent) : QWidget(parent)
{
	setupUi(this);
	initGUI();
	connect(logTable, SIGNAL(clicked(const QModelIndex &)), this, SLOT(onTableCellClicked(const QModelIndex &)));
	this->errorLogComboBox->installEventFilter(this);
}

ErrorLog::~ErrorLog()
{
	if(errorLogModel) delete errorLogModel;
}


bool ErrorLog::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::Wheel) {
		return true;
	}
	return QObject::eventFilter(obj, event);
}

void ErrorLog::initGUI()
{
	row=0;
	const int numColumns = 4;
	this->errorLogModel = new QStandardItemModel(row, numColumns, logTable);
	QList<QString> labels = QList<QString>() << QString("Group")<< QString("File") << QString("Line")<<QString("Info"); 
	errorLogModel->setHorizontalHeaderLabels(labels);
	logTable->verticalHeader()->hide();
	logTable->setModel(errorLogModel);
	logTable->setColumnWidth(0,80);
	logTable->setColumnWidth(1,200);
	logTable->setColumnWidth(2,80); //last column will strech itself

}

void ErrorLog::toErrorLog(const Message &log_msg)
{
	if(log_msg.group==message_group::None || log_msg.group==message_group::Echo) return;
	lastMessages.push_back(std::forward<const Message>(log_msg));
	QString currGroup = errorLogComboBox->currentText();
	//handle combobox
	if(errorLogComboBox->currentIndex()==0);
	else if(currGroup.toStdString()!=getGroupName(log_msg.group)) return;
	
	showtheErrorInGUI(log_msg);
}

void ErrorLog::showtheErrorInGUI(const Message &log_msg)
{
	QStandardItem* groupName = new QStandardItem(QString::fromStdString(getGroupName(log_msg.group)));
	groupName->setEditable(false);
	
	if(log_msg.group==message_group::Error) groupName->setForeground(QColor::fromRgb(255,0,0)); //make this item red.
	else if(log_msg.group==message_group::Warning) groupName->setForeground(QColor::fromRgb(252, 211, 3)); //make this item yellow
	
	errorLogModel->setItem(row,0,groupName);

	QStandardItem* fileName;
	QStandardItem* lineNo;
	if(!log_msg.loc.isNone())
	{
		if(is_regular_file(log_msg.loc.filePath())) fileName = new QStandardItem(QString::fromStdString(log_msg.loc.fileName()));
		else fileName = new QStandardItem(QString());
		lineNo = new QStandardItem(QString::number(log_msg.loc.firstLine()));
	}
	else
	{
		fileName = new QStandardItem(QString());
		lineNo = new QStandardItem(QString());
	}
	fileName->setEditable(false);
	lineNo->setEditable(false);
	errorLogModel->setItem(row,1,fileName);
	errorLogModel->setItem(row,2,lineNo);


	QStandardItem* msg = new QStandardItem(QString::fromStdString(log_msg.msg));
	msg->setEditable(false);
	errorLogModel->setItem(row,3,msg);
	errorLogModel->setRowCount(++row);
}

void ErrorLog::clearModel()
{
	errorLogModel->clear();
	initGUI();
	lastMessages.clear();
}

int ErrorLog::getLine(int row,int col)
{
	return logTable->model()->index(row,col).data().toInt();
}

void ErrorLog::onTableCellClicked(const QModelIndex & index)
{
    if (index.isValid() && index.column()!=0) 
    {
		int r= index.row();
		int line = getLine(r,2);
		QString path = logTable->model()->index(r,1).data().toString();
		emit openFile(path,line-1);
	}
}

void ErrorLog::on_errorLogComboBox_currentIndexChanged(const QString &group)
{
	errorLogModel->clear();
	initGUI();
	for(auto itr = lastMessages.begin();itr!=lastMessages.end();itr++)
	{
		if(group==QString::fromStdString("All")) showtheErrorInGUI(*itr);
		else if(group==QString::fromStdString(getGroupName(itr->group))) 
		{
			showtheErrorInGUI(*itr);
		}
	}
}
