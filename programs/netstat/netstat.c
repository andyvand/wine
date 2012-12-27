/*
 * Copyright 2011-2012 André Hentschel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "netstat.h"
#include <winsock2.h>
#include <iphlpapi.h>
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(netstat);

static const WCHAR tcpW[] = {'T', 'C', 'P', 0};
static const WCHAR fmtport[] = {'%', 'd', 0};
static const WCHAR fmtip[] = {'%', 'd', '.', '%', 'd', '.', '%', 'd', '.', '%', 'd', 0};
static const WCHAR fmtn[] = {'\n', 0};
static const WCHAR fmtnn[] = {'\n', '%', 's', '\n', 0};
static const WCHAR fmtcolon[] = {'%', 's', ':', '%', 's', 0};
static const WCHAR fmttcpout[] = {' ', ' ', '%', '-', '6', 's', ' ', '%', '-', '2', '2', 's', ' ', '%', '-', '2', '2', 's', ' ', '%', 's', '\n', 0};

/* =========================================================================
 *  Output a unicode string. Ideally this will go to the console
 *  and hence required WriteConsoleW to output it, however if file i/o is
 *  redirected, it needs to be WriteFile'd using OEM (not ANSI) format
 * ========================================================================= */
static int __cdecl NETSTAT_wprintf(const WCHAR *format, ...)
{
    static WCHAR *output_bufW = NULL;
    static char  *output_bufA = NULL;
    static BOOL  toConsole    = TRUE;
    static BOOL  traceOutput  = FALSE;
#define MAX_WRITECONSOLE_SIZE 65535

    __ms_va_list parms;
    DWORD   nOut;
    int len;
    DWORD   res = 0;

    /*
     * Allocate buffer to use when writing to console
     * Note: Not freed - memory will be allocated once and released when
     *         xcopy ends
     */

    if (!output_bufW) output_bufW = HeapAlloc(GetProcessHeap(), 0,
                                              MAX_WRITECONSOLE_SIZE);
    if (!output_bufW) {
        WINE_FIXME("Out of memory - could not allocate 2 x 64K buffers\n");
        return 0;
    }

    __ms_va_start(parms, format);
    len = wvsprintfW(output_bufW, format, parms);
    __ms_va_end(parms);

    /* Try to write as unicode all the time we think its a console */
    if (toConsole) {
        res = WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE),
                            output_bufW, len, &nOut, NULL);
    }

    /* If writing to console has failed (ever) we assume its file
       i/o so convert to OEM codepage and output                  */
    if (!res) {
        BOOL usedDefaultChar = FALSE;
        DWORD convertedChars;

        toConsole = FALSE;

        /*
         * Allocate buffer to use when writing to file. Not freed, as above
         */
        if (!output_bufA) output_bufA = HeapAlloc(GetProcessHeap(), 0,
                                                  MAX_WRITECONSOLE_SIZE);
        if (!output_bufA) {
            WINE_FIXME("Out of memory - could not allocate 2 x 64K buffers\n");
            return 0;
        }

        /* Convert to OEM, then output */
        convertedChars = WideCharToMultiByte(GetConsoleOutputCP(), 0, output_bufW,
                                             len, output_bufA, MAX_WRITECONSOLE_SIZE,
                                             "?", &usedDefaultChar);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output_bufA, convertedChars,
                  &nOut, FALSE);
    }

    /* Trace whether screen or console */
    if (!traceOutput) {
        WINE_TRACE("Writing to console? (%d)\n", toConsole);
        traceOutput = TRUE;
    }
    return nOut;
}

WCHAR *NETSTAT_load_message(UINT id) {
    static WCHAR msg[2048];
    static const WCHAR failedW[]  = {'F','a','i','l','e','d','!','\0'};

    if (!LoadStringW(GetModuleHandleW(NULL), id, msg, sizeof(msg)/sizeof(WCHAR))) {
        WINE_FIXME("LoadString failed with %d\n", GetLastError());
        strcpyW(msg, failedW);
    }
    return msg;
}

WCHAR *NETSTAT_port_name(UINT port, WCHAR name[])
{
    /* FIXME: can we get the name? */
    sprintfW(name, fmtport, htons((WORD)port));
    return name;
}

WCHAR *NETSTAT_host_name(UINT ip, WCHAR name[])
{
    UINT nip;

    /* FIXME: can we get the name? */
    nip = htonl(ip);
    sprintfW(name, fmtip, (nip >> 24) & 0xFF, (nip >> 16) & 0xFF, (nip >> 8) & 0xFF, (nip) & 0xFF);
    return name;
}

void NETSTAT_tcp_table(void)
{
    PMIB_TCPTABLE table;
    DWORD err, size, i;
    WCHAR local[22], remote[22], state[22];
    WCHAR HostIp[MAX_HOSTNAME_LEN], HostPort[32];
    WCHAR RemoteIp[MAX_HOSTNAME_LEN], RemotePort[32];
    WCHAR Host[MAX_HOSTNAME_LEN + 32];
    WCHAR Remote[MAX_HOSTNAME_LEN + 32];

    size = sizeof(MIB_TCPTABLE);
    do
    {
        table = (PMIB_TCPTABLE)HeapAlloc(GetProcessHeap(), 0, size);
        err = GetTcpTable(table, &size, TRUE);
        if (err != NO_ERROR) HeapFree(GetProcessHeap(), 0, table);
    } while (err == ERROR_INSUFFICIENT_BUFFER);

    if (err) return;

    NETSTAT_wprintf(fmtnn, NETSTAT_load_message(IDS_TCP_ACTIVE_CONN));
    NETSTAT_wprintf(fmtn);
    strcpyW(local, NETSTAT_load_message(IDS_TCP_LOCAL_ADDR));
    strcpyW(remote, NETSTAT_load_message(IDS_TCP_REMOTE_ADDR));
    strcpyW(state, NETSTAT_load_message(IDS_TCP_STATE));
    NETSTAT_wprintf(fmttcpout, NETSTAT_load_message(IDS_TCP_PROTO), local, remote, state);

    for (i = 0; i < table->dwNumEntries; i++)
    {
        if ((table->table[i].dwState ==  MIB_TCP_STATE_CLOSE_WAIT) ||
            (table->table[i].dwState ==  MIB_TCP_STATE_ESTAB) ||
            (table->table[i].dwState ==  MIB_TCP_STATE_TIME_WAIT))
        {
            NETSTAT_host_name(table->table[i].dwLocalAddr, HostIp);
            NETSTAT_port_name(table->table[i].dwLocalPort, HostPort);
            NETSTAT_host_name(table->table[i].dwRemoteAddr, RemoteIp);
            NETSTAT_port_name(table->table[i].dwRemotePort, RemotePort);

            sprintfW(Host, fmtcolon, HostIp, HostPort);
            sprintfW(Remote, fmtcolon, RemoteIp, RemotePort);
            NETSTAT_wprintf(fmttcpout, tcpW, Host, Remote, NETSTAT_load_message(table->table[i].dwState));
        }
    }
    HeapFree(GetProcessHeap(), 0, table);
}

int wmain(int argc, WCHAR *argv[])
{
    WSADATA wsa_data;

    if (argc > 1)
    {
        int i;
        WINE_FIXME("stub:");
        for (i = 0; i < argc; i++)
            WINE_FIXME(" %s", wine_dbgstr_w(argv[i]));
        WINE_FIXME("\n");
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
    {
        WINE_ERR("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    NETSTAT_tcp_table();

    return 0;
}
