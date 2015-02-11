/****************************************************************************
 *   Copyright (C) 2012-2014 by Savoir-Faire Linux                          *
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
#ifndef CODECMODEL2_H
#define CODECMODEL2_H

#include "../typedefs.h"
#include <QtCore/QAbstractListModel>

//Qt

//Ring
class Account;

//Typedef
namespace Video {
   class Codec;
}

typedef QHash<QString,Video::Codec*> CodecHash;


namespace Video {

class CodecModelPrivate;

///Abstract model for managing account video codec list
class LIB_EXPORT CodecModel2 : public QAbstractListModel {
   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
   Q_OBJECT
   #pragma GCC diagnostic pop

public:
   //Private constructor, can only be called by 'Account'
   explicit CodecModel2(Account* account = nullptr);
   virtual ~CodecModel2();

   //Roles
   static const int BITRATE_ROLE = 101;

   //Model functions
   virtual QVariant      data     ( const QModelIndex& index, int role = Qt::DisplayRole     ) const override;
   virtual int           rowCount ( const QModelIndex& parent = QModelIndex()                ) const override;
   virtual Qt::ItemFlags flags    ( const QModelIndex& index                                 ) const override;
   virtual bool  setData          ( const QModelIndex& index, const QVariant &value, int role)       override;
   virtual QHash<int,QByteArray> roleNames() const override;

   void reload();
   void save();
   bool moveUp  (const QModelIndex& idx);
   bool moveDown(const QModelIndex& idx);

private:
   QScopedPointer<CodecModelPrivate> d_ptr;

};

}
Q_DECLARE_METATYPE(Video::CodecModel2*)
#endif
