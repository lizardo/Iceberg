/*
    This file is part of Icecream.

    Copyright (c) 2003 Frerich Raabe <raabe@kde.org>
    Copyright (c) 2003,2004 Stephan Kulow <coolo@kde.org>
    Copyright (c) 2003,2004 Cornelius Schumacher <schumacher@kde.org>
    Copyright (c) 2007 Dirk Mueller <mueller@kde.org>
    Copyright (c) 2011 Hugo Parente Lima <hugo.pl@gmail.com>
    Copyright (c) 2011 Anselmo L. S. Melo <anselmolsm@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "monitor.h"

#include "hostinfo.h"
#include "statusview.h"

#include <icecc/comm.h>
#include <QSocketNotifier>
#include <QTimer>

#include <list>
#include <iostream>
#include <ctime>

using namespace std;

Monitor::Monitor(HostInfoManager* m, QObject *parent)
    : QObject(parent),
      m_hostInfoManager(m),
      m_view(0),
      m_scheduler(0),
      m_schedulerOnline(false),
      m_discover(0),
      m_fd_notify(0),
      m_fd_type(QSocketNotifier::Exception)
{
    qsrand(std::time(0));
}

Monitor::~Monitor()
{
    delete m_scheduler;
    delete m_discover;
}

void Monitor::checkScheduler(bool deleteit)
{
    int delay = 0;
    if (deleteit) {
        m_rememberedJobs.clear();

        delete m_scheduler;
        m_scheduler = 0;

        delete m_fd_notify;
        m_fd_notify = 0;
        m_fd_type = QSocketNotifier::Exception;

        delete m_discover;
        m_discover = 0;

        setSchedulerState(false);
        delay = 1000 + (qrand() & 1023);
    } else if (m_scheduler)
        return;
    QTimer::singleShot(delay, this, SLOT(slotCheckScheduler()));
}

void Monitor::registerNotify(int fd, QSocketNotifier::Type type, const char* slot)
{
    if (m_fd_notify) {
        m_fd_notify->disconnect(this);
        m_fd_notify->deleteLater();
    }
    m_fd_notify = new QSocketNotifier(fd, type, this);
    m_fd_type = type;
    QObject::connect(m_fd_notify, SIGNAL(activated(int)), slot);
}

void Monitor::slotCheckScheduler()
{
    if (m_scheduler)
        return;

    list<string> names;

    if (!m_currentNetName.isEmpty())
        names.push_front(m_currentNetName.data());
    else
        names.push_front("ICECREAM");

    if (!qgetenv("USE_SCHEDULER").isEmpty())
        names.push_front(""); // try $USE_SCHEDULER

    for (list<string>::const_iterator it = names.begin(); it != names.end(); ++it) {
        m_currentNetName = it->c_str();
        if (!m_discover || m_discover->timed_out()) {
            delete m_discover;
            m_discover = new DiscoverSched (m_currentNetName.data());
        }

        m_scheduler = m_discover->try_get_scheduler();

        if (m_scheduler) {
            m_hostInfoManager->setSchedulerName(QString::fromLatin1(m_discover->schedulerName().data()));
            m_hostInfoManager->setNetworkName(QString::fromLatin1(m_discover->networkName().data()));
            m_scheduler->setBulkTransfer();

            delete m_discover;
            m_discover = 0;

            registerNotify(m_scheduler->fd, QSocketNotifier::Read, SLOT(msgReceived()));

            if (!m_scheduler->send_msg(MonLoginMsg())) {
                checkScheduler(true);
                QTimer::singleShot(0, this, SLOT(slotCheckScheduler()));
            }
            else {
                setSchedulerState(true);
            }
            return;
        }

        if (m_fd_type != QSocketNotifier::Write && m_discover->connect_fd() >= 0) {
            registerNotify(m_discover->connect_fd(),
                           QSocketNotifier::Write, SLOT(slotCheckScheduler()));
            return;
        }
        else if (m_fd_type != QSocketNotifier::Read && m_discover->listen_fd() >= 0) {
                registerNotify(m_discover->listen_fd(),
                               QSocketNotifier::Read, SLOT(slotCheckScheduler()));
        }

        if (m_fd_type == QSocketNotifier::Read)
            QTimer::singleShot(1000 + (qrand() & 1023), this, SLOT(slotCheckScheduler()));
    }
    setSchedulerState(false);
}

void Monitor::msgReceived()
{
    while (!m_scheduler->read_a_bit() || m_scheduler->has_msg()) {
        if (!handleActivity())
            break;
    }
}

bool Monitor::handleActivity()
{
    Msg *m = m_scheduler->get_msg();
    if (!m) {
        checkScheduler(true);
        setSchedulerState(false);
        return false;
    }

    switch (m->type) {
    case M_MON_GET_CS:
        handleGetcs(m);
        break;
    case M_MON_JOB_BEGIN:
        handleJobBegin(m);
        break;
    case M_MON_JOB_DONE:
        handleJobDone(m);
        break;
    case M_END:
        checkScheduler(true);
        break;
    case M_MON_STATS:
        handleStats(m);
        break;
    case M_MON_LOCAL_JOB_BEGIN:
        handleLocalBegin(m);
        break;
    case M_JOB_LOCAL_DONE:
        handleLocalDone(m);
        break;
    default:
        break;
    }
    delete m;
    return true;
}

void Monitor::handleGetcs(Msg* _m)
{
    MonGetCSMsg *m = dynamic_cast<MonGetCSMsg*>(_m);
    if (!m)
        return;

    m_rememberedJobs[m->job_id] = Job(m->job_id, m->clientid,
                                      m->filename.c_str(),
                                      m->lang == CompileJob::Lang_C ? "C" : "C++");
    m_view->update(m_rememberedJobs[m->job_id]);
}

void Monitor::handleLocalBegin(Msg* _m)
{
    MonLocalJobBeginMsg *m = dynamic_cast<MonLocalJobBeginMsg*>(_m);
    if (!m)
        return;

    m_rememberedJobs[m->job_id] = Job(m->job_id, m->hostid,
                                      m->file.c_str(), "C++");
    m_rememberedJobs[m->job_id].setState(Job::LocalOnly);
    m_view->update(m_rememberedJobs[m->job_id]);
}

void Monitor::handleLocalDone(Msg* _m)
{
    JobLocalDoneMsg *m = dynamic_cast<JobLocalDoneMsg*>(_m);
    if (!m)
        return;

    JobList::iterator it = m_rememberedJobs.find(m->job_id);
    if (it == m_rememberedJobs.end()) {
        // we started in between
        return;
    }

    it->setState(Job::Finished);
    m_view->update(*it);

    if (m_rememberedJobs.size() > 3000) { // now remove 1000
        uint count = 1000;

        while (--count)
            m_rememberedJobs.erase(m_rememberedJobs.begin());
    }
}

void Monitor::handleStats(Msg* _m)
{
    MonStatsMsg *m = dynamic_cast<MonStatsMsg*>(_m);
    if (!m)
        return;

    QStringList statmsg = QString(m->statmsg.c_str()).split('\n');
    HostInfo::StatsMap stats;
    QStringList::ConstIterator it;
    for (it = statmsg.constBegin(); it != statmsg.constEnd(); ++it) {
        QString key = *it;
        key = key.left(key.indexOf(':'));
        QString value = *it;
        value = value.mid(value.indexOf(':') + 1);
        stats[key] = value;
    }

    HostInfo *hostInfo = m_hostInfoManager->checkNode(m->hostid, stats);
    if (hostInfo->isOffline())
        m_view->removeNode(m->hostid);
    else
        m_view->checkNode(m->hostid);
}

void Monitor::handleJobBegin(Msg* _m)
{
    MonJobBeginMsg *m = dynamic_cast<MonJobBeginMsg*>(_m);
    if (!m)
        return;

    JobList::iterator it = m_rememberedJobs.find(m->job_id);
    if (it == m_rememberedJobs.end()) {
        // we started in between
        return;
    }

    it->setServer(m->hostid);
    it->setStartTime(QDateTime::fromTime_t(m->stime));
    it->setState(Job::Compiling);

    m_view->update(*it);
}

void Monitor::handleJobDone(Msg* _m)
{
    MonJobDoneMsg *m = dynamic_cast<MonJobDoneMsg*>(_m);
    if (!m)
        return;

    JobList::iterator it = m_rememberedJobs.find(m->job_id);
    if (it == m_rememberedJobs.end()) {
        // we started in between
        return;
    }

    it->setExitCode(m->exitcode);
    if (m->exitcode) {
        it->setState(Job::Failed);
    } else {
        it->setState(Job::Finished);
        it->setExecInfo(m->real_msec, m->user_msec, m->sys_msec, m->pfaults);
        it->setSizes(m->in_compressed, m->in_uncompressed,
                     m->out_compressed, m->out_uncompressed);
    }

    m_view->update(*it);
}

void Monitor::setCurrentView(StatusView* view, bool rememberJobs)
{
    m_view = view;
    m_view->updateSchedulerState(m_schedulerOnline);

    if (rememberJobs) {
        JobList::ConstIterator it = m_rememberedJobs.constBegin();
        for (; it != m_rememberedJobs.constEnd(); ++it)
            m_view->update(*it);
    }
}

void Monitor::setSchedulerState(bool online)
{
    if (m_schedulerOnline == online)
        return;

    m_schedulerOnline = online;
    m_view->updateSchedulerState(online);
}

#include "monitor.moc"
