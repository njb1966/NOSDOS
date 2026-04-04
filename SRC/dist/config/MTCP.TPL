REM NOS-DOS: dist/config
REM MTCP.TPL - mTCP configuration template
REM {{VARIABLES}} are substituted by NOS-DETECT/genconf.c after DHCP
PACKETINT  0x60
HOSTNAME   nosdos
IPADDR     {{IP_ADDR}}
NETMASK    {{NETMASK}}
GATEWAY    {{GATEWAY}}
NAMESERVER {{DNS_SERVER}}
