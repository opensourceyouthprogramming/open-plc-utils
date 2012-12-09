/*====================================================================*
 *   
 *   Copyright (c) 2011 by Qualcomm Atheros.
 *   
 *   Permission to use, copy, modify, and/or distribute this software 
 *   for any purpose with or without fee is hereby granted, provided 
 *   that the above copyright notice and this permission notice appear 
 *   in all copies.
 *   
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL 
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED 
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL  
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR 
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, 
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *   
 *--------------------------------------------------------------------*/

/*====================================================================*
 *
 *   plctest.c - Atheros Test Applet Loader;
 *
 *.  Qualcomm Atheros HomePlug AV Powerline Toolkit
 *:  Published 2009-2011 by Qualcomm Atheros. ALL RIGHTS RESERVED
 *;  For demonstration and evaluation only. Not for production use
 *
 *   Contributor(s):
 *      Charles Maier <cmaier@qualcomm.com>
 *      Matthieu Poullet <m.poullet@avm.de>
 *
 *--------------------------------------------------------------------*/

#define _GETOPT_H

/*====================================================================*"
 *   system header files;
 *--------------------------------------------------------------------*/

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

/*====================================================================*
 *   custom header files;
 *--------------------------------------------------------------------*/

#include "../tools/getoptv.h"
#include "../tools/putoptv.h"
#include "../tools/memory.h"
#include "../tools/number.h"
#include "../tools/types.h"
#include "../tools/flags.h"
#include "../tools/files.h"
#include "../tools/error.h"
#include "../ether/channel.h"
#include "../ram/nvram.h"
#include "../ram/sdram.h"
#include "../nvm/nvm.h"
#include "../pib/pib.h"
#include "../plc/plc.h"

/*====================================================================*
 *   custom source files;
 *--------------------------------------------------------------------*/

#ifndef MAKEFILE
#include "../plc/chipset.c"
#include "../plc/Confirm.c"
#include "../plc/Devices.c"
#include "../plc/Display.c"
#include "../plc/Failure.c"
#include "../plc/Request.c"
#include "../plc/SendMME.c"
#include "../plc/WriteFirmware1.c"
#include "../plc/StartFirmware1.c"
#include "../plc/WaitForStart.c"
#endif

#ifndef MAKEFILE
#include "../tools/getoptv.c"
#include "../tools/putoptv.c"
#include "../tools/version.c"
#include "../tools/uintspec.c"
#include "../tools/checkfilename.c"
#include "../tools/hexdecode.c"
#include "../tools/todigit.c"
#include "../tools/hexdump.c"
#include "../tools/checksum32.c"
#include "../tools/fdchecksum32.c"
#include "../tools/error.c"
#include "../tools/strfbits.c"
#include "../tools/typename.c"
#endif

#ifndef MAKEFILE
#include "../ether/openchannel.c"
#include "../ether/closechannel.c"
#include "../ether/readpacket.c"
#include "../ether/sendpacket.c"
#include "../ether/channel.c"
#endif

#ifndef MAKEFILE
#include "../nvm/nvmfile1.c"
#endif

#ifndef MAKEFILE
#include "../mme/MMECode.c"
#include "../mme/EthernetHeader.c"
#include "../mme/QualcommHeader.c"
#include "../mme/UnwantedMessage.c"
#endif

/*====================================================================*
 *
 *   signed ReadMME (struct plc * plc, uint8_t MMV, uint16_t MMTYPE);
 *
 *   plc.h        
 *
 *   this is a custom version of ReadMME that intercepts VS_ARPC 
 *   messages and print them on the console in human readable format;
 *   
 *   this implementation was an early attempt the does not take full
 *   advantage of the VS_ARPC structure and so messages are printed 
 *   in a format that is incompatible with the Windows Device Manager; 
 *   program amptest corrects this problem by calling ARPCPrint; 
 *
 *   this function is maintained for legacy purposes only and will be
 *   deleted at some point in the future;
 *
 *.  Qualcomm Atheros HomePlug AV Powerline Toolkit
 *:  Published 2009-2011 by Qualcomm Atheros. ALL RIGHTS RESERVED
 *;  For demonstration and evaluation only. Not for production use
 *
 *   Contributor(s):
 *      Charles Maier <cmaier@qualcomm.com>
 *
 *--------------------------------------------------------------------*/

signed ReadMME (struct plc * plc, uint8_t MMV, uint16_t MMTYPE) 

{
	struct channel * channel = (struct channel *)(plc->channel);
	struct message * message = (struct message *)(plc->message);

#ifndef __GNUC__
#pragma pack (push,1)
#endif

	static qualcomm_std header_arpc = 
	{
		0,
		VS_ARPC | MMTYPE_IND,
		{
			0x00,
			0xB0,
			0x52
		}
	};
	struct __packed vs_arpc_ind 
	{
		struct ethernet_std ethernet;
		struct qualcomm_std qualcomm;
		uint16_t RDATALENGTH;
		uint8_t RDATAOFFSET;
		uint8_t RDATA [1];
	}
	* indicate = (struct vs_arpc_ind *)(plc->message);

#ifndef __GNUC__
#pragma pack (pop)
#endif

	while ((plc->packetsize = readpacket (channel, message, sizeof (* message))) > 0) 
	{

#if 1

/*
 *	Normally this block of code would be replaced by function FirmwareMessage () but due to an
 *	ARPC implmenetation error on early diagnostic applets we need to use this code instead;
 */

		header_arpc.MMTYPE = HTOLE16 (header_arpc.MMTYPE);
		if (!memcmp (&indicate->qualcomm, &header_arpc, sizeof (header_arpc))) 
		{
			indicate->RDATA [indicate->RDATALENGTH - indicate->RDATAOFFSET] = (char)(0);
			Display (plc, "0x%02X %s", indicate->RDATA [0], &indicate->RDATA [5]);
			continue;
		}

#endif

		if (UnwantedMessage (message, plc->packetsize, 0, MMTYPE)) 
		{
			continue;
		}
		break;
	}
	return (plc->packetsize);
}


/*====================================================================*
 *  
 *   signed sequence (struct plc * plc);
 *
 *   plc.h
 *  
 *   sequentially download and execute all modules found in a .nvm 
 *   file regardless of module type; users must ensure that modules
 *   are executable or results may be unpredictable;
 *
 *   DO NOT USE THIS FUNCTION TO DOWNBOOT AND EXECUTE RUNTIME FIRMWARE;
 *
 *.  Qualcomm Atheros HomePlug AV Powerline Toolkit
 *:  Published 2009-2011 by Qualcomm Atheros. ALL RIGHTS RESERVED
 *;  For demonstration and evaluation only. Not for production use
 *
 *   Contributor(s):
 *      Charles Maier <cmaier@qualcomm.com>
 *
 *--------------------------------------------------------------------*/

static signed sequence (struct plc * plc) 

{
	struct nvm_header1 nvm_header;
	unsigned module = 0;
	do 
	{
		if (read (plc->NVM.file, &nvm_header, sizeof (nvm_header)) != sizeof (nvm_header)) 
		{
			if (_allclr (plc->flags, PLC_SILENCE)) 
			{
				error (0, errno, NVM_HDR_CANTREAD, plc->NVM.name, module);
			}
			return (-1);
		}
		if (LE32TOH (nvm_header.HEADERVERSION) != 0x60000000) 
		{
			if (_allclr (plc->flags, PLC_SILENCE)) 
			{
				error (0, 0, NVM_HDR_VERSION, plc->NVM.name, module);
			}
			return (-1);
		}
		if (checksum32 (&nvm_header, sizeof (nvm_header), 0)) 
		{
			if (_allclr (plc->flags, PLC_SILENCE)) 
			{
				error (0, 0, NVM_HDR_CHECKSUM, plc->NVM.name, module);
			}
			return (-1);
		}
		if (WriteFirmware1 (plc, module, &nvm_header)) 
		{
			return (-1);
		}
		if (StartFirmware1 (plc, module, &nvm_header)) 
		{
			return (-1);
		}
		if (_anyset (plc->flags, PLC_QUICK_FLASH)) 
		{
			break;
		}
		while (!ReadMME (plc, 0, (VS_HST_ACTION | MMTYPE_IND)));
		module++;
	}
	while (nvm_header.NEXTHEADER);
	return (0);
}


/*====================================================================*
 *
 *   void function (struct plc * plc, int argc, char const * argv);
 *
 *   execute all applets in each file on the command line; return no
 *   value; this function may be called repeatedly;
 *
 *.  Qualcomm Atheros HomePlug AV Powerline Toolkit
 *:  Published 2009-2011 by Qualcomm Atheros. ALL RIGHTS RESERVED
 *;  For demonstration and evaluation only. Not for production use
 *
 *--------------------------------------------------------------------*/

static void function (struct plc * plc, int argc, char const * argv []) 

{
	while ((argc) && (* argv)) 
	{
		if ((plc->NVM.file = open (plc->NVM.name = * argv, O_BINARY|O_RDONLY)) == -1) 
		{
			error (0, errno, "%s", plc->NVM.name);
		}
		else if (nvmfile1 (&plc->NVM)) 
		{
			error (0, errno, "Won't load %s", plc->NVM.name);
		}
		else if (sequence (plc)) 
		{
			error (0, errno, "Abandoning %s", plc->NVM.name);
		}
		close (plc->NVM.file);
		argc--;
		argv++;
	}
	return;
}


/*====================================================================*
 *   
 *   int main (int argc, char const * argv[]);
 *   
 *   parse command line, populate plc structure and perform selected 
 *   operations; show help summary when asked; see getoptv and putoptv
 *   to understand command line parsing and help summary display; see
 *   plc.h for the definition of struct plc; 
 *
 *   the default interface is eth1 because most people use eth0 as 
 *   their principle network connection; you can specify another 
 *   interface with -i or define environment string PLC to make
 *   that the default interface and save typing;
 *   
 *.  Qualcomm Atheros HomePlug AV Powerline Toolkit
 *:  Published 2009-2011 by Qualcomm Atheros. ALL RIGHTS RESERVED
 *;  For demonstration and evaluation only. Not for production use
 *
 *--------------------------------------------------------------------*/

int main (int argc, char const * argv []) 

{
	extern struct channel channel;
	char firmware [PLC_VERSION_STRING];
	static char const * optv [] = 
	{
		"c:ei:lp:qt:vx",
		"file [file] [...]",
		"Qualcomm Atheros Test Applet Loader",
		"e\tredirect stderr to stdout",

#if defined (WINPCAP) || defined (LIBPCAP)

		"i n\thost interface is (n) [" LITERAL (CHANNEL_ETHNUMBER) "]",

#else

		"i s\thost interface is (s) [" LITERAL (CHANNEL_ETHDEVICE) "]",

#endif

		"l\tloop until program is terminated",
		"q\tquiet mode",
		"t n\tread timeout is (n) milliseconds [" LITERAL (CHANNEL_TIMEOUT) "]",
		"v\tverbose mode",
		"x\texit on error",
		(char const *) (0)
	};

#include "../plc/plc.c"

	signed c;
	if (getenv (PLCDEVICE)) 
	{

#if defined (WINPCAP) || defined (LIBPCAP)

		channel.ifindex = atoi (getenv (PLCDEVICE));

#else

		channel.ifname = strdup (getenv (PLCDEVICE));

#endif

	}
	optind = 1;
	while ((c = getoptv (argc, argv, optv)) != -1) 
	{
		switch (c) 
		{
		case 'e':
			dup2 (STDOUT_FILENO, STDERR_FILENO);
			break;
		case 'i':

#if defined (WINPCAP) || defined (LIBPCAP)

			channel.ifindex = atoi (optarg);

#else

			channel.ifname = optarg;

#endif

			break;
		case 'l':
			_setbits (plc.flags, PLC_FOREVER);
			break;
		case 'q':
			_setbits (channel.flags, CHANNEL_SILENCE);
			_setbits (plc.flags, PLC_SILENCE);
			break;
		case 't':
			channel.timeout = (signed)(uintspec (optarg, 0, UINT_MAX));
			break;
		case 'v':
			_setbits (channel.flags, CHANNEL_VERBOSE);
			_setbits (plc.flags, PLC_VERBOSE);
			break;
		case 'x':
			_setbits (plc.flags, PLC_BAILOUT);
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;
	openchannel (&channel);
	if (!(plc.message = malloc (sizeof (* plc.message)))) 
	{
		error (1, errno, PLC_NOMEMORY);
	}
	if (WaitForStart (&plc, firmware, sizeof (firmware))) 
	{
		error (1, ECANCELED, PLC_NODETECT);
	}
	if (strcmp (firmware, "BootLoader")) 
	{
		error (1, ECANCELED, "BootLoader must be running");
	}
	do 
	{
		function (&plc, argc, argv);
	}
	while (_anyset (plc.flags, PLC_FOREVER));
	free (plc.message);
	closechannel (&channel);
	return (plc.state);
}
