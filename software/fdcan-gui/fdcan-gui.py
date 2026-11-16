import sys
import time
import os
import socket
import serial
import serial.tools.list_ports as sp
import struct

from os import path
from PySide6.QtWidgets import QApplication, QMainWindow, QFileDialog
from PySide6.QtCore import QFile
from PySide6.QtGui import *
from ui.ui_main import *
from ui.ui_rs485 import *
from ui.ui_can import *

from lib.err_code import *
from lib.log import LogWidget
from lib.cmd import *
from lib.cmd_boot import *
from lib.net_find import findBoards
from lib.tcp_port import isNetAddress

from lib.down_firm import *
from tab.tab_can import *
from tab.tab_rs485 import *
from configparser import ConfigParser







class MainWindow(QMainWindow):
    
  def __init__(self):
    super(MainWindow, self).__init__()
    self.ui = Ui_MainWindow()
    self.ui.setupUi(self)

    self.font_size = 14
    self.update_file = ""
    self.conn_mode = "LAN"
    self.tab_can = None
    self.tab_rs485 = None

    self.loadInit()
    self.loadConfig()

    gui_ver = 'FDCAN-GUI 23-08-05'
    
    self.log = LogWidget(self.ui.log_text)
    self.log.setTimeLog(True)
    self.log.printLog(gui_ver + '\n')
    self.setWindowTitle(gui_ver)
    self.file_list = []
    self.cmd = Cmd()
    self.cmd_boot = CmdBoot(self.cmd)
    self.device_info = []
    self.is_fdcan = []
    self.down_thread = None

    self.syslog = LogWidget(self.ui.system_log)


    self.tab_can = TabCAN(self.ui, self.cmd, self.config_item, self.syslog)
    self.ui.tabWidget.addTab(self.tab_can, "CAN")
    self.tab_rs485 = TabRS485(self.ui, self.cmd, self.config_item, self.syslog)
    self.ui.tabWidget.addTab(self.tab_rs485, "RS485")

    self.updateFontSize(self.font_size)

    self.ui.action10.triggered.connect(lambda: self.updateFontSize(10))
    self.ui.action11.triggered.connect(lambda: self.updateFontSize(11))
    self.ui.action12.triggered.connect(lambda: self.updateFontSize(12))
    self.ui.action13.triggered.connect(lambda: self.updateFontSize(13))
    self.ui.action14.triggered.connect(lambda: self.updateFontSize(14))

    self.setClickedEvent(self.ui.btn_scan, self.btnScan)  
    self.setClickedEvent(self.ui.btn_open, self.btnOpen)  
    self.setClickedEvent(self.ui.btn_led, self.btnLed)  
    self.setClickedEvent(self.ui.btn_connect, self.btnConnect)  
    self.setClickedEvent(self.ui.btn_down, self.btnDown)  
    self.setClickedEvent(self.ui.btn_down_stop, self.btnDownStop)  
    self.setClickedEvent(self.ui.btn_log_clear, self.btnLogClear)  
    self.setClickedEvent(self.ui.btn_syslog_clear, self.btnSysLogClear)  

    self.ui.combo_device.currentTextChanged.connect(self.onComboDeviceChanged)

    # [h5-eth-fdcan] 연결 방법을 먼저 고른다: LAN 또는 COM
    #   LAN : Scan = 랜에서 보드 찾기. IP 를 직접 써넣어도 된다.
    #   COM : Scan = COM 포트 목록 (Baram USB, ST-Link VCP, USB-UART 등)
    self.ui.combo_device.setEditable(True)
    if self.conn_mode == "COM":
      self.ui.radio_mode_com.setChecked(True)
    else:
      self.ui.radio_mode_lan.setChecked(True)
    self.ui.radio_mode_lan.toggled.connect(self.onModeChanged)

    self.ui.btn_connect.setEnabled(False)
    self.ui.btn_down_stop.setEnabled(False)
    self.btnScan()
    self.updateBtn()

    if len(self.update_file) > 0:
      self.file_list.append(self.update_file)
      self.ui.combo_file.addItem(os.path.basename(self.update_file))
      self.log.printLog(os.path.dirname(self.update_file))

    self.cmd.setRxdEvent(self.rxdEvent)

  def rxdEvent(self, packet: CmdPacket):
    # packet = self.tab_rs485.event_q.get()
    if packet.type == PKT_TYPE_UART:
      self.tab_rs485.rxdEvent(packet)
    if packet.type == PKT_TYPE_CAN:
      self.tab_can.rxdEvent(packet)

  def loadInit(self):        
    self.config = ConfigParser() 
    self.config.optionxform = lambda optionstr: optionstr

    self.config['config'] = {}
    self.config_item = self.config['config']

    if os.path.exists("config.ini"):
      self.config.read('config.ini')  
    else:
      self.saveConfig()

  def loadConfig(self):        
    try:
      self.font_size = int(self.config_item['font_size'])
      self.update_file = self.config_item['update_file']
    except Exception as e:
      print(e)
    self.conn_mode = self.config_item.get('conn_mode', 'LAN')

  def saveConfig(self):  
    if self.tab_can is not None: 
      self.tab_can.saveConfig()
    if self.tab_rs485 is not None:
      self.tab_rs485.saveConfig()

    self.config_item['font_size'] = str(self.font_size)
    self.config_item['update_file'] = self.update_file
    self.config_item['conn_mode'] = self.conn_mode
    with open('config.ini', 'w') as configfile:
      self.config.write(configfile)

  def updateFontSize(self, font_size):
    self.font_size = font_size
    self.setFont(QFont("D2Coding", font_size))
    self.ui.log_text.setFont(QFont("D2Coding", font_size))
    self.tab_can.view_can_rx.setFont(QFont("D2Coding", font_size))
    self.tab_can.table_can_tx.setFont(QFont("D2Coding", font_size))

    font_check = [[10,self.ui.action10],
                  [11,self.ui.action11], 
                  [12,self.ui.action12], 
                  [13,self.ui.action13], 
                  [14,self.ui.action14],]

    for i in font_check:
      if i[0] == font_size:
        i[1].setChecked(True)
      else:
        i[1].setChecked(False)  
    self.saveConfig()      

  def __del__(self):
    self.cmd.stop()
    print("del()")

  def setClickedEvent(self, event_dst, event_func):
    event_dst.clicked.connect(lambda: self.btnClicked(event_dst, event_func))   

  def btnClicked(self, button, event_func):
    event_func()
    self.updateBtn()

  # [h5-eth-fdcan] LAN / COM 을 바꾸면 그 방식으로 다시 찾는다
  def onModeChanged(self, checked):
    if self.cmd.is_open:
      return                                # 연결 중에는 버튼이 꺼져 있다 (updateBtn)
    self.conn_mode = "LAN" if self.ui.radio_mode_lan.isChecked() else "COM"
    self.saveConfig()
    self.btnScan()

  def isLanMode(self):
    return self.ui.radio_mode_lan.isChecked()

  def btnScan(self):
    self.ui.btn_connect.setEnabled(False)

    self.ui.combo_device.clear()
    self.device_info.clear()
    self.is_fdcan.clear()

    if self.isLanMode():
      self.scanLan()
      self.ui.combo_device.lineEdit().setPlaceholderText("IP  (ex 192.168.0.50)")
    else:
      self.scanCom()
      self.ui.combo_device.lineEdit().setPlaceholderText("COMx")

  # LAN : 랜에 있는 보드 찾기 (UDP 5001)
  def scanLan(self):
    try:
      boards = findBoards()
    except Exception as e:
      boards = []
      self.log.printLog("LAN scan error : " + str(e))

    for b in boards:
      info = "LAN : %s\n    %s %s\n    MAC %s\n    %s%s" % (
        b["ip"], b["name"], b["version"], b["mac"],
        "DHCP" if b["dhcp"] else "static IP",
        "\n    (busy: other PC connected)" if b["busy"] else "")
      self.is_fdcan.append(True)
      self.device_info.append(info)
      self.ui.combo_device.addItem(b["ip"])
      self.ui.combo_device.setItemData(self.ui.combo_device.count()-1, b["name"] + " " + b["version"], Qt.ToolTipRole)

    self.log.printLog("LAN scan : %d board(s)%s" % (len(boards), "" if boards else "  (IP can be typed in)"))

  # COM : USB 로 꽂힌 COM 포트 (Bluetooth 가상 포트처럼 USB 가 아닌 것은 뺀다)
  #   Baram USB(0483:5740) 는 보드가 확실하다. 나머지(ST-Link VCP, USB-UART)는 연결할 때 버전을 물어본다.
  def scanCom(self):
    count = 0
    for i in sorted(sp.comports()):
      if i.vid is None:
        continue

      is_fdcan = (i.vid == 0x0483 and i.pid == 0x5740)
      desc = str(i.description or "").replace("(" + i.device + ")", "").strip()

      self.is_fdcan.append(is_fdcan)
      self.device_info.append("COM : %s\n    %s\n    S/N %s" % (i.device, desc, i.serial_number))
      self.ui.combo_device.addItem(i.device)
      self.ui.combo_device.setItemData(self.ui.combo_device.count()-1, desc, Qt.ToolTipRole)
      count += 1

    self.log.printLog("COM scan : %d port(s)" % count)

  # [h5-eth-fdcan] 지금 고른(또는 써넣은) 장치: (port, is_fdcan, info)
  def getSelectedDevice(self):
    port  = self.ui.combo_device.currentText().strip()
    index = self.ui.combo_device.currentIndex()

    # 목록에 있는 항목을 그대로 골랐으면 목록의 정보를 쓴다
    if 0 <= index < len(self.device_info) and self.ui.combo_device.itemText(index) == port:
      return port, self.is_fdcan[index], self.device_info[index]

    # 직접 써넣은 것
    if self.isLanMode():
      return port, True, "LAN : " + port + " (typed)"
    return port, False, "COM : " + port + " (typed)"

  def onComboDeviceChanged(self, value):
    port, is_fdcan, info = self.getSelectedDevice()
    self.ui.text_device_info.clear()
    if port == "":
      self.ui.btn_connect.setEnabled(False)
      return
    self.ui.text_device_info.appendPlainText(info)
    self.ui.btn_connect.setEnabled(True)
        
      
  def btnOpen(self):
    file_path = os.path.dirname(self.update_file)
    fname = QFileDialog.getOpenFileNames(self, "Open File", file_path, "Bin File(*.bin)")

    if len(fname[0]) > 0:
      self.update_file = fname[0][0]
      self.saveConfig()
      self.updateFileList(fname)
      self.showFileInfo()

  def btnLed(self):
    err_code, resp = self.cmd_boot.ledToggle(100)

  def btnLogClear(self):
    self.log.clear()

  def btnSysLogClear(self):
    self.syslog.clear()

  def btnConnect(self):
    if self.cmd.is_open:
      # [h5-eth-fdcan] COM 포트는 끊어도 보드가 모른다. 닫기 전에 CAN/RS485 를 닫아 달라고 말해 둔다.
      #   (랜은 보드가 접속 끊김을 알아서 스스로 닫는다)
      if not isNetAddress(self.cmd.port):
        try:
          self.tab_can.cmd_can.close(100)
          self.tab_rs485.cmd_rs485.close(100)
        except Exception:
          pass
      self.cmd.close()

      self.tab_can.btnUpdate()
      return

    # [h5-eth-fdcan] 고른 방식(LAN/COM)과 써넣은 것이 맞는지 먼저 본다
    port, is_fdcan, device_info = self.getSelectedDevice()
    if port == "":
      return

    if self.isLanMode():
      if not isNetAddress(port):
        self.log.printLog("LAN : '%s' is not an IP address  (ex 192.168.0.50)" % port)
        return
      baud = 0
      self.log.printLog("LAN : connect " + port)
    else:
      if isNetAddress(port):
        self.log.printLog("COM : '%s' looks like an IP. Select LAN." % port)
        return
      baud = 600 if is_fdcan else 115200   # Baram USB 는 속도 무관(600), UART 는 115200
      self.log.printLog("COM : open %s (%d)" % (port, baud))

    ret = self.cmd.open(port, baud)
    if ret == False:
      if self.isLanMode():
        self.log.printLog("LAN : connect fail (wrong IP, or another PC is connected?)")
      else:
        self.log.printLog("COM : open fail (port used by another program? e.g. TeraTerm)")
      return

    time.sleep(0.1)

    # [h5-eth-fdcan] 그냥 COM 포트(ST-Link VCP, USB-UART 등)도 보드일 수 있다.
    #   버전을 물어봐서 대답하면 보드로 다룬다. 대답이 없으면 예전처럼 그냥 COM 포트로 둔다.
    is_plain_com = (is_fdcan != True) and not isNetAddress(port)
    if is_plain_com:
      is_fdcan = True

    if is_fdcan == True:
      is_connected = False

      for i in range(3):
        err_code, resp = self.cmd_boot.readVersion(100)
        if err_code == OK:
          self.ui.text_device_info.clear()
          self.ui.text_device_info.appendPlainText(device_info)
          self.ui.text_device_info.appendPlainText("    ")

          resp.boot.name_str = resp.boot.name_str.replace('\x00', '')
          self.ui.text_device_info.appendPlainText(resp.boot.name_str)
          resp.boot.version_str = resp.boot.version_str.replace('\x00', '')
          self.ui.text_device_info.appendPlainText("    " + resp.boot.version_str)

          resp.firm.name_str = resp.firm.name_str.replace('\x00', '')
          self.ui.text_device_info.appendPlainText(resp.firm.name_str)
          resp.firm.version_str = resp.firm.version_str.replace('\x00', '')
          self.ui.text_device_info.appendPlainText("    " + resp.firm.version_str)     
          is_connected = True
          break
        time.sleep(0.1)

      if is_connected != True:
        if is_plain_com:
          self.log.printLog("no board answer (plain COM port)")
        else:
          self.log.printLog("Connect Fail")
          self.cmd.close()
      
  def isCanDown(self):
    if self.ui.btn_down.isEnabled() == False:
      return False    
    if self.down_thread is not None:
      if self.down_thread.isRunning() == True:
        return False
    return True

  def btnDown(self):
    # err_code, resp = self.cmd_boot.firmBegin(BootBegin("test.bin", 100))
    # if err_code == OK:
    #   self.log.printLog("firmBegin() OK")

    # err_code, resp = self.cmd_boot.firmErase(0, 1024)
    # if err_code == OK:
    #   self.log.printLog("firmErase() OK")

    # err_code, resp = self.cmd_boot.firmEnd()
    # if err_code == OK:
    #   self.log.printLog("firmEnd() OK")
    if self.isCanDown() == True:
      down_file = self.file_list[self.ui.combo_file.currentIndex()]
      self.down_thread = DownFirm(self, self.cmd_boot)
      self.down_thread.start_sig.connect(self.onStartDown)
      self.down_thread.finish_sig.connect(self.onFinishDown)
      self.down_thread.update_sig.connect(self.onUpdateDown)
      self.down_thread.log_sig.connect(self.onDownLog)
      self.down_thread.setDown(down_file)
      self.down_thread.start()

  def btnDownStop(self):
    if self.down_thread is not None:
      if self.down_thread.isRunning() == True:
        self.down_thread.stop()
        self.log.printLog("Down Stop")
    pass

  def updateFileList(self, fname):
    self.ui.combo_file.clear()
    self.file_list.clear()
    for i in fname[0]:
      self.file_list.append(i)
      self.ui.combo_file.addItem(os.path.basename(i))

  def showFileInfo(self):
    if self.ui.combo_file.count() == 0:
      return

  def updateBtn(self):
    if self.ui.combo_device.currentText().strip() == "":
      self.ui.btn_connect.setText("Connect")
      self.ui.btn_connect.setEnabled(False)
    else:
      if self.cmd.is_open:
        self.ui.btn_connect.setText("Disconnect")
        self.ui.btn_connect.setEnabled(True)   
      else:
        self.ui.btn_connect.setText("Connect")
        self.ui.btn_connect.setEnabled(True)   
        self.onComboDeviceChanged("")

    if self.cmd.is_open:
      self.ui.btn_scan.setEnabled(False)
      self.ui.combo_device.setEnabled(False)
      self.ui.btn_led.setEnabled(True)
    else:
      self.ui.btn_scan.setEnabled(True)
      self.ui.combo_device.setEnabled(True)
      self.ui.btn_led.setEnabled(False)

    # [h5-eth-fdcan] 연결 중에는 LAN/COM 을 못 바꾼다
    self.ui.radio_mode_lan.setEnabled(not self.cmd.is_open)
    self.ui.radio_mode_com.setEnabled(not self.cmd.is_open)

    if self.cmd.is_open and self.ui.combo_file.count() > 0:
      self.ui.btn_down.setEnabled(True)
    else:
      self.ui.btn_down.setEnabled(False)

    self.tab_rs485.btnUpdate()

  def onStartDown(self):
    self.log.printLog("Down Start..")
    self.ui.btn_down.setEnabled(False)
    self.ui.btn_down_stop.setEnabled(True)

  def onFinishDown(self, err_code):
    self.log.printLog("Down Finish.." + str(err_code))    
    self.ui.btn_down.setEnabled(True)
    self.ui.btn_down_stop.setEnabled(False)
    if err_code == 0:
      self.btnConnect()
      self.updateBtn()

  def onUpdateDown(self, update_value):
    self.ui.prog_bar_down.setValue(update_value)

  def onDownLog(self, data):
    self.log.printLog(data)
    

  def closeEvent(self, QCloseEvent):
    self.saveConfig()
    if self.down_thread is not None:
      self.down_thread.stop()     
    QCloseEvent.accept()




def main():
  app = QApplication(sys.argv)
  
  fontpath = os.path.abspath("data/font/D2Coding.ttf")
  QFontDatabase.addApplicationFont(fontpath)
  app.setFont(QFont("D2Coding", 14))
  
  window = MainWindow()
  center = QScreen.availableGeometry(QApplication.primaryScreen()).center()
  geo = window.frameGeometry()
  geo.moveCenter(center)
  window.move(geo.topLeft())
  window.show()

  sys.exit(app.exec())


if __name__ == "__main__":  
  main()