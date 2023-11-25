#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: Per Vices Burst GMSK Loopback example
# GNU Radio version: 3.10.8.0

from PyQt5 import Qt
from gnuradio import qtgui
from gnuradio import blocks
import pmt
from gnuradio import digital
from gnuradio import gr
from gnuradio.filter import firdes
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio import gr, pdu
from gnuradio import pdu
from gnuradio import uhd
import time
import math,cmath
import sip



class pvburstgmsksc16loopbackex(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "Per Vices Burst GMSK Loopback example", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("Per Vices Burst GMSK Loopback example")
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

        self.settings = Qt.QSettings("GNU Radio", "pvburstgmsksc16loopbackex")

        try:
            geometry = self.settings.value("geometry")
            if geometry:
                self.restoreGeometry(geometry)
        except BaseException as exc:
            print(f"Qt GUI: Could not restore geometry: {str(exc)}", file=sys.stderr)

        ##################################################
        # Variables
        ##################################################
        self.strobe_per = strobe_per = 120
        self.pl_min = pl_min = 50
        self.pl_max = pl_max = 50
        self.tx_sob_t = tx_sob_t = 2
        self.strobe_period_ms = strobe_period_ms = strobe_per
        self.samp_rate = samp_rate = 1000000
        self.rx_sob_t = rx_sob_t = 2
        self.rx_rec_t = rx_rec_t = 1.5
        self.min_payload_byte = min_payload_byte = pl_min
        self.max_payload_byte = max_payload_byte = pl_max
        self.freq = freq = 401525000
        self.GMSK_symb_per_byte = GMSK_symb_per_byte = 8
        self.GMSK_samp_per_symb = GMSK_samp_per_symb = 10

        ##################################################
        # Blocks
        ##################################################

        self.uhd_usrp_source_0_1_0 = uhd.usrp_source(
            ",".join(('', "crimson")),
            uhd.stream_args(
                cpu_format="sc16",
                otw_format="sc16",
                args='',
                channels=[0],
            ),
        )
        self.uhd_usrp_source_0_1_0.set_samp_rate(samp_rate)
        self.uhd_usrp_source_0_1_0.set_time_unknown_pps(uhd.time_spec(0))

        self.uhd_usrp_source_0_1_0.set_center_freq(freq, 0)
        self.uhd_usrp_source_0_1_0.set_antenna("RX1", 0)
        self.uhd_usrp_source_0_1_0.set_bandwidth(samp_rate, 0)
        self.uhd_usrp_source_0_1_0.set_rx_agc(False, 0)
        self.uhd_usrp_source_0_1_0.set_gain(50, 0)

        self.uhd_usrp_source_0_1_0.set_start_time(uhd.time_spec(rx_sob_t))
        self.uhd_usrp_sink_0 = uhd.usrp_sink(
            ",".join(('', "crimson")),
            uhd.stream_args(
                cpu_format="sc16",
                otw_format="sc16",
                args='sc16',
                channels=[0],
            ),
            'packet_len',
        )
        self.uhd_usrp_sink_0.set_samp_rate(samp_rate)
        self.uhd_usrp_sink_0.set_time_unknown_pps(uhd.time_spec(0))

        self.uhd_usrp_sink_0.set_center_freq(freq, 0)
        self.uhd_usrp_sink_0.set_antenna("TX/RX", 0)
        self.uhd_usrp_sink_0.set_bandwidth(200000, 0)
        self.uhd_usrp_sink_0.set_gain(31, 0)

        self.uhd_usrp_sink_0.set_lo_source('internal', uhd.ALL_LOS, 0)
        self.uhd_usrp_sink_0.set_lo_export_enabled(False, uhd.ALL_LOS, 0)
        self.uhd_usrp_sink_0.set_start_time(uhd.time_spec(tx_sob_t))
        self.qtgui_time_sink_x_0 = qtgui.time_sink_c(
            (int(math.floor(samp_rate*rx_rec_t))), #size
            samp_rate, #samp_rate
            "", #name
            1, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_x_0.set_update_time(rx_rec_t)
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
        self.pdu_random_pdu_0 = pdu.random_pdu(min_payload_byte, max_payload_byte, 0xFF, 1)
        self.pdu_pdu_to_tagged_stream_1 = pdu.pdu_to_tagged_stream(gr.types.byte_t, 'packet_len')
        self.digital_gmsk_mod_0 = digital.gmsk_mod(
            samples_per_symbol=GMSK_samp_per_symb,
            bt=0.35,
            verbose=False,
            log=False,
            do_unpack=True)
        self.blocks_tagged_stream_multiply_length_0 = blocks.tagged_stream_multiply_length(gr.sizeof_gr_complex*1, 'packet_len', (GMSK_symb_per_byte*GMSK_samp_per_symb))
        self.blocks_message_strobe_0 = blocks.message_strobe(pmt.intern("TEST"), strobe_period_ms)
        self.blocks_interleaved_short_to_complex_0 = blocks.interleaved_short_to_complex(True, False,1)
        self.blocks_complex_to_interleaved_short_0 = blocks.complex_to_interleaved_short(True,30000)


        ##################################################
        # Connections
        ##################################################
        self.msg_connect((self.blocks_message_strobe_0, 'strobe'), (self.pdu_random_pdu_0, 'generate'))
        self.msg_connect((self.pdu_random_pdu_0, 'pdus'), (self.pdu_pdu_to_tagged_stream_1, 'pdus'))
        self.connect((self.blocks_complex_to_interleaved_short_0, 0), (self.uhd_usrp_sink_0, 0))
        self.connect((self.blocks_interleaved_short_to_complex_0, 0), (self.qtgui_time_sink_x_0, 0))
        self.connect((self.blocks_tagged_stream_multiply_length_0, 0), (self.blocks_complex_to_interleaved_short_0, 0))
        self.connect((self.digital_gmsk_mod_0, 0), (self.blocks_tagged_stream_multiply_length_0, 0))
        self.connect((self.pdu_pdu_to_tagged_stream_1, 0), (self.digital_gmsk_mod_0, 0))
        self.connect((self.uhd_usrp_source_0_1_0, 0), (self.blocks_interleaved_short_to_complex_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("GNU Radio", "pvburstgmsksc16loopbackex")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_strobe_per(self):
        return self.strobe_per

    def set_strobe_per(self, strobe_per):
        self.strobe_per = strobe_per
        self.set_strobe_period_ms(self.strobe_per)

    def get_pl_min(self):
        return self.pl_min

    def set_pl_min(self, pl_min):
        self.pl_min = pl_min
        self.set_min_payload_byte(self.pl_min)

    def get_pl_max(self):
        return self.pl_max

    def set_pl_max(self, pl_max):
        self.pl_max = pl_max
        self.set_max_payload_byte(self.pl_max)

    def get_tx_sob_t(self):
        return self.tx_sob_t

    def set_tx_sob_t(self, tx_sob_t):
        self.tx_sob_t = tx_sob_t

    def get_strobe_period_ms(self):
        return self.strobe_period_ms

    def set_strobe_period_ms(self, strobe_period_ms):
        self.strobe_period_ms = strobe_period_ms
        self.blocks_message_strobe_0.set_period(self.strobe_period_ms)

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.qtgui_time_sink_x_0.set_samp_rate(self.samp_rate)
        self.uhd_usrp_sink_0.set_samp_rate(self.samp_rate)
        self.uhd_usrp_source_0_1_0.set_samp_rate(self.samp_rate)
        self.uhd_usrp_source_0_1_0.set_bandwidth(self.samp_rate, 0)

    def get_rx_sob_t(self):
        return self.rx_sob_t

    def set_rx_sob_t(self, rx_sob_t):
        self.rx_sob_t = rx_sob_t

    def get_rx_rec_t(self):
        return self.rx_rec_t

    def set_rx_rec_t(self, rx_rec_t):
        self.rx_rec_t = rx_rec_t
        self.qtgui_time_sink_x_0.set_update_time(self.rx_rec_t)

    def get_min_payload_byte(self):
        return self.min_payload_byte

    def set_min_payload_byte(self, min_payload_byte):
        self.min_payload_byte = min_payload_byte

    def get_max_payload_byte(self):
        return self.max_payload_byte

    def set_max_payload_byte(self, max_payload_byte):
        self.max_payload_byte = max_payload_byte

    def get_freq(self):
        return self.freq

    def set_freq(self, freq):
        self.freq = freq
        self.uhd_usrp_sink_0.set_center_freq(self.freq, 0)
        self.uhd_usrp_source_0_1_0.set_center_freq(self.freq, 0)

    def get_GMSK_symb_per_byte(self):
        return self.GMSK_symb_per_byte

    def set_GMSK_symb_per_byte(self, GMSK_symb_per_byte):
        self.GMSK_symb_per_byte = GMSK_symb_per_byte
        self.blocks_tagged_stream_multiply_length_0.set_scalar((self.GMSK_symb_per_byte*self.GMSK_samp_per_symb))

    def get_GMSK_samp_per_symb(self):
        return self.GMSK_samp_per_symb

    def set_GMSK_samp_per_symb(self, GMSK_samp_per_symb):
        self.GMSK_samp_per_symb = GMSK_samp_per_symb
        self.blocks_tagged_stream_multiply_length_0.set_scalar((self.GMSK_symb_per_byte*self.GMSK_samp_per_symb))




def main(top_block_cls=pvburstgmsksc16loopbackex, options=None):

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
