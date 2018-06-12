#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <kern/e1000.h>
#include <kern/pmap.h>

// LAB 6: Your driver code here

struct tx_desc tx_queue[E1000_NTXDESC] __attribute__((aligned(16)));
struct tx_pkt  tx_pkt_buf[E1000_NTXDESC];
struct rcv_desc rcv_queue[E1000_NRCVDESC] __attribute__((aligned(16)));
struct rcv_pkt  rcv_pkt_buf[E1000_NRCVDESC];

static void
e1000_mem_init()
{
	memset(tx_queue, 0, sizeof(struct tx_desc) * E1000_NTXDESC);
	memset(tx_pkt_buf, 0, sizeof(struct tx_pkt) * E1000_NTXDESC);
  for(int i = 0; i < E1000_NTXDESC; ++i)
	{
		tx_queue[i].addr = PADDR(tx_pkt_buf[i].pkt);
		tx_queue[i].status |= E1000_TXD_STAT_DD;
	}

	memset(rcv_queue, 0, sizeof(struct rcv_desc) * E1000_NRCVDESC);
	memset(rcv_pkt_buf, 0, sizeof(struct rcv_pkt) * E1000_NRCVDESC);
	for(int i = 0; i < E1000_NRCVDESC; ++i)
	{
		rcv_queue[i].addr = PADDR(rcv_pkt_buf[i].pkt);
	}
}

static uint16_t
e1000_read_eeprom(uint8_t addr)
{
  volatile uint32_t* eerd = (uint32_t*)(e1000 + E1000_EERD);
  *eerd = (addr << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START;
  while ((*eerd & E1000_EERD_DONE) == 0); /* wait until done*/
  return *eerd >> 16;
}

int
e1000_attach(struct pci_func *f)
{
  pci_func_enable(f);
  boot_map_region(kern_pgdir, E1000_BAR0, f->reg_size[0], f->reg_base[0], PTE_W|PTE_PCD|PTE_PWT);
  e1000 = (void*)E1000_BAR0;

  //pre-allocate buffers
  e1000_mem_init();

  //initialize TDBAL & TDBAH
  uint32_t* tdbal = (uint32_t*)(e1000 + E1000_TDBAL);
  *tdbal = PADDR(tx_queue);
  uint32_t* tdbah = (uint32_t*)(e1000 + E1000_TDBAH);
  *tdbah = 0;

  //initialize TDLEN
  uint32_t* tdlen = (uint32_t*)(e1000 + E1000_TDLEN);
  *tdlen = sizeof(struct tx_desc) * E1000_NTXDESC;

  //initialize TDH & TDT
  uint32_t* tdh = (uint32_t*)(e1000 + E1000_TDH);
  *tdh = 0;
  uint32_t* tdt = (uint32_t*)(e1000 + E1000_TDT);
  *tdt = 0;

  //initialize TCTL
  uint32_t* tctl = (uint32_t*)(e1000 + E1000_TCTL);
  *tctl |= (E1000_TCTL_EN|E1000_TCTL_PSP|E1000_TCTL_CT|E1000_TCTL_COLD);

  //initialize TIPG
  uint32_t* tipg = (uint32_t*)(e1000 + E1000_TIPG);
  *tipg |= (E1000_TIPG_IPGT|E1000_TIPG_IPGR1|E1000_TIPG_IPGR2);

	// read mac from eeprom
	uint32_t mac_l = e1000_read_eeprom(0x0);
  uint32_t mac_m = e1000_read_eeprom(0x1);
  uint32_t mac_h = e1000_read_eeprom(0x2);
  e1000_mac[0] = mac_l & 0xff;
  e1000_mac[1] = (mac_l >> E1000_EERD_ADDR_SHIFT) & 0xff;
  e1000_mac[2] = mac_m & 0xff;
  e1000_mac[3] = (mac_m >> E1000_EERD_ADDR_SHIFT) & 0xff;
  e1000_mac[4] = mac_h & 0xff;
  e1000_mac[5] = (mac_h >> E1000_EERD_ADDR_SHIFT) & 0xff;

  //initialize RA
  uint32_t* ral = (uint32_t*)(e1000 + E1000_RAL);
  //*ral = 0x12005452;
	*ral = (mac_m << 16) | mac_l;
  uint32_t* rah = (uint32_t*)(e1000 + E1000_RAH);
  //*rah = (0x00005634 | E1000_RAH_VALID);
	*rah =  (mac_h | E1000_RAH_VALID);

  //initialize RDBAL & RDBAH
  uint32_t* rdbal = (uint32_t*)(e1000 + E1000_RDBAL);
  *rdbal = PADDR(rcv_queue);
  uint32_t* rdbah = (uint32_t*)(e1000 + E1000_RDBAH);
  *rdbah = 0;

  //initialize RDLEN
  uint32_t* rdlen = (uint32_t*)(e1000 + E1000_RDLEN);
  *rdlen = sizeof(struct rcv_desc) * E1000_NRCVDESC;

  //initialize RDH & RDT
  uint32_t* rdh = (uint32_t*)(e1000 + E1000_RDH);
  *rdh = 0;
  uint32_t* rdt = (uint32_t*)(e1000 + E1000_RDT);
  *rdt = E1000_NRCVDESC - 1;

  //initialize RCTL
  uint32_t* rctl = (uint32_t*)(e1000 + E1000_RCTL);
  *rctl = (E1000_RCTL_EN|E1000_RCTL_SZ|E1000_RCTL_SECRC);

  return 0;
}

int
e1000_transmit(const char* data, uint32_t len)
{
  if(len > E1000_TX_PKT_LEN)
    return -E_INVAL;
  uint32_t* tdt = (uint32_t*)(e1000 + E1000_TDT);
  uint32_t cur = *tdt;
  if((tx_queue[cur].status & E1000_TXD_STAT_DD) == 0)
    return -E_TXQ_FULL;

  memmove(tx_pkt_buf[cur].pkt, data, len);
  tx_queue[cur].length = len;
  tx_queue[cur].status &= ~E1000_TXD_STAT_DD;
  tx_queue[cur].cmd |= (E1000_TXD_CMD_RS|E1000_TXD_CMD_EOP);

  *tdt = (cur + 1) % E1000_NTXDESC;
  return 0;
}

int
e1000_receive(char* data)
{
  uint32_t* rdt = (uint32_t*)(e1000 + E1000_RDT);
  uint32_t cur = (*rdt + 1) % E1000_NRCVDESC;
  if((rcv_queue[cur].status & E1000_RXD_STAT_DD) == 0)
    return -E_RCVQ_EMPTY;

  uint32_t len = rcv_queue[cur].length;
  memmove(data, rcv_pkt_buf[cur].pkt, len);
  rcv_queue[cur].status &= ~E1000_RXD_STAT_DD;

  *rdt = cur;
  return len;
}
