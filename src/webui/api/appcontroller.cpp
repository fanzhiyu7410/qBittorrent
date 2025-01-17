/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2018  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006-2012  Christophe Dumez <chris@qbittorrent.org>
 * Copyright (C) 2006-2012  Ishan Arora <ishan@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "appcontroller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QTranslator>

#include "base/bittorrent/session.h"
#include "base/global.h"
#include "base/net/portforwarder.h"
#include "base/net/proxyconfigurationmanager.h"
#include "base/preferences.h"
#include "base/rss/rss_autodownloader.h"
#include "base/rss/rss_session.h"
#include "base/scanfoldersmodel.h"
#include "base/torrentfileguard.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/net.h"
#include "base/utils/password.h"
#include "../webapplication.h"

void AppController::webapiVersionAction()
{
    setResult(static_cast<QString>(API_VERSION));
}

void AppController::versionAction()
{
    setResult(QBT_VERSION);
}

void AppController::buildInfoAction()
{
    const QJsonObject versions = {
        {"qt", QT_VERSION_STR},
        {"libtorrent", Utils::Misc::libtorrentVersionString()},
        {"boost", Utils::Misc::boostVersionString()},
        {"openssl", Utils::Misc::opensslVersionString()},
        {"bitness", (QT_POINTER_SIZE * 8)}
    };
    setResult(versions);
}

void AppController::shutdownAction()
{
    qDebug() << "Shutdown request from Web UI";

    // Special case handling for shutdown, we
    // need to reply to the Web UI before
    // actually shutting down.
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
}

void AppController::preferencesAction()
{
    const Preferences *const pref = Preferences::instance();
    const auto *session = BitTorrent::Session::instance();
    QVariantMap data;

    // Downloads
    // When adding a torrent
    data["create_subfolder_enabled"] = session->isCreateTorrentSubfolder();
    data["start_paused_enabled"] = session->isAddTorrentPaused();
    data["auto_delete_mode"] = static_cast<int>(TorrentFileGuard::autoDeleteMode());
    data["preallocate_all"] = session->isPreallocationEnabled();
    data["incomplete_files_ext"] = session->isAppendExtensionEnabled();
    // Saving Management
    data["auto_tmm_enabled"] = !session->isAutoTMMDisabledByDefault();
    data["torrent_changed_tmm_enabled"] = !session->isDisableAutoTMMWhenCategoryChanged();
    data["save_path_changed_tmm_enabled"] = !session->isDisableAutoTMMWhenDefaultSavePathChanged();
    data["category_changed_tmm_enabled"] = !session->isDisableAutoTMMWhenCategorySavePathChanged();
    data["save_path"] = Utils::Fs::toNativePath(session->defaultSavePath());
    data["temp_path_enabled"] = session->isTempPathEnabled();
    data["temp_path"] = Utils::Fs::toNativePath(session->tempPath());
    data["export_dir"] = Utils::Fs::toNativePath(session->torrentExportDirectory());
    data["export_dir_fin"] = Utils::Fs::toNativePath(session->finishedTorrentExportDirectory());
    // Automatically add torrents from
    const QVariantHash dirs = pref->getScanDirs();
    QVariantMap nativeDirs;
    for (auto i = dirs.cbegin(); i != dirs.cend(); ++i) {
        if (i.value().type() == QVariant::Int)
            nativeDirs.insert(Utils::Fs::toNativePath(i.key()), i.value().toInt());
        else
            nativeDirs.insert(Utils::Fs::toNativePath(i.key()), Utils::Fs::toNativePath(i.value().toString()));
    }
    data["scan_dirs"] = nativeDirs;
    // Email notification upon download completion
    data["mail_notification_enabled"] = pref->isMailNotificationEnabled();
    data["mail_notification_sender"] = pref->getMailNotificationSender();
    data["mail_notification_email"] = pref->getMailNotificationEmail();
    data["mail_notification_smtp"] = pref->getMailNotificationSMTP();
    data["mail_notification_ssl_enabled"] = pref->getMailNotificationSMTPSSL();
    data["mail_notification_auth_enabled"] = pref->getMailNotificationSMTPAuth();
    data["mail_notification_username"] = pref->getMailNotificationSMTPUsername();
    data["mail_notification_password"] = pref->getMailNotificationSMTPPassword();
    // Run an external program on torrent completion
    data["autorun_enabled"] = pref->isAutoRunEnabled();
    data["autorun_program"] = Utils::Fs::toNativePath(pref->getAutoRunProgram());

    // Connection
    // Listening Port
    data["listen_port"] = session->port();
    data["upnp"] = Net::PortForwarder::instance()->isEnabled();
    data["random_port"] = session->useRandomPort();
    // Connections Limits
    data["max_connec"] = session->maxConnections();
    data["max_connec_per_torrent"] = session->maxConnectionsPerTorrent();
    data["max_uploads"] = session->maxUploads();
    data["max_uploads_per_torrent"] = session->maxUploadsPerTorrent();

    // Proxy Server
    const auto *proxyManager = Net::ProxyConfigurationManager::instance();
    Net::ProxyConfiguration proxyConf = proxyManager->proxyConfiguration();
    data["proxy_type"] = static_cast<int>(proxyConf.type);
    data["proxy_ip"] = proxyConf.ip;
    data["proxy_port"] = proxyConf.port;
    data["proxy_auth_enabled"] = proxyManager->isAuthenticationRequired(); // deprecated
    data["proxy_username"] = proxyConf.username;
    data["proxy_password"] = proxyConf.password;

    data["proxy_peer_connections"] = session->isProxyPeerConnectionsEnabled();
    data["force_proxy"] = session->isForceProxyEnabled();
    data["proxy_torrents_only"] = proxyManager->isProxyOnlyForTorrents();

    // IP Filtering
    data["ip_filter_enabled"] = session->isIPFilteringEnabled();
    data["ip_filter_path"] = Utils::Fs::toNativePath(session->IPFilterFile());
    data["ip_filter_trackers"] = session->isTrackerFilteringEnabled();
    data["banned_IPs"] = session->bannedIPs().join("\n");

    // Speed
    // Global Rate Limits
    data["dl_limit"] = session->globalDownloadSpeedLimit();
    data["up_limit"] = session->globalUploadSpeedLimit();
    data["alt_dl_limit"] = session->altGlobalDownloadSpeedLimit();
    data["alt_up_limit"] = session->altGlobalUploadSpeedLimit();
    data["bittorrent_protocol"] = static_cast<int>(session->btProtocol());
    data["limit_utp_rate"] = session->isUTPRateLimited();
    data["limit_tcp_overhead"] = session->includeOverheadInLimits();
    data["limit_lan_peers"] = !session->ignoreLimitsOnLAN();
    // Scheduling
    data["scheduler_enabled"] = session->isBandwidthSchedulerEnabled();
    const QTime start_time = pref->getSchedulerStartTime();
    data["schedule_from_hour"] = start_time.hour();
    data["schedule_from_min"] = start_time.minute();
    const QTime end_time = pref->getSchedulerEndTime();
    data["schedule_to_hour"] = end_time.hour();
    data["schedule_to_min"] = end_time.minute();
    data["scheduler_days"] = pref->getSchedulerDays();

    // Bittorrent
    // Privacy
    data["dht"] = session->isDHTEnabled();
    data["pex"] = session->isPeXEnabled();
    data["lsd"] = session->isLSDEnabled();
    data["encryption"] = session->encryption();
    data["anonymous_mode"] = session->isAnonymousModeEnabled();
    // Torrent Queueing
    data["queueing_enabled"] = session->isQueueingSystemEnabled();
    data["max_active_downloads"] = session->maxActiveDownloads();
    data["max_active_torrents"] = session->maxActiveTorrents();
    data["max_active_uploads"] = session->maxActiveUploads();
    data["dont_count_slow_torrents"] = session->ignoreSlowTorrentsForQueueing();
    data["slow_torrent_dl_rate_threshold"] = session->downloadRateForSlowTorrents();
    data["slow_torrent_ul_rate_threshold"] = session->uploadRateForSlowTorrents();
    data["slow_torrent_inactive_timer"] = session->slowTorrentsInactivityTimer();
    // Share Ratio Limiting
    data["max_ratio_enabled"] = (session->globalMaxRatio() >= 0.);
    data["max_ratio"] = session->globalMaxRatio();
    data["max_seeding_time_enabled"] = (session->globalMaxSeedingMinutes() >= 0.);
    data["max_seeding_time"] = session->globalMaxSeedingMinutes();
    data["max_ratio_act"] = session->maxRatioAction();
    // Add trackers
    data["add_trackers_enabled"] = session->isAddTrackersEnabled();
    data["add_trackers"] = session->additionalTrackers();

    // Web UI
    // Language
    data["locale"] = pref->getLocale();
    // HTTP Server
    data["web_ui_domain_list"] = pref->getServerDomains();
    data["web_ui_address"] = pref->getWebUiAddress();
    data["web_ui_port"] = pref->getWebUiPort();
    data["web_ui_upnp"] = pref->useUPnPForWebUIPort();
    data["use_https"] = pref->isWebUiHttpsEnabled();
    data["web_ui_https_cert_path"] = pref->getWebUIHttpsCertificatePath();
    data["web_ui_https_key_path"] = pref->getWebUIHttpsKeyPath();
    // Authentication
    data["web_ui_username"] = pref->getWebUiUsername();
    data["bypass_local_auth"] = !pref->isWebUiLocalAuthEnabled();
    data["bypass_auth_subnet_whitelist_enabled"] = pref->isWebUiAuthSubnetWhitelistEnabled();
    QStringList authSubnetWhitelistStringList;
    for (const Utils::Net::Subnet &subnet : asConst(pref->getWebUiAuthSubnetWhitelist()))
        authSubnetWhitelistStringList << Utils::Net::subnetToString(subnet);
    data["bypass_auth_subnet_whitelist"] = authSubnetWhitelistStringList.join("\n");
    // Use alternative Web UI
    data["alternative_webui_enabled"] = pref->isAltWebUiEnabled();
    data["alternative_webui_path"] = pref->getWebUiRootFolder();
    // Security
    data["web_ui_clickjacking_protection_enabled"] = pref->isWebUiClickjackingProtectionEnabled();
    data["web_ui_csrf_protection_enabled"] = pref->isWebUiCSRFProtectionEnabled();
    data["web_ui_host_header_validation_enabled"] = pref->isWebUIHostHeaderValidationEnabled();
    // Update my dynamic domain name
    data["dyndns_enabled"] = pref->isDynDNSEnabled();
    data["dyndns_service"] = pref->getDynDNSService();
    data["dyndns_username"] = pref->getDynDNSUsername();
    data["dyndns_password"] = pref->getDynDNSPassword();
    data["dyndns_domain"] = pref->getDynDomainName();

    // RSS settings
    data["rss_refresh_interval"] = RSS::Session::instance()->refreshInterval();
    data["rss_max_articles_per_feed"] = RSS::Session::instance()->maxArticlesPerFeed();
    data["rss_processing_enabled"] = RSS::Session::instance()->isProcessingEnabled();
    data["rss_auto_downloading_enabled"] = RSS::AutoDownloader::instance()->isProcessingEnabled();

    setResult(QJsonObject::fromVariantMap(data));
}

void AppController::setPreferencesAction()
{
    checkParams({"json"});

    Preferences *const pref = Preferences::instance();
    auto session = BitTorrent::Session::instance();
    const QVariantMap m = QJsonDocument::fromJson(params()["json"].toUtf8()).toVariant().toMap();

    QVariantMap::ConstIterator it;
    const auto hasKey = [&it, &m](const char *key) -> bool
    {
        it = m.find(QLatin1String(key));
        return (it != m.constEnd());
    };

    // Downloads
    // When adding a torrent
    if (hasKey("create_subfolder_enabled"))
        session->setCreateTorrentSubfolder(it.value().toBool());
    if (hasKey("start_paused_enabled"))
        session->setAddTorrentPaused(it.value().toBool());
    if (hasKey("auto_delete_mode"))
        TorrentFileGuard::setAutoDeleteMode(static_cast<TorrentFileGuard::AutoDeleteMode>(it.value().toInt()));

    if (hasKey("preallocate_all"))
        session->setPreallocationEnabled(it.value().toBool());
    if (hasKey("incomplete_files_ext"))
        session->setAppendExtensionEnabled(it.value().toBool());

    // Saving Management
    if (hasKey("auto_tmm_enabled"))
        session->setAutoTMMDisabledByDefault(!it.value().toBool());
    if (hasKey("torrent_changed_tmm_enabled"))
        session->setDisableAutoTMMWhenCategoryChanged(!it.value().toBool());
    if (hasKey("save_path_changed_tmm_enabled"))
        session->setDisableAutoTMMWhenDefaultSavePathChanged(!it.value().toBool());
    if (hasKey("category_changed_tmm_enabled"))
        session->setDisableAutoTMMWhenCategorySavePathChanged(!it.value().toBool());
    if (hasKey("save_path"))
        session->setDefaultSavePath(it.value().toString());
    if (hasKey("temp_path_enabled"))
        session->setTempPathEnabled(it.value().toBool());
    if (hasKey("temp_path"))
        session->setTempPath(it.value().toString());
    if (hasKey("export_dir"))
        session->setTorrentExportDirectory(it.value().toString());
    if (hasKey("export_dir_fin"))
        session->setFinishedTorrentExportDirectory(it.value().toString());
    // Automatically add torrents from
    if (hasKey("scan_dirs")) {
        const QVariantMap nativeDirs = it.value().toMap();
        QVariantHash oldScanDirs = pref->getScanDirs();
        QVariantHash scanDirs;
        ScanFoldersModel *model = ScanFoldersModel::instance();
        for (auto i = nativeDirs.cbegin(); i != nativeDirs.cend(); ++i) {
            QString folder = Utils::Fs::fromNativePath(i.key());
            int downloadType;
            QString downloadPath;
            ScanFoldersModel::PathStatus ec;
            if (i.value().type() == QVariant::String) {
                downloadType = ScanFoldersModel::CUSTOM_LOCATION;
                downloadPath = Utils::Fs::fromNativePath(i.value().toString());
            }
            else {
                downloadType = i.value().toInt();
                downloadPath = (downloadType == ScanFoldersModel::DEFAULT_LOCATION) ? "Default folder" : "Watch folder";
            }

            if (!oldScanDirs.contains(folder))
                ec = model->addPath(folder, static_cast<ScanFoldersModel::PathType>(downloadType), downloadPath);
            else
                ec = model->updatePath(folder, static_cast<ScanFoldersModel::PathType>(downloadType), downloadPath);

            if (ec == ScanFoldersModel::Ok) {
                scanDirs.insert(folder, (downloadType == ScanFoldersModel::CUSTOM_LOCATION) ? QVariant(downloadPath) : QVariant(downloadType));
                qDebug("New watched folder: %s to %s", qUtf8Printable(folder), qUtf8Printable(downloadPath));
            }
            else {
                qDebug("Watched folder %s failed with error %d", qUtf8Printable(folder), ec);
            }
        }

        // Update deleted folders
        for (auto i = oldScanDirs.cbegin(); i != oldScanDirs.cend(); ++i) {
            const QString &folder = i.key();
            if (!scanDirs.contains(folder)) {
                model->removePath(folder);
                qDebug("Removed watched folder %s", qUtf8Printable(folder));
            }
        }
        pref->setScanDirs(scanDirs);
    }
    // Email notification upon download completion
    if (hasKey("mail_notification_enabled"))
        pref->setMailNotificationEnabled(it.value().toBool());
    if (hasKey("mail_notification_sender"))
        pref->setMailNotificationSender(it.value().toString());
    if (hasKey("mail_notification_email"))
        pref->setMailNotificationEmail(it.value().toString());
    if (hasKey("mail_notification_smtp"))
        pref->setMailNotificationSMTP(it.value().toString());
    if (hasKey("mail_notification_ssl_enabled"))
        pref->setMailNotificationSMTPSSL(it.value().toBool());
    if (hasKey("mail_notification_auth_enabled"))
        pref->setMailNotificationSMTPAuth(it.value().toBool());
    if (hasKey("mail_notification_username"))
        pref->setMailNotificationSMTPUsername(it.value().toString());
    if (hasKey("mail_notification_password"))
        pref->setMailNotificationSMTPPassword(it.value().toString());
    // Run an external program on torrent completion
    if (hasKey("autorun_enabled"))
        pref->setAutoRunEnabled(it.value().toBool());
    if (hasKey("autorun_program"))
        pref->setAutoRunProgram(it.value().toString());

    // Connection
    // Listening Port
    if (hasKey("listen_port"))
        session->setPort(it.value().toInt());
    if (hasKey("upnp"))
        Net::PortForwarder::instance()->setEnabled(it.value().toBool());
    if (hasKey("random_port"))
        session->setUseRandomPort(it.value().toBool());
    // Connections Limits
    if (hasKey("max_connec"))
        session->setMaxConnections(it.value().toInt());
    if (hasKey("max_connec_per_torrent"))
        session->setMaxConnectionsPerTorrent(it.value().toInt());
    if (hasKey("max_uploads"))
        session->setMaxUploads(it.value().toInt());
    if (hasKey("max_uploads_per_torrent"))
        session->setMaxUploadsPerTorrent(it.value().toInt());

    // Proxy Server
    auto proxyManager = Net::ProxyConfigurationManager::instance();
    Net::ProxyConfiguration proxyConf = proxyManager->proxyConfiguration();
    if (hasKey("proxy_type"))
        proxyConf.type = static_cast<Net::ProxyType>(it.value().toInt());
    if (hasKey("proxy_ip"))
        proxyConf.ip = it.value().toString();
    if (hasKey("proxy_port"))
        proxyConf.port = it.value().toUInt();
    if (hasKey("proxy_username"))
        proxyConf.username = it.value().toString();
    if (hasKey("proxy_password"))
        proxyConf.password = it.value().toString();
    proxyManager->setProxyConfiguration(proxyConf);

    if (hasKey("proxy_peer_connections"))
        session->setProxyPeerConnectionsEnabled(it.value().toBool());
    if (hasKey("force_proxy"))
        session->setForceProxyEnabled(it.value().toBool());
    if (hasKey("proxy_torrents_only"))
        proxyManager->setProxyOnlyForTorrents(it.value().toBool());

    // IP Filtering
    if (hasKey("ip_filter_enabled"))
        session->setIPFilteringEnabled(it.value().toBool());
    if (hasKey("ip_filter_path"))
        session->setIPFilterFile(it.value().toString());
    if (hasKey("ip_filter_trackers"))
        session->setTrackerFilteringEnabled(it.value().toBool());
    if (hasKey("banned_IPs"))
        session->setBannedIPs(it.value().toString().split('\n'));

    // Speed
    // Global Rate Limits
    if (hasKey("dl_limit"))
        session->setGlobalDownloadSpeedLimit(it.value().toInt());
    if (hasKey("up_limit"))
        session->setGlobalUploadSpeedLimit(it.value().toInt());
    if (hasKey("alt_dl_limit"))
        session->setAltGlobalDownloadSpeedLimit(it.value().toInt());
    if (hasKey("alt_up_limit"))
       session->setAltGlobalUploadSpeedLimit(it.value().toInt());
    if (hasKey("bittorrent_protocol"))
        session->setBTProtocol(static_cast<BitTorrent::BTProtocol>(it.value().toInt()));
    if (hasKey("limit_utp_rate"))
        session->setUTPRateLimited(it.value().toBool());
    if (hasKey("limit_tcp_overhead"))
        session->setIncludeOverheadInLimits(it.value().toBool());
    if (hasKey("limit_lan_peers"))
        session->setIgnoreLimitsOnLAN(!it.value().toBool());
    // Scheduling
    if (hasKey("scheduler_enabled"))
        session->setBandwidthSchedulerEnabled(it.value().toBool());
    if (m.contains("schedule_from_hour") && m.contains("schedule_from_min"))
        pref->setSchedulerStartTime(QTime(m["schedule_from_hour"].toInt(), m["schedule_from_min"].toInt()));
    if (m.contains("schedule_to_hour") && m.contains("schedule_to_min"))
        pref->setSchedulerEndTime(QTime(m["schedule_to_hour"].toInt(), m["schedule_to_min"].toInt()));
    if (hasKey("scheduler_days"))
        pref->setSchedulerDays(SchedulerDays(it.value().toInt()));

    // Bittorrent
    // Privacy
    if (hasKey("dht"))
        session->setDHTEnabled(it.value().toBool());
    if (hasKey("pex"))
        session->setPeXEnabled(it.value().toBool());
    if (hasKey("lsd"))
        session->setLSDEnabled(it.value().toBool());
    if (hasKey("encryption"))
        session->setEncryption(it.value().toInt());
    if (hasKey("anonymous_mode"))
        session->setAnonymousModeEnabled(it.value().toBool());
    // Torrent Queueing
    if (hasKey("queueing_enabled"))
        session->setQueueingSystemEnabled(it.value().toBool());
    if (hasKey("max_active_downloads"))
        session->setMaxActiveDownloads(it.value().toInt());
    if (hasKey("max_active_torrents"))
        session->setMaxActiveTorrents(it.value().toInt());
    if (hasKey("max_active_uploads"))
        session->setMaxActiveUploads(it.value().toInt());
    if (hasKey("dont_count_slow_torrents"))
        session->setIgnoreSlowTorrentsForQueueing(it.value().toBool());
    if (hasKey("slow_torrent_dl_rate_threshold"))
        session->setDownloadRateForSlowTorrents(it.value().toInt());
    if (hasKey("slow_torrent_ul_rate_threshold"))
        session->setUploadRateForSlowTorrents(it.value().toInt());
    if (hasKey("slow_torrent_inactive_timer"))
        session->setSlowTorrentsInactivityTimer(it.value().toInt());
    // Share Ratio Limiting
    if (hasKey("max_ratio_enabled")) {
        if (it.value().toBool())
            session->setGlobalMaxRatio(m["max_ratio"].toReal());
        else
            session->setGlobalMaxRatio(-1);
    }
    if (hasKey("max_seeding_time_enabled")) {
        if (it.value().toBool())
            session->setGlobalMaxSeedingMinutes(m["max_seeding_time"].toInt());
        else
            session->setGlobalMaxSeedingMinutes(-1);
    }
    if (hasKey("max_ratio_act"))
        session->setMaxRatioAction(static_cast<MaxRatioAction>(it.value().toInt()));
    // Add trackers
    session->setAddTrackersEnabled(m["add_trackers_enabled"].toBool());
    session->setAdditionalTrackers(m["add_trackers"].toString());

    // Web UI
    // Language
    if (hasKey("locale")) {
        QString locale = it.value().toString();
        if (pref->getLocale() != locale) {
            auto *translator = new QTranslator;
            if (translator->load(QLatin1String(":/lang/qbittorrent_") + locale)) {
                qDebug("%s locale recognized, using translation.", qUtf8Printable(locale));
            }
            else {
                qDebug("%s locale unrecognized, using default (en).", qUtf8Printable(locale));
            }
            qApp->installTranslator(translator);

            pref->setLocale(locale);
        }
    }
    // HTTP Server
    if (hasKey("web_ui_domain_list"))
        pref->setServerDomains(it.value().toString());
    if (hasKey("web_ui_address"))
        pref->setWebUiAddress(it.value().toString());
    if (hasKey("web_ui_port"))
        pref->setWebUiPort(it.value().toUInt());
    if (hasKey("web_ui_upnp"))
        pref->setUPnPForWebUIPort(it.value().toBool());
    if (hasKey("use_https"))
        pref->setWebUiHttpsEnabled(it.value().toBool());
    if (hasKey("web_ui_https_cert_path"))
        pref->setWebUIHttpsCertificatePath(it.value().toString());
    if (hasKey("web_ui_https_key_path"))
        pref->setWebUIHttpsKeyPath(it.value().toString());
    // Authentication
    if (hasKey("web_ui_username"))
        pref->setWebUiUsername(it.value().toString());
    if (hasKey("web_ui_password"))
        pref->setWebUIPassword(Utils::Password::PBKDF2::generate(it.value().toByteArray()));
    if (hasKey("bypass_local_auth"))
        pref->setWebUiLocalAuthEnabled(!it.value().toBool());
    if (hasKey("bypass_auth_subnet_whitelist_enabled"))
        pref->setWebUiAuthSubnetWhitelistEnabled(it.value().toBool());
    if (hasKey("bypass_auth_subnet_whitelist")) {
        // recognize new lines and commas as delimiters
        pref->setWebUiAuthSubnetWhitelist(it.value().toString().split(QRegularExpression("\n|,"), QString::SkipEmptyParts));
    }
    // Use alternative Web UI
    if (hasKey("alternative_webui_enabled"))
        pref->setAltWebUiEnabled(it.value().toBool());
    if (hasKey("alternative_webui_path"))
        pref->setWebUiRootFolder(it.value().toString());
    // Security
    if (hasKey("web_ui_clickjacking_protection_enabled"))
        pref->setWebUiClickjackingProtectionEnabled(it.value().toBool());
    if (hasKey("web_ui_csrf_protection_enabled"))
        pref->setWebUiCSRFProtectionEnabled(it.value().toBool());
    if (hasKey("web_ui_host_header_validation_enabled"))
        pref->setWebUIHostHeaderValidationEnabled(it.value().toBool());
    // Update my dynamic domain name
    if (hasKey("dyndns_enabled"))
        pref->setDynDNSEnabled(it.value().toBool());
    if (hasKey("dyndns_service"))
        pref->setDynDNSService(it.value().toInt());
    if (hasKey("dyndns_username"))
        pref->setDynDNSUsername(it.value().toString());
    if (hasKey("dyndns_password"))
        pref->setDynDNSPassword(it.value().toString());
    if (hasKey("dyndns_domain"))
        pref->setDynDomainName(it.value().toString());

    // Save preferences
    pref->apply();

    if (hasKey("rss_refresh_interval"))
        RSS::Session::instance()->setRefreshInterval(it.value().toUInt());
    if (hasKey("rss_max_articles_per_feed"))
        RSS::Session::instance()->setMaxArticlesPerFeed(it.value().toInt());
    if (hasKey("rss_processing_enabled"))
        RSS::Session::instance()->setProcessingEnabled(it.value().toBool());
    if (hasKey("rss_auto_downloading_enabled"))
        RSS::AutoDownloader::instance()->setProcessingEnabled(it.value().toBool());
}

void AppController::defaultSavePathAction()
{
    setResult(BitTorrent::Session::instance()->defaultSavePath());
}
