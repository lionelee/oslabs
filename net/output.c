#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver

	int r;
  while(1){
		r = sys_ipc_recv(&nsipcbuf);
    if(r  < 0) panic("net_output:sys_ipc_recv: %e\n",r);

    if (thisenv->env_ipc_value == NSREQ_OUTPUT){
	    while((r = sys_net_try_transmit(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) < 0){
			  if(r != -E_TXQ_FULL) panic("net_output:sys_net_try_transmit: %e\n", r);
			}
  	}
	}
}
