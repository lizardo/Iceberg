/*
    This file is part of Icecream.

    Copyright (c) 2004-2006 Andre Wöbbeking <Woebbeking@web.de>
    Copyright (c) 2011 Hugo Parente Lima <hugo.pl@gmail.com>

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

#include "hostlistview.h"

#include <QLocale>
#include <QFontMetrics>
#include <QPainter>
#include <QApplication>

enum Columns
{
    ColumnID,
    ColumnName,
    ColumnColor,
    ColumnIP,
    ColumnPlatform,
    ColumnMaxJobs,
    ColumnSpeed,
    ColumnLoad
};


HostListViewItem::HostListViewItem(QTreeWidget* parent, const HostInfo& info)
    :  QTreeWidgetItem(parent),
       m_hostInfo(info),
       m_active(false)
{
    updateText(info);
}


const HostInfo& HostListViewItem::hostInfo() const
{
    return m_hostInfo;
}


void HostListViewItem::setActiveNode(bool active)
{
    m_active = active;
    //repaint();
}


void HostListViewItem::updateText(const HostInfo& info)
{
    m_hostInfo = info;
    setText(ColumnID, QString::number(info.id()));
    setText(ColumnName, info.name());
    setText(ColumnColor, HostInfo::colorName(info.color()));
    setText(ColumnIP, info.ip());
    setText(ColumnPlatform, info.platform());
    setText(ColumnMaxJobs, QString::number(info.maxJobs()));
    setText(ColumnSpeed, QLocale::c().toString(info.serverSpeed()));
    setText(ColumnLoad, QString::number(info.serverLoad()));
}


bool HostListViewItem::operator<(const QTreeWidgetItem& item) const
{
    const HostListViewItem* other = dynamic_cast<const HostListViewItem *>(&item);
    const int col = (treeWidget() ? treeWidget()->sortColumn() : 0);

    switch (col) {
    case ColumnID:
        return m_hostInfo.id() < other->m_hostInfo.id();
    case ColumnMaxJobs:
        return m_hostInfo.maxJobs() < other->m_hostInfo.maxJobs();
    case ColumnSpeed:
        return m_hostInfo.serverSpeed() < other->m_hostInfo.serverSpeed();
    case ColumnLoad:
        return m_hostInfo.serverLoad() < other->m_hostInfo.serverLoad();
    default:
        return text(col) < other->text(col);
    }
}


void HostListViewItem::paintCell(QPainter* painter, const QColorGroup& cg,
                                  int column, int width, int align)
{
    const QFont oldFont(painter->font());

    if (m_active) {
        QFont font(oldFont);
        font.setBold(true);
        painter->setFont(font);
    }

    //QTreeWidgetItem::paintCell(painter, cg, column, width, align);
    painter->setFont(oldFont);
}


int HostListViewItem::width(const QFontMetrics& fm, const QTreeWidget* lv, int column) const
{
    int width = 0;
    if (m_active) {
        QFont font(lv->font());
        font.setBold(true);
        const QFontMetrics metrics(font);
        width = metrics.width(text(column)) + /*lv->itemMargin()*/ 0 * 2 + 2;
    } else {
        //width = QTreeWidgetItem::width(fm, lv, column);
    }

    return width;
}


HostListView::HostListView(HostInfoManager* manager, QWidget* parent)
    : QTreeWidget(parent),
      m_hostInfoManager(manager),
      m_activeNode(0)
{
    qApp->setStyleSheet("QTreeView::branch { border-image: none; image: none }");

    QStringList headers;
    headers << tr("ID") << tr("Name") << tr("Color") << tr("IP") << tr("Platform")
            << tr("Max Jobs") << tr("Speed") << tr("Load");
    setHeaderLabels(headers);

    /*setColumnAlignment(ColumnID, Qt::AlignRight);
    setColumnAlignment(ColumnMaxJobs, Qt::AlignRight);
    setColumnAlignment(ColumnSpeed, Qt::AlignRight);
    setColumnAlignment(ColumnLoad, Qt::AlignRight);*/

    setAllColumnsShowFocus(true);

    connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)),
            this, SLOT(slotNodeActivated(QTreeWidgetItem*)));
    connect(&m_updateSortTimer, SIGNAL(timeout()), SLOT(updateSort()));
}


unsigned int HostListView::activeNode() const
{
    return m_activeNode;
}


void HostListView::setActiveNode(unsigned int hostid)
{
    if (m_activeNode == hostid)
        return;

    setActiveNode(m_activeNode, false);
    setActiveNode(hostid, true);

    m_activeNode = hostid;
    emit nodeActivated(hostid);
}


void HostListView::checkNode(unsigned int hostid)
{
    const HostInfo* info = m_hostInfoManager->find(hostid);
    if (!info)
        return;

    ItemMap::iterator it = m_items.find(hostid);
    if (it == m_items.end()) {
        if (!info->name().isEmpty())
            m_items[hostid] = new HostListViewItem(this, *info);
    }
    else
        (*it)->updateText(*info);

    m_updateSortTimer.setSingleShot(true);
    m_updateSortTimer.start(0);
}


void HostListView::removeNode(unsigned int hostid)
{
    ItemMap::iterator it = m_items.find(hostid);
    if (it != m_items.end()) {
        delete *it;
        m_items.erase(it);
    }

    if (hostid == m_activeNode)
        setActiveNode(0);
}


void HostListView::clear()
{
    m_items.clear();
    QTreeWidget::clear();
    setActiveNode(0);
}


void HostListView::slotNodeActivated(QTreeWidgetItem* item)
{
    HostListViewItem* hostItem = dynamic_cast<HostListViewItem*>(item);
    if (hostItem)
        setActiveNode(hostItem->hostInfo().id());
}


void HostListView::setActiveNode(unsigned int hostid, bool active)
{
    ItemMap::iterator it = m_items.find(hostid);
    if (it != m_items.end())
        (*it)->setActiveNode(active);
}

void HostListView::updateSort()
{
    if(sortColumn() != 0)
        sortItems(sortColumn(), Qt::AscendingOrder);
}

#include "hostlistview.moc"
