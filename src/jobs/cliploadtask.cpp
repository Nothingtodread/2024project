/*
Copyright (C) 2021  Jean-Baptiste Mardelle <jb@kdenlive.org>
This file is part of Kdenlive. See www.kdenlive.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of
the License or (at your option) version 3 or any later version
accepted by the membership of KDE e.V. (or its successor approved
by the membership of KDE e.V.), which shall act as a proxy
defined in Section 14 of version 3 of the license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "cliploadtask.h"
#include "core.h"
#include "bin/projectitemmodel.h"
#include "bin/projectclip.h"
#include "audio/audioStreamInfo.h"
#include "kdenlivesettings.h"

#include "xml/xml.hpp"
#include <QString>
#include <QVariantList>
#include <QImage>
#include <QList>
#include <QThreadPool>
#include <QMutex>
#include <QTime>
#include <QFile>
#include <QAction>
#include <QElapsedTimer>
#include <QMimeDatabase>
#include <monitor/monitor.h>
#include <profiles/profilemodel.hpp>
#include <klocalizedstring.h>
#include <KMessageWidget>
#include <project/dialogs/slideshowclip.h>
#include <project/dialogs/slideshowclip.h>

static QList<ClipLoadTask*> tasksList;
static QMutex tasksListMutex;

ClipLoadTask::ClipLoadTask(const QString &clipId, const QDomElement &xml, QObject* object, std::function<void()> readyCallBack)
    : QRunnable()
    , m_cid(clipId)
    , m_object(object)
    , m_xml(xml)
    , m_readyCallBack(std::move(readyCallBack))
    , m_isCanceled(false)
    , m_isForce(false)
{
    setAutoDelete(true);
}

const QString ClipLoadTask::clipId() const
{
    return m_cid;
}

ClipLoadTask::~ClipLoadTask()
{
}

void ClipLoadTask::cancel(const QString cid)
{
    QMutexLocker lk(&tasksListMutex);
    // See if there is already a task for this MLT service and resource.
    foreach (ClipLoadTask* t, tasksList) {
        if (t->clipId() == cid) {
            // If so, then just add ourselves to be notified upon completion.
            t->m_isCanceled = true;
            break;
        }
    }
}

void ClipLoadTask::start(const QString cid, const QDomElement &xml, QObject* object, bool force, std::function<void()> readyCallBack)
{
    ClipLoadTask* task = new ClipLoadTask(cid, xml, object, readyCallBack);
    tasksListMutex.lock();
    // See if there is already a task for this MLT service and resource.
    foreach (ClipLoadTask* t, tasksList) {
        if (*t == *task) {
            // If so, then just add ourselves to be notified upon completion.
            delete task;
            task = 0;
            break;
        }
    }
    if (task) {
        // Otherwise, start a new audio levels generation thread.
        task->m_isForce = force;
        tasksList << task;
        pCore->clipJobPool.start(task, 0);
    }
    tasksListMutex.unlock();
}

void ClipLoadTask::closeAll()
{
    // Tell all of the audio levels tasks to stop.
    tasksListMutex.lock();
    while (!tasksList.isEmpty()) {
        ClipLoadTask* task = tasksList.first();
        task->m_isCanceled = true;
        tasksList.removeFirst();
    }
    tasksListMutex.unlock();
}

bool ClipLoadTask::operator==(ClipLoadTask &b)
{
    return m_cid == b.clipId();
}

void ClipLoadTask::cleanup()
{
    tasksListMutex.lock();
    for (int i = 0; i < tasksList.size(); ++i) {
        if (*tasksList[i] == *this) {
            tasksList.removeAt(i);
            break;
        }
    }
    tasksListMutex.unlock();
}

ClipType::ProducerType ClipLoadTask::getTypeForService(const QString &id, const QString &path)
{
    if (id.isEmpty()) {
        QString ext = path.section(QLatin1Char('.'), -1);
        if (ext == QLatin1String("mlt") || ext == QLatin1String("kdenlive")) {
            return ClipType::Playlist;
        }
        return ClipType::Unknown;
    }
    if (id == QLatin1String("color") || id == QLatin1String("colour")) {
        return ClipType::Color;
    }
    if (id == QLatin1String("kdenlivetitle")) {
        return ClipType::Text;
    }
    if (id == QLatin1String("qtext")) {
        return ClipType::QText;
    }
    if (id == QLatin1String("xml") || id == QLatin1String("consumer")) {
        return ClipType::Playlist;
    }
    if (id == QLatin1String("webvfx")) {
        return ClipType::WebVfx;
    }
    if (id == QLatin1String("qml")) {
        return ClipType::Qml;
    }
    return ClipType::Unknown;
}

// static
std::shared_ptr<Mlt::Producer> ClipLoadTask::loadResource(QString resource, const QString &type)
{
    if (!resource.startsWith(type)) {
        resource.prepend(type);
    }
    return std::make_shared<Mlt::Producer>(pCore->getCurrentProfile()->profile(), nullptr, resource.toUtf8().constData());
}

std::shared_ptr<Mlt::Producer> ClipLoadTask::loadPlaylist(QString &resource)
{
    std::unique_ptr<Mlt::Profile> xmlProfile(new Mlt::Profile());
    xmlProfile->set_explicit(0);
    std::unique_ptr<Mlt::Producer> producer(new Mlt::Producer(*xmlProfile, "xml", resource.toUtf8().constData()));
    if (!producer->is_valid()) {
        qDebug() << "////// ERROR, CANNOT LOAD SELECTED PLAYLIST: " << resource;
        return nullptr;
    }
    std::unique_ptr<ProfileParam> clipProfile(new ProfileParam(xmlProfile.get()));
    std::unique_ptr<ProfileParam> projectProfile(new ProfileParam(pCore->getCurrentProfile().get()));
    if (*clipProfile.get() == *projectProfile.get()) {
        // We can use the "xml" producer since profile is the same (using it with different profiles corrupts the project.
        // Beware that "consumer" currently crashes on audio mixes!
        //resource.prepend(QStringLiteral("xml:"));
    } else {
        // This is currently crashing so I guess we'd better reject it for now
        if (!pCore->getCurrentProfile()->isCompatible(xmlProfile.get())) {
            m_errorMessage.append(i18n("Playlist has a different framerate (%1/%2fps), not recommended.", xmlProfile->frame_rate_num(), xmlProfile->frame_rate_den()));
            QString loader = resource;
            loader.prepend(QStringLiteral("consumer:"));
            pCore->getCurrentProfile()->set_explicit(1);
            return std::make_shared<Mlt::Producer>(pCore->getCurrentProfile()->profile(), loader.toUtf8().constData());
        } else {
            m_errorMessage.append(i18n("No matching profile"));
            return nullptr;
        }
    }
    pCore->getCurrentProfile()->set_explicit(1);
    return std::make_shared<Mlt::Producer>(pCore->getCurrentProfile()->profile(), "xml", resource.toUtf8().constData());
}

// Read the properties of the xml and pass them to the producer. Note that some properties like resource are ignored
void ClipLoadTask::processProducerProperties(const std::shared_ptr<Mlt::Producer> &prod, const QDomElement &xml)
{
    // TODO: there is some duplication with clipcontroller > updateproducer that also copies properties
    QString value;
    QStringList internalProperties;
    internalProperties << QStringLiteral("bypassDuplicate") << QStringLiteral("resource") << QStringLiteral("mlt_service") << QStringLiteral("audio_index")
                       << QStringLiteral("video_index") << QStringLiteral("mlt_type") << QStringLiteral("length");
    QDomNodeList props;

    if (xml.tagName() == QLatin1String("producer")) {
        props = xml.childNodes();
    } else {
        props = xml.firstChildElement(QStringLiteral("producer")).childNodes();
    }
    for (int i = 0; i < props.count(); ++i) {
        if (props.at(i).toElement().tagName() != QStringLiteral("property")) {
            continue;
        }
        QString propertyName = props.at(i).toElement().attribute(QStringLiteral("name"));
        if (!internalProperties.contains(propertyName) && !propertyName.startsWith(QLatin1Char('_'))) {
            value = props.at(i).firstChild().nodeValue();
            if (propertyName.startsWith(QLatin1String("kdenlive-force."))) {
                // this is a special forced property, pass it
                propertyName.remove(0, 15);
            }
            prod->set(propertyName.toUtf8().constData(), value.toUtf8().constData());
        }
    }
}

void ClipLoadTask::processSlideShow(std::shared_ptr<Mlt::Producer> producer)
{
    int ttl = Xml::getXmlProperty(m_xml, QStringLiteral("ttl")).toInt();
    QString anim = Xml::getXmlProperty(m_xml, QStringLiteral("animation"));
    if (!anim.isEmpty()) {
        auto *filter = new Mlt::Filter(pCore->getCurrentProfile()->profile(), "affine");
        if ((filter != nullptr) && filter->is_valid()) {
            int cycle = ttl;
            QString geometry = SlideshowClip::animationToGeometry(anim, cycle);
            if (!geometry.isEmpty()) {
                if (anim.contains(QStringLiteral("low-pass"))) {
                    auto *blur = new Mlt::Filter(pCore->getCurrentProfile()->profile(), "boxblur");
                    if ((blur != nullptr) && blur->is_valid()) {
                        producer->attach(*blur);
                    }
                }
                filter->set("transition.geometry", geometry.toUtf8().data());
                filter->set("transition.cycle", cycle);
                producer->attach(*filter);
            }
        }
    }
    QString fade = Xml::getXmlProperty(m_xml, QStringLiteral("fade"));
    if (fade == QLatin1String("1")) {
        // user wants a fade effect to slideshow
        auto *filter = new Mlt::Filter(pCore->getCurrentProfile()->profile(), "luma");
        if ((filter != nullptr) && filter->is_valid()) {
            if (ttl != 0) {
                filter->set("cycle", ttl);
            }
            QString luma_duration = Xml::getXmlProperty(m_xml, QStringLiteral("luma_duration"));
            QString luma_file = Xml::getXmlProperty(m_xml, QStringLiteral("luma_file"));
            if (!luma_duration.isEmpty()) {
                filter->set("duration", luma_duration.toInt());
            }
            if (!luma_file.isEmpty()) {
                filter->set("luma.resource", luma_file.toUtf8().constData());
                QString softness = Xml::getXmlProperty(m_xml, QStringLiteral("softness"));
                if (!softness.isEmpty()) {
                    int soft = softness.toInt();
                    filter->set("luma.softness", double(soft) / 100.0);
                }
            }
            producer->attach(*filter);
        }
    }
    QString crop = Xml::getXmlProperty(m_xml, QStringLiteral("crop"));
    if (crop == QLatin1String("1")) {
        // user wants to center crop the slides
        auto *filter = new Mlt::Filter(pCore->getCurrentProfile()->profile(), "crop");
        if ((filter != nullptr) && filter->is_valid()) {
            filter->set("center", 1);
            producer->attach(*filter);
        }
    }
}

void ClipLoadTask::run()
{
    // 2 channels interleaved of uchar values
    if (m_isCanceled) {
        cleanup();
        return;
    }
    pCore->getMonitor(Kdenlive::ClipMonitor)->resetPlayOrLoopZone(m_cid);
    QString resource = Xml::getXmlProperty(m_xml, QStringLiteral("resource"));
    qDebug()<<"============STARTING LOAD TASK FOR: "<<resource<<"\n\n:::::::::::::::::::";
    int duration = 0;
    ClipType::ProducerType type = static_cast<ClipType::ProducerType>(m_xml.attribute(QStringLiteral("type")).toInt());
    QString service = Xml::getXmlProperty(m_xml, QStringLiteral("mlt_service"));
    if (type == ClipType::Unknown) {
        type = getTypeForService(service, resource);
    }
    if (type == ClipType::Playlist && Xml::getXmlProperty(m_xml, QStringLiteral("kdenlive:proxy")).length() > 2) {
        // If this is a proxied playlist, load as AV
        type = ClipType::AV;
        service.clear();
    }
    std::shared_ptr<Mlt::Producer> producer;
    switch (type) {
    case ClipType::Color:
        producer = loadResource(resource, QStringLiteral("color:"));
        break;
    case ClipType::Text:
    case ClipType::TextTemplate: {
            bool ok = false;
            int producerLength = 0;
            QString pLength = Xml::getXmlProperty(m_xml, QStringLiteral("length"));
            if (pLength.isEmpty()) {
                producerLength = m_xml.attribute(QStringLiteral("length")).toInt();
            } else {
                producerLength = pLength.toInt(&ok);
            }
            producer = loadResource(resource, QStringLiteral("kdenlivetitle:"));

            if (!resource.isEmpty()) {
                if (!ok) {
                    producerLength = producer->time_to_frames(pLength.toUtf8().constData());
                }
                // Title from .kdenlivetitle file
                QFile txtfile(resource);
                QDomDocument txtdoc(QStringLiteral("titledocument"));
                if (txtfile.open(QIODevice::ReadOnly) && txtdoc.setContent(&txtfile)) {
                    txtfile.close();
                    if (txtdoc.documentElement().hasAttribute(QStringLiteral("duration"))) {
                        duration = txtdoc.documentElement().attribute(QStringLiteral("duration")).toInt();
                    } else if (txtdoc.documentElement().hasAttribute(QStringLiteral("out"))) {
                        duration = txtdoc.documentElement().attribute(QStringLiteral("out")).toInt();
                    }
                }
            } else {
                QString xmlDuration = Xml::getXmlProperty(m_xml, QStringLiteral("kdenlive:duration"));
                duration = xmlDuration.toInt(&ok);
                if (!ok) {
                    // timecode duration
                    duration = producer->time_to_frames(xmlDuration.toUtf8().constData());
                }
            }
            qDebug()<<"===== GOT PRODUCER DURATION: "<<duration<<"; PROD: "<<producerLength;
            if (duration <= 0) {
                if (producerLength > 0) {
                    duration = producerLength;
                } else {
                    duration = pCore->getDurationFromString(KdenliveSettings::title_duration());
                }
            }
            if (producerLength <= 0) {
                producerLength = duration;
            }
            producer->set("length", producerLength);
            producer->set("kdenlive:duration", duration);
            producer->set("out", producerLength - 1);
        }
        break;
    case ClipType::QText:
        producer = loadResource(resource, QStringLiteral("qtext:"));
        break;
    case ClipType::Qml: {
            bool ok;
            int producerLength = 0;
            QString pLength = Xml::getXmlProperty(m_xml, QStringLiteral("length"));
            if (pLength.isEmpty()) {
                producerLength = m_xml.attribute(QStringLiteral("length")).toInt();
            } else {
                producerLength = pLength.toInt(&ok);
            }
            if (producerLength <= 0) {
                producerLength = pCore->getDurationFromString(KdenliveSettings::title_duration());
            }
            producer = loadResource(resource, QStringLiteral("qml:"));
            producer->set("length", producerLength);
            producer->set("kdenlive:duration", producerLength);
            producer->set("out", producerLength - 1);
            break;
    }
    case ClipType::Playlist: {
        producer = loadPlaylist(resource);
        if (!m_errorMessage.isEmpty()) {
            QMetaObject::invokeMethod(pCore.get(), "displayBinMessage", Qt::QueuedConnection, Q_ARG(QString,m_errorMessage),
                                  Q_ARG(int, int(KMessageWidget::Warning)));
        }
        if (producer && resource.endsWith(QLatin1String(".kdenlive"))) {
            QFile f(resource);
            QDomDocument doc;
            doc.setContent(&f, false);
            f.close();
            QDomElement pl = doc.documentElement().firstChildElement(QStringLiteral("playlist"));
            if (!pl.isNull()) {
                QString offsetData = Xml::getXmlProperty(pl, QStringLiteral("kdenlive:docproperties.seekOffset"));
                if (offsetData.isEmpty() && Xml::getXmlProperty(pl, QStringLiteral("kdenlive:docproperties.version")) == QLatin1String("0.98")) {
                    offsetData = QStringLiteral("30000");
                }
                if (!offsetData.isEmpty()) {
                    bool ok = false;
                    int offset = offsetData.toInt(&ok);
                    if (ok) {
                        qDebug()<<" / / / FIXING OFFSET DATA: "<<offset;
                        offset = producer->get_playtime() - offset - 1;
                        producer->set("out", offset - 1);
                        producer->set("length", offset);
                        producer->set("kdenlive:duration", offset);
                    }
                } else {
                    qDebug()<<"// NO OFFSET DAT FOUND\n\n";
                }
            } else {
                qDebug()<<":_______\n______<nEMPTY PLAYLIST\n----";
            }
        }
        break;
    }
    case ClipType::SlideShow: {
        resource.prepend(QStringLiteral("qimage:"));
        producer = std::make_shared<Mlt::Producer>(pCore->getCurrentProfile()->profile(), nullptr, resource.toUtf8().constData());
        break;
    }
    default:
        if (!service.isEmpty()) {
            service.append(QChar(':'));
            producer = loadResource(resource, service);
        } else {
            producer = std::make_shared<Mlt::Producer>(pCore->getCurrentProfile()->profile(), nullptr, resource.toUtf8().constData());
        }
        break;
    }
    if (!producer || producer->is_blank() || !producer->is_valid()) {
        qCDebug(KDENLIVE_LOG) << " / / / / / / / / ERROR / / / / // CANNOT LOAD PRODUCER: " << resource;
        m_successful = false;
        if (producer) {
            producer.reset();
        }
        QMetaObject::invokeMethod(pCore.get(), "displayBinMessage", Qt::QueuedConnection, Q_ARG(QString, i18n("Cannot open file %1", resource)),
                                  Q_ARG(int, int(KMessageWidget::Warning)));
        m_errorMessage.append(i18n("ERROR: Could not load clip %1: producer is invalid", resource));
    }
    if (producer->get_length() == INT_MAX && producer->get("eof") == QLatin1String("loop")) {
        // This is a live source or broken clip
        if (producer) {
            producer.reset();
        }
        qDebug()<<"=== MAX DURATION: "<<INT_MAX<<", DURATION: "<<(INT_MAX / 25 / 60);
        QAction *ac = new QAction(i18n("Transcode"), m_object);
        QObject::connect(ac, &QAction::triggered, [&]() {
            pCore->transcodeFile(resource);
        });
        QList<QAction*>actions = {ac};
        
        QMetaObject::invokeMethod(pCore.get(), "displayBinMessage", Qt::QueuedConnection, Q_ARG(QString, i18n("Cannot get duration for file %1", resource)),
                                  Q_ARG(int, int(KMessageWidget::Warning)), Q_ARG(QList<QAction*>, actions));
        m_errorMessage.append(i18n("ERROR: Could not load clip %1: producer is invalid", resource));
        cleanup();
        return;
    }
    processProducerProperties(producer, m_xml);
    QString clipName = Xml::getXmlProperty(m_xml, QStringLiteral("kdenlive:clipname"));
    if (clipName.isEmpty()) {
        clipName = QFileInfo(Xml::getXmlProperty(m_xml, QStringLiteral("kdenlive:originalurl"))).fileName();
    }
    producer->set("kdenlive:clipname", clipName.toUtf8().constData());
    QString groupId = Xml::getXmlProperty(m_xml, QStringLiteral("kdenlive:folderid"));
    if (!groupId.isEmpty()) {
        producer->set("kdenlive:folderid", groupId.toUtf8().constData());
    }
    int clipOut = 0;
    if (m_xml.hasAttribute(QStringLiteral("out"))) {
        clipOut = m_xml.attribute(QStringLiteral("out")).toInt();
    }
    // setup length here as otherwise default length (currently 15000 frames in MLT) will be taken even if outpoint is larger
    const QString mltService = producer->get("mlt_service");
    QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(resource).name();
    const bool isGif = mime.contains(QLatin1String("image/gif"));
    if ((duration == 0 && (
                type == ClipType::Text || type == ClipType::TextTemplate || type == ClipType::QText
                || type == ClipType::Color || type == ClipType::Image || type == ClipType::SlideShow))
            || (isGif && mltService == QLatin1String("qimage"))) {
        int length;
        if (m_xml.hasAttribute(QStringLiteral("length"))) {
            length = m_xml.attribute(QStringLiteral("length")).toInt();
            clipOut = qMax(1, length - 1);
        } else {
            if(isGif && mltService == QLatin1String("qimage")) {
                length = pCore->getDurationFromString(KdenliveSettings::image_duration());
                clipOut = qMax(1, length - 1);
            } else {
                length = Xml::getXmlProperty(m_xml, QStringLiteral("length")).toInt();
                clipOut -= m_xml.attribute(QStringLiteral("in")).toInt();
                if (length < clipOut) {
                    length = clipOut == 1 ? 1 : clipOut + 1;
                }
            }

        }
        // Pass duration if it was forced
        if (m_xml.hasAttribute(QStringLiteral("duration"))) {
            duration = m_xml.attribute(QStringLiteral("duration")).toInt();
            if (length < duration) {
                length = duration;
                if (clipOut > 0) {
                    clipOut = length - 1;
                }
            }
        }
        if (duration == 0) {
            duration = length;
        }
        producer->set("length", producer->frames_to_time(length, mlt_time_clock));
        int kdenlive_duration = producer->time_to_frames(Xml::getXmlProperty(m_xml, QStringLiteral("kdenlive:duration")).toUtf8().constData());
        if (kdenlive_duration > 0) {
            producer->set("kdenlive:duration", producer->frames_to_time(kdenlive_duration, mlt_time_clock));
        } else {
            producer->set("kdenlive:duration", producer->get("length"));
        }
    }
    if (clipOut > 0) {
        producer->set_in_and_out(m_xml.attribute(QStringLiteral("in")).toInt(), clipOut);
    }

    if (m_xml.hasAttribute(QStringLiteral("templatetext"))) {
        producer->set("templatetext", m_xml.attribute(QStringLiteral("templatetext")).toUtf8().constData());
    }
    if (type == ClipType::SlideShow) {
        processSlideShow(producer);
    }

    int vindex = -1;
    double fps = -1;
    if (mltService == QLatin1String("xml") || mltService == QLatin1String("consumer")) {
        // MLT playlist, create producer with blank profile to get real profile info
        QString tmpPath = resource;
        if (tmpPath.startsWith(QLatin1String("consumer:"))) {
            tmpPath = "xml:" + tmpPath.section(QLatin1Char(':'), 1);
        }
        Mlt::Profile original_profile;
        std::unique_ptr<Mlt::Producer> tmpProd(new Mlt::Producer(original_profile, nullptr, tmpPath.toUtf8().constData()));
        original_profile.set_explicit(1);
        double originalFps = original_profile.fps();
        fps = originalFps;
        if (originalFps > 0 && !qFuzzyCompare(originalFps, pCore->getCurrentFps())) {
            int originalLength = tmpProd->get_length();
            int fixedLength = int(originalLength * pCore->getCurrentFps() / originalFps);
            producer->set("length", fixedLength);
            producer->set("out", fixedLength - 1);
        }
    } else if (mltService == QLatin1String("avformat")) {
        // Check if file is seekable
        bool seekable = producer->get_int("seekable");
        if (!seekable) {
            producer->set("kdenlive:transcodingrequired", 1);
            qDebug()<<"================0\n\nFOUND UNSEEKABLE FILE: "<<producer->get("resource")<<"\n\n===================";
        }
        // check if there are multiple streams
        vindex = producer->get_int("video_index");
        // List streams
        int streams = producer->get_int("meta.media.nb_streams");
        QList<int> audio_list, video_list;
        for (int i = 0; i < streams; ++i) {
            QByteArray propertyName = QStringLiteral("meta.media.%1.stream.type").arg(i).toLocal8Bit();
            QString stype = producer->get(propertyName.data());
            if (stype == QLatin1String("audio")) {
                audio_list.append(i);
            } else if (stype == QLatin1String("video")) {
                video_list.append(i);
            }
        }

        if (vindex > -1) {
            char property[200];
            snprintf(property, sizeof(property), "meta.media.%d.stream.frame_rate", vindex);
            fps = producer->get_double(property);
        }

        if (fps <= 0) {
            if (producer->get_double("meta.media.frame_rate_den") > 0) {
                fps = producer->get_double("meta.media.frame_rate_num") / producer->get_double("meta.media.frame_rate_den");
            } else {
                fps = producer->get_double("source_fps");
            }
        }
    }
    if (fps <= 0 && type == ClipType::Unknown) {
        // something wrong, maybe audio file with embedded image
        if (mime.startsWith(QLatin1String("audio"))) {
            producer->set("video_index", -1);
            vindex = -1;
        }
    }
    auto binClip = pCore->projectItemModel()->getClipByBinID(m_cid);
    if (binClip) {
        QMetaObject::invokeMethod(binClip.get(), "setProducer", Qt::QueuedConnection, Q_ARG(std::shared_ptr<Mlt::Producer>,producer),
                                  Q_ARG(bool , true));
        //binClip->setProducer(producer, true);
    }
    
    cleanup();
}
