/****************************************************************************
 *   Copyright (C) 2009-2015 by Savoir-Faire Linux                          *
 *   Author : Jérémy Quentin <jeremy.quentin@savoirfairelinux.com>          *
 *            Emmanuel Lepage Vallee <emmanuel.lepage@savoirfairelinux.com> *
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
#ifndef ACCOUNTMODEL_H
#define ACCOUNTMODEL_H

#include <QtCore/QVector>
#include <QtCore/QStringList>
#include <QtCore/QAbstractListModel>

#include "account.h"
#include "typedefs.h"

//Private
class AccountModelPrivate;

///AccountList: List of all daemon accounts
class LIB_EXPORT AccountModel : public QAbstractListModel {
   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
   Q_OBJECT
   #pragma GCC diagnostic pop

public:
   Q_PROPERTY(Account* ip2ip                      READ ip2ip                        )
   Q_PROPERTY(bool     presenceEnabled            READ isPresenceEnabled            )
   Q_PROPERTY(bool     presencePublishSupported   READ isPresencePublishSupported   )
   Q_PROPERTY(bool     presenceSubscribeSupported READ isPresenceSubscribeSupported )

   friend class Account;
   friend class AccountPrivate;
   friend class AvailableAccountModel;
   friend class AvailableAccountModelPrivate;

   //Static getter and destructor
   static AccountModel* instance();

   //Getters
   Q_INVOKABLE Account* getById                     ( const QByteArray& id, bool ph = false) const;
   int                  size                        (                                      ) const;
   Account*             getAccountByModelIndex      ( const QModelIndex& item              ) const;
   static QString       getSimilarAliasIndex        ( const QString& alias                 )      ;
   Account*             ip2ip                       (                                      ) const;
   bool                 isPresenceEnabled           (                                      ) const;
   bool                 isPresencePublishSupported  (                                      ) const;
   bool                 isPresenceSubscribeSupported(                                      ) const;

   //Abstract model accessors
   virtual QVariant              data     ( const QModelIndex& index, int role = Qt::DisplayRole     ) const override;
   virtual int                   rowCount ( const QModelIndex& parent = QModelIndex()                ) const override;
   virtual Qt::ItemFlags         flags    ( const QModelIndex& index                                 ) const override;
   virtual bool                  setData  ( const QModelIndex& index, const QVariant &value, int role)       override;
   virtual QHash<int,QByteArray> roleNames(                                                          ) const override;

   //Mutators
   Q_INVOKABLE Account* add      ( const QString& alias     );
   Q_INVOKABLE void     remove   ( Account* account         );
   void                 remove   ( const QModelIndex& index );
   void                 save     (                          );
   Q_INVOKABLE bool     moveUp   ( const QModelIndex& idx   );
   Q_INVOKABLE bool     moveDown ( const QModelIndex& idx   );
   Q_INVOKABLE void     cancel   (                          );

   //Operators
   Account*       operator[] (int               i)      ;
   Account*       operator[] (const QByteArray& i)      ;
   const Account* operator[] (int               i) const;

private:
   //Constructors & Destructors
   explicit AccountModel ();
   virtual  ~AccountModel();

   //Helpers
   void add(Account* acc);

   AccountModelPrivate* d_ptr;
   Q_DECLARE_PRIVATE(AccountModel)

public Q_SLOTS:
   void update             ();
   void updateAccounts     ();
   void registerAllAccounts();


Q_SIGNALS:
   ///The account list changed
   void accountListUpdated(                                          );
   ///Emitted when an account enable attribute change
   void accountEnabledChanged( Account* source                       );
   ///Emitted when the default account change
   void defaultAccountChanged( Account* a                            );
   ///Emitted when one account registration state change
   void registrationChanged(Account* a, bool registration            );
   ///Emitted when the network is down
   void badGateway(                                                  );
   ///Emitted when a new voice mail is available
   void voiceMailNotify(Account* account, int count                  );
   ///Propagate Account::presenceEnabledChanged
   void presenceEnabledChanged(bool isPresent                        );
   ///An account has been removed
   void accountRemoved(Account* account                              );
   ///Emitted when an account state change
   void accountStateChanged  ( Account* account, const Account::RegistrationState state);
};
Q_DECLARE_METATYPE(AccountModel*)

#endif
