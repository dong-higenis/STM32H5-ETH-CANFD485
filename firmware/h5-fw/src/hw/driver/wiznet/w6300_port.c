/*
 * w6300_port.c  -  WIZnet 라이브러리와 우리 QSPI 를 잇는 "어댑터"
 *
 *  WIZnet 라이브러리(lib/wiznet)는 칩을 만질 때 항상 아래 4개 함수만 부른다.
 *    WIZCHIP_READ / WIZCHIP_WRITE / WIZCHIP_READ_BUF / WIZCHIP_WRITE_BUF
 *  이 4개를 우리 보드의 QSPI(wiz_qspi.c)로 구현해 주면 라이브러리 전체가 동작한다.
 *
 *  AddrSel(32비트) 안에 주소가 이렇게 들어 있다:
 *      [ 23 ........ 8 ][ 7 ..... 0 ]
 *        레지스터 주소      block
 *
 *  아래쪽 getSn_TX_FSR() ~ wiz_mdio_read() 는 WIZnet 원본(Baram stm32h5-w6300 의 w6300.c)과 같다.
 */
#include "hw_def.h"

#ifdef _USE_HW_WIZNET

#include "wiz_qspi.h"
#include "wiznet/wizchip_conf.h"


static inline uint8_t  addrBlock(uint32_t AddrSel) { return (uint8_t)(AddrSel & 0xFF); }
static inline uint16_t addrReg(uint32_t AddrSel)   { return (uint16_t)((AddrSel >> 8) & 0xFFFF); }


uint8_t WIZCHIP_READ(uint32_t AddrSel)
{
  uint8_t data = 0;

  wizQspiRead(addrBlock(AddrSel), addrReg(AddrSel), &data, 1);
  return data;
}

void WIZCHIP_WRITE(uint32_t AddrSel, uint8_t wb)
{
  wizQspiWrite(addrBlock(AddrSel), addrReg(AddrSel), &wb, 1);
}

void WIZCHIP_READ_BUF(uint32_t AddrSel, uint8_t *pBuf, datasize_t len)
{
  if (len <= 0) return;
  wizQspiRead(addrBlock(AddrSel), addrReg(AddrSel), pBuf, (uint32_t)len);
}

void WIZCHIP_WRITE_BUF(uint32_t AddrSel, uint8_t *pBuf, datasize_t len)
{
  if (len <= 0) return;
  wizQspiWrite(addrBlock(AddrSel), addrReg(AddrSel), pBuf, (uint32_t)len);
}


/* ---------------- 이하 WIZnet 원본 ---------------- */

uint16_t getSn_TX_FSR(uint8_t sn)
{
  uint16_t prev_val = -1, val = 0;
  do
  {
    prev_val = val;
    val      = WIZCHIP_READ(_Sn_TX_FSR_(sn));
    val      = (val << 8) + WIZCHIP_READ(WIZCHIP_OFFSET_INC(_Sn_TX_FSR_(sn), 1));
  } while (val != prev_val);
  return val;
}

uint16_t getSn_RX_RSR(uint8_t sn)
{
  uint16_t prev_val = -1, val = 0;
  do
  {
    prev_val = val;
    val      = WIZCHIP_READ(_Sn_RX_RSR_(sn));
    val      = (val << 8) + WIZCHIP_READ(WIZCHIP_OFFSET_INC(_Sn_RX_RSR_(sn), 1));
  } while (val != prev_val);
  return val;
}

void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
  uint16_t ptr     = 0;
  uint32_t addrsel = 0;
  ptr              = getSn_TX_WR(sn);
  addrsel          = ((uint32_t)ptr << 8) + WIZCHIP_TXBUF_BLOCK(sn);
  WIZCHIP_WRITE_BUF(addrsel, wizdata, len);
  ptr += len;
  setSn_TX_WR(sn, ptr);
}

void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
  uint16_t ptr     = 0;
  uint32_t addrsel = 0;
  if (len == 0)
  {
    return;
  }
  ptr     = getSn_RX_RD(sn);
  addrsel = ((uint32_t)ptr << 8) + WIZCHIP_RXBUF_BLOCK(sn);
  WIZCHIP_READ_BUF(addrsel, wizdata, len);
  ptr += len;
  setSn_RX_RD(sn, ptr);
}

void wiz_recv_ignore(uint8_t sn, uint16_t len)
{
  setSn_RX_RD(sn, getSn_RX_RD(sn) + len);
}

void wiz_delay_ms(uint32_t milliseconds)
{
  uint32_t i;
  for (i = 0; i < milliseconds; i++)
  {
    // Write any values to clear the TCNTCLKR register
    setTCNTRCLR(0xff);

    // Wait until counter register value reaches 10.(10 = 1ms : TCNTR is 100us tick counter register)
    while (getTCNTR() < 0x0a)
    {
    }
  }
}

#if (_PHY_IO_MODE_ == _PHY_IO_MODE_MII_)
void wiz_mdio_write(uint8_t phyregaddr, uint16_t var)
{
  setPHYRAR(phyregaddr);
  setPHYDIR(var);
  setPHYACR(PHYACR_WRITE);
  while (getPHYACR())
    ;
}

uint16_t wiz_mdio_read(uint8_t phyregaddr)
{
  setPHYRAR(phyregaddr);
  setPHYACR(PHYACR_READ);
  while (getPHYACR())
    ;
  return getPHYDOR();
}
#endif

#endif
