

#ifndef lint
static const char rcsid[] _U_ =
    "@(#) $Header: /tcpdump/master/tcpdump/oui.c,v 1.4.2.1 2005/04/17 01:20:56 guy Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tcpdump-stdinc.h>
#include "interface.h"
#include "oui.h"

/* FIXME complete OUI list using a script */

struct tok oui_values[] = {
    { OUI_ENCAP_ETHER, "Ethernet" },
    { OUI_CISCO, "Cisco" },
    { OUI_NORTEL, "Nortel Networks SONMP" },
    { OUI_CISCO_90, "Cisco bridged" },
    { OUI_RFC2684, "Ethernet bridged" },
    { OUI_ATM_FORUM, "ATM Forum" },
    { OUI_CABLE_BPDU, "DOCSIS Spanning Tree" },
    { OUI_APPLETALK, "Appletalk" },
    { OUI_JUNIPER, "Juniper" },
    { OUI_HP, "Hewlett-Packard" },
    { 0, NULL }
};

struct tok smi_values[] = {
    { SMI_IETF,                 "IETF (reserved)"},
    { SMI_ACC,                  "ACC"},
    { SMI_CISCO,                "Cisco"},
    { SMI_HEWLETT_PACKARD,      "Hewlett Packard"},
    { SMI_SUN_MICROSYSTEMS,     "Sun Microsystems"},
    { SMI_MERIT,                "Merit"},
    { SMI_SHIVA,                "Shiva"},
    { SMI_ERICSSON,             "Ericsson AB"},
    { SMI_CISCO_VPN5000,        "Cisco VPN 5000"},
    { SMI_LIVINGSTON,           "Livingston"},
    { SMI_MICROSOFT,            "Microsoft"},
    { SMI_3COM,                 "3Com"},
    { SMI_ASCEND,               "Ascend"},
    { SMI_BAY,                  "Bay Networks"},
    { SMI_FOUNDRY,              "Foundry"},
    { SMI_VERSANET,             "Versanet"},
    { SMI_REDBACK,              "Redback"},
    { SMI_JUNIPER,              "Juniper Networks"},
    { SMI_APTIS,                "Aptis"},
    { SMI_CISCO_VPN3000,        "Cisco VPN 3000"},
    { SMI_COSINE,               "CoSine Communications"},
    { SMI_NETSCREEN,            "Netscreen"},
    { SMI_SHASTA,               "Shasta"},
    { SMI_NOMADIX,              "Nomadix"},
    { SMI_SIEMENS,              "Siemens"},
    { SMI_CABLELABS,            "CableLabs"},
    { SMI_UNISPHERE,            "Unisphere Networks"},
    { SMI_CISCO_BBSM,           "Cisco BBSM"},
    { SMI_THE3GPP2,             "3rd Generation Partnership Project 2 (3GPP2)"},
    { SMI_IP_UNPLUGGED,         "ipUnplugged"},
    { SMI_ISSANNI,              "Issanni Communications"},
    { SMI_QUINTUM,              "Quintum"},
    { SMI_INTERLINK,            "Interlink"},
    { SMI_COLUBRIS,             "Colubris"},
    { SMI_COLUMBIA_UNIVERSITY,  "Columbia University"},
    { SMI_THE3GPP,              "3GPP"},
    { SMI_GEMTEK_SYSTEMS,       "Gemtek-Systems"},
    { SMI_WIFI_ALLIANCE,        "Wi-Fi Alliance"},
    { 0, NULL}
};
