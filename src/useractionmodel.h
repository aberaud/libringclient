/****************************************************************************
 *   Copyright (C) 2012-2015 by Savoir-Faire Linux                          *
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
#ifndef USERACTIONMODEL_H
#define USERACTIONMODEL_H

#include <QtCore/QString>
#include <QtCore/QAbstractItemModel>
#include "typedefs.h"

#include "call.h"

class Call;
class UserActionModelPrivate;

/**
 * @class UserActionModel Hold available actions for a given call state
 *
 * @todo This is not a model yet, however, it would be nice if it was
 **/
class LIB_EXPORT UserActionModel : public QObject/*QAbstractItemModel*/ {
   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
   Q_OBJECT
   #pragma GCC diagnostic pop
public:

   //Roles
   enum Role {
      VISIBLE       = 100,
      RELATIVEINDEX = 101,
   };

   ///(End)user action, all possibility, not only state aware ones like "Action"
   enum class Action {
      PICKUP   = 0,
      HOLD     = 1,
      UNHOLD   = 2,
      MUTE     = 3,
      TRANSFER = 4,
      RECORD   = 5,
      REFUSE   = 6,
      ACCEPT   = 7,
      HANGUP   = 8,
      COUNT__,
   };
   Q_ENUMS(Action)

   //Properties
   Q_PROPERTY( bool isPickupEnabled   READ isPickupEnabled   NOTIFY actionStateChanged )
   Q_PROPERTY( bool isHoldEnabled     READ isHoldEnabled     NOTIFY actionStateChanged )
   Q_PROPERTY( bool isUnholdEnabled   READ isUnholdEnabled   NOTIFY actionStateChanged )
   Q_PROPERTY( bool isHangupEnabled   READ isHangupEnabled   NOTIFY actionStateChanged )
   Q_PROPERTY( bool isMuteEnabled     READ isMuteEnabled     NOTIFY actionStateChanged )
   Q_PROPERTY( bool isTransferEnabled READ isTransferEnabled NOTIFY actionStateChanged )
   Q_PROPERTY( bool isRecordEnabled   READ isRecordEnabled   NOTIFY actionStateChanged )
   Q_PROPERTY( bool isRefuseEnabled   READ isRefuseEnabled   NOTIFY actionStateChanged )
   Q_PROPERTY( bool isAcceptEnabled   READ isAcceptEnabled   NOTIFY actionStateChanged )
   Q_PROPERTY( uint enabledCount      READ enabledCount      NOTIFY actionStateChanged )

   //Constructor
   explicit UserActionModel(Call* parent);
   virtual ~UserActionModel();

   //Abstract model members
//    virtual QVariant      data       (const QModelIndex& index, int role = Qt::DisplayRole  ) const;
//    virtual int           rowCount   (const QModelIndex& parent = QModelIndex()             ) const;
//    virtual int           columnCount(const QModelIndex& parent = QModelIndex()             ) const;
//    virtual Qt::ItemFlags flags      (const QModelIndex& index                              ) const;
//    virtual bool          setData    (const QModelIndex& index, const QVariant &value, int role);
//    virtual QHash<int,QByteArray> roleNames() const override;

   //Getters
   Q_INVOKABLE bool isActionEnabled ( UserActionModel::Action action ) const;
   Q_INVOKABLE uint relativeIndex   ( UserActionModel::Action action ) const;
   Q_INVOKABLE uint enabledCount    (                                ) const;

   bool isPickupEnabled  () const;
   bool isHoldEnabled    () const;
   bool isUnholdEnabled  () const;
   bool isHangupEnabled  () const;
   bool isMuteEnabled    () const;
   bool isTransferEnabled() const;
   bool isRecordEnabled  () const;
   bool isRefuseEnabled  () const;
   bool isAcceptEnabled  () const;

private:
   const QScopedPointer<UserActionModelPrivate> d_ptr;
   Q_DECLARE_PRIVATE(UserActionModel)

Q_SIGNALS:
   ///The list of currently available actions has changed
   void actionStateChanged();
};
// Q_DECLARE_METATYPE(UserActionModel*)

#endif
