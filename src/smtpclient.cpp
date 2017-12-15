/*
  Copyright (c) 2011-2012 - Tőkés Attila

  This file is part of SmtpClient for Qt.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  See the LICENSE file for more details.
*/

#include "smtpclient.h"

#include <QFileInfo>
#include <QByteArray>
#include <QSslConfiguration>
#include <QSslCipher>

#define SMTP_DEBUG

/* [1] Constructors and destructors */

SmtpClient::SmtpClient(const QString & host, int port, ConnectionType connectionType) :
    socket(NULL),
    name("localhost"),
    authMethod(AuthPlain),
    connectionTimeout(5000),
    responseTimeout(5000),
    sendMessageTimeout(60000)
{
    setConnectionType(connectionType);

    this->host = host;
    this->port = port;

    connect(socket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(socketStateChanged(QAbstractSocket::SocketState)));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()));
}

SmtpClient::~SmtpClient() {
    if (socket)
        delete socket;
}

/* [1] --- */


/* [2] Getters and Setters */

void SmtpClient::setUser(const QString &user)
{
    this->user = user;
}

void SmtpClient::setPassword(const QString &password)
{
    this->password = password;
}

void SmtpClient::setAuthMethod(AuthMethod method)
{
    this->authMethod = method;
}

void SmtpClient::setHost(const QString &host)
{
    this->host = host;
}

void SmtpClient::setPort(int port)
{
    this->port = port;
}

void SmtpClient::setConnectionType(ConnectionType ct)
{
    this->connectionType = ct;

    if (socket)
        delete socket;

    switch (connectionType)
    {
        case TcpConnection:
        {
            socket = new QTcpSocket();
            break;
        }
        case SslConnection:
        case TlsConnection:
        {
            socket = new QSslSocket();
            connect(socket, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(onSslError(QList<QSslError>)));
            QSslConfiguration conf = ((QSslSocket*) socket)->sslConfiguration();
            conf.setPeerVerifyMode(QSslSocket::VerifyNone);
            ((QSslSocket*) socket)->setSslConfiguration(conf);
            break;
        }
    }
}

const QString& SmtpClient::getHost() const
{
    return this->host;
}

const QString& SmtpClient::getUser() const
{
    return this->user;
}

const QString& SmtpClient::getPassword() const
{
    return this->password;
}

SmtpClient::AuthMethod SmtpClient::getAuthMethod() const
{
    return this->authMethod;
}

int SmtpClient::getPort() const
{
    return this->port;
}

SmtpClient::ConnectionType SmtpClient::getConnectionType() const
{
    return connectionType;
}

const QString& SmtpClient::getName() const
{
    return this->name;
}

void SmtpClient::setName(const QString &name)
{
    this->name = name;
}

const QString & SmtpClient::getResponseText() const
{
    return responseText;
}

int SmtpClient::getResponseCode() const
{
    return responseCode;
}

QTcpSocket* SmtpClient::getSocket() {
    return socket;
}

int SmtpClient::getConnectionTimeout() const
{
    return connectionTimeout;
}

void SmtpClient::setConnectionTimeout(int msec)
{
    connectionTimeout = msec;
}

int SmtpClient::getResponseTimeout() const
{
    return responseTimeout;
}

void SmtpClient::setResponseTimeout(int msec)
{
    responseTimeout = msec;
}
int SmtpClient::getSendMessageTimeout() const
{
  return sendMessageTimeout;
}
void SmtpClient::setSendMessageTimeout(int msec)
{
  sendMessageTimeout = msec;
}

/* [2] --- */


/* [3] Public methods */

bool SmtpClient::connectToHost()
{
    switch (connectionType)
    {
        case TlsConnection:
        case TcpConnection:
        {
            socket->connectToHost(host, port);
            break;
        }
        case SslConnection:
        {
            ((QSslSocket*) socket)->connectToHostEncrypted(host, port);
        }
        break;
    }

    // Tries to connect to server
    if (!socket->waitForConnected(connectionTimeout))
    {
        emit smtpError(ConnectionTimeoutError);
        return false;
    }

    try
    {
        // Wait for the server's response
        waitForResponse();

        #ifdef SMTP_DEBUG
            //            for (QSslCipher ch: ((QSslSocket*) socket)->sslConfiguration().ciphers())
            //            {
            //                qDebug() << "Used cipher: " << ch.name();
            //            }
            qDebug() << "Used SSL Protocol: " << ((QSslSocket*) socket)->sslConfiguration().protocol();
            qDebug() << "Setificate: " << ((QSslSocket*) socket)->sslConfiguration().peerCertificate().toText();

            qDebug() << "Used SSL session cipher: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().name();
            qDebug() << "Authentication: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().authenticationMethod();
            qDebug() << "Encryption: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().encryptionMethod();
            qDebug() << "Key Exchange: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().keyExchangeMethod();
            qDebug() << "Cipher Name: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().name();
            qDebug() << "Protocol: " <<  ((QSslSocket*) socket)->sslConfiguration().sessionCipher().protocolString();
            qDebug() << "Supported Bits: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().supportedBits();
            qDebug() << "Used Bits: " << ((QSslSocket*) socket)->sslConfiguration().sessionCipher().usedBits();
        #endif

        // If the response code is not 220 (Service ready)
        // means that is something wrong with the server
        if (responseCode != 220)
        {
            emit smtpError(ServerError);
            return false;
        }

        // Send a EHLO/HELO message to the server
        // The client's first command must be EHLO/HELO
        sendMessage("EHLO " + name);

        // Wait for the server's response
        waitForResponse();

        // The response code needs to be 250.
        if (responseCode != 250) {
            emit smtpError(ServerError);
            return false;
        }

        if (connectionType == TlsConnection) {
            // send a request to start TLS handshake
            sendMessage("STARTTLS");

            // Wait for the server's response
            waitForResponse();

            // The response code needs to be 220.
            if (responseCode != 220) {
                emit smtpError(ServerError);
                return false;
            };

            ((QSslSocket*) socket)->startClientEncryption();

            if (!((QSslSocket*) socket)->waitForEncrypted(connectionTimeout)) {
                qDebug() << ((QSslSocket*) socket)->errorString();
                emit smtpError(ConnectionTimeoutError);
                return false;
            }

            // Send ELHO one more time
            sendMessage("EHLO " + name);

            // Wait for the server's response
            waitForResponse();

            // The response code needs to be 250.
            if (responseCode != 250) {
                emit smtpError(ServerError);
                return false;
            }
        }
    }
    catch (ResponseTimeoutException)
    {
        return false;
    }
    catch (SendMessageTimeoutException)
    {
        return false;
    }

    // If no errors occured the function returns true.
    return true;
}

bool SmtpClient::login()
{
    return login(user, password, authMethod);
}

bool SmtpClient::login(const QString &user, const QString &password, AuthMethod method)
{
    try {
        if (method == AuthPlain)
        {
            // Sending command: AUTH PLAIN base64('\0' + username + '\0' + password)
            sendMessage("AUTH PLAIN " + QByteArray().append((char) 0).append(user).append((char) 0).append(password).toBase64());

            // Wait for the server's response
            waitForResponse();

            // If the response is not 235 then the authentication was faild
            if (responseCode != 235)
            {
                emit smtpError(AuthenticationFailedError);
                return false;
            }
        }
        else if (method == AuthLogin)
        {
            // Sending command: AUTH LOGIN
            sendMessage("AUTH LOGIN");

            // Wait for 334 response code
            waitForResponse();
            if (responseCode != 334) { emit smtpError(AuthenticationFailedError); return false; }

            // Send the username in base64
            sendMessage(QByteArray().append(user).toBase64());

            // Wait for 334
            waitForResponse();
            if (responseCode != 334) { emit smtpError(AuthenticationFailedError); return false; }

            // Send the password in base64
            sendMessage(QByteArray().append(password).toBase64());

            // Wait for the server's responce
            waitForResponse();

            // If the response is not 235 then the authentication was faild
            if (responseCode != 235)
            {
                emit smtpError(AuthenticationFailedError);
                return false;
            }
        }
    }
    catch (ResponseTimeoutException)
    {
        // Responce Timeout exceeded
        emit smtpError(AuthenticationFailedError);
        return false;
    }
    catch (SendMessageTimeoutException)
    {
	// Send Timeout exceeded
        emit smtpError(AuthenticationFailedError);
        return false;
    }

    return true;
}

bool SmtpClient::sendMail(MimeMessage& email)
{
    try
    {
        // Send the MAIL command with the sender
        sendMessage("MAIL FROM: <" + email.getSender().getAddress() + ">");

        waitForResponse();

        if (responseCode != 250) return false;

        // Send RCPT command for each recipient
        QList<EmailAddress*>::const_iterator it, itEnd;
        // To (primary recipients)
        for (it = email.getRecipients().begin(), itEnd = email.getRecipients().end();
             it != itEnd; ++it)
        {

            sendMessage("RCPT TO:<" + (*it)->getAddress() + ">");
            waitForResponse();

            if (responseCode != 250) return false;
        }

        // Cc (carbon copy)
        for (it = email.getRecipients(MimeMessage::Cc).begin(), itEnd = email.getRecipients(MimeMessage::Cc).end();
             it != itEnd; ++it)
        {
            sendMessage("RCPT TO:<" + (*it)->getAddress() + ">");
            waitForResponse();

            if (responseCode != 250) return false;
        }

        // Bcc (blind carbon copy)
        for (it = email.getRecipients(MimeMessage::Bcc).begin(), itEnd = email.getRecipients(MimeMessage::Bcc).end();
             it != itEnd; ++it)
        {
            sendMessage("RCPT TO: <" + (*it)->getAddress() + ">");
            waitForResponse();

            if (responseCode != 250) return false;
        }

        // Send DATA command
        sendMessage("DATA");
        waitForResponse();

        if (responseCode != 354) return false;

        sendMessage(email.toString());

        // Send \r\n.\r\n to end the mail data
        sendMessage(".");

        waitForResponse();

        if (responseCode != 250) return false;
    }
    catch (ResponseTimeoutException)
    {
        return false;
    }
    catch (SendMessageTimeoutException)
    {
        return false;
    }

    return true;
}

void SmtpClient::quit()
{
    try 
    {
        sendMessage("QUIT");
    }
    catch(SmtpClient::SendMessageTimeoutException) 
    {
	//Manually close the connection to the smtp server if message "QUIT" wasn't received by the smtp server
        if(socket->state() == QAbstractSocket::ConnectedState || socket->state() == QAbstractSocket::ConnectingState || socket->state() == QAbstractSocket::HostLookupState)
            socket->disconnectFromHost();
    }
}

/* [3] --- */


/* [4] Protected methods */

void SmtpClient::waitForResponse()
{
    qDebug() << "Waiting for reply from smtp server";
    do {
        if (!socket->waitForReadyRead(responseTimeout))
        {
            emit smtpError(ResponseTimeoutError);
            throw ResponseTimeoutException();
        }

        while (socket->canReadLine()) {
            // Save the server's response
            responseText = socket->readLine();
            qDebug() << "Reply from SMTP server: " << responseText;

            // Extract the respose code from the server's responce (first 3 digits)
            responseCode = responseText.left(3).toInt();

            if (responseCode / 100 == 4)
                emit smtpError(ServerError);

            if (responseCode / 100 == 5)
                emit smtpError(ClientError);

            if (responseText[3] == ' ') { return; }
        }
    } while (true);
}

void SmtpClient::sendMessage(const QString &text)
{
    qDebug() << "Sending message to smtp server: " << text;
    socket->write(text.toUtf8() + "\r\n");
    if (! socket->waitForBytesWritten(sendMessageTimeout))
    {
      emit smtpError(SendDataTimeoutError);
      throw SendMessageTimeoutException();
    }
}

/* [4] --- */


/* [5] Slots for the socket's signals */

void SmtpClient::socketStateChanged(QAbstractSocket::SocketState state)
{
    qDebug() << "SMTP socket state: " << state;
}

void SmtpClient::socketError(QAbstractSocket::SocketError error)
{
    qDebug() << "SMTP socket error: " << error;
}

void SmtpClient::socketReadyRead()
{
}

void SmtpClient::onSslError(QList<QSslError> errors)
{
    QSslError ignoreNOErrors(QSslError::NoError);

    foreach(QSslError error, errors)
    {
        if(error.error() != QSslError::NoError)
        {
            qDebug() << "SMTP Server SSL Error: " << error.errorString();
        }
    }

    QList<QSslError> expectedSslErrors;
    expectedSslErrors.append(ignoreNOErrors);
    ((QSslSocket*) socket)->ignoreSslErrors(expectedSslErrors);

}

/* [5] --- */




