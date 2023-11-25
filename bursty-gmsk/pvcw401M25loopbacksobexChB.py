#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: CW Channel B (Tx B to Rx B) loopback at 401.25MHz
# GNU Radio version: 3.10.8.0

from PyQt5 import Qt
from gnuradio import qtgui
from gnuradio import analog
from gnuradio import blocks
from gnuradio import gr
from gnuradio.filter import firdes
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio import uhd
import time
import math,cmath
import sip



class pvcw401M25loopbacksobexChB(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "CW Channel B (Tx B to Rx B) loopback at 401.25MHz", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("CW Channel B (Tx B to Rx B) loopback at 401.25MHz")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except BaseException as exc:
            print(f"Qt GUI: Could not set Icon: {str(exc)}", file=sys.stderr)
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("GNU Radio", "pvcw401M25loopbacksobexChB")

        try:
            geometry = self.settings.value("geometry")
            if geometry:
                self.restoreGeometry(geometry)
        except BaseException as exc:
            print(f"Qt GUI: Could not restore geometry: {str(exc)}", file=sys.stderr)

        ##################################################
        # Variables
        ##################################################
        self.rx_sob_s = rx_sob_s = 2
        self.tx_sob_s = tx_sob_s = rx_sob_s+0.5
        self.samp_rate = samp_rate = 1000000
        self.rx_rec_t = rx_rec_t = 1.5
        self.freq = freq = 401525000

        ##################################################
        # Blocks
        ##################################################

        self.uhd_usrp_source_0_1 = uhd.usrp_source(
            ",".join(('', "crimson")),
            uhd.stream_args(
                cpu_format="sc16",
                otw_format="sc16",
                args='',
                channels=[1],
            ),
        )
        self.uhd_usrp_source_0_1.set_samp_rate(samp_rate)
        self.uhd_usrp_source_0_1.set_time_unknown_pps(uhd.time_spec(0))

        self.uhd_usrp_source_0_1.set_center_freq(freq, 0)
        self.uhd_usrp_source_0_1.set_antenna("RX1", 0)
        self.uhd_usrp_source_0_1.set_bandwidth(samp_rate, 0)
        self.uhd_usrp_source_0_1.set_rx_agc(False, 0)
        self.uhd_usrp_source_0_1.set_gain(50, 0)

        self.uhd_usrp_source_0_1.set_start_time(uhd.time_spec(rx_sob_s))
        self.uhd_usrp_sink_0 = uhd.usrp_sink(
            ",".join(('', "crimson")),
            uhd.stream_args(
                cpu_format="sc16",
                otw_format="sc16",
                args='sc16',
                channels=[1],
            ),
            '',
        )
        self.uhd_usrp_sink_0.set_samp_rate(samp_rate)
        self.uhd_usrp_sink_0.set_time_unknown_pps(uhd.time_spec(0))

        self.uhd_usrp_sink_0.set_center_freq(freq, 0)
        self.uhd_usrp_sink_0.set_antenna("TX/RX", 0)
        self.uhd_usrp_sink_0.set_bandwidth(200000, 0)
        self.uhd_usrp_sink_0.set_gain(31, 0)

        self.uhd_usrp_sink_0.set_lo_source('internal', uhd.ALL_LOS, 0)
        self.uhd_usrp_sink_0.set_lo_export_enabled(False, uhd.ALL_LOS, 0)
        self.uhd_usrp_sink_0.set_start_time(uhd.time_spec(tx_sob_s))
        self.qtgui_time_sink_x_0 = qtgui.time_sink_c(
            (int(math.floor(samp_rate*rx_rec_t))), #size
            samp_rate, #samp_rate
            "", #name
            1, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_x_0.set_update_time(rx_rec_t+rx_sob_s)
        self.qtgui_time_sink_x_0.set_y_axis(-35000, 35000)

        self.qtgui_time_sink_x_0.set_y_label('Amplitude', "")

        self.qtgui_time_sink_x_0.enable_tags(True)
        self.qtgui_time_sink_x_0.set_trigger_mode(qtgui.TRIG_MODE_NORM, qtgui.TRIG_SLOPE_POS, 0.0, 0, 0, "")
        self.qtgui_time_sink_x_0.enable_autoscale(False)
        self.qtgui_time_sink_x_0.enable_grid(True)
        self.qtgui_time_sink_x_0.enable_axis_labels(True)
        self.qtgui_time_sink_x_0.enable_control_panel(False)
        self.qtgui_time_sink_x_0.enable_stem_plot(False)


        labels = ['Signal 1', 'Signal 2', 'Signal 3', 'Signal 4', 'Signal 5',
            'Signal 6', 'Signal 7', 'Signal 8', 'Signal 9', 'Signal 10']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ['blue', 'red', 'green', 'black', 'cyan',
            'magenta', 'yellow', 'dark red', 'dark green', 'dark blue']
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]
        styles = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        markers = [-1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1]


        for i in range(2):
            if len(labels[i]) == 0:
                if (i % 2 == 0):
                    self.qtgui_time_sink_x_0.set_line_label(i, "Re{{Data {0}}}".format(i/2))
                else:
                    self.qtgui_time_sink_x_0.set_line_label(i, "Im{{Data {0}}}".format(i/2))
            else:
                self.qtgui_time_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_time_sink_x_0.set_line_width(i, widths[i])
            self.qtgui_time_sink_x_0.set_line_color(i, colors[i])
            self.qtgui_time_sink_x_0.set_line_style(i, styles[i])
            self.qtgui_time_sink_x_0.set_line_marker(i, markers[i])
            self.qtgui_time_sink_x_0.set_line_alpha(i, alphas[i])

        self._qtgui_time_sink_x_0_win = sip.wrapinstance(self.qtgui_time_sink_x_0.qwidget(), Qt.QWidget)
        self.top_layout.addWidget(self._qtgui_time_sink_x_0_win)
        self.blocks_skiphead_0 = blocks.skiphead(gr.sizeof_gr_complex*1, (int((tx_sob_s-rx_sob_s)*samp_rate*0.5)))
        self.blocks_interleaved_short_to_complex_0 = blocks.interleaved_short_to_complex(True, False,1)
        self.blocks_head_0 = blocks.head(gr.sizeof_gr_complex*1, (int(math.floor((rx_rec_t+rx_sob_s)*samp_rate*1.1))))
        self.blocks_complex_to_interleaved_short_0 = blocks.complex_to_interleaved_short(True,30000)
        self.analog_sig_source_x_0 = analog.sig_source_c(samp_rate, analog.GR_COS_WAVE, 1000, 1, 0, 0)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_complex_to_interleaved_short_0, 0))
        self.connect((self.blocks_complex_to_interleaved_short_0, 0), (self.uhd_usrp_sink_0, 0))
        self.connect((self.blocks_head_0, 0), (self.blocks_skiphead_0, 0))
        self.connect((self.blocks_interleaved_short_to_complex_0, 0), (self.blocks_head_0, 0))
        self.connect((self.blocks_skiphead_0, 0), (self.qtgui_time_sink_x_0, 0))
        self.connect((self.uhd_usrp_source_0_1, 0), (self.blocks_interleaved_short_to_complex_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("GNU Radio", "pvcw401M25loopbacksobexChB")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_rx_sob_s(self):
        return self.rx_sob_s

    def set_rx_sob_s(self, rx_sob_s):
        self.rx_sob_s = rx_sob_s
        self.set_tx_sob_s(self.rx_sob_s+0.5)
        self.blocks_head_0.set_length((int(math.floor((self.rx_rec_t+self.rx_sob_s)*self.samp_rate*1.1))))
        self.qtgui_time_sink_x_0.set_update_time(self.rx_rec_t+self.rx_sob_s)

    def get_tx_sob_s(self):
        return self.tx_sob_s

    def set_tx_sob_s(self, tx_sob_s):
        self.tx_sob_s = tx_sob_s

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.analog_sig_source_x_0.set_sampling_freq(self.samp_rate)
        self.blocks_head_0.set_length((int(math.floor((self.rx_rec_t+self.rx_sob_s)*self.samp_rate*1.1))))
        self.qtgui_time_sink_x_0.set_samp_rate(self.samp_rate)
        self.uhd_usrp_sink_0.set_samp_rate(self.samp_rate)
        self.uhd_usrp_source_0_1.set_samp_rate(self.samp_rate)
        self.uhd_usrp_source_0_1.set_bandwidth(self.samp_rate, 0)

    def get_rx_rec_t(self):
        return self.rx_rec_t

    def set_rx_rec_t(self, rx_rec_t):
        self.rx_rec_t = rx_rec_t
        self.blocks_head_0.set_length((int(math.floor((self.rx_rec_t+self.rx_sob_s)*self.samp_rate*1.1))))
        self.qtgui_time_sink_x_0.set_update_time(self.rx_rec_t+self.rx_sob_s)

    def get_freq(self):
        return self.freq

    def set_freq(self, freq):
        self.freq = freq
        self.uhd_usrp_sink_0.set_center_freq(self.freq, 0)
        self.uhd_usrp_source_0_1.set_center_freq(self.freq, 0)




def main(top_block_cls=pvcw401M25loopbacksobexChB, options=None):

    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
