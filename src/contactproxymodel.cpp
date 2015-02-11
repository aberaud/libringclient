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
#include "contactproxymodel.h"

//Qt
#include <QtCore/QDebug>
#include <QtCore/QDate>
#include <QtCore/QMimeData>
#include <QtCore/QCoreApplication>

//Ring
#include "callmodel.h"
#include "historymodel.h"
#include "contactmethod.h"
#include "phonedirectorymodel.h"
#include "historytimecategorymodel.h"
#include "person.h"
#include "uri.h"
#include "mime.h"
#include "personmodel.h"

class ContactTreeNode;

class ContactTreeBinder : public QObject { //FIXME Qt5 remove when dropping Qt4
   Q_OBJECT
public:
   ContactTreeBinder(ContactProxyModel* m,ContactTreeNode* n);
private:
   ContactTreeNode* m_pTreeNode;
   ContactProxyModel* m_pModel;
private Q_SLOTS:
   void slotContactChanged();
   void slotStatusChanged();
   void slotContactMethodCountChanged(int,int);
   void slotContactMethodCountAboutToChange(int,int);
};

class ContactTopLevelItem : public CategorizedCompositeNode {
   friend class ContactProxyModel;
   friend class ContactProxyModelPrivate;
   friend class ContactTreeBinder;
   public:
      virtual QObject* getSelf() const override;
      virtual ~ContactTopLevelItem();
   private:
      explicit ContactTopLevelItem(const QString& name) : CategorizedCompositeNode(CategorizedCompositeNode::Type::TOP_LEVEL),m_Name(name),
      m_lChildren(),m_Index(-1){
         m_lChildren.reserve(32);
      }
      QVector<ContactTreeNode*> m_lChildren;
      QString m_Name;
      int m_Index;
};

class ContactTreeNode : public CategorizedCompositeNode {
public:
   ContactTreeNode(Person* ct, ContactProxyModel* parent);
   virtual ~ContactTreeNode();
   Person* m_pContact;
   ContactTopLevelItem* m_pParent3;
   uint m_Index;
   virtual QObject* getSelf() const override;
   ContactTreeBinder* m_pBinder;
};

class ContactProxyModelPrivate : public QObject
{
   Q_OBJECT
public:
   ContactProxyModelPrivate(ContactProxyModel* parent);

   //Helpers
   QString category(const Person* ct) const;

   //Attributes
   QHash<Person*, time_t>      m_hContactByDate   ;
   QVector<ContactTopLevelItem*>       m_lCategoryCounter ;
   QHash<QString,ContactTopLevelItem*> m_hCategories      ;
   int                          m_Role             ;
   bool                         m_ShowAll          ;
   QStringList                  m_lMimes           ;

   //Helper
   ContactTopLevelItem* getContactTopLevelItem(const QString& category);

private:
   ContactProxyModel* q_ptr;

public Q_SLOTS:
   void reloadCategories();
   void slotContactAdded(Person* c);
};

ContactTopLevelItem::~ContactTopLevelItem() {
   while(m_lChildren.size()) {
      ContactTreeNode* node = m_lChildren[0];
      m_lChildren.remove(0);
      delete node;
   }
}

QObject* ContactTopLevelItem::getSelf() const
{
   return nullptr;
}

ContactTreeNode::ContactTreeNode(Person* ct, ContactProxyModel* parent) : CategorizedCompositeNode(CategorizedCompositeNode::Type::CONTACT),
   m_pContact(ct),m_pParent3(nullptr),m_Index(-1)
{
   m_pBinder = new ContactTreeBinder(parent,this);
}

ContactTreeNode::~ContactTreeNode()
{
   delete m_pBinder;
}

QObject* ContactTreeNode::getSelf() const
{
   return m_pContact;
}

ContactTreeBinder::ContactTreeBinder(ContactProxyModel* m,ContactTreeNode* n) :
   QObject(),m_pTreeNode(n),m_pModel(m)
{
   connect(n->m_pContact,SIGNAL(changed()),this,SLOT(slotContactChanged()));
   connect(n->m_pContact,SIGNAL(phoneNumberCountChanged(int,int)),this,SLOT(slotContactMethodCountChanged(int,int)));
   connect(n->m_pContact,SIGNAL(phoneNumberCountAboutToChange(int,int)),this,SLOT(slotContactMethodCountAboutToChange(int,int)));
}


void ContactTreeBinder::slotContactChanged()
{
   const QModelIndex idx = m_pModel->index(m_pTreeNode->m_Index,0,m_pModel->index(m_pTreeNode->m_pParent3->m_Index,0));
   const QModelIndex lastPhoneIdx = m_pModel->index(m_pTreeNode->m_pContact->phoneNumbers().size()-1,0,idx);
   emit m_pModel->dataChanged(idx,idx);
   if (lastPhoneIdx.isValid()) //Need to be done twice
      emit m_pModel->dataChanged(m_pModel->index(0,0,idx),lastPhoneIdx);
}

void ContactTreeBinder::slotStatusChanged()
{
   
}

void ContactTreeBinder::slotContactMethodCountChanged(int count, int oldCount)
{
   const QModelIndex idx = m_pModel->index(m_pTreeNode->m_Index,0,m_pModel->index(m_pTreeNode->m_pParent3->m_Index,0));
   if (count > oldCount) {
      const QModelIndex lastPhoneIdx = m_pModel->index(oldCount-1,0,idx);
      m_pModel->beginInsertRows(idx,oldCount,count-1);
      m_pModel->endInsertRows();
   }
   emit m_pModel->dataChanged(idx,idx);
}

void ContactTreeBinder::slotContactMethodCountAboutToChange(int count, int oldCount)
{
   const QModelIndex idx = m_pModel->index(m_pTreeNode->m_Index,0,m_pModel->index(m_pTreeNode->m_pParent3->m_Index,0));
   if (count < oldCount) {
      //If count == 1, disable all children
      m_pModel->beginRemoveRows(idx,count == 1?0:count,oldCount-1);
      m_pModel->endRemoveRows();
   }
}

ContactProxyModelPrivate::ContactProxyModelPrivate(ContactProxyModel* parent) : QObject(parent), q_ptr(parent),
m_lCategoryCounter()
{
   
}

//
ContactProxyModel::ContactProxyModel(int role, bool showAll) : QAbstractItemModel(QCoreApplication::instance()),d_ptr(new ContactProxyModelPrivate(this))
{
   setObjectName("ContactProxyModel");
   d_ptr->m_Role    = role;
   d_ptr->m_ShowAll = showAll;
   d_ptr->m_lCategoryCounter.reserve(32);
   d_ptr->m_lMimes << RingMimes::PLAIN_TEXT << RingMimes::PHONENUMBER;
   connect(PersonModel::instance(),SIGNAL(reloaded()),d_ptr.data(),SLOT(reloadCategories()));
   connect(PersonModel::instance(),SIGNAL(newContactAdded(Person*)),d_ptr.data(),SLOT(slotContactAdded(Person*)));
}

ContactProxyModel::~ContactProxyModel()
{
   foreach(ContactTopLevelItem* item,d_ptr->m_lCategoryCounter) {
      delete item;
   }
}

QHash<int,QByteArray> ContactProxyModel::roleNames() const
{
   static QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
   static bool initRoles = false;
   if (!initRoles) {
      initRoles = true;
      roles.insert(PersonModel::Role::Organization      ,QByteArray("organization")     );
      roles.insert(PersonModel::Role::Group             ,QByteArray("group")            );
      roles.insert(PersonModel::Role::Department        ,QByteArray("department")       );
      roles.insert(PersonModel::Role::PreferredEmail    ,QByteArray("preferredEmail")   );
      roles.insert(PersonModel::Role::FormattedLastUsed ,QByteArray("formattedLastUsed"));
      roles.insert(PersonModel::Role::IndexedLastUsed   ,QByteArray("indexedLastUsed")  );
      roles.insert(PersonModel::Role::DatedLastUsed     ,QByteArray("datedLastUsed")    );
      roles.insert(PersonModel::Role::Filter            ,QByteArray("filter")           );
      roles.insert(PersonModel::Role::DropState         ,QByteArray("dropState")        );
   }
   return roles;
}

ContactTopLevelItem* ContactProxyModelPrivate::getContactTopLevelItem(const QString& category)
{
   if (!m_hCategories[category]) {
      ContactTopLevelItem* item = new ContactTopLevelItem(category);
      m_hCategories[category] = item;
      item->m_Index = m_lCategoryCounter.size();
//       emit layoutAboutToBeChanged();
      q_ptr->beginInsertRows(QModelIndex(),m_lCategoryCounter.size(),m_lCategoryCounter.size()); {
         m_lCategoryCounter << item;
      } q_ptr->endInsertRows();
//       emit layoutChanged();
   }
   ContactTopLevelItem* item = m_hCategories[category];
   return item;
}

void ContactProxyModelPrivate::reloadCategories()
{
   emit q_ptr->layoutAboutToBeChanged();
   q_ptr->beginResetModel();
   m_hCategories.clear();
   q_ptr->beginRemoveRows(QModelIndex(),0,m_lCategoryCounter.size()-1);
   foreach(ContactTopLevelItem* item,m_lCategoryCounter) {
      delete item;
   }
   q_ptr->endRemoveRows();
   m_lCategoryCounter.clear();
   foreach(const Person* cont, PersonModel::instance()->contacts()) {
      if (cont) {
         const QString val = category(cont);
         ContactTopLevelItem* item = getContactTopLevelItem(val);
         ContactTreeNode* contactNode = new ContactTreeNode(const_cast<Person*>(cont),q_ptr);
         contactNode->m_pParent3 = item;
         contactNode->m_Index = item->m_lChildren.size();
         item->m_lChildren << contactNode;
      }
   }
   q_ptr->endResetModel();
   emit q_ptr->layoutChanged();
}

void ContactProxyModelPrivate::slotContactAdded(Person* c)
{
   if (!c) return;
   const QString val = category(c);
   ContactTopLevelItem* item = getContactTopLevelItem(val);
   ContactTreeNode* contactNode = new ContactTreeNode(c,q_ptr);
   contactNode->m_pParent3 = item;
   contactNode->m_Index = item->m_lChildren.size();
   //emit layoutAboutToBeChanged();
   q_ptr->beginInsertRows(q_ptr->index(item->m_Index,0,QModelIndex()),item->m_lChildren.size(),item->m_lChildren.size()); {
      item->m_lChildren << contactNode;
   } q_ptr->endInsertRows();
   //emit layoutChanged();
}

bool ContactProxyModel::setData( const QModelIndex& index, const QVariant &value, int role)
{
   if (index.isValid() && index.parent().isValid()) {
      CategorizedCompositeNode* modelItem = (CategorizedCompositeNode*)index.internalPointer();
      if (role == PersonModel::Role::DropState) {
         modelItem->setDropState(value.toInt());
         emit dataChanged(index, index);
         return true;
      }
   }
   return false;
}

QVariant ContactProxyModel::data( const QModelIndex& index, int role) const
{
   if (!index.isValid())
      return QVariant();

   CategorizedCompositeNode* modelItem = (CategorizedCompositeNode*)index.internalPointer();
   switch (modelItem->type()) {
      case CategorizedCompositeNode::Type::TOP_LEVEL:
      switch (role) {
         case Qt::DisplayRole:
            return static_cast<const ContactTopLevelItem*>(modelItem)->m_Name;
         case PersonModel::Role::IndexedLastUsed:
            return index.child(0,0).data(PersonModel::Role::IndexedLastUsed);
         case PersonModel::Role::Active:
            return true;
         default:
            break;
      }
      break;
   case CategorizedCompositeNode::Type::CONTACT:{
      const Person* c = static_cast<Person*>(modelItem->getSelf());
      switch (role) {
         case Qt::DisplayRole:
            return QVariant(c->formattedName());
         case PersonModel::Role::Organization:
            return QVariant(c->organization());
         case PersonModel::Role::Group:
            return QVariant(c->group());
         case PersonModel::Role::Department:
            return QVariant(c->department());
         case PersonModel::Role::PreferredEmail:
            return QVariant(c->preferredEmail());
         case PersonModel::Role::DropState:
            return QVariant(modelItem->dropState());
         case PersonModel::Role::FormattedLastUsed:
            return QVariant(HistoryTimeCategoryModel::timeToHistoryCategory(c->phoneNumbers().lastUsedTimeStamp()));
         case PersonModel::Role::IndexedLastUsed:
            return QVariant((int)HistoryTimeCategoryModel::timeToHistoryConst(c->phoneNumbers().lastUsedTimeStamp()));
         case PersonModel::Role::Active:
            return c->isActive();
         case PersonModel::Role::DatedLastUsed:
            return QVariant(QDateTime::fromTime_t( c->phoneNumbers().lastUsedTimeStamp()));
         case PersonModel::Role::Filter:
            return c->filterString();
         default:
            break;
      }
      break;
   }
   case CategorizedCompositeNode::Type::NUMBER: /* && (role == Qt::DisplayRole)) {*/
   case CategorizedCompositeNode::Type::CALL:
   case CategorizedCompositeNode::Type::BOOKMARK:
   default:
      switch (role) {
         case PersonModel::Role::Active:
            return true;
      }
      break;
   };
   return QVariant();
}

QVariant ContactProxyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   Q_UNUSED(section)
   if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
      return QVariant(tr("Contacts"));
   return QVariant();
}

bool ContactProxyModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
   Q_UNUSED( action )
   setData(parent,-1,Call::Role::DropState);
   if (data->hasFormat(RingMimes::CALLID)) {
      const QByteArray encodedCallId = data->data( RingMimes::CALLID    );
      const QModelIndex targetIdx    = index   ( row,column,parent );
      Call* call                     = CallModel::instance()->getCall ( encodedCallId        );
      if (call && targetIdx.isValid()) {
         CategorizedCompositeNode* modelItem = (CategorizedCompositeNode*)targetIdx.internalPointer();
         switch (modelItem->type()) {
            case CategorizedCompositeNode::Type::CONTACT: {
               const Person* ct = static_cast<Person*>(modelItem->getSelf());
               if (ct) {
                  switch(ct->phoneNumbers().size()) {
                     case 0: //Do nothing when there is no phone numbers
                        return false;
                     case 1: //Call when there is one
                        CallModel::instance()->transfer(call,ct->phoneNumbers()[0]);
                        break;
                     default:
                        //TODO
                        break;
                  };
               }
            } break;
            case CategorizedCompositeNode::Type::NUMBER: {
               const Person::ContactMethods nbs = *static_cast<Person::ContactMethods*>(modelItem);
               const ContactMethod*          nb  = nbs[row];
               if (nb) {
                  call->setTransferNumber(nb->uri());
                  CallModel::instance()->transfer(call,nb);
               }
            } break;
            case CategorizedCompositeNode::Type::CALL:
            case CategorizedCompositeNode::Type::BOOKMARK:
            case CategorizedCompositeNode::Type::TOP_LEVEL:
               break;
         }
      }
   }
   return false;
}


int ContactProxyModel::rowCount( const QModelIndex& parent ) const
{
   if (!parent.isValid() || !parent.internalPointer())
      return d_ptr->m_lCategoryCounter.size();
   const CategorizedCompositeNode* parentNode = static_cast<CategorizedCompositeNode*>(parent.internalPointer());
   switch(parentNode->type()) {
      case CategorizedCompositeNode::Type::TOP_LEVEL:
         return static_cast<const ContactTopLevelItem*>(parentNode)->m_lChildren.size();
      case CategorizedCompositeNode::Type::CONTACT: {
         const Person* ct = static_cast<Person*>(parentNode->getSelf());
         const int size = ct->phoneNumbers().size();
         //Do not return the number if there is only one, it will be drawn part of the contact
         return size==1?0:size;
      }
      case CategorizedCompositeNode::Type::CALL:
      case CategorizedCompositeNode::Type::NUMBER:
      case CategorizedCompositeNode::Type::BOOKMARK:
      default:
         return 0;
   };
}

Qt::ItemFlags ContactProxyModel::flags( const QModelIndex& index ) const
{
   if (!index.isValid())
      return Qt::NoItemFlags;
   return Qt::ItemIsEnabled | Qt::ItemIsSelectable | (index.parent().isValid()?Qt::ItemIsDragEnabled|Qt::ItemIsDropEnabled:Qt::ItemIsEnabled);
}

int ContactProxyModel::columnCount ( const QModelIndex& parent) const
{
   Q_UNUSED(parent)
   return 1;
}

QModelIndex ContactProxyModel::parent( const QModelIndex& index) const
{
   if (!index.isValid() || !index.internalPointer())
      return QModelIndex();
   const CategorizedCompositeNode* modelItem = static_cast<CategorizedCompositeNode*>(index.internalPointer());
   switch (modelItem->type()) {
      case CategorizedCompositeNode::Type::CONTACT: {
         const ContactTopLevelItem* tl = ((ContactTreeNode*)(modelItem))->m_pParent3;
         return createIndex(tl->m_Index,0,(void*)tl);
      }
      break;
      case CategorizedCompositeNode::Type::NUMBER: {
         const ContactTreeNode* parentNode = static_cast<const ContactTreeNode*>(modelItem->parentNode());
         return createIndex(parentNode->m_Index, 0, (void*)parentNode);
      }
      case CategorizedCompositeNode::Type::TOP_LEVEL:
      case CategorizedCompositeNode::Type::BOOKMARK:
      case CategorizedCompositeNode::Type::CALL:
      default:
         return QModelIndex();
         break;
   };
}

QModelIndex ContactProxyModel::index( int row, int column, const QModelIndex& parent) const
{
   if (parent.isValid() && parent.internalPointer()) {
      CategorizedCompositeNode* parentNode = static_cast<CategorizedCompositeNode*>(parent.internalPointer());
      switch(parentNode->type()) {
         case CategorizedCompositeNode::Type::TOP_LEVEL: {
            ContactTopLevelItem* tld = static_cast<ContactTopLevelItem*>(parentNode);
            if (tld && row < tld->m_lChildren.size())
               return createIndex(row,column,(void*)tld->m_lChildren[row]);
         }
            break;
         case CategorizedCompositeNode::Type::CONTACT: {
            const ContactTreeNode* ctn = (ContactTreeNode*)parentNode;
            const Person*          ct = (Person*)ctn->getSelf()    ;
            if (ct->phoneNumbers().size()>row) {
               const_cast<Person::ContactMethods*>(&ct->phoneNumbers())->setParentNode((CategorizedCompositeNode*)ctn);
               return createIndex(row,column,(void*)&ct->phoneNumbers());
            }
         }
            break;
         case CategorizedCompositeNode::Type::CALL:
         case CategorizedCompositeNode::Type::BOOKMARK:
         case CategorizedCompositeNode::Type::NUMBER:
            break;
      };
   }
   else if (row < d_ptr->m_lCategoryCounter.size()){
      //Return top level
      return createIndex(row,column,(void*)d_ptr->m_lCategoryCounter[row]);
   }
   return QModelIndex();
}

QStringList ContactProxyModel::mimeTypes() const
{
   return d_ptr->m_lMimes;
}

QMimeData* ContactProxyModel::mimeData(const QModelIndexList &indexes) const
{
   QMimeData *mimeData = new QMimeData();
   foreach (const QModelIndex &index, indexes) {
      if (index.isValid()) {
         const CategorizedCompositeNode* modelItem = static_cast<CategorizedCompositeNode*>(index.internalPointer());
         switch(modelItem->type()) {
            case CategorizedCompositeNode::Type::CONTACT: {
               //Contact
               const Person* ct = static_cast<Person*>(modelItem->getSelf());
               if (ct) {
                  if (ct->phoneNumbers().size() == 1) {
                     mimeData->setData(RingMimes::PHONENUMBER , ct->phoneNumbers()[0]->toHash().toUtf8());
                  }
                  mimeData->setData(RingMimes::CONTACT , ct->uid());
               }
               return mimeData;
               } break;
            case CategorizedCompositeNode::Type::NUMBER: {
               //Phone number
               const QString text = data(index, Qt::DisplayRole).toString();
               const Person::ContactMethods nbs = *static_cast<Person::ContactMethods*>(index.internalPointer());
               const ContactMethod*          nb  = nbs[index.row()];
               mimeData->setData(RingMimes::PLAIN_TEXT , text.toUtf8());
               mimeData->setData(RingMimes::PHONENUMBER, nb->toHash().toUtf8());
               return mimeData;
               } break;
            case CategorizedCompositeNode::Type::TOP_LEVEL:
            case CategorizedCompositeNode::Type::CALL:
            case CategorizedCompositeNode::Type::BOOKMARK:
            default:
               return nullptr;
         };
      }
   }
   return mimeData;
}

///Return valid payload types
int ContactProxyModel::acceptedPayloadTypes()
{
   return CallModel::DropPayloadType::CALL;
}



/*****************************************************************************
 *                                                                           *
 *                                  Helpers                                  *
 *                                                                           *
 ****************************************************************************/


QString ContactProxyModelPrivate::category(const Person* ct) const {
   if (!ct)
      return QString();
   QString cat;
   switch (m_Role) {
      case PersonModel::Role::Organization:
         cat = ct->organization();
         break;
      case PersonModel::Role::Group:
         cat = ct->group();
         break;
      case PersonModel::Role::Department:
         cat = ct->department();
         break;
      case PersonModel::Role::PreferredEmail:
         cat = ct->preferredEmail();
         break;
      case PersonModel::Role::FormattedLastUsed:
         cat = HistoryTimeCategoryModel::timeToHistoryCategory(ct->phoneNumbers().lastUsedTimeStamp());
         break;
      case PersonModel::Role::IndexedLastUsed:
         cat = QString::number((int)HistoryTimeCategoryModel::timeToHistoryConst(ct->phoneNumbers().lastUsedTimeStamp()));
         break;
      case PersonModel::Role::DatedLastUsed:
         cat = QDateTime::fromTime_t(ct->phoneNumbers().lastUsedTimeStamp()).toString();
         break;
      default:
         cat = ct->formattedName();
   }
   if (cat.size() && !m_ShowAll)
      cat = cat[0].toUpper();
   return cat;
}

void ContactProxyModel::setRole(int role)
{
   if (role != d_ptr->m_Role) {
      d_ptr->m_Role = role;
      d_ptr->reloadCategories();
   }
}

void ContactProxyModel::setShowAll(bool showAll)
{
   if (showAll != d_ptr->m_ShowAll) {
      d_ptr->m_ShowAll = showAll;
      d_ptr->reloadCategories();
   }
}

#include <contactproxymodel.moc>
