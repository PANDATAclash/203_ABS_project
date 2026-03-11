import csv
import os
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional

import numpy as np
import pyqtgraph as pg
from pyqtgraph.exporters import ImageExporter

from PySide6.QtCore import QAbstractTableModel, QModelIndex, Qt
from PySide6.QtGui import QAction, QIcon
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFileDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSplitter,
    QTableView,
    QVBoxLayout,
    QWidget,
)


# =========================================================
# Helpers
# =========================================================

def resource_path(relative_path: str) -> str:
    """Return absolute path to resource for dev and PyInstaller."""
    if hasattr(sys, "_MEIPASS"):
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.abspath("."), relative_path)


def normalize_name(s: str) -> str:
    return "".join(ch.lower() for ch in s.strip() if ch.isalnum() or ch in ["_", " "])


def parse_float(value: str) -> float:
    try:
        return float(value.strip().replace(",", "."))
    except Exception:
        return np.nan


# =========================================================
# Signal configuration
# =========================================================

SIGNALS = {
    "longitudinal_acceleration": {
        "label": "Longitudinal Acceleration (m/s²)",
        "unit": "m/s²",
        "color": "#e41a1c",
        "preferred_headers": ["ax"],
        "fallback_headers": ["longitudinal acceleration", "longitudinal_acceleration", "acc_long", "long_acc", "acceleration"],
    },
    "pitch_angle": {
        "label": "Pitch Angle (deg)",
        "unit": "deg",
        "color": "#377eb8",
        "preferred_headers": ["pitch"],
        "fallback_headers": ["pitch angle", "pitch_angle", "pitch_deg"],
    },
    "lean_angle": {
        "label": "Lean Angle (deg)",
        "unit": "deg",
        "color": "#4daf4a",
        "preferred_headers": ["roll"],
        "fallback_headers": ["lean angle", "lean_angle", "roll_deg", "lean"],
    },
    "rear_wheel_speed": {
        "label": "Rear Wheel Speed (km/h)",
        "unit": "km/h",
        "color": "#984ea3",
        "preferred_headers": ["hall_r"],
        "fallback_headers": ["hall_rear", "rear wheel speed", "rear_wheel_speed", "v_rear", "rear_speed", "rear_kmh"],
    },
    "front_wheel_speed": {
        "label": "Front Wheel Speed (km/h)",
        "unit": "km/h",
        "color": "#ff7f00",
        "preferred_headers": ["hall_f"],
        "fallback_headers": ["hall_front", "front wheel speed", "front_wheel_speed", "v_front", "front_speed", "front_kmh"],
    },
    "gps_speed": {
        "label": "GPS Speed (km/h)",
        "unit": "km/h",
        "color": "#ff1493",
        "preferred_headers": ["gps"],
        "fallback_headers": ["gps_speed", "gps_speed_kmh", "gps_speed_km/h", "gps_speed_kph"],
    },
}

TIME_HEADERS = ["time", "timestamp", "t", "sec", "seconds"]

UNIT_AXIS_LABELS = {
    "m/s²": "Acceleration (m/s²)",
    "deg": "Angle (deg)",
    "km/h": "Speed (km/h)",
}


# =========================================================
# Data model
# =========================================================

@dataclass
class LoadedCSV:
    headers: List[str]
    rows: List[List[str]]
    x: np.ndarray
    series: Dict[str, np.ndarray]


def find_column(headers: List[str], candidates: List[str]) -> Optional[int]:
    normalized_headers = [normalize_name(h) for h in headers]
    normalized_candidates = [normalize_name(c) for c in candidates]

    # exact match first
    for candidate in normalized_candidates:
        for i, header in enumerate(normalized_headers):
            if header == candidate:
                return i

    # substring fallback
    for candidate in normalized_candidates:
        for i, header in enumerate(normalized_headers):
            if candidate in header:
                return i

    return None


def parse_float_column(rows: List[List[str]], col: int) -> np.ndarray:
    data = []
    for row in rows:
        if col < len(row):
            data.append(parse_float(row[col]))
        else:
            data.append(np.nan)
    return np.array(data, dtype=float)


def load_csv(path: str) -> LoadedCSV:
    with open(path, "r", newline="", encoding="utf-8-sig") as f:
        reader = csv.reader(f)
        all_rows = list(reader)

    if len(all_rows) < 2:
        raise ValueError("CSV appears empty or missing data rows.")

    headers = all_rows[0]
    rows = all_rows[1:]

    # X-axis
    time_col = find_column(headers, TIME_HEADERS)
    if time_col is not None:
        x = parse_float_column(rows, time_col)
        if np.all(np.isnan(x)):
            x = np.arange(len(rows), dtype=float)
    else:
        x = np.arange(len(rows), dtype=float)

    # Signals
    series: Dict[str, np.ndarray] = {}
    for key, config in SIGNALS.items():
        col = find_column(headers, config["preferred_headers"])
        if col is None:
            col = find_column(headers, config["fallback_headers"])
        if col is not None:
            series[key] = parse_float_column(rows, col)

    return LoadedCSV(headers=headers, rows=rows, x=x, series=series)


# =========================================================
# Table model
# =========================================================

class CSVTableModel(QAbstractTableModel):
    def __init__(self, headers: List[str], rows: List[List[str]]):
        super().__init__()
        self._headers = headers
        self._rows = rows

    def rowCount(self, parent=QModelIndex()) -> int:
        return len(self._rows)

    def columnCount(self, parent=QModelIndex()) -> int:
        return len(self._headers)

    def data(self, index: QModelIndex, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        if role == Qt.DisplayRole:
            r = index.row()
            c = index.column()
            if r < len(self._rows) and c < len(self._rows[r]):
                return self._rows[r][c]
        return None

    def headerData(self, section: int, orientation: Qt.Orientation, role=Qt.DisplayRole):
        if role != Qt.DisplayRole:
            return None
        if orientation == Qt.Horizontal and section < len(self._headers):
            return self._headers[section]
        if orientation == Qt.Vertical:
            return str(section)
        return None


# =========================================================
# Main window
# =========================================================

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Motorcycle Forensic Data Visualizer")
        self.setWindowIcon(QIcon(resource_path("assets/icon.ico")))

        self.loaded: Optional[LoadedCSV] = None
        self.curves: Dict[str, Dict] = {}
        self.unit_order: List[str] = []

        pg.setConfigOptions(antialias=True)

        # Main plot
        self.plot = pg.PlotWidget()
        self.plot_item = self.plot.getPlotItem()
        self.plot_item.showGrid(x=True, y=True, alpha=0.2)
        self.plot_item.setLabel("bottom", "Time (s) / Index")
        self.plot_item.addLegend(offset=(10, 10))

        self.main_vb = self.plot_item.vb
        self.left_axis = self.plot_item.getAxis("left")

        # Additional right-side axes
        self.right_axis_1 = pg.AxisItem("right")
        self.right_axis_2 = pg.AxisItem("right")
        self.plot_item.layout.addItem(self.right_axis_1, 2, 3)
        self.plot_item.layout.addItem(self.right_axis_2, 2, 4)

        self.right_vb_1 = pg.ViewBox()
        self.right_vb_2 = pg.ViewBox()
        self.plot_item.scene().addItem(self.right_vb_1)
        self.plot_item.scene().addItem(self.right_vb_2)

        self.right_axis_1.linkToView(self.right_vb_1)
        self.right_axis_2.linkToView(self.right_vb_2)
        self.right_vb_1.setXLink(self.main_vb)
        self.right_vb_2.setXLink(self.main_vb)

        self.main_vb.sigResized.connect(self.update_views)

        self.axis_slots = {
            "left": {"axis": self.left_axis, "viewbox": self.main_vb},
            "right1": {"axis": self.right_axis_1, "viewbox": self.right_vb_1},
            "right2": {"axis": self.right_axis_2, "viewbox": self.right_vb_2},
        }

        self.set_all_axes_hidden()
        self.update_views()

        # Controls
        self.load_btn = QPushButton("Load CSV")
        self.load_btn.clicked.connect(self.on_load_csv)

        self.export_btn = QPushButton("Export Plot as PNG")
        self.export_btn.clicked.connect(self.on_export_png)
        self.export_btn.setEnabled(False)

        self.status_lbl = QLabel("No file loaded.")

        self.checkbox_container = QWidget()
        self.checkbox_layout = QVBoxLayout(self.checkbox_container)
        self.checkbox_layout.setAlignment(Qt.AlignTop)
        self.checkboxes: Dict[str, QCheckBox] = {}

        for key, config in SIGNALS.items():
            cb = QCheckBox(config["label"])
            cb.setEnabled(False)
            cb.stateChanged.connect(self.on_signal_toggled)
            self.checkboxes[key] = cb
            self.checkbox_layout.addWidget(cb)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.checkbox_container)

        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.addWidget(self.load_btn)
        left_layout.addWidget(self.export_btn)
        left_layout.addSpacing(8)
        left_layout.addWidget(QLabel("Signals:"))
        left_layout.addWidget(scroll, stretch=1)
        left_layout.addWidget(self.status_lbl)

        self.table = QTableView()
        self.table.setSortingEnabled(False)

        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.addWidget(self.plot, stretch=3)
        right_layout.addWidget(self.table, stretch=2)

        splitter = QSplitter()
        splitter.addWidget(left_panel)
        splitter.addWidget(right_panel)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([300, 900])

        self.setCentralWidget(splitter)

        reset_view = QAction("Reset View", self)
        reset_view.triggered.connect(self.reset_view)
        self.menuBar().addAction(reset_view)

    # -----------------------------------------------------
    # Plot view / axes
    # -----------------------------------------------------

    def update_views(self):
        rect = self.main_vb.sceneBoundingRect()
        self.right_vb_1.setGeometry(rect)
        self.right_vb_2.setGeometry(rect)
        self.right_vb_1.linkedViewChanged(self.main_vb, self.right_vb_1.XAxis)
        self.right_vb_2.linkedViewChanged(self.main_vb, self.right_vb_2.XAxis)

    def set_all_axes_hidden(self):
        for slot in self.axis_slots.values():
            axis = slot["axis"]
            axis.setStyle(showValues=False)
            axis.setLabel("")
            if axis is self.left_axis:
                axis.show()
            else:
                axis.hide()

    def get_active_units(self) -> List[str]:
        units = []
        for item in self.curves.values():
            unit = item["unit"]
            if unit not in units:
                units.append(unit)
        return units

    def refresh_unit_order(self):
        active_units = self.get_active_units()
        self.unit_order = [u for u in self.unit_order if u in active_units]
        for unit in active_units:
            if unit not in self.unit_order:
                self.unit_order.append(unit)
        self.unit_order = self.unit_order[:3]

    def get_slot_for_unit(self, unit: str) -> Optional[str]:
        mapping = ["left", "right1", "right2"]
        if unit in self.unit_order:
            idx = self.unit_order.index(unit)
            if idx < len(mapping):
                return mapping[idx]
        return None

    def get_viewbox_for_unit(self, unit: str) -> pg.ViewBox:
        slot = self.get_slot_for_unit(unit)
        if slot is None:
            return self.main_vb
        return self.axis_slots[slot]["viewbox"]

    def configure_axes(self):
        self.set_all_axes_hidden()
        for unit in self.unit_order:
            slot_name = self.get_slot_for_unit(unit)
            if slot_name is None:
                continue
            axis = self.axis_slots[slot_name]["axis"]
            axis.setStyle(showValues=True)
            axis.setLabel(UNIT_AXIS_LABELS.get(unit, unit))
            axis.show()

    # -----------------------------------------------------
    # Curve management
    # -----------------------------------------------------

    def clear_all_curves(self):
        for item in self.curves.values():
            try:
                item["viewbox"].removeItem(item["curve"])
            except Exception:
                pass
        self.curves.clear()

    def rebuild_legend(self):
        legend = self.plot_item.legend
        if legend is not None:
            legend.clear()
            for key, item in self.curves.items():
                legend.addItem(item["curve"], SIGNALS[key]["label"])

    def remap_curves(self):
        for key, item in list(self.curves.items()):
            new_vb = self.get_viewbox_for_unit(item["unit"])
            if new_vb is not item["viewbox"]:
                try:
                    item["viewbox"].removeItem(item["curve"])
                except Exception:
                    pass
                new_vb.addItem(item["curve"])
                item["viewbox"] = new_vb

    def auto_range_visible_units(self):
        if not self.loaded or not self.curves:
            self.set_all_axes_hidden()
            return

        self.refresh_unit_order()
        self.configure_axes()
        self.remap_curves()

        # X auto range on main view
        finite_x = self.loaded.x[np.isfinite(self.loaded.x)]
        if finite_x.size > 0:
            self.main_vb.setXRange(float(np.min(finite_x)), float(np.max(finite_x)), padding=0.02)

        # Y per unit
        for unit in self.unit_order:
            vb = self.get_viewbox_for_unit(unit)
            ys = []
            for key in self.curves:
                if SIGNALS[key]["unit"] == unit:
                    y = self.loaded.series[key]
                    finite = y[np.isfinite(y)]
                    if finite.size:
                        ys.append(finite)

            if not ys:
                continue

            all_y = np.concatenate(ys)
            y_min, y_max = float(np.min(all_y)), float(np.max(all_y))
            if y_min == y_max:
                pad = 1.0 if y_min == 0 else abs(y_min) * 0.1
            else:
                pad = (y_max - y_min) * 0.05
            vb.setYRange(y_min - pad, y_max + pad, padding=0)

        self.update_views()

    # -----------------------------------------------------
    # Actions
    # -----------------------------------------------------

    def reset_view(self):
        self.auto_range_visible_units()

    def on_load_csv(self):
        path, _ = QFileDialog.getOpenFileName(self, "Open CSV", "", "CSV Files (*.csv);;All Files (*)")
        if not path:
            return

        try:
            self.loaded = load_csv(path)
        except Exception as e:
            QMessageBox.critical(self, "Load error", f"Failed to load CSV:\n{e}")
            return

        self.status_lbl.setText(f"Loaded: {os.path.basename(path)}")
        self.table.setModel(CSVTableModel(self.loaded.headers, self.loaded.rows))
        self.table.resizeColumnsToContents()

        self.clear_all_curves()
        self.unit_order = []
        self.rebuild_legend()
        self.set_all_axes_hidden()

        any_signal = False
        for key, cb in self.checkboxes.items():
            present = key in self.loaded.series
            cb.blockSignals(True)
            cb.setEnabled(present)
            cb.setChecked(False)
            cb.blockSignals(False)
            any_signal = any_signal or present

        self.export_btn.setEnabled(any_signal)

        if not any_signal:
            QMessageBox.warning(
                self,
                "No signals found",
                "No expected signal columns were detected in the CSV."
            )

    def on_signal_toggled(self):
        if not self.loaded:
            return

        x = self.loaded.x

        # Remove unchecked
        for key, cb in self.checkboxes.items():
            if key in self.curves and not cb.isChecked():
                item = self.curves.pop(key)
                try:
                    item["viewbox"].removeItem(item["curve"])
                except Exception:
                    pass

        self.refresh_unit_order()
        self.configure_axes()
        self.remap_curves()

        # Add checked
        for key, cb in self.checkboxes.items():
            if not cb.isEnabled() or not cb.isChecked() or key in self.curves:
                continue

            unit = SIGNALS[key]["unit"]
            if unit not in self.unit_order:
                if len(self.unit_order) >= 3:
                    QMessageBox.warning(self, "Too many unit groups", "Only three unit groups can be displayed at once.")
                    cb.blockSignals(True)
                    cb.setChecked(False)
                    cb.blockSignals(False)
                    continue
                self.unit_order.append(unit)
                self.configure_axes()

            vb = self.get_viewbox_for_unit(unit)
            pen = pg.mkPen(color=SIGNALS[key]["color"], width=2)
            curve = pg.PlotDataItem(x, self.loaded.series[key], pen=pen, name=SIGNALS[key]["label"])
            vb.addItem(curve)

            self.curves[key] = {
                "curve": curve,
                "viewbox": vb,
                "unit": unit,
            }

        self.rebuild_legend()
        self.auto_range_visible_units()

    def on_export_png(self):
        if not self.loaded:
            return

        out_path, _ = QFileDialog.getSaveFileName(self, "Save Plot as PNG", "plot.png", "PNG Image (*.png)")
        if not out_path:
            return

        try:
            exporter = ImageExporter(self.plot.plotItem)
            exporter.parameters()["width"] = 2000
            exporter.export(out_path)
        except Exception as e:
            QMessageBox.critical(self, "Export error", f"Failed to export PNG:\n{e}")
            return

        QMessageBox.information(self, "Exported", f"Saved:\n{out_path}")


# =========================================================
# Main
# =========================================================

def main():
    app = QApplication([])
    win = MainWindow()
    win.resize(1300, 850)
    win.show()
    app.exec()


if __name__ == "__main__":
    main()