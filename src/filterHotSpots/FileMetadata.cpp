/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FileMetadata.h"
#include "TextPreviewContent.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QObject>
#include <QSysInfo>
#include <QStringList>
#include <string>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif
using namespace Konsole;

QString FileMetadata::localFilePath(QUrl url, bool remoteHost)
{
    if (!url.isLocalFile()) { return {}; }
    const QString host = url.host();
    if (!remoteHost && !host.isEmpty() && host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) != 0
        && host.compare(QSysInfo::machineHostName(), Qt::CaseInsensitive) != 0) { return {}; }
    url.setHost(QString());
    return url.toLocalFile();
}

QString FileMetadata::clipboardPath(const QString &path)
{
    return QDir::toNativeSeparators(QFileInfo(TextPreviewContent::pathWithoutLocation(path)).absoluteFilePath());
}

#ifdef Q_OS_WIN
static QString accountName(PSID sid)
{
    if (!sid || !IsValidSid(sid)) { return QObject::tr("Unavailable"); }
    DWORD nameLength = 0, domainLength = 0;
    SID_NAME_USE use;
    LookupAccountSidW(nullptr, sid, nullptr, &nameLength, nullptr, &domainLength, &use);
    if (nameLength) {
        std::wstring name(nameLength, L'\0'), domain(domainLength, L'\0');
        if (LookupAccountSidW(nullptr, sid, name.data(), &nameLength, domain.data(), &domainLength, &use)) {
            const QString account = QString::fromWCharArray(name.c_str());
            if (domainLength) {
                QString qualified = QString::fromWCharArray(domain.c_str());
                qualified += QLatin1Char('\\');
                qualified += account;
                return qualified;
            }
            return account;
        }
    }
    LPWSTR value = nullptr;
    if (ConvertSidToStringSidW(sid, &value)) {
        const QString result = QString::fromWCharArray(value);
        LocalFree(value);
        return result;
    }
    return QObject::tr("Unavailable");
}

static QString securityDescription(const QString &path)
{
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD status = GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, &group, &dacl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS) {
        return QObject::tr("Owner / primary group / permissions: unavailable (Windows error %1)").arg(status);
    }
    QString text = QObject::tr("Owner: %1\nPrimary group: %2\nPermissions (Windows ACL):\n").arg(accountName(owner), accountName(group));
    if (!dacl) {
        text += QObject::tr("No DACL: unrestricted access\n");
    } else if (dacl->AceCount == 0) {
        text += QObject::tr("Empty DACL: no access granted\n");
    } else {
        for (DWORD index = 0; index < dacl->AceCount; ++index) {
            void *entry = nullptr;
            if (!GetAce(dacl, index, &entry)) { continue; }
            auto *header = static_cast<ACE_HEADER *>(entry);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE && header->AceType != ACCESS_DENIED_ACE_TYPE) { continue; }
            auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(entry);
            QStringList rights;
            if ((ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS || (ace->Mask & GENERIC_ALL)) {
                rights << QObject::tr("Full control");
            } else {
                if (ace->Mask & (GENERIC_READ | FILE_READ_DATA)) { rights << QObject::tr("Read"); }
                if (ace->Mask & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) { rights << QObject::tr("Write"); }
                if (ace->Mask & (GENERIC_EXECUTE | FILE_EXECUTE)) { rights << QObject::tr("Execute / traverse"); }
                if (ace->Mask & DELETE) { rights << QObject::tr("Delete"); }
            }
            text += QStringLiteral("%1 %2: %3 [0x%4]%5\n")
                .arg(header->AceType == ACCESS_ALLOWED_ACE_TYPE ? QObject::tr("Allow") : QObject::tr("Deny"),
                     accountName(reinterpret_cast<PSID>(&ace->SidStart)), rights.join(QStringLiteral(", ")))
                .arg(qulonglong(ace->Mask), 0, 16)
                .arg(header->AceFlags & INHERITED_ACE ? QObject::tr(" (inherited)") : QString());
        }
    }
    LPWSTR sddl = nullptr;
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &sddl, nullptr)) {
        text += QObject::tr("Full DACL (SDDL): %1").arg(QString::fromWCharArray(sddl));
        LocalFree(sddl);
    }
    LocalFree(descriptor);
    return text;
}
#endif

QString FileMetadata::describe(const QString &path)
{
    const QFileInfo info(path);
    const auto date = [](const QDateTime &value) {
        return value.isValid() ? value.toLocalTime().toString(Qt::ISODateWithMs) : QObject::tr("Unavailable");
    };
    QString text = QObject::tr("Path: %1\nType: %2\nSize: %3 bytes (%4)\nCreated: %5\nModified: %6\n")
        .arg(clipboardPath(path), QMimeDatabase().mimeTypeForFile(info.absoluteFilePath(), QMimeDatabase::MatchContent).name()).arg(info.size())
        .arg(QLocale().formattedDataSize(info.size()), date(info.birthTime()), date(info.lastModified()));
#ifdef Q_OS_WIN
    text += securityDescription(info.absoluteFilePath());
#else
    QString mode;
    for (const auto permission : {QFile::ReadOwner, QFile::WriteOwner, QFile::ExeOwner, QFile::ReadGroup, QFile::WriteGroup,
                                 QFile::ExeGroup, QFile::ReadOther, QFile::WriteOther, QFile::ExeOther}) {
        const int index = mode.size() % 3;
        mode += info.permission(permission) ? QLatin1Char("rwx"[index]) : QLatin1Char('-');
    }
    text += QObject::tr("Owner: %1\nGroup: %2\nPermissions: %3").arg(info.owner(), info.group(), mode);
#endif
    return text;
}
