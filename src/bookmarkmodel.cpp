/****************************************************************************
 *   Copyright (C) 2013-2015 by Savoir-Faire Linux                          *
 *   Author : Emmanuel Lepage Vallee <emmanuel.lepage@savoirfairelinux.com> *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Lesser General Public             *
 *   License as published by the Free Software Foundation; either           *
 *   version 2.1 of the License, or (at your option) any later version.     *
 *                                                                          *
 *   This library is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/
#include "bookmarkmodel.h"

//Qt
#include <QtCore/QMimeData>
#include <QtCore/QCoreApplication>

//Ring
#include "historymodel.h"
#include "dbus/presencemanager.h"
#include "phonedirectorymodel.h"
#include "contactmethod.h"
#include "callmodel.h"
#include "call.h"
#include "person.h"
#include "uri.h"
#include "mime.h"
#include "collectioneditor.h"
#include "collectioninterface.h"

///Top level bookmark item
class BookmarkTopLevelItem : public CategorizedCompositeNode {
   friend class BookmarkModel;
   public:
      virtual QObject* getSelf() const;
      int m_Row;
   private:
      explicit BookmarkTopLevelItem(QString name);
      QList<NumberTreeBackend*> m_lChildren;
      QString m_Name;
      bool m_MostPopular;
};

class BookmarkModelPrivate : public QObject
{
   Q_OBJECT
public:
   BookmarkModelPrivate(BookmarkModel* parent);

//    QVector<CollectionInterface*> m_lBackends;

   //Attributes
   QList<BookmarkTopLevelItem*>         m_lCategoryCounter ;
   QHash<QString,BookmarkTopLevelItem*> m_hCategories      ;
   QStringList                          m_lMimes           ;

   //Helpers
   QVariant commonCallInfo(NumberTreeBackend* call, int role = Qt::DisplayRole) const;
   QString category(NumberTreeBackend* number) const;
   bool                  displayFrequentlyUsed() const;
   QVector<ContactMethod*>   bookmarkList         () const;
   static QVector<ContactMethod*> serialisedToList(const QStringList& list);

private Q_SLOTS:
   void slotRequest(const QString& uri);
   void slotIndexChanged(const QModelIndex& idx);

private:
   BookmarkModel* q_ptr;
};

BookmarkModel* BookmarkModel::m_spInstance = nullptr;

class BookmarkItemNode;

static bool test = false;
//Model item/index
class NumberTreeBackend : public CategorizedCompositeNode
{
   friend class BookmarkModel;
   public:
      NumberTreeBackend(ContactMethod* number);
      virtual ~NumberTreeBackend();
      virtual QObject* getSelf() const { return nullptr; }

      ContactMethod* m_pNumber;
      BookmarkTopLevelItem* m_pParent;
      int m_Index;
      BookmarkItemNode* m_pNode;
};

class BookmarkItemNode : public QObject //TODO remove this once Qt4 support is dropped
{
   Q_OBJECT
public:
   BookmarkItemNode(BookmarkModel* m, ContactMethod* n, NumberTreeBackend* backend);
private:
   ContactMethod* m_pNumber;
   NumberTreeBackend* m_pBackend;
   BookmarkModel* m_pModel;
private Q_SLOTS:
   void slotNumberChanged();
Q_SIGNALS:
   void changed(const QModelIndex& idx);
};

BookmarkModelPrivate::BookmarkModelPrivate(BookmarkModel* parent) : QObject(parent), q_ptr(parent)
{
   
}

NumberTreeBackend::NumberTreeBackend(ContactMethod* number): CategorizedCompositeNode(CategorizedCompositeNode::Type::BOOKMARK),
   m_pNumber(number),m_pParent(nullptr),m_pNode(nullptr),m_Index(-1){
   Q_ASSERT(number != nullptr);
}

NumberTreeBackend::~NumberTreeBackend() {
   if (m_pNode) delete m_pNode;
}

BookmarkItemNode::BookmarkItemNode(BookmarkModel* m, ContactMethod* n, NumberTreeBackend* backend) :
m_pNumber(n),m_pBackend(backend),m_pModel(m){
   connect(n,SIGNAL(changed()),this,SLOT(slotNumberChanged()));
}

void BookmarkItemNode::slotNumberChanged()
{
   emit changed(m_pModel->index(m_pBackend->m_Index,0,m_pModel->index(m_pBackend->m_pParent->m_Row,0)));
}

QObject* BookmarkTopLevelItem::getSelf() const
{
   return nullptr;
}

BookmarkModel::BookmarkModel(QObject* parent) : QAbstractItemModel(parent), d_ptr(new BookmarkModelPrivate(this))
{
   setObjectName("BookmarkModel");
   reloadCategories();
   d_ptr->m_lMimes << RingMimes::PLAIN_TEXT << RingMimes::PHONENUMBER;

   //Connect
   connect(&DBus::PresenceManager::instance(),SIGNAL(newServerSubscriptionRequest(QString)),d_ptr,SLOT(slotRequest(QString)));
//    if (Call::contactBackend()) {
//       connect(Call::contactBackend(),SIGNAL(collectionChanged()),this,SLOT(reloadCategories()));
//    } //TODO implement reordering
}

BookmarkModel* BookmarkModel::instance()
{
   if (! m_spInstance )
      m_spInstance = new BookmarkModel(QCoreApplication::instance());
   return m_spInstance;
}

QHash<int,QByteArray> BookmarkModel::roleNames() const
{
   static QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
   static bool initRoles = false;
   if (!initRoles) {
      initRoles = true;
      roles[Call::Role::Name] = CallModel::instance()->roleNames()[Call::Role::Name];
   }
   return roles;
}

///Reload bookmark cateogries
void BookmarkModel::reloadCategories()
{
   test = true;
   beginResetModel(); {
      d_ptr->m_hCategories.clear();

      //TODO this is not efficient, nor necessary
      foreach(BookmarkTopLevelItem* item, d_ptr->m_lCategoryCounter) {
         foreach (NumberTreeBackend* child, item->m_lChildren) {
            delete child;
         }
         delete item;
      }
      d_ptr->m_lCategoryCounter.clear();

      //Load most used contacts
      if (d_ptr->displayFrequentlyUsed()) {
         BookmarkTopLevelItem* item = new BookmarkTopLevelItem(tr("Most popular"));
         d_ptr->m_hCategories["mp"] = item;
         item->m_Row = d_ptr->m_lCategoryCounter.size();
         item->m_MostPopular = true;
         d_ptr->m_lCategoryCounter << item;
         const QVector<ContactMethod*> cl = PhoneDirectoryModel::instance()->getNumbersByPopularity();

         for (int i=0;i<((cl.size()>=10)?10:cl.size());i++) {
            ContactMethod* n = cl[i];
            NumberTreeBackend* bm = new NumberTreeBackend(n);
            bm->m_pParent = item;
            bm->m_Index = item->m_lChildren.size();
            bm->m_pNode = new BookmarkItemNode(this,n,bm);
            connect(bm->m_pNode,SIGNAL(changed(QModelIndex)),d_ptr,SLOT(slotIndexChanged(QModelIndex)));
            item->m_lChildren << bm;
         }

      }

      foreach(ContactMethod* bookmark, d_ptr->bookmarkList()) {
         NumberTreeBackend* bm = new NumberTreeBackend(bookmark);
         const QString val = d_ptr->category(bm);
         if (!d_ptr->m_hCategories[val]) {
            BookmarkTopLevelItem* item = new BookmarkTopLevelItem(val);
            d_ptr->m_hCategories[val] = item;
            item->m_Row = d_ptr->m_lCategoryCounter.size();
            d_ptr->m_lCategoryCounter << item;
         }
         BookmarkTopLevelItem* item = d_ptr->m_hCategories[val];
         if (item) {
            bookmark->setBookmarked(true);
            bm->m_pParent = item;
            bm->m_Index = item->m_lChildren.size();
            bm->m_pNode = new BookmarkItemNode(this,bookmark,bm);
            connect(bm->m_pNode,SIGNAL(changed(QModelIndex)),d_ptr,SLOT(slotIndexChanged(QModelIndex)));
            item->m_lChildren << bm;
         }
         else
            qDebug() << "ERROR count";
      }

   } endResetModel();

   emit layoutAboutToBeChanged();
   test = false;
   emit layoutChanged();
} //reloadCategories

//Do nothing
bool BookmarkModel::setData( const QModelIndex& index, const QVariant &value, int role)
{
   Q_UNUSED(index)
   Q_UNUSED(value)
   Q_UNUSED(role)
   return false;
}

///Get bookmark model data CategorizedCompositeNode::Type and Call::Role
QVariant BookmarkModel::data( const QModelIndex& index, int role) const
{
   if (!index.isValid() || test)
      return QVariant();

   CategorizedCompositeNode* modelItem = static_cast<CategorizedCompositeNode*>(index.internalPointer());
   if (!modelItem)
      return QVariant();
   switch (modelItem->type()) {
      case CategorizedCompositeNode::Type::TOP_LEVEL:
         switch (role) {
            case Qt::DisplayRole:
               return static_cast<BookmarkTopLevelItem*>(modelItem)->m_Name;
            case Call::Role::Name:
               if (static_cast<BookmarkTopLevelItem*>(modelItem)->m_MostPopular) {
                  return "000000";
               }
               else {
                  return static_cast<BookmarkTopLevelItem*>(modelItem)->m_Name;
               }
         }
         break;
      case CategorizedCompositeNode::Type::BOOKMARK:
         return d_ptr->commonCallInfo(static_cast<NumberTreeBackend*>(modelItem),role);
         break;
      case CategorizedCompositeNode::Type::CALL:
      case CategorizedCompositeNode::Type::NUMBER:
      case CategorizedCompositeNode::Type::CONTACT:
         break;
   };
   return QVariant();
} //Data

///Get header data
QVariant BookmarkModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   Q_UNUSED(section)
   if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
      return QVariant(tr("Contacts"));
   return QVariant();
}


///Get the number of child of "parent"
int BookmarkModel::rowCount( const QModelIndex& parent ) const
{
   if (test) return 0; //HACK
   if (!parent.isValid())
      return d_ptr->m_lCategoryCounter.size();
   else if (!parent.parent().isValid() && parent.row() < d_ptr->m_lCategoryCounter.size()) {
      BookmarkTopLevelItem* item = static_cast<BookmarkTopLevelItem*>(parent.internalPointer());
      return item->m_lChildren.size();
   }
   return 0;
}

Qt::ItemFlags BookmarkModel::flags( const QModelIndex& index ) const
{
   if (!index.isValid())
      return 0;
   return Qt::ItemIsEnabled | Qt::ItemIsSelectable | (index.parent().isValid()?Qt::ItemIsDragEnabled|Qt::ItemIsDropEnabled:Qt::ItemIsEnabled);
}

///There is only 1 column
int BookmarkModel::columnCount ( const QModelIndex& parent) const
{
   Q_UNUSED(parent)
   return 1;
}

///Get the bookmark parent
QModelIndex BookmarkModel::parent( const QModelIndex& idx) const
{
   if (!idx.isValid()) {
      return QModelIndex();
   }
   const CategorizedCompositeNode* modelItem = static_cast<CategorizedCompositeNode*>(idx.internalPointer());
   if (modelItem->type() == CategorizedCompositeNode::Type::BOOKMARK) {
      BookmarkTopLevelItem* item = static_cast<const NumberTreeBackend*>(modelItem)->m_pParent;
      if (item) {
         return index(item->m_Row,0);
      }
   }
   return QModelIndex();
} //parent

///Get the index
QModelIndex BookmarkModel::index(int row, int column, const QModelIndex& parent) const
{
   if (parent.isValid() && !column)
      return createIndex(row,column,(void*) static_cast<CategorizedCompositeNode*>(d_ptr->m_lCategoryCounter[parent.row()]->m_lChildren[row]));
   else if (row >= 0 && row < d_ptr->m_lCategoryCounter.size() && !column) {
      return createIndex(row,column,(void*) static_cast<CategorizedCompositeNode*>(d_ptr->m_lCategoryCounter[row]));
   }
   return QModelIndex();
}

///Get bookmarks mime types
QStringList BookmarkModel::mimeTypes() const
{
   return d_ptr->m_lMimes;
}

///Generate mime data
QMimeData* BookmarkModel::mimeData(const QModelIndexList &indexes) const
{
   QMimeData *mimeData = new QMimeData();
   foreach (const QModelIndex &index, indexes) {
      if (index.isValid()) {
         QString text = data(index, Call::Role::Number).toString();
         mimeData->setData(RingMimes::PLAIN_TEXT , text.toUtf8());
         mimeData->setData(RingMimes::PHONENUMBER, text.toUtf8());
         return mimeData;
      }
   }
   return mimeData;
} //mimeData

///Return valid payload types
int BookmarkModel::acceptedPayloadTypes()
{
   return CallModel::DropPayloadType::CALL;
}

///Get call info TODO use Call:: one
QVariant BookmarkModelPrivate::commonCallInfo(NumberTreeBackend* number, int role) const
{
   if (!number)
      return QVariant();
   QVariant cat;
   switch (role) {
      case Qt::DisplayRole:
      case Call::Role::Name:
         cat = number->m_pNumber->contact()?number->m_pNumber->contact()->formattedName():number->m_pNumber->primaryName();
         break;
      case Qt::ToolTipRole:
         cat = number->m_pNumber->presenceMessage();
         break;
      case Call::Role::Number:
         cat = number->m_pNumber->uri();//call->getPeerContactMethod();
         break;
      case Call::Role::Direction2:
         cat = 4;//call->getHistoryState();
         break;
      case Call::Role::Date:
         cat = tr("N/A");//call->getStartTimeStamp();
         break;
      case Call::Role::Length:
         cat = tr("N/A");//call->getLength();
         break;
      case Call::Role::FormattedDate:
         cat = tr("N/A");//QDateTime::fromTime_t(call->getStartTimeStamp().toUInt()).toString();
         break;
      case Call::Role::HasRecording:
         cat = false;//call->hasRecording();
         break;
      case Call::Role::Historystate:
         cat = (int)Call::LegacyHistoryState::NONE;//call->getHistoryState();
         break;
      case Call::Role::FuzzyDate:
         cat = "N/A";//timeToHistoryCategory(QDateTime::fromTime_t(call->getStartTimeStamp().toUInt()).date());
         break;
      case Call::Role::PhoneNu:
         return QVariant::fromValue(const_cast<ContactMethod*>(number->m_pNumber));
      case Call::Role::IsBookmark:
         return true;
      case Call::Role::Filter:
         return number->m_pNumber->uri()+number->m_pNumber->primaryName();
      case Call::Role::IsPresent:
         return number->m_pNumber->isPresent();
      case Call::Role::PhotoPtr:
         if (number->m_pNumber->contact())
            return number->m_pNumber->contact()->photo();
         cat = true;
         break;
   }
   return cat;
} //commonCallInfo

///Get category
QString BookmarkModelPrivate::category(NumberTreeBackend* number) const
{
   QString cat = commonCallInfo(number).toString();
   if (cat.size())
      cat = cat[0].toUpper();
   return cat;
}

void BookmarkModelPrivate::slotRequest(const QString& uri)
{
   Q_UNUSED(uri)
   qDebug() << "Presence Request" << uri << "denied";
   //DBus::PresenceManager::instance().answerServerRequest(uri,true); //FIXME turn on after 1.3.0
}



QVector<ContactMethod*> BookmarkModelPrivate::serialisedToList(const QStringList& list)
{
   QVector<ContactMethod*> numbers;
   foreach(const QString& item,list) {
      ContactMethod* nb = PhoneDirectoryModel::instance()->fromHash(item);
      if (nb) {
         nb->setTracked(true);
         nb->setUid(item);
         numbers << nb;
      }
   }
   return numbers;
}

bool BookmarkModelPrivate::displayFrequentlyUsed() const
{
   return true;
}

QVector<ContactMethod*> BookmarkModelPrivate::bookmarkList() const
{
   return (q_ptr->backends().size() > 0) ? q_ptr->backends()[0]->items<ContactMethod>() : QVector<ContactMethod*>();
}

BookmarkTopLevelItem::BookmarkTopLevelItem(QString name) 
   : CategorizedCompositeNode(CategorizedCompositeNode::Type::TOP_LEVEL),m_Name(name),
      m_MostPopular(false),m_Row(-1)
{
}

bool BookmarkModel::removeRows( int row, int count, const QModelIndex & parent)
{
   if (parent.isValid()) {
      const int parentRow = parent.row();
      beginRemoveRows(parent,row,row+count-1);
      for (int i=row;i<row+count;i++)
         d_ptr->m_lCategoryCounter[parent.row()]->m_lChildren.removeAt(i);
      endRemoveRows();
      if (!d_ptr->m_lCategoryCounter[parentRow]->m_lChildren.size()) {
         beginRemoveRows(QModelIndex(),parentRow,parentRow);
         d_ptr->m_hCategories.remove(d_ptr->m_hCategories.key(d_ptr->m_lCategoryCounter[parentRow]));
         d_ptr->m_lCategoryCounter.removeAt(parentRow);
         for (int i=0;i<d_ptr->m_lCategoryCounter.size();i++) {
            d_ptr->m_lCategoryCounter[i]->m_Row =i;
         }
         endRemoveRows();
      }
      return true;
   }
   return false;
}

void BookmarkModel::addBookmark(ContactMethod* number)
{
   Q_UNUSED(number)
   if (backends().size())
      backends()[0]->editor<ContactMethod>()->append(number);
   else
      qWarning() << "No bookmark backend is set";
}

void BookmarkModel::removeBookmark(ContactMethod* number)
{
   Q_UNUSED(number)
}

void BookmarkModel::remove(const QModelIndex& idx)
{
   Q_UNUSED(idx)
//    ContactMethod* nb = getNumber(idx);
//    if (nb) {
//       removeRows(idx.row(),1,idx.parent());
//       removeBookmark(nb);
//       emit layoutAboutToBeChanged();
//       emit layoutChanged();
//    }
}

ContactMethod* BookmarkModel::getNumber(const QModelIndex& idx)
{
   if (idx.isValid()) {
      if (idx.parent().isValid() && idx.parent().row() < d_ptr->m_lCategoryCounter.size()) {
         return d_ptr->m_lCategoryCounter[idx.parent().row()]->m_lChildren[idx.row()]->m_pNumber;
      }
   }
   return nullptr;
}

///Callback when an item change
void BookmarkModelPrivate::slotIndexChanged(const QModelIndex& idx)
{
   emit q_ptr->dataChanged(idx,idx);
}


// bool BookmarkModel::hasBackends() const
// {
//    return d_ptr->m_lBackends.size();
// }

// bool BookmarkModel::hasEnabledBackends() const
// {
//    foreach(CollectionInterface* b, d_ptr->m_lBackends) {
//       if (b->isEnabled())
//          return true;
//    }
//    return false;
// }

// const QVector<CollectionInterface*> BookmarkModel::backends() const
// {
//    return d_ptr->m_lBackends;
// }


bool BookmarkModel::addItemCallback(ContactMethod* item)
{
   Q_UNUSED(item)
   reloadCategories(); //TODO this is far from optimal
   return true;
}

bool BookmarkModel::removeItemCallback(ContactMethod* item)
{
   Q_UNUSED(item)
   return false;
}

// const QVector<CollectionInterface*> BookmarkModel::enabledBackends() const
// {
//    return d_ptr->m_lBackends; //TODO filter them
// }

// CommonCollectionModel* BookmarkModel::backendModel() const
// {
//    return nullptr; //TODO
// }

bool BookmarkModel::clearAllBackends() const
{
   foreach (CollectionInterface* backend, backends()) {
      if (backend->supportedFeatures() & CollectionInterface::ADD) {
         backend->clear();
      }
   }
   return true;
}

// bool BookmarkModel::enableBackend(CollectionInterface* backend, bool enable)
// {
//    Q_UNUSED(backend)
//    Q_UNUSED(enable)
//    return false; //TODO
// }

// void BookmarkModel::addBackend(CollectionInterface* backend, LoadOptions options)
// {
//    d_ptr->m_lBackends << backend;
//    connect(backend,SIGNAL(newBookmarkAdded(ContactMethod*)),this,SLOT(reloadCategories()));
//    if (options & LoadOptions::FORCE_ENABLED)
//       backend->load();
// }

void BookmarkModel::backendAddedCallback(CollectionInterface* backend)
{
   Q_UNUSED(backend)
}

// QString BookmarkModel::backendCategoryName() const
// {
//    return tr("Bookmarks");
// }

#include <bookmarkmodel.moc>