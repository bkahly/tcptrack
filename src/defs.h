// after a connection has been closed for this many seconds,
// remove it
#define CLOSED_PURGE 2

// This is the amount of time pcap_loop will wait for more packets before
// passing what it has to tcptrack. This is in seconds.
// This is used for the third argument to pcap_open_live
#define PCAP_DELAY 0.001

// the amount of time we will wait for a connection to finish opening after
// the initial syn has been sent before we discard it
#define SYN_SYNACK_WAIT 30

// connections in the CLOSING state are removed after this timeout
#define FIN_FINACK_WAIT 60

// pcap snaplen. Should be as long as biggest link level header len + 
// vlan header len + IP header len + tcp header len.
#define SNAPLEN 100

// stack sizes for the different threads
#define SS_PB  2048 // PacketBuffer
#define SS_S   4096 // Sniffer 2048 -> segfault on freebsd
#define SS_TCC 4096 // TCContainer
#define SS_TUI 5120 // TextUI. 4096 -> segfault on solaris
