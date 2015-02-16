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

//Parent
#include "account.h"

//Qt
#include <QtCore/QDebug>
#include <QtCore/QObject>
#include <QtCore/QString>

//Ring daemon
#include <account_const.h>

//Ring lib
#include "dbus/configurationmanager.h"
#include "dbus/callmanager.h"
#include "dbus/videomanager.h"
#include "delegates/accountlistcolordelegate.h"
#include "certificate.h"
#include "certificatemodel.h"
#include "accountmodel.h"
#include "private/account_p.h"
#include "private/accountmodel_p.h"
#include "credentialmodel.h"
#include "ciphermodel.h"
#include "accountstatusmodel.h"
#include "audio/codecmodel.h"
#include "video/codecmodel2.h"
#include "ringtonemodel.h"
#include "contactmethod.h"
#include "phonedirectorymodel.h"
#include "presencestatusmodel.h"
#include "uri.h"
#include "securityvalidationmodel.h"
#define TO_BOOL ?"true":"false"
#define IS_TRUE == "true"

#define AP &AccountPrivate
const account_function AccountPrivate::stateMachineActionsOnState[6][7] = {
/*               NOTHING        EDIT         RELOAD        SAVE        REMOVE      MODIFY         CANCEL       */
/*READY    */{ AP::nothing, AP::edit   , AP::reload , AP::nothing, AP::remove , AP::modify   , AP::nothing },/**/
/*EDITING  */{ AP::nothing, AP::nothing, AP::outdate, AP::nothing, AP::remove , AP::modify   , AP::cancel  },/**/
/*OUTDATED */{ AP::nothing, AP::nothing, AP::nothing, AP::nothing, AP::remove , AP::reloadMod, AP::reload  },/**/
/*NEW      */{ AP::nothing, AP::nothing, AP::nothing, AP::save   , AP::remove , AP::nothing  , AP::nothing },/**/
/*MODIFIED */{ AP::nothing, AP::nothing, AP::nothing, AP::save   , AP::remove , AP::nothing  , AP::reload  },/**/
/*REMOVED  */{ AP::nothing, AP::nothing, AP::nothing, AP::nothing, AP::nothing, AP::nothing  , AP::cancel  } /**/
/*                                                                                                                                                       */
};
#undef AP

AccountPrivate::AccountPrivate(Account* acc) : QObject(acc),q_ptr(acc),m_pCredentials(nullptr),m_pAudioCodecs(nullptr),
m_pVideoCodecs(nullptr),m_LastErrorCode(-1),m_VoiceMailCount(0),m_pRingToneModel(nullptr),
m_CurrentState(Account::EditState::READY),
m_pAccountNumber(nullptr),m_pKeyExchangeModel(nullptr),m_pSecurityValidationModel(nullptr),m_pTlsMethodModel(nullptr),
m_pCaCert(nullptr),m_pTlsCert(nullptr),m_pPrivateKey(nullptr),m_isLoaded(true),m_pCipherModel(nullptr),
m_pStatusModel(nullptr),m_LastTransportCode(0),m_RegistrationState(Account::RegistrationState::UNREGISTERED)
{
   Q_Q(Account);
}

void AccountPrivate::changeState(Account::EditState state) {
   Q_Q(Account);
   m_CurrentState = state;
   emit q_ptr->changed(q_ptr);
}

///Constructors
Account::Account():QObject(AccountModel::instance()),d_ptr(new AccountPrivate(this))
{
}

///Build an account from it'id
Account* AccountPrivate::buildExistingAccountFromId(const QByteArray& _accountId)
{
//    qDebug() << "Building an account from id: " << _accountId;
   Account* a = new Account();
   a->d_ptr->m_AccountId = _accountId;
   a->d_ptr->setObjectName(_accountId);

   a->performAction(Account::EditAction::RELOAD);

   //If a placeholder exist for this account, upgrade it
   if (AccountModel::instance()->d_ptr->m_hsPlaceHolder[_accountId]) {
      AccountModel::instance()->d_ptr->m_hsPlaceHolder[_accountId]->d_ptr->merge(a);
   }

   return a;
} //buildExistingAccountFromId

///Build an account from it's name / alias
Account* AccountPrivate::buildNewAccountFromAlias(const QString& alias)
{
   qDebug() << "Building an account from alias: " << alias;
   ConfigurationManagerInterface& configurationManager = DBus::ConfigurationManager::instance();
   Account* a = new Account();
   a->d_ptr->m_hAccountDetails.clear();
   a->d_ptr->m_hAccountDetails[DRing::Account::ConfProperties::ENABLED] = "false";
   a->d_ptr->m_pAccountNumber = const_cast<ContactMethod*>(ContactMethod::BLANK());
   MapStringString tmp = configurationManager.getAccountTemplate();
   QMutableMapIterator<QString, QString> iter(tmp);
   while (iter.hasNext()) {
      iter.next();
      a->d_ptr->m_hAccountDetails[iter.key()] = iter.value();
   }
   a->setHostname(a->d_ptr->m_hAccountDetails[DRing::Account::ConfProperties::HOSTNAME]);
   a->d_ptr->setAccountProperty(DRing::Account::ConfProperties::ALIAS,alias);
   a->setObjectName(a->id());
   return a;
}

///Destructor
Account::~Account()
{
   disconnect();
   if (d_ptr->m_pCredentials) delete d_ptr->m_pCredentials ;
   if (d_ptr->m_pAudioCodecs) delete d_ptr->m_pAudioCodecs ;
}

/*****************************************************************************
 *                                                                           *
 *                                   Slots                                   *
 *                                                                           *
 ****************************************************************************/

void AccountPrivate::slotPresentChanged(bool present)
{
   Q_UNUSED(present)
   emit q_ptr->changed(q_ptr);
}

void AccountPrivate::slotPresenceMessageChanged(const QString& message)
{
   Q_UNUSED(message)
   emit q_ptr->changed(q_ptr);
}

void AccountPrivate::slotUpdateCertificate()
{
   Certificate* cert = qobject_cast<Certificate*>(sender());
   if (cert) {
      switch (cert->type()) {
         case Certificate::Type::AUTHORITY:
            if (accountDetail(DRing::Account::ConfProperties::TLS::CA_LIST_FILE) != cert->path().toString())
               setAccountProperty(DRing::Account::ConfProperties::TLS::CA_LIST_FILE, cert->path().toString());
            break;
         case Certificate::Type::USER:
            if (accountDetail(DRing::Account::ConfProperties::TLS::CERTIFICATE_FILE) != cert->path().toString())
               setAccountProperty(DRing::Account::ConfProperties::TLS::CERTIFICATE_FILE, cert->path().toString());
            break;
         case Certificate::Type::PRIVATE_KEY:
            if (accountDetail(DRing::Account::ConfProperties::TLS::PRIVATE_KEY_FILE) != cert->path().toString())
               setAccountProperty(DRing::Account::ConfProperties::TLS::PRIVATE_KEY_FILE, cert->path().toString());
            break;
         case Certificate::Type::NONE:
            break;
      };
   }
}

/*****************************************************************************
 *                                                                           *
 *                                  Getters                                  *
 *                                                                           *
 ****************************************************************************/

///IS this account new
bool Account::isNew() const
{
   return (d_ptr->m_AccountId == nullptr) || d_ptr->m_AccountId.isEmpty();
}

///Get this account ID
const QByteArray Account::id() const
{
   if (isNew()) {
      qDebug() << "Error : getting AccountId of a new account.";
   }
   if (d_ptr->m_AccountId.isEmpty()) {
      qDebug() << "Account not configured";
      return QByteArray(); //WARNING May explode
   }

   return d_ptr->m_AccountId;
}

///Get current state
const QString Account::toHumanStateName() const
{
   const QString s = d_ptr->m_hAccountDetails[DRing::Account::ConfProperties::Registration::STATUS];

   static const QString registered             = tr("Registered"               );
   static const QString notRegistered          = tr("Not Registered"           );
   static const QString trying                 = tr("Trying..."                );
   static const QString error                  = tr("Error"                    );
   static const QString authenticationFailed   = tr("Authentication Failed"    );
   static const QString networkUnreachable     = tr("Network unreachable"      );
   static const QString hostUnreachable        = tr("Host unreachable"         );
   static const QString stunConfigurationError = tr("Stun configuration error" );
   static const QString stunServerInvalid      = tr("Stun server invalid"      );
   static const QString serviceUnavailable     = tr("Service unavailable"      );
   static const QString notAcceptable          = tr("Unacceptable"             );
   static const QString invalid                = tr("Invalid"                  );
   static const QString requestTimeout         = tr("Request Timeout"          );

   if(s == DRing::Account::States::REGISTERED       )
      return registered             ;
   if(s == DRing::Account::States::UNREGISTERED     )
      return notRegistered          ;
   if(s == DRing::Account::States::TRYING           )
      return trying                 ;
   if(s == DRing::Account::States::ERROR            )
      return d_ptr->m_LastErrorMessage.isEmpty()?error:d_ptr->m_LastErrorMessage;
   if(s == DRing::Account::States::ERROR_AUTH       )
      return authenticationFailed   ;
   if(s == DRing::Account::States::ERROR_NETWORK    )
      return networkUnreachable     ;
   if(s == DRing::Account::States::ERROR_HOST       )
      return hostUnreachable        ;
   if(s == DRing::Account::States::ERROR_CONF_STUN  )
      return stunConfigurationError ;
   if(s == DRing::Account::States::ERROR_EXIST_STUN )
      return stunServerInvalid      ;
   if(s == DRing::Account::States::ERROR_SERVICE_UNAVAILABLE )
      return serviceUnavailable     ;
   if(s == DRing::Account::States::ERROR_NOT_ACCEPTABLE      )
      return notAcceptable          ;
   if(s == DRing::Account::States::REQUEST_TIMEOUT           )
      return requestTimeout         ;
   return invalid                   ;
}

///Get an account detail
const QString AccountPrivate::accountDetail(const QString& param) const
{
   if (!m_hAccountDetails.size()) {
      qDebug() << "The account details is not set";
      return QString(); //May crash, but better than crashing now
   }
   if (m_hAccountDetails.find(param) != m_hAccountDetails.end()) {
      return m_hAccountDetails[param];
   }
   else if (m_hAccountDetails.count() > 0) {
      if (param == DRing::Account::ConfProperties::ENABLED) //If an account is invalid, at least does not try to register it
         return AccountPrivate::RegistrationEnabled::NO;
      if (param == DRing::Account::ConfProperties::Registration::STATUS) { //If an account is new, then it is unregistered
         return DRing::Account::States::UNREGISTERED;
      }
      if (q_ptr->protocol() != Account::Protocol::IAX) //IAX accounts lack some fields, be quiet
         qDebug() << "Account parameter \"" << param << "\" not found";
      return QString();
   }
   else {
      qDebug() << "Account details not found, there is " << m_hAccountDetails.count() << " details available";
      return QString();
   }
} //accountDetail

///Get the alias
const QString Account::alias() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ALIAS);
}

///Return the model index of this item
QModelIndex Account::index() const
{
   //There is usually < 5 accounts, the loop may be faster than a hash for most users
   for (int i=0;i < AccountModel::instance()->size();i++) {
      if (this == (*AccountModel::instance())[i]) {
         return AccountModel::instance()->index(i,0);
      }
   }
   return QModelIndex();
}

///Return status color name
QString Account::stateColorName() const
{
   switch(registrationState()) {
      case RegistrationState::READY:
         return "darkGreen";
      case RegistrationState::UNREGISTERED:
         return "black";
      case RegistrationState::TRYING:
         return "orange";
      case RegistrationState::ERROR:
         return "red";
   };
   return QString();
}

///I
bool Account::isLoaded() const
{
   return d_ptr->m_isLoaded;
}

///Return status Qt color, QColor is not part of QtCore, use using the global variant
QVariant Account::stateColor() const
{
   return AccountListColorDelegate::instance()->getColor(this);
}

///Create and return the credential model
CredentialModel* Account::credentialsModel() const
{
   if (!d_ptr->m_pCredentials)
      const_cast<Account*>(this)->reloadCredentials();
   return d_ptr->m_pCredentials;
}

///Create and return the audio codec model
Audio::CodecModel* Account::audioCodecModel() const
{
   if (!d_ptr->m_pAudioCodecs)
      const_cast<Account*>(this)->reloadAudioCodecs();
   return d_ptr->m_pAudioCodecs;
}

///Create and return the video codec model
Video::CodecModel2* Account::videoCodecModel() const
{
   if (!d_ptr->m_pVideoCodecs)
      d_ptr->m_pVideoCodecs = new Video::CodecModel2(const_cast<Account*>(this));
   return d_ptr->m_pVideoCodecs;
}

RingToneModel* Account::ringToneModel() const
{
   if (!d_ptr->m_pRingToneModel)
      d_ptr->m_pRingToneModel = new RingToneModel(const_cast<Account*>(this));
   return d_ptr->m_pRingToneModel;
}

KeyExchangeModel* Account::keyExchangeModel() const
{
   if (!d_ptr->m_pKeyExchangeModel) {
      d_ptr->m_pKeyExchangeModel = new KeyExchangeModel(const_cast<Account*>(this));
   }
   return d_ptr->m_pKeyExchangeModel;
}

CipherModel* Account::cipherModel() const
{
   if (!d_ptr->m_pCipherModel) {
      d_ptr->m_pCipherModel = new CipherModel(const_cast<Account*>(this));
   }
   return d_ptr->m_pCipherModel;
}

AccountStatusModel* Account::statusModel() const
{
   if (!d_ptr->m_pStatusModel) {
      d_ptr->m_pStatusModel = new AccountStatusModel(const_cast<Account*>(this));
   }
   return d_ptr->m_pStatusModel;
}

SecurityValidationModel* Account::securityValidationModel() const
{
   if (!d_ptr->m_pSecurityValidationModel) {
      d_ptr->m_pSecurityValidationModel = new SecurityValidationModel(const_cast<Account*>(this));
   }
   return d_ptr->m_pSecurityValidationModel;
}

TlsMethodModel* Account::tlsMethodModel() const
{
   if (!d_ptr->m_pTlsMethodModel ) {
      d_ptr->m_pTlsMethodModel  = new TlsMethodModel(const_cast<Account*>(this));
   }
   return d_ptr->m_pTlsMethodModel;
}

void Account::setAlias(const QString& detail)
{
   const bool accChanged = detail != alias();
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ALIAS,detail);
   if (accChanged)
      emit aliasChanged(detail);
}

///Return the account hostname
QString Account::hostname() const
{
   return d_ptr->m_HostName;
}

///Return if the account is enabled
bool Account::isEnabled() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ENABLED) IS_TRUE;
}

///Return if the account should auto answer
bool Account::isAutoAnswer() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::AUTOANSWER) IS_TRUE;
}

///Return the account user name
QString Account::username() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::USERNAME);
}

///Return the account mailbox address
QString Account::mailbox() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::MAILBOX);
}

///Return the account mailbox address
QString Account::proxy() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ROUTE);
}


QString Account::password() const
{
   switch (protocol()) {
      case Account::Protocol::SIP:
         if (credentialsModel()->rowCount())
            return credentialsModel()->data(credentialsModel()->index(0,0),CredentialModel::Role::PASSWORD).toString();
      case Account::Protocol::IAX:
         return d_ptr->accountDetail(DRing::Account::ConfProperties::PASSWORD);
   };
   return "";
}

///
bool Account::isDisplaySasOnce() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ZRTP::DISPLAY_SAS_ONCE) IS_TRUE;
}

///Return the account security fallback
bool Account::isSrtpRtpFallback() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::SRTP::RTP_FALLBACK) IS_TRUE;
}

//Return if SRTP is enabled or not
bool Account::isSrtpEnabled() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::SRTP::ENABLED) IS_TRUE;
}

///
bool Account::isZrtpDisplaySas         () const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ZRTP::DISPLAY_SAS) IS_TRUE;
}

///Return if the other side support warning
bool Account::isZrtpNotSuppWarning() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ZRTP::NOT_SUPP_WARNING) IS_TRUE;
}

///
bool Account::isZrtpHelloHash() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::ZRTP::HELLO_HASH) IS_TRUE;
}

///Return if the account is using a STUN server
bool Account::isSipStunEnabled() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::STUN::ENABLED) IS_TRUE;
}

///Return the account STUN server
QString Account::sipStunServer() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::STUN::SERVER);
}

///Return when the account expire (require renewal)
int Account::registrationExpire() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Registration::EXPIRE).toInt();
}

///Return if the published address is the same as the local one
bool Account::isPublishedSameAsLocal() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::PUBLISHED_SAMEAS_LOCAL) IS_TRUE;
}

///Return the account published address
QString Account::publishedAddress() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::PUBLISHED_ADDRESS);
}

///Return the account published port
int Account::publishedPort() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::PUBLISHED_PORT).toUInt();
}

///Return the account tls password
QString Account::tlsPassword() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::PASSWORD);
}

///Return the account TLS port
int Account::tlsListenerPort() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::LISTENER_PORT).toInt();
}

///Return the account TLS certificate authority list file
Certificate* Account::tlsCaListCertificate() const
{
   if (!d_ptr->m_pCaCert) {
      const QString& path = d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::CA_LIST_FILE);
      if (path.isEmpty())
         return nullptr;
      d_ptr->m_pCaCert = CertificateModel::instance()->getCertificate(path,Certificate::Type::AUTHORITY);
      connect(d_ptr->m_pCaCert,SIGNAL(changed()),d_ptr.data(),SLOT(slotUpdateCertificate()));
   }
   return d_ptr->m_pCaCert;
}

///Return the account TLS certificate
Certificate* Account::tlsCertificate() const
{
   if (!d_ptr->m_pTlsCert) {
      const QString& path = d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::CERTIFICATE_FILE);
      if (path.isEmpty())
         return nullptr;
      d_ptr->m_pTlsCert = CertificateModel::instance()->getCertificate(path,Certificate::Type::USER);
      connect(d_ptr->m_pTlsCert,SIGNAL(changed()),d_ptr.data(),SLOT(slotUpdateCertificate()));
   }
   return d_ptr->m_pTlsCert;
}

///Return the account private key
Certificate* Account::tlsPrivateKeyCertificate() const
{
   if (!d_ptr->m_pPrivateKey) {
      const QString& path = d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::PRIVATE_KEY_FILE);
      if (path.isEmpty())
         return nullptr;
      d_ptr->m_pPrivateKey = CertificateModel::instance()->getCertificate(path,Certificate::Type::PRIVATE_KEY);
      connect(d_ptr->m_pPrivateKey,SIGNAL(changed()),d_ptr.data(),SLOT(slotUpdateCertificate()));
   }
   return d_ptr->m_pPrivateKey;
}

///Return the account TLS server name
QString Account::tlsServerName() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::SERVER_NAME);
}

///Return the account negotiation timeout in seconds
int Account::tlsNegotiationTimeoutSec() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::NEGOTIATION_TIMEOUT_SEC).toInt();
}

///Return the account TLS verify server
bool Account::isTlsVerifyServer() const
{
   return (d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::VERIFY_SERVER) IS_TRUE);
}

///Return the account TLS verify client
bool Account::isTlsVerifyClient() const
{
   return (d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::VERIFY_CLIENT) IS_TRUE);
}

///Return if it is required for the peer to have a certificate
bool Account::isTlsRequireClientCertificate() const
{
   return (d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::REQUIRE_CLIENT_CERTIFICATE) IS_TRUE);
}

///Return the account TLS security is enabled
bool Account::isTlsEnabled() const
{
   return (d_ptr->accountDetail(DRing::Account::ConfProperties::TLS::ENABLED) IS_TRUE);
}

///Return the key exchange mechanism
KeyExchangeModel::Type Account::keyExchange() const
{
   return KeyExchangeModel::fromDaemonName(d_ptr->accountDetail(DRing::Account::ConfProperties::SRTP::KEY_EXCHANGE));
}

///Return if the ringtone are enabled
bool Account::isRingtoneEnabled() const
{
   return (d_ptr->accountDetail(DRing::Account::ConfProperties::Ringtone::ENABLED) IS_TRUE);
}

///Return the account ringtone path
QString Account::ringtonePath() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Ringtone::PATH);
}

///Return the last error message received
QString Account::lastErrorMessage() const
{
   return d_ptr->m_LastErrorMessage;
}

///Return the last error code (useful for debugging)
int Account::lastErrorCode() const
{
   return d_ptr->m_LastErrorCode;
}

///Get the last transport error code, this is used to debug why registration failed
int Account::lastTransportErrorCode() const
{
   return d_ptr->m_LastTransportCode;
}

///Get the last transport error message, this is used to debug why registration failed
QString Account::lastTransportErrorMessage() const
{
   return d_ptr->m_LastTransportMessage;
}

///Return the account local port
int Account::localPort() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::LOCAL_PORT).toInt();
}

///Return the number of voicemails
int Account::voiceMailCount() const
{
   return d_ptr->m_VoiceMailCount;
}

///Return the account local interface
QString Account::localInterface() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::LOCAL_INTERFACE);
}

///Return the account registration status
Account::RegistrationState Account::registrationState() const
{
   return d_ptr->m_RegistrationState;
}

///Return the account type
Account::Protocol Account::protocol() const
{
   const QString str = d_ptr->accountDetail(DRing::Account::ConfProperties::TYPE);
   if (str.isEmpty() || str == Account::ProtocolName::SIP)
      return Account::Protocol::SIP;
   else if (str == Account::ProtocolName::IAX)
      return Account::Protocol::IAX;
   qDebug() << "Warning: unhandled protocol name" << str << ", defaulting to SIP";
   return Account::Protocol::SIP;
}

///Return the DTMF type
DtmfType Account::DTMFType() const
{
   QString type = d_ptr->accountDetail(DRing::Account::ConfProperties::DTMF_TYPE);
   return (type == "overrtp" || type.isEmpty())? DtmfType::OverRtp:DtmfType::OverSip;
}

bool Account::presenceStatus() const
{
   return d_ptr->m_pAccountNumber->isPresent();
}

QString Account::presenceMessage() const
{
   return d_ptr->m_pAccountNumber->presenceMessage();
}

bool Account::supportPresencePublish() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Presence::SUPPORT_PUBLISH) IS_TRUE;
}

bool Account::supportPresenceSubscribe() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Presence::SUPPORT_SUBSCRIBE) IS_TRUE;
}

bool Account::presenceEnabled() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Presence::ENABLED) IS_TRUE;
}

bool Account::isVideoEnabled() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Video::ENABLED) IS_TRUE;
}

int Account::videoPortMax() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Video::PORT_MAX).toInt();
}

int Account::videoPortMin() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Video::PORT_MIN).toInt();
}

int Account::audioPortMin() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Audio::PORT_MIN).toInt();
}

int Account::audioPortMax() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::Audio::PORT_MAX).toInt();
}

QString Account::userAgent() const
{
   return d_ptr->accountDetail(DRing::Account::ConfProperties::USER_AGENT);
}

#define CAST(item) static_cast<int>(item)
QVariant Account::roleData(int role) const
{
   switch(role) {
      //Generic
      case Qt::DisplayRole:
      case Qt::EditRole:
         return alias();
      case Qt::CheckStateRole:
         return QVariant(isEnabled() ? Qt::Checked : Qt::Unchecked);
      case Qt::BackgroundRole:
         return stateColor();
      case Qt::DecorationRole:
         return AccountListColorDelegate::instance()->getIcon(this);

      //Specialized
      case CAST(Account::Role::Alias):
         return alias();
      case CAST(Account::Role::Proto):
         return static_cast<int>(protocol());
      case CAST(Account::Role::Hostname):
         return hostname();
      case CAST(Account::Role::Username):
         return username();
      case CAST(Account::Role::Mailbox):
         return mailbox();
      case CAST(Account::Role::Proxy):
         return proxy();
//       case Password:
//          return accountPassword();
      case CAST(Account::Role::TlsPassword):
         return tlsPassword();
      case CAST(Account::Role::TlsCaListCertificate):
         return tlsCaListCertificate()?tlsCaListCertificate()->path().toLocalFile():QVariant();
      case CAST(Account::Role::TlsCertificate):
         return tlsCertificate()?tlsCertificate()->path().toLocalFile():QVariant();
      case CAST(Account::Role::TlsPrivateKeyCertificate):
         return tlsPrivateKeyCertificate()?tlsPrivateKeyCertificate()->path().toLocalFile():QVariant();
      case CAST(Account::Role::TlsServerName):
         return tlsServerName();
      case CAST(Account::Role::SipStunServer):
         return sipStunServer();
      case CAST(Account::Role::PublishedAddress):
         return publishedAddress();
      case CAST(Account::Role::LocalInterface):
         return localInterface();
      case CAST(Account::Role::RingtonePath):
         return ringtonePath();
      case CAST(Account::Role::RegistrationExpire):
         return registrationExpire();
      case CAST(Account::Role::TlsNegotiationTimeoutSec):
         return tlsNegotiationTimeoutSec();
      case CAST(Account::Role::LocalPort):
         return localPort();
      case CAST(Account::Role::TlsListenerPort):
         return tlsListenerPort();
      case CAST(Account::Role::PublishedPort):
         return publishedPort();
      case CAST(Account::Role::Enabled):
         return isEnabled();
      case CAST(Account::Role::AutoAnswer):
         return isAutoAnswer();
      case CAST(Account::Role::TlsVerifyServer):
         return isTlsVerifyServer();
      case CAST(Account::Role::TlsVerifyClient):
         return isTlsVerifyClient();
      case CAST(Account::Role::TlsRequireClientCertificate):
         return isTlsRequireClientCertificate();
      case CAST(Account::Role::TlsEnabled):
         return isTlsEnabled();
      case CAST(Account::Role::DisplaySasOnce):
         return isDisplaySasOnce();
      case CAST(Account::Role::SrtpRtpFallback):
         return isSrtpRtpFallback();
      case CAST(Account::Role::ZrtpDisplaySas):
         return isZrtpDisplaySas();
      case CAST(Account::Role::ZrtpNotSuppWarning):
         return isZrtpNotSuppWarning();
      case CAST(Account::Role::ZrtpHelloHash):
         return isZrtpHelloHash();
      case CAST(Account::Role::SipStunEnabled):
         return isSipStunEnabled();
      case CAST(Account::Role::PublishedSameAsLocal):
         return isPublishedSameAsLocal();
      case CAST(Account::Role::RingtoneEnabled):
         return isRingtoneEnabled();
      case CAST(Account::Role::dTMFType):
         return DTMFType();
      case CAST(Account::Role::Id):
         return id();
      case CAST(Account::Role::Object): {
         QVariant var;
         var.setValue(const_cast<Account*>(this));
         return var;
      }
      case CAST(Account::Role::TypeName):
         return static_cast<int>(protocol());
      case CAST(Account::Role::PresenceStatus):
         return PresenceStatusModel::instance()->currentStatus();
      case CAST(Account::Role::PresenceMessage):
         return PresenceStatusModel::instance()->currentMessage();
      case CAST(Account::Role::RegistrationState):
         return QVariant::fromValue(registrationState());
      default:
         return QVariant();
   }
}
#undef CAST


/*****************************************************************************
 *                                                                           *
 *                                  Setters                                  *
 *                                                                           *
 ****************************************************************************/

///Set account details
void AccountPrivate::setAccountProperties(const QHash<QString,QString>& m)
{
   m_hAccountDetails.clear();
   m_hAccountDetails = m;
   m_HostName = m[DRing::Account::ConfProperties::HOSTNAME];
}

///Set a specific detail
bool AccountPrivate::setAccountProperty(const QString& param, const QString& val)
{
   const bool accChanged = m_hAccountDetails[param] != val;
   const QString buf = m_hAccountDetails[param];
   //Status can be changed regardless of the EditState
   //TODO make this more generic for volatile properties
   if (param == DRing::Account::ConfProperties::Registration::STATUS) {
      m_hAccountDetails[param] = val;
      if (accChanged) {
         emit q_ptr->propertyChanged(q_ptr,param,val,buf);
      }
   }
   else {
      q_ptr->performAction(Account::EditAction::MODIFY);
      if (m_CurrentState == Account::EditState::MODIFIED || m_CurrentState == Account::EditState::NEW) {
         m_hAccountDetails[param] = val;
         if (accChanged) {
            emit q_ptr->propertyChanged(q_ptr,param,val,buf);
         }
      }
   }
   return m_CurrentState == Account::EditState::MODIFIED || m_CurrentState == Account::EditState::NEW;
}

///Set the account id
void Account::setId(const QByteArray& id)
{
   qDebug() << "Setting accountId = " << d_ptr->m_AccountId;
   if (! isNew())
      qDebug() << "Error : setting AccountId of an existing account.";
   d_ptr->m_AccountId = id;
}

///Set the account type, SIP or IAX
void Account::setProtocol(Account::Protocol proto)
{
   switch (proto) {
      case Account::Protocol::SIP:
         d_ptr->setAccountProperty(DRing::Account::ConfProperties::TYPE ,Account::ProtocolName::SIP);
         break;
      case Account::Protocol::IAX:
         d_ptr->setAccountProperty(DRing::Account::ConfProperties::TYPE ,Account::ProtocolName::IAX);
         break;
   };
}

///The set account hostname, it can be an hostname or an IP address
void Account::setHostname(const QString& detail)
{
   if (d_ptr->m_HostName != detail) {
      d_ptr->m_HostName = detail;
      d_ptr->setAccountProperty(DRing::Account::ConfProperties::HOSTNAME, detail);
   }
}

///Set the account username, everything is valid, some might be rejected by the PBX server
void Account::setUsername(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::USERNAME, detail);
}

///Set the account mailbox, usually a number, but can be anything
void Account::setMailbox(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::MAILBOX, detail);
}

///Set the account mailbox, usually a number, but can be anything
void Account::setProxy(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ROUTE, detail);
}

///Set the main credential password
void Account::setPassword(const QString& detail)
{
   switch (protocol()) {
      case Account::Protocol::SIP:
         if (credentialsModel()->rowCount())
            credentialsModel()->setData(credentialsModel()->index(0,0),detail,CredentialModel::Role::PASSWORD);
         else {
            const QModelIndex idx = credentialsModel()->addCredentials();
            credentialsModel()->setData(idx,detail,CredentialModel::Role::PASSWORD);
         }
         break;
      case Account::Protocol::IAX:
         d_ptr->setAccountProperty(DRing::Account::ConfProperties::PASSWORD, detail);
         break;
   };
}

///Set the TLS (encryption) password
void Account::setTlsPassword(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::PASSWORD, detail);
}

///Set the certificate authority list file
void Account::setTlsCaListCertificate(Certificate* cert)
{
   d_ptr->m_pCaCert = cert; //FIXME memory leak
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::CA_LIST_FILE, cert?cert->path().toLocalFile():QString());
}

///Set the certificate
void Account::setTlsCertificate(Certificate* cert)
{
   d_ptr->m_pTlsCert = cert; //FIXME memory leak
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::CERTIFICATE_FILE, cert?cert->path().toLocalFile():QString());
}

///Set the private key
void Account::setTlsPrivateKeyCertificate(Certificate* cert)
{
   d_ptr->m_pPrivateKey = cert; //FIXME memory leak
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::PRIVATE_KEY_FILE, cert?cert->path().toLocalFile():QString());
}

///Set the TLS server
void Account::setTlsServerName(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::SERVER_NAME, detail);
}

///Set the stun server
void Account::setSipStunServer(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::STUN::SERVER, detail);
}

///Set the published address
void Account::setPublishedAddress(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::PUBLISHED_ADDRESS, detail);
}

///Set the local interface
void Account::setLocalInterface(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::LOCAL_INTERFACE, detail);
}

///Set the ringtone path, it have to be a valid absolute path
void Account::setRingtonePath(const QString& detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Ringtone::PATH, detail);
}

///Set the number of voice mails
void Account::setVoiceMailCount(int count)
{
   d_ptr->m_VoiceMailCount = count;
}

///Set the last error message to be displayed as status instead of "Error"
void Account::setLastErrorMessage(const QString& message)
{
   d_ptr->m_LastErrorMessage = message;
}

///Set the last error code
void Account::setLastErrorCode(int code)
{
   d_ptr->m_LastErrorCode = code;
}

///Set the Tls method
void Account::setKeyExchange(KeyExchangeModel::Type detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::SRTP::KEY_EXCHANGE ,KeyExchangeModel::toDaemonName(detail));
}

///Set the account timeout, it will be renegotiated when that timeout occur
void Account::setRegistrationExpire(int detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Registration::EXPIRE, QString::number(detail));
}

///Set TLS negotiation timeout in second
void Account::setTlsNegotiationTimeoutSec(int detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::NEGOTIATION_TIMEOUT_SEC, QString::number(detail));
}

///Set the local port for SIP/IAX communications
void Account::setLocalPort(unsigned short detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::LOCAL_PORT, QString::number(detail));
}

///Set the TLS listener port (0-2^16)
void Account::setTlsListenerPort(unsigned short detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::LISTENER_PORT, QString::number(detail));
}

///Set the published port (0-2^16)
void Account::setPublishedPort(unsigned short detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::PUBLISHED_PORT, QString::number(detail));
}

///Set if the account is enabled or not
void Account::setEnabled(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ENABLED, (detail)TO_BOOL);
}

///Set if the account should auto answer
void Account::setAutoAnswer(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::AUTOANSWER, (detail)TO_BOOL);
}

///Set the TLS verification server
void Account::setTlsVerifyServer(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::VERIFY_SERVER, (detail)TO_BOOL);
}

///Set the TLS verification client
void Account::setTlsVerifyClient(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::VERIFY_CLIENT, (detail)TO_BOOL);
}

///Set if the peer need to be providing a certificate
void Account::setTlsRequireClientCertificate(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::REQUIRE_CLIENT_CERTIFICATE ,(detail)TO_BOOL);
}

///Set if the security settings are enabled
void Account::setTlsEnabled(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::TLS::ENABLED ,(detail)TO_BOOL);
}

void Account::setDisplaySasOnce(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ZRTP::DISPLAY_SAS_ONCE, (detail)TO_BOOL);
}

void Account::setSrtpRtpFallback(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::SRTP::RTP_FALLBACK, (detail)TO_BOOL);
}

void Account::setSrtpEnabled(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::SRTP::ENABLED, (detail)TO_BOOL);
}

void Account::setZrtpDisplaySas(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ZRTP::DISPLAY_SAS, (detail)TO_BOOL);
}

void Account::setZrtpNotSuppWarning(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ZRTP::NOT_SUPP_WARNING, (detail)TO_BOOL);
}

void Account::setZrtpHelloHash(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::ZRTP::HELLO_HASH, (detail)TO_BOOL);
}

void Account::setSipStunEnabled(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::STUN::ENABLED, (detail)TO_BOOL);
}

/**
 * Set if the published address is the same as the local IP address
 * @see Account::setPublishedAddress
 * @see Account::publishedAddress
 */
void Account::setPublishedSameAsLocal(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::PUBLISHED_SAMEAS_LOCAL, (detail)TO_BOOL);
}

///Set if custom ringtone are enabled
void Account::setRingtoneEnabled(bool detail)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Ringtone::ENABLED, (detail)TO_BOOL);
}

/**
 * Set if the account broadcast its presence data
 *
 * @note This only works when the account support presence
 * @see Account::supportPresencePublish()
 */
void Account::setPresenceEnabled(bool enable)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Presence::ENABLED, (enable)TO_BOOL);
   emit presenceEnabledChanged(enable);
}

///Use video by default when available
void Account::setVideoEnabled(bool enable)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Video::ENABLED, (enable)TO_BOOL);
}

/**Set the maximum audio port
 * This can be used when some routers without UPnP support open a narrow range
 * of ports to allow the stream to go through.
 */
void Account::setAudioPortMax(int port )
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Audio::PORT_MAX, QString::number(port));
}

/**Set the minimum audio port
 * This can be used when some routers without UPnP support open a narrow range
 * of ports to allow the stream to go through.
 */
void Account::setAudioPortMin(int port )
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Audio::PORT_MIN, QString::number(port));
}

/**Set the maximum video port
 * This can be used when some routers without UPnP support open a narrow range
 * of ports to allow the stream to go through.
 */
void Account::setVideoPortMax(int port )
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Video::PORT_MAX, QString::number(port));
}

/**Set the minimum video port
 * This can be used when some routers without UPnP support open a narrow range
 * of ports to allow the stream to go through.
 */
void Account::setVideoPortMin(int port )
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::Video::PORT_MIN, QString::number(port));
}

///TODO implement the "use default" logic correctly
/**Set the user agent
 * If the string is unchanged, the daemon should upgrade it automatically
 */
void Account::setUserAgent(const QString& agent)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::USER_AGENT, agent);
}

///Set the DTMF type
void Account::setDTMFType(DtmfType type)
{
   d_ptr->setAccountProperty(DRing::Account::ConfProperties::DTMF_TYPE,(type==OverRtp)?"overrtp":"oversip");
}

#define CAST(item) static_cast<int>(item)
///Proxy for AccountModel::setData
void Account::setRoleData(int role, const QVariant& value)
{
   switch(role) {
      case CAST(Account::Role::Alias):
         setAlias(value.toString());
         break;
      case CAST(Account::Role::Proto): {
         const int proto = value.toInt();
         setProtocol((proto>=0&&proto<=1)?static_cast<Account::Protocol>(proto):Account::Protocol::SIP);
         break;
      }
      case CAST(Account::Role::Hostname):
         setHostname(value.toString());
         break;
      case CAST(Account::Role::Username):
         setUsername(value.toString());
         break;
      case CAST(Account::Role::Mailbox):
         setMailbox(value.toString());
         break;
      case CAST(Account::Role::Proxy):
         setProxy(value.toString());
         break;
//       case Password:
//          accountPassword();
      case CAST(Account::Role::TlsPassword):
         setTlsPassword(value.toString());
         break;
      case CAST(Account::Role::TlsCaListCertificate): {
         const QString path = value.toString();
         if ((tlsCaListCertificate() && tlsCaListCertificate()->path() != QUrl(path)) || !tlsCaListCertificate()) {
            tlsCaListCertificate()->setPath(path);
         }
         break;
      }
      case CAST(Account::Role::TlsCertificate): {
         const QString path = value.toString();
         if ((tlsCertificate() && tlsCertificate()->path() != QUrl(path)) || !tlsCertificate())
            tlsCertificate()->setPath(path);
      }
         break;
      case CAST(Account::Role::TlsPrivateKeyCertificate): {
         const QString path = value.toString();
         if ((tlsPrivateKeyCertificate() && tlsPrivateKeyCertificate()->path() != QUrl(path)) || !tlsPrivateKeyCertificate())
            tlsPrivateKeyCertificate()->setPath(path);
      }
         break;
      case CAST(Account::Role::TlsServerName):
         setTlsServerName(value.toString());
         break;
      case CAST(Account::Role::SipStunServer):
         setSipStunServer(value.toString());
         break;
      case CAST(Account::Role::PublishedAddress):
         setPublishedAddress(value.toString());
         break;
      case CAST(Account::Role::LocalInterface):
         setLocalInterface(value.toString());
         break;
      case CAST(Account::Role::RingtonePath):
         setRingtonePath(value.toString());
         break;
      case CAST(Account::Role::KeyExchange): {
         const int method = value.toInt();
         setKeyExchange(method<=keyExchangeModel()->rowCount()?static_cast<KeyExchangeModel::Type>(method):KeyExchangeModel::Type::NONE);
         break;
      }
      case CAST(Account::Role::RegistrationExpire):
         setRegistrationExpire(value.toInt());
         break;
      case CAST(Account::Role::TlsNegotiationTimeoutSec):
         setTlsNegotiationTimeoutSec(value.toInt());
         break;
      case CAST(Account::Role::LocalPort):
         setLocalPort(value.toInt());
         break;
      case CAST(Account::Role::TlsListenerPort):
         setTlsListenerPort(value.toInt());
         break;
      case CAST(Account::Role::PublishedPort):
         setPublishedPort(value.toInt());
         break;
      case CAST(Account::Role::Enabled):
         setEnabled(value.toBool());
         break;
      case CAST(Account::Role::AutoAnswer):
         setAutoAnswer(value.toBool());
         break;
      case CAST(Account::Role::TlsVerifyServer):
         setTlsVerifyServer(value.toBool());
         break;
      case CAST(Account::Role::TlsVerifyClient):
         setTlsVerifyClient(value.toBool());
         break;
      case CAST(Account::Role::TlsRequireClientCertificate):
         setTlsRequireClientCertificate(value.toBool());
         break;
      case CAST(Account::Role::TlsEnabled):
         setTlsEnabled(value.toBool());
         break;
      case CAST(Account::Role::DisplaySasOnce):
         setDisplaySasOnce(value.toBool());
         break;
      case CAST(Account::Role::SrtpRtpFallback):
         setSrtpRtpFallback(value.toBool());
         break;
      case CAST(Account::Role::ZrtpDisplaySas):
         setZrtpDisplaySas(value.toBool());
         break;
      case CAST(Account::Role::ZrtpNotSuppWarning):
         setZrtpNotSuppWarning(value.toBool());
         break;
      case CAST(Account::Role::ZrtpHelloHash):
         setZrtpHelloHash(value.toBool());
         break;
      case CAST(Account::Role::SipStunEnabled):
         setSipStunEnabled(value.toBool());
         break;
      case CAST(Account::Role::PublishedSameAsLocal):
         setPublishedSameAsLocal(value.toBool());
         break;
      case CAST(Account::Role::RingtoneEnabled):
         setRingtoneEnabled(value.toBool());
         break;
      case CAST(Account::Role::dTMFType):
         setDTMFType((DtmfType)value.toInt());
         break;
      case CAST(Account::Role::Id):
         setId(value.toByteArray());
         break;
   }
}
#undef CAST


/*****************************************************************************
 *                                                                           *
 *                                  Mutator                                  *
 *                                                                           *
 ****************************************************************************/

void AccountPrivate::performAction(const Account::EditAction action)
{
   (this->*(stateMachineActionsOnState[(int)m_CurrentState][(int)action]))();//FIXME don't use integer cast
}

///Change the current edition state
bool Account::performAction(const Account::EditAction action)
{
   Account::EditState curState = d_ptr->m_CurrentState;
   d_ptr->performAction(action);
   return curState != d_ptr->m_CurrentState;
}

///Get the current account edition state
Account::EditState Account::editState() const
{
   return d_ptr->m_CurrentState;
}

/**Update the account
 * @return if the state changed
 */
bool AccountPrivate::updateState()
{
   if(! q_ptr->isNew()) {
      ConfigurationManagerInterface& configurationManager = DBus::ConfigurationManager::instance();
      const MapStringString details        = configurationManager.getVolatileAccountDetails(q_ptr->id());
      const QString         status         = details[DRing::Account::VolatileProperties::Registration::STATUS];
      const Account::RegistrationState cst = q_ptr->registrationState();
      const Account::RegistrationState st  = AccountModelPrivate::fromDaemonName(status);

      setAccountProperty(DRing::Account::ConfProperties::Registration::STATUS, status); //Update -internal- object state
      m_RegistrationState = st;

      if (st != cst)
         emit q_ptr->stateChanged(q_ptr->registrationState());

      return st == cst;
   }
   return true;
}

///Save the current account to the daemon
void AccountPrivate::save()
{
   ConfigurationManagerInterface& configurationManager = DBus::ConfigurationManager::instance();
   if (q_ptr->isNew()) {
      MapStringString details;
      QMutableHashIterator<QString,QString> iter(m_hAccountDetails);

      while (iter.hasNext()) {
         iter.next();
         details[iter.key()] = iter.value();
      }

      const QString currentId = configurationManager.addAccount(details);

      //Be sure there is audio codec enabled to avoid obscure error messages for the user
      const QVector<int> codecIdList = configurationManager.getAudioCodecList();
      foreach (const int aCodec, codecIdList) {
         const QStringList codec = configurationManager.getAudioCodecDetails(aCodec);
         const QModelIndex idx = m_pAudioCodecs->add();
         m_pAudioCodecs->setData(idx,codec[0],Audio::CodecModel::Role::NAME       );
         m_pAudioCodecs->setData(idx,codec[1],Audio::CodecModel::Role::SAMPLERATE );
         m_pAudioCodecs->setData(idx,codec[2],Audio::CodecModel::Role::BITRATE    );
         m_pAudioCodecs->setData(idx,aCodec  ,Audio::CodecModel::Role::ID         );
         m_pAudioCodecs->setData(idx, Qt::Checked ,Qt::CheckStateRole);
      }
      q_ptr->saveAudioCodecs();

      q_ptr->setId(currentId.toLatin1());
      q_ptr->saveCredentials();
   } //New account
   else { //Existing account
      MapStringString tmp;
      QMutableHashIterator<QString,QString> iter(m_hAccountDetails);

      while (iter.hasNext()) {
         iter.next();
         tmp[iter.key()] = iter.value();
      }
      configurationManager.setAccountDetails(q_ptr->id(), tmp);
   }

   if (!q_ptr->id().isEmpty()) {
      Account* acc =  AccountModel::instance()->getById(q_ptr->id());
      qDebug() << "Adding the new account to the account list (" << q_ptr->id() << ")";
      if (acc != q_ptr) {
         AccountModel::instance()->add(q_ptr);
      }

      q_ptr->performAction(Account::EditAction::RELOAD);
      updateState();
      m_CurrentState = Account::EditState::READY;
   }
   #ifdef ENABLE_VIDEO
   q_ptr->videoCodecModel()->save();
   #endif
   q_ptr->saveAudioCodecs();
   emit q_ptr->changed(q_ptr);
}

///sync with the daemon, this need to be done manually to prevent reloading the account while it is being edited
void AccountPrivate::reload()
{
   if (!q_ptr->isNew()) {
      if (m_hAccountDetails.size())
         qDebug() << "Reloading" << q_ptr->id() << q_ptr->alias();
      else
         qDebug() << "Loading" << q_ptr->id();
      ConfigurationManagerInterface& configurationManager = DBus::ConfigurationManager::instance();
      QMap<QString,QString> aDetails = configurationManager.getAccountDetails(q_ptr->id());

      if (!aDetails.count()) {
         qDebug() << "Account not found";
      }
      else {
         m_hAccountDetails.clear();
         QMutableMapIterator<QString, QString> iter(aDetails);
         while (iter.hasNext()) {
            iter.next();
            m_hAccountDetails[iter.key()] = iter.value();
         }
         q_ptr->setHostname(m_hAccountDetails[DRing::Account::ConfProperties::HOSTNAME]);
      }
      m_CurrentState = Account::EditState::READY;

      //TODO port this to the URI class helpers, this doesn't cover all corner cases
      const QString currentUri = QString("%1@%2").arg(q_ptr->username()).arg(m_HostName);

      if (!m_pAccountNumber || (m_pAccountNumber && m_pAccountNumber->uri() != currentUri)) {
         if (m_pAccountNumber) {
            disconnect(m_pAccountNumber,SIGNAL(presenceMessageChanged(QString)),this,SLOT(slotPresenceMessageChanged(QString)));
            disconnect(m_pAccountNumber,SIGNAL(presentChanged(bool)),this,SLOT(slotPresentChanged(bool)));
         }
         m_pAccountNumber = PhoneDirectoryModel::instance()->getNumber(currentUri,q_ptr);
         m_pAccountNumber->setType(ContactMethod::Type::ACCOUNT);
         connect(m_pAccountNumber,SIGNAL(presenceMessageChanged(QString)),this,SLOT(slotPresenceMessageChanged(QString)));
         connect(m_pAccountNumber,SIGNAL(presentChanged(bool)),this,SLOT(slotPresentChanged(bool)));
      }

      //If the credential model is loaded, then update it
      if (m_pCredentials)
         q_ptr->reloadCredentials();
      emit q_ptr->changed(q_ptr);

      //The registration state is cached, update that cache
      updateState();

      AccountModel::instance()->d_ptr->slotVolatileAccountDetailsChange(q_ptr->id(),configurationManager.getVolatileAccountDetails(q_ptr->id()));
   }
}

void AccountPrivate::nothing()
{

}

void AccountPrivate::edit()    {
   changeState(Account::EditState::EDITING );
}

void AccountPrivate::modify()  {
   changeState(Account::EditState::MODIFIED);
}

void AccountPrivate::remove()  {
   changeState(Account::EditState::REMOVED );
}

void AccountPrivate::cancel()  {
   changeState(Account::EditState::READY   );
}

void AccountPrivate::outdate() {
   changeState(Account::EditState::OUTDATED);
}


///Reload credentials from DBUS
void Account::reloadCredentials()
{
   if (!d_ptr->m_pCredentials) {
      d_ptr->m_pCredentials = new CredentialModel(this);
   }
   if (!isNew()) {
      d_ptr->m_pCredentials->clear();
      ConfigurationManagerInterface& configurationManager = DBus::ConfigurationManager::instance();
      VectorMapStringString credentials = configurationManager.getCredentials(id());
      for (int i=0; i < credentials.size(); i++) {
         QModelIndex idx = d_ptr->m_pCredentials->addCredentials();
         d_ptr->m_pCredentials->setData(idx,credentials[i][ DRing::Account::ConfProperties::USERNAME ],CredentialModel::Role::NAME    );
         d_ptr->m_pCredentials->setData(idx,credentials[i][ DRing::Account::ConfProperties::PASSWORD ],CredentialModel::Role::PASSWORD);
         d_ptr->m_pCredentials->setData(idx,credentials[i][ DRing::Account::ConfProperties::REALM    ],CredentialModel::Role::REALM   );
      }
   }
}

///Save all credentials
void Account::saveCredentials() {
   if (d_ptr->m_pCredentials) {
      ConfigurationManagerInterface& configurationManager = DBus::ConfigurationManager::instance();
      VectorMapStringString toReturn;
      for (int i=0; i < d_ptr->m_pCredentials->rowCount();i++) {
         QModelIndex idx = d_ptr->m_pCredentials->index(i,0);
         MapStringString credentialData;
         QString user = d_ptr->m_pCredentials->data(idx,CredentialModel::Role::NAME).toString();
         QString realm = d_ptr->m_pCredentials->data(idx,CredentialModel::Role::REALM).toString();
         if (user.isEmpty()) {
            user = username();
            d_ptr->m_pCredentials->setData(idx,user,CredentialModel::Role::NAME);
         }
         if (realm.isEmpty()) {
            realm = '*';
            d_ptr->m_pCredentials->setData(idx,realm,CredentialModel::Role::REALM);
         }
         credentialData[ DRing::Account::ConfProperties::USERNAME ] = user;
         credentialData[ DRing::Account::ConfProperties::PASSWORD ] = d_ptr->m_pCredentials->data(idx,CredentialModel::Role::PASSWORD).toString();
         credentialData[ DRing::Account::ConfProperties::REALM    ] = realm;
         toReturn << credentialData;
      }
      configurationManager.setCredentials(id(),toReturn);
   }
}

///Reload all audio codecs
void Account::reloadAudioCodecs()
{
   if (!d_ptr->m_pAudioCodecs) {
      d_ptr->m_pAudioCodecs = new Audio::CodecModel(this);
   }
   d_ptr->m_pAudioCodecs->reload();
}

///Save audio codecs
void Account::saveAudioCodecs() {
   if (d_ptr->m_pAudioCodecs)
      d_ptr->m_pAudioCodecs->save();
}

/*****************************************************************************
 *                                                                           *
 *                                 Operator                                  *
 *                                                                           *
 ****************************************************************************/

///Are both account the same
bool Account::operator==(const Account& a)const
{
   return d_ptr->m_AccountId == a.d_ptr->m_AccountId;
}

/*****************************************************************************
 *                                                                           *
 *                               Placeholder                                 *
 *                                                                           *
 ****************************************************************************/

///Constructor
AccountPlaceHolder::AccountPlaceHolder(const QByteArray& uid) : Account()
{
   d_ptr->m_AccountId = uid  ;
   d_ptr->m_isLoaded  = false;
}

///Merge an existing account into this temporary one
bool AccountPrivate::merge(Account* account)
{
   if ((!account) || this == account->d_ptr.data())
      return false;

   AccountPrivate* p = (AccountPrivate*) d_ptr.take();
   delete p;

   q_ptr->d_ptr = account->d_ptr;
   emit q_ptr->changed(q_ptr);

   return true;
}

#undef TO_BOOL
#undef IS_TRUE
#include <account.moc>
