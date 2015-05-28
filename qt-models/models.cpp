/*
 * models.cpp
 *
 * classes for the equipment models of Subsurface
 *
 */
#include "models.h"
#include "diveplanner.h"
#include "mainwindow.h"
#include "helpers.h"
#include "dive.h"
#include "device.h"
#include "statistics.h"
#include "qthelper.h"
#include "gettextfromc.h"
#include "display.h"
#include "color.h"
#include "cleanertablemodel.h"
#include "weigthsysteminfomodel.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QColor>
#include <QBrush>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QStringListModel>

// initialize the trash icon if necessary

const QPixmap &trashIcon()
{
	static QPixmap trash = QPixmap(":trash").scaledToHeight(defaultIconMetrics().sz_small);
	return trash;
}

//#################################################################################################
//#
//#	Tree Model - a Basic Tree Model so I don't need to kill myself repeating this for every model.
//#
//#################################################################################################

/*! A DiveItem for use with a DiveTripModel
 *
 * A simple class which wraps basic stats for a dive (e.g. duration, depth) and
 * tidies up after it's children. This is done manually as we don't inherit from
 * QObject.
 *
*/

TreeItem::TreeItem()
{
	parent = NULL;
}

TreeItem::~TreeItem()
{
	qDeleteAll(children);
}

Qt::ItemFlags TreeItem::flags(const QModelIndex &index) const
{
	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

int TreeItem::row() const
{
	if (parent)
		return parent->children.indexOf(const_cast<TreeItem *>(this));
	return 0;
}

QVariant TreeItem::data(int column, int role) const
{
	return QVariant();
}

TreeModel::TreeModel(QObject *parent) : QAbstractItemModel(parent)
{
	columns = 0; // I'm not sure about this one - I can't see where it gets initialized
	rootItem = new TreeItem();
}

TreeModel::~TreeModel()
{
	delete rootItem;
}

QVariant TreeModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return QVariant();

	TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
	QVariant val = item->data(index.column(), role);

	if (role == Qt::FontRole && !val.isValid())
		return defaultModelFont();
	else
		return val;
}

bool TreeItem::setData(const QModelIndex &index, const QVariant &value, int role)
{
	return false;
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex &parent) const
{
	if (!hasIndex(row, column, parent))
		return QModelIndex();

	TreeItem *parentItem = (!parent.isValid()) ? rootItem : static_cast<TreeItem *>(parent.internalPointer());

	TreeItem *childItem = parentItem->children[row];

	return (childItem) ? createIndex(row, column, childItem) : QModelIndex();
}

QModelIndex TreeModel::parent(const QModelIndex &index) const
{
	if (!index.isValid())
		return QModelIndex();

	TreeItem *childItem = static_cast<TreeItem *>(index.internalPointer());
	TreeItem *parentItem = childItem->parent;

	if (parentItem == rootItem || !parentItem)
		return QModelIndex();

	return createIndex(parentItem->row(), 0, parentItem);
}

int TreeModel::rowCount(const QModelIndex &parent) const
{
	TreeItem *parentItem;

	if (!parent.isValid())
		parentItem = rootItem;
	else
		parentItem = static_cast<TreeItem *>(parent.internalPointer());

	int amount = parentItem->children.count();
	return amount;
}

int TreeModel::columnCount(const QModelIndex &parent) const
{
	return columns;
}

/*################################################################
 *
 *  Implementation of the Dive List.
 *
 * ############################################################### */
struct TripItem : public TreeItem {
	virtual QVariant data(int column, int role) const;
	dive_trip_t *trip;
};

QVariant TripItem::data(int column, int role) const
{
	QVariant ret;

	if (role == DiveTripModel::TRIP_ROLE)
		return QVariant::fromValue<void *>(trip);

	if (role == DiveTripModel::SORT_ROLE)
		return (qulonglong)trip->when;

	if (role == Qt::DisplayRole) {
		switch (column) {
		case DiveTripModel::NR:
			QString shownText;
			struct dive *d = trip->dives;
			int countShown = 0;
			while (d) {
				if (!d->hidden_by_filter)
					countShown++;
				d = d->next;
			}
			if (countShown < trip->nrdives)
				shownText = tr(" (%1 shown)").arg(countShown);
			if (trip->location && *trip->location)
				ret = QString(trip->location) + ", " + get_trip_date_string(trip->when, trip->nrdives) + shownText;
			else
				ret = get_trip_date_string(trip->when, trip->nrdives) + shownText;
			break;
		}
	}

	return ret;
}

static int nitrox_sort_value(struct dive *dive)
{
	int o2, he, o2max;
	get_dive_gas(dive, &o2, &he, &o2max);
	return he * 1000 + o2;
}

static QVariant dive_table_alignment(int column)
{
	QVariant retVal;
	switch (column) {
	case DiveTripModel::DEPTH:
	case DiveTripModel::DURATION:
	case DiveTripModel::TEMPERATURE:
	case DiveTripModel::TOTALWEIGHT:
	case DiveTripModel::SAC:
	case DiveTripModel::OTU:
	case DiveTripModel::MAXCNS:
		// Right align numeric columns
		retVal = int(Qt::AlignRight | Qt::AlignVCenter);
		break;
	// NR needs to be left aligned becase its the indent marker for trips too
	case DiveTripModel::NR:
	case DiveTripModel::DATE:
	case DiveTripModel::RATING:
	case DiveTripModel::SUIT:
	case DiveTripModel::CYLINDER:
	case DiveTripModel::GAS:
	case DiveTripModel::LOCATION:
		retVal = int(Qt::AlignLeft | Qt::AlignVCenter);
		break;
	}
	return retVal;
}

QVariant DiveItem::data(int column, int role) const
{
	QVariant retVal;
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive)
		return QVariant();

	switch (role) {
	case Qt::TextAlignmentRole:
		retVal = dive_table_alignment(column);
		break;
	case DiveTripModel::SORT_ROLE:
		Q_ASSERT(dive != NULL);
		switch (column) {
		case NR:
			retVal = (qulonglong)dive->when;
			break;
		case DATE:
			retVal = (qulonglong)dive->when;
			break;
		case RATING:
			retVal = dive->rating;
			break;
		case DEPTH:
			retVal = dive->maxdepth.mm;
			break;
		case DURATION:
			retVal = dive->duration.seconds;
			break;
		case TEMPERATURE:
			retVal = dive->watertemp.mkelvin;
			break;
		case TOTALWEIGHT:
			retVal = total_weight(dive);
			break;
		case SUIT:
			retVal = QString(dive->suit);
			break;
		case CYLINDER:
			retVal = QString(dive->cylinder[0].type.description);
			break;
		case GAS:
			retVal = nitrox_sort_value(dive);
			break;
		case SAC:
			retVal = dive->sac;
			break;
		case OTU:
			retVal = dive->otu;
			break;
		case MAXCNS:
			retVal = dive->maxcns;
			break;
		case LOCATION:
			retVal = QString(get_dive_location(dive));
			break;
		}
		break;
	case Qt::DisplayRole:
		Q_ASSERT(dive != NULL);
		switch (column) {
		case NR:
			retVal = dive->number;
			break;
		case DATE:
			retVal = displayDate();
			break;
		case DEPTH:
			retVal = displayDepth();
			break;
		case DURATION:
			retVal = displayDuration();
			break;
		case TEMPERATURE:
			retVal = displayTemperature();
			break;
		case TOTALWEIGHT:
			retVal = displayWeight();
			break;
		case SUIT:
			retVal = QString(dive->suit);
			break;
		case CYLINDER:
			retVal = QString(dive->cylinder[0].type.description);
			break;
		case SAC:
			retVal = displaySac();
			break;
		case OTU:
			retVal = dive->otu;
			break;
		case MAXCNS:
			retVal = dive->maxcns;
			break;
		case LOCATION:
			retVal = QString(get_dive_location(dive));
			break;
		case GAS:
			const char *gas_string = get_dive_gas_string(dive);
			retVal = QString(gas_string);
			free((void*)gas_string);
			break;
		}
		break;
	case Qt::ToolTipRole:
		switch (column) {
		case NR:
			retVal = tr("#");
			break;
		case DATE:
			retVal = tr("Date");
			break;
		case RATING:
			retVal = tr("Rating");
			break;
		case DEPTH:
			retVal = tr("Depth(%1)").arg((get_units()->length == units::METERS) ? tr("m") : tr("ft"));
			break;
		case DURATION:
			retVal = tr("Duration");
			break;
		case TEMPERATURE:
			retVal = tr("Temp(%1%2)").arg(UTF8_DEGREE).arg((get_units()->temperature == units::CELSIUS) ? "C" : "F");
			break;
		case TOTALWEIGHT:
			retVal = tr("Weight(%1)").arg((get_units()->weight == units::KG) ? tr("kg") : tr("lbs"));
			break;
		case SUIT:
			retVal = tr("Suit");
			break;
		case CYLINDER:
			retVal = tr("Cyl");
			break;
		case GAS:
			retVal = tr("Gas");
			break;
		case SAC:
			const char *unit;
			get_volume_units(0, NULL, &unit);
			retVal = tr("SAC(%1)").arg(QString(unit).append(tr("/min")));
			break;
		case OTU:
			retVal = tr("OTU");
			break;
		case MAXCNS:
			retVal = tr("Max CNS");
			break;
		case LOCATION:
			retVal = tr("Location");
			break;
		}
		break;
	}

	if (role == DiveTripModel::STAR_ROLE) {
		Q_ASSERT(dive != NULL);
		retVal = dive->rating;
	}
	if (role == DiveTripModel::DIVE_ROLE) {
		retVal = QVariant::fromValue<void *>(dive);
	}
	if (role == DiveTripModel::DIVE_IDX) {
		Q_ASSERT(dive != NULL);
		retVal = get_divenr(dive);
	}
	return retVal;
}

Qt::ItemFlags DiveItem::flags(const QModelIndex &index) const
{
	if (index.column() == NR) {
		return TreeItem::flags(index) | Qt::ItemIsEditable;
	}
	return TreeItem::flags(index);
}

bool DiveItem::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (role != Qt::EditRole)
		return false;
	if (index.column() != NR)
		return false;

	int v = value.toInt();
	if (v == 0)
		return false;

	int i;
	struct dive *d;
	for_each_dive (i, d) {
		if (d->number == v)
			return false;
	}
	d = get_dive_by_uniq_id(diveId);
	d->number = value.toInt();
	mark_divelist_changed(true);
	return true;
}

QString DiveItem::displayDate() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_dive_date_string(dive->when);
}

QString DiveItem::displayDepth() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_depth_string(dive->maxdepth);
}

QString DiveItem::displayDepthWithUnit() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	return get_depth_string(dive->maxdepth, true);
}

QString DiveItem::displayDuration() const
{
	int hrs, mins, fullmins, secs;
	struct dive *dive = get_dive_by_uniq_id(diveId);
	mins = (dive->duration.seconds + 59) / 60;
	fullmins = dive->duration.seconds / 60;
	secs = dive->duration.seconds - 60 * fullmins;
	hrs = mins / 60;
	mins -= hrs * 60;

	QString displayTime;
	if (hrs)
		displayTime = QString("%1:%2").arg(hrs).arg(mins, 2, 10, QChar('0'));
	else if (mins < 15 || dive->dc.divemode == FREEDIVE)
		displayTime = QString("%1m%2s").arg(fullmins).arg(secs, 2, 10, QChar('0'));
	else
		displayTime = QString("%1").arg(mins);
	return displayTime;
}

QString DiveItem::displayTemperature() const
{
	QString str;
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (!dive->watertemp.mkelvin)
		return str;
	if (get_units()->temperature == units::CELSIUS)
		str = QString::number(mkelvin_to_C(dive->watertemp.mkelvin), 'f', 1);
	else
		str = QString::number(mkelvin_to_F(dive->watertemp.mkelvin), 'f', 1);
	return str;
}

QString DiveItem::displaySac() const
{
	QString str;
	struct dive *dive = get_dive_by_uniq_id(diveId);
	if (dive->sac) {
		const char *unit;
		int decimal;
		double value = get_volume_units(dive->sac, &decimal, &unit);
		return QString::number(value, 'f', decimal);
	}
	return QString("");
}

QString DiveItem::displayWeight() const
{
	QString str = weight_string(weight());
	return str;
}

int DiveItem::weight() const
{
	struct dive *dive = get_dive_by_uniq_id(diveId);
	weight_t tw = { total_weight(dive) };
	return tw.grams;
}

DiveTripModel::DiveTripModel(QObject *parent) : TreeModel(parent)
{
	columns = COLUMNS;
}

Qt::ItemFlags DiveTripModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return 0;

	TripItem *item = static_cast<TripItem *>(index.internalPointer());
	return item->flags(index);
}

QVariant DiveTripModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	QVariant ret;
	if (orientation == Qt::Vertical)
		return ret;

	switch (role) {
	case Qt::TextAlignmentRole:
		ret = dive_table_alignment(section);
		break;
	case Qt::FontRole:
		ret = defaultModelFont();
		break;
	case Qt::DisplayRole:
		switch (section) {
		case NR:
			ret = tr("#");
			break;
		case DATE:
			ret = tr("Date");
			break;
		case RATING:
			ret = tr("Rating");
			break;
		case DEPTH:
			ret = tr("Depth");
			break;
		case DURATION:
			ret = tr("Duration");
			break;
		case TEMPERATURE:
			ret = tr("Temp");
			break;
		case TOTALWEIGHT:
			ret = tr("Weight");
			break;
		case SUIT:
			ret = tr("Suit");
			break;
		case CYLINDER:
			ret = tr("Cyl");
			break;
		case GAS:
			ret = tr("Gas");
			break;
		case SAC:
			ret = tr("SAC");
			break;
		case OTU:
			ret = tr("OTU");
			break;
		case MAXCNS:
			ret = tr("Max CNS");
			break;
		case LOCATION:
			ret = tr("Location");
			break;
		}
		break;
	case Qt::ToolTipRole:
		switch (section) {
		case NR:
			ret = tr("#");
			break;
		case DATE:
			ret = tr("Date");
			break;
		case RATING:
			ret = tr("Rating");
			break;
		case DEPTH:
			ret = tr("Depth(%1)").arg((get_units()->length == units::METERS) ? tr("m") : tr("ft"));
			break;
		case DURATION:
			ret = tr("Duration");
			break;
		case TEMPERATURE:
			ret = tr("Temp(%1%2)").arg(UTF8_DEGREE).arg((get_units()->temperature == units::CELSIUS) ? "C" : "F");
			break;
		case TOTALWEIGHT:
			ret = tr("Weight(%1)").arg((get_units()->weight == units::KG) ? tr("kg") : tr("lbs"));
			break;
		case SUIT:
			ret = tr("Suit");
			break;
		case CYLINDER:
			ret = tr("Cyl");
			break;
		case GAS:
			ret = tr("Gas");
			break;
		case SAC:
			const char *unit;
			get_volume_units(0, NULL, &unit);
			ret = tr("SAC(%1)").arg(QString(unit).append(tr("/min")));
			break;
		case OTU:
			ret = tr("OTU");
			break;
		case MAXCNS:
			ret = tr("Max CNS");
			break;
		case LOCATION:
			ret = tr("Location");
			break;
		}
		break;
	}

	return ret;
}

void DiveTripModel::setupModelData()
{
	int i = dive_table.nr;

	if (rowCount()) {
		beginRemoveRows(QModelIndex(), 0, rowCount() - 1);
		endRemoveRows();
	}

	if (autogroup)
		autogroup_dives();
	dive_table.preexisting = dive_table.nr;
	while (--i >= 0) {
		struct dive *dive = get_dive(i);
		update_cylinder_related_info(dive);
		dive_trip_t *trip = dive->divetrip;

		DiveItem *diveItem = new DiveItem();
		diveItem->diveId = dive->id;

		if (!trip || currentLayout == LIST) {
			diveItem->parent = rootItem;
			rootItem->children.push_back(diveItem);
			continue;
		}
		if (currentLayout == LIST)
			continue;

		if (!trips.keys().contains(trip)) {
			TripItem *tripItem = new TripItem();
			tripItem->trip = trip;
			tripItem->parent = rootItem;
			tripItem->children.push_back(diveItem);
			trips[trip] = tripItem;
			rootItem->children.push_back(tripItem);
			continue;
		}
		TripItem *tripItem = trips[trip];
		tripItem->children.push_back(diveItem);
	}

	if (rowCount()) {
		beginInsertRows(QModelIndex(), 0, rowCount() - 1);
		endInsertRows();
	}
}

DiveTripModel::Layout DiveTripModel::layout() const
{
	return currentLayout;
}

void DiveTripModel::setLayout(DiveTripModel::Layout layout)
{
	currentLayout = layout;
	setupModelData();
}

bool DiveTripModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	TreeItem *item = static_cast<TreeItem *>(index.internalPointer());
	DiveItem *diveItem = dynamic_cast<DiveItem *>(item);
	if (!diveItem)
		return false;
	return diveItem->setData(index, value, role);
}

/*####################################################################
 *
 * Dive Computer Model
 *
 *####################################################################
 */

DiveComputerModel::DiveComputerModel(QMultiMap<QString, DiveComputerNode> &dcMap, QObject *parent) : CleanerTableModel()
{
	setHeaderDataStrings(QStringList() << "" << tr("Model") << tr("Device ID") << tr("Nickname"));
	dcWorkingMap = dcMap;
	numRows = 0;
}

QVariant DiveComputerModel::data(const QModelIndex &index, int role) const
{
	QList<DiveComputerNode> values = dcWorkingMap.values();
	DiveComputerNode node = values.at(index.row());

	QVariant ret;
	if (role == Qt::DisplayRole || role == Qt::EditRole) {
		switch (index.column()) {
		case ID:
			ret = QString("0x").append(QString::number(node.deviceId, 16));
			break;
		case MODEL:
			ret = node.model;
			break;
		case NICKNAME:
			ret = node.nickName;
			break;
		}
	}

	if (index.column() == REMOVE) {
		switch (role) {
		case Qt::DecorationRole:
			ret = trashIcon();
			break;
		case Qt::SizeHintRole:
			ret = trashIcon().size();
			break;
		case Qt::ToolTipRole:
			ret = tr("Clicking here will remove this dive computer.");
			break;
		}
	}
	return ret;
}

int DiveComputerModel::rowCount(const QModelIndex &parent) const
{
	return numRows;
}

void DiveComputerModel::update()
{
	QList<DiveComputerNode> values = dcWorkingMap.values();
	int count = values.count();

	if (numRows) {
		beginRemoveRows(QModelIndex(), 0, numRows - 1);
		numRows = 0;
		endRemoveRows();
	}

	if (count) {
		beginInsertRows(QModelIndex(), 0, count - 1);
		numRows = count;
		endInsertRows();
	}
}

Qt::ItemFlags DiveComputerModel::flags(const QModelIndex &index) const
{
	Qt::ItemFlags flags = QAbstractItemModel::flags(index);
	if (index.column() == NICKNAME)
		flags |= Qt::ItemIsEditable;
	return flags;
}

bool DiveComputerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	QList<DiveComputerNode> values = dcWorkingMap.values();
	DiveComputerNode node = values.at(index.row());
	dcWorkingMap.remove(node.model, node);
	node.nickName = value.toString();
	dcWorkingMap.insert(node.model, node);
	emit dataChanged(index, index);
	return true;
}

void DiveComputerModel::remove(const QModelIndex &index)
{
	QList<DiveComputerNode> values = dcWorkingMap.values();
	DiveComputerNode node = values.at(index.row());
	dcWorkingMap.remove(node.model, node);
	update();
}

void DiveComputerModel::dropWorkingList()
{
	// how do I prevent the memory leak ?
}

void DiveComputerModel::keepWorkingList()
{
	if (dcList.dcMap != dcWorkingMap)
		mark_divelist_changed(true);
	dcList.dcMap = dcWorkingMap;
}

/*#################################################################
 * #
 * #	Yearly Statistics Model
 * #
 * ################################################################
 */

class YearStatisticsItem : public TreeItem {
public:
	enum {
		YEAR,
		DIVES,
		TOTAL_TIME,
		AVERAGE_TIME,
		SHORTEST_TIME,
		LONGEST_TIME,
		AVG_DEPTH,
		MIN_DEPTH,
		MAX_DEPTH,
		AVG_SAC,
		MIN_SAC,
		MAX_SAC,
		AVG_TEMP,
		MIN_TEMP,
		MAX_TEMP,
		COLUMNS
	};

	QVariant data(int column, int role) const;
	YearStatisticsItem(stats_t interval);

private:
	stats_t stats_interval;
};

YearStatisticsItem::YearStatisticsItem(stats_t interval) : stats_interval(interval)
{
}

QVariant YearStatisticsItem::data(int column, int role) const
{
	double value;
	QVariant ret;

	if (role == Qt::FontRole) {
		QFont font = defaultModelFont();
		font.setBold(stats_interval.is_year);
		return font;
	} else if (role != Qt::DisplayRole) {
		return ret;
	}
	switch (column) {
	case YEAR:
		if (stats_interval.is_trip) {
			ret = stats_interval.location;
		} else {
			ret = stats_interval.period;
		}
		break;
	case DIVES:
		ret = stats_interval.selection_size;
		break;
	case TOTAL_TIME:
		ret = get_time_string(stats_interval.total_time.seconds, 0);
		break;
	case AVERAGE_TIME:
		ret = get_minutes(stats_interval.total_time.seconds / stats_interval.selection_size);
		break;
	case SHORTEST_TIME:
		ret = get_minutes(stats_interval.shortest_time.seconds);
		break;
	case LONGEST_TIME:
		ret = get_minutes(stats_interval.longest_time.seconds);
		break;
	case AVG_DEPTH:
		ret = get_depth_string(stats_interval.avg_depth);
		break;
	case MIN_DEPTH:
		ret = get_depth_string(stats_interval.min_depth);
		break;
	case MAX_DEPTH:
		ret = get_depth_string(stats_interval.max_depth);
		break;
	case AVG_SAC:
		ret = get_volume_string(stats_interval.avg_sac);
		break;
	case MIN_SAC:
		ret = get_volume_string(stats_interval.min_sac);
		break;
	case MAX_SAC:
		ret = get_volume_string(stats_interval.max_sac);
		break;
	case AVG_TEMP:
		if (stats_interval.combined_temp && stats_interval.combined_count) {
			ret = QString::number(stats_interval.combined_temp / stats_interval.combined_count, 'f', 1);
		}
		break;
	case MIN_TEMP:
		value = get_temp_units(stats_interval.min_temp, NULL);
		if (value > -100.0)
			ret = QString::number(value, 'f', 1);
		break;
	case MAX_TEMP:
		value = get_temp_units(stats_interval.max_temp, NULL);
		if (value > -100.0)
			ret = QString::number(value, 'f', 1);
		break;
	}
	return ret;
}

YearlyStatisticsModel::YearlyStatisticsModel(QObject *parent)
{
	columns = COLUMNS;
	update_yearly_stats();
}

QVariant YearlyStatisticsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	QVariant val;
	if (role == Qt::FontRole)
		val = defaultModelFont();

	if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
		switch (section) {
		case YEAR:
			val = tr("Year \n > Month / Trip");
			break;
		case DIVES:
			val = tr("#");
			break;
		case TOTAL_TIME:
			val = tr("Duration \n Total");
			break;
		case AVERAGE_TIME:
			val = tr("\nAverage");
			break;
		case SHORTEST_TIME:
			val = tr("\nShortest");
			break;
		case LONGEST_TIME:
			val = tr("\nLongest");
			break;
		case AVG_DEPTH:
			val = QString(tr("Depth (%1)\n Average")).arg(get_depth_unit());
			break;
		case MIN_DEPTH:
			val = tr("\nMinimum");
			break;
		case MAX_DEPTH:
			val = tr("\nMaximum");
			break;
		case AVG_SAC:
			val = QString(tr("SAC (%1)\n Average")).arg(get_volume_unit());
			break;
		case MIN_SAC:
			val = tr("\nMinimum");
			break;
		case MAX_SAC:
			val = tr("\nMaximum");
			break;
		case AVG_TEMP:
			val = QString(tr("Temp. (%1)\n Average").arg(get_temp_unit()));
			break;
		case MIN_TEMP:
			val = tr("\nMinimum");
			break;
		case MAX_TEMP:
			val = tr("\nMaximum");
			break;
		}
	}
	return val;
}

void YearlyStatisticsModel::update_yearly_stats()
{
	int i, month = 0;
	unsigned int j, combined_months;

	for (i = 0; stats_yearly != NULL && stats_yearly[i].period; ++i) {
		YearStatisticsItem *item = new YearStatisticsItem(stats_yearly[i]);
		combined_months = 0;
		for (j = 0; combined_months < stats_yearly[i].selection_size; ++j) {
			combined_months += stats_monthly[month].selection_size;
			YearStatisticsItem *iChild = new YearStatisticsItem(stats_monthly[month]);
			item->children.append(iChild);
			iChild->parent = item;
			month++;
		}
		rootItem->children.append(item);
		item->parent = rootItem;
	}


	if (stats_by_trip != NULL && stats_by_trip[0].is_trip == true) {
		YearStatisticsItem *item = new YearStatisticsItem(stats_by_trip[0]);
		for (i = 1; stats_by_trip != NULL && stats_by_trip[i].is_trip; ++i) {
			YearStatisticsItem *iChild = new YearStatisticsItem(stats_by_trip[i]);
			item->children.append(iChild);
			iChild->parent = item;
		}
		rootItem->children.append(item);
		item->parent = rootItem;
	}
}

/*#################################################################
 * #
 * #	Table Print Model
 * #
 * ################################################################
 */
TablePrintModel::TablePrintModel()
{
	columns = 7;
	rows = 0;
}

TablePrintModel::~TablePrintModel()
{
	for (int i = 0; i < list.size(); i++)
		delete list.at(i);
}

void TablePrintModel::insertRow(int index)
{
	struct TablePrintItem *item = new struct TablePrintItem();
	item->colorBackground = 0xffffffff;
	if (index == -1) {
		beginInsertRows(QModelIndex(), rows, rows);
		list.append(item);
	} else {
		beginInsertRows(QModelIndex(), index, index);
		list.insert(index, item);
	}
	endInsertRows();
	rows++;
}

void TablePrintModel::callReset()
{
	beginResetModel();
	endResetModel();
}

QVariant TablePrintModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return QVariant();
	if (role == Qt::BackgroundRole)
		return QColor(list.at(index.row())->colorBackground);
	if (role == Qt::DisplayRole)
		switch (index.column()) {
		case 0:
			return list.at(index.row())->number;
		case 1:
			return list.at(index.row())->date;
		case 2:
			return list.at(index.row())->depth;
		case 3:
			return list.at(index.row())->duration;
		case 4:
			return list.at(index.row())->divemaster;
		case 5:
			return list.at(index.row())->buddy;
		case 6:
			return list.at(index.row())->location;
		}
	if (role == Qt::FontRole) {
		QFont font;
		font.setPointSizeF(7.5);
		if (index.row() == 0 && index.column() == 0) {
			font.setBold(true);
		}
		return QVariant::fromValue(font);
	}
	return QVariant();
}

bool TablePrintModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (index.isValid()) {
		if (role == Qt::DisplayRole) {
			switch (index.column()) {
			case 0:
				list.at(index.row())->number = value.toString();
			case 1:
				list.at(index.row())->date = value.toString();
			case 2:
				list.at(index.row())->depth = value.toString();
			case 3:
				list.at(index.row())->duration = value.toString();
			case 4:
				list.at(index.row())->divemaster = value.toString();
			case 5:
				list.at(index.row())->buddy = value.toString();
			case 6: {
				/* truncate if there are more than N lines of text,
				 * we don't want a row to be larger that a single page! */
				QString s = value.toString();
				const int maxLines = 15;
				int count = 0;
				for (int i = 0; i < s.length(); i++) {
					if (s.at(i) != QChar('\n'))
						continue;
					count++;
					if (count > maxLines) {
						s = s.left(i - 1);
						break;
					}
				}
				list.at(index.row())->location = s;
			}
			}
			return true;
		}
		if (role == Qt::BackgroundRole) {
			list.at(index.row())->colorBackground = value.value<unsigned int>();
			return true;
		}
	}
	return false;
}

int TablePrintModel::rowCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	return rows;
}

int TablePrintModel::columnCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	return columns;
}

/*#################################################################
 * #
 * #	Profile Print Model
 * #
 * ################################################################
 */

ProfilePrintModel::ProfilePrintModel(QObject *parent)
{
}

void ProfilePrintModel::setDive(struct dive *divePtr)
{
	diveId = divePtr->id;
	// reset();
}

void ProfilePrintModel::setFontsize(double size)
{
	fontSize = size;
}

int ProfilePrintModel::rowCount(const QModelIndex &parent) const
{
	return 12;
}

int ProfilePrintModel::columnCount(const QModelIndex &parent) const
{
	return 5;
}

QVariant ProfilePrintModel::data(const QModelIndex &index, int role) const
{
	const int row = index.row();
	const int col = index.column();

	switch (role) {
	case Qt::DisplayRole: {
		struct dive *dive = get_dive_by_uniq_id(diveId);
		struct DiveItem di;
		di.diveId = diveId;

		const QString unknown = tr("unknown");

		// dive# + date, depth, location, duration
		if (row == 0) {
			if (col == 0)
				return tr("Dive #%1 - %2").arg(dive->number).arg(di.displayDate());
			if (col == 3) {
				QString unit = (get_units()->length == units::METERS) ? "m" : "ft";
				return tr("Max depth: %1 %2").arg(di.displayDepth()).arg(unit);
			}
		}
		if (row == 1) {
			if (col == 0)
				return QString(get_dive_location(dive));
			if (col == 3)
				return QString(tr("Duration: %1 min")).arg(di.displayDuration());
		}
		// headings
		if (row == 2) {
			if (col == 0)
				return tr("Gas used:");
			if (col == 2)
				return tr("Tags:");
			if (col == 3)
				return tr("SAC:");
			if (col == 4)
				return tr("Weights:");
		}
		// notes
		if (col == 0) {
			if (row == 6)
				return tr("Notes:");
			if (row == 7)
				return QString(dive->notes);
		}
		// more headings
		if (row == 4) {
			if (col == 0)
				return tr("Divemaster:");
			if (col == 1)
				return tr("Buddy:");
			if (col == 2)
				return tr("Suit:");
			if (col == 3)
				return tr("Viz:");
			if (col == 4)
				return tr("Rating:");
		}
		// values for gas, sac, etc...
		if (row == 3) {
			if (col == 0) {
				int added = 0;
				QString gas, gases;
				for (int i = 0; i < MAX_CYLINDERS; i++) {
					if (!is_cylinder_used(dive, i))
						continue;
					gas = dive->cylinder[i].type.description;
					gas += QString(!gas.isEmpty() ? " " : "") + gasname(&dive->cylinder[i].gasmix);
					// if has a description and if such gas is not already present
					if (!gas.isEmpty() && gases.indexOf(gas) == -1) {
						if (added > 0)
							gases += QString(" / ");
						gases += gas;
						added++;
					}
				}
				return gases;
			}
			if (col == 2) {
				char buffer[256];
				taglist_get_tagstring(dive->tag_list, buffer, 256);
				return QString(buffer);
			}
			if (col == 3)
				return di.displaySac();
			if (col == 4) {
				weight_t tw = { total_weight(dive) };
				return get_weight_string(tw, true);
			}
		}
		// values for DM, buddy, suit, etc...
		if (row == 5) {
			if (col == 0)
				return QString(dive->divemaster);
			if (col == 1)
				return QString(dive->buddy);
			if (col == 2)
				return QString(dive->suit);
			if (col == 3)
				return (dive->visibility) ? QString::number(dive->visibility).append(" / 5") : QString();
			if (col == 4)
				return (dive->rating) ? QString::number(dive->rating).append(" / 5") : QString();
		}
		return QString();
	}
	case Qt::FontRole: {
		QFont font;
		font.setPointSizeF(fontSize);
		if (row == 0 && col == 0) {
			font.setBold(true);
		}
		return QVariant::fromValue(font);
	}
	case Qt::TextAlignmentRole: {
		// everything is aligned to the left
		unsigned int align = Qt::AlignLeft;
		// align depth and duration right
		if (row < 2 && col == 4)
			align = Qt::AlignRight | Qt::AlignVCenter;
		return QVariant::fromValue(align);
	}
	} // switch (role)
	return QVariant();
}

Qt::ItemFlags GasSelectionModel::flags(const QModelIndex &index) const
{
	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

GasSelectionModel *GasSelectionModel::instance()
{
	static QScopedPointer<GasSelectionModel> self(new GasSelectionModel());
	return self.data();
}

//TODO: Remove this #include here when the issue below is fixed.
#include "diveplannermodel.h"
void GasSelectionModel::repopulate()
{
	/* TODO:
	 * getGasList shouldn't be a member of DivePlannerPointsModel,
	 * it has nothing to do with the current plain being calculated:
	 * it's internal to the current_dive.
	 */
	setStringList(DivePlannerPointsModel::instance()->getGasList());
}

QVariant GasSelectionModel::data(const QModelIndex &index, int role) const
{
	if (role == Qt::FontRole) {
		return defaultModelFont();
	}
	return QStringListModel::data(index, role);
}

// Language Model, The Model to populate the list of possible Languages.

LanguageModel *LanguageModel::instance()
{
	static LanguageModel *self = new LanguageModel();
	QLocale l;
	return self;
}

LanguageModel::LanguageModel(QObject *parent) : QAbstractListModel(parent)
{
	QSettings s;
	QDir d(getSubsurfaceDataPath("translations"));
	Q_FOREACH (const QString &s, d.entryList()) {
		if (s.startsWith("subsurface_") && s.endsWith(".qm")) {
			languages.push_back((s == "subsurface_source.qm") ? "English" : s);
		}
	}
}

QVariant LanguageModel::data(const QModelIndex &index, int role) const
{
	QLocale loc;
	QString currentString = languages.at(index.row());
	if (!index.isValid())
		return QVariant();
	switch (role) {
	case Qt::DisplayRole: {
		QLocale l(currentString.remove("subsurface_"));
		return currentString == "English" ? currentString : QString("%1 (%2)").arg(l.languageToString(l.language())).arg(l.countryToString(l.country()));
	}
	case Qt::UserRole:
		return currentString == "English" ? "en_US" : currentString.remove("subsurface_");
	}
	return QVariant();
}

int LanguageModel::rowCount(const QModelIndex &parent) const
{
	return languages.count();
}