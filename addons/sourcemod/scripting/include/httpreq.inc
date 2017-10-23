#if defined _httpreq_included
    #endinput
#endif
#define _httpreq_included

#include <socket>

#define MAX_ACTIVE_REQUESTS 15
#define MAX_URL_LENGTH 2084
#define MAX_BODY_SIZE 8192

Handle    g_hSockets[MAX_ACTIVE_REQUESTS];
StringMap g_tHeaders[MAX_ACTIVE_REQUESTS];
StringMap g_tParams[MAX_ACTIVE_REQUESTS];
char      g_sMethod[MAX_ACTIVE_REQUESTS][6];
char      g_sURL[MAX_ACTIVE_REQUESTS][MAX_URL_LENGTH];
char      g_sResponseCallback[MAX_ACTIVE_REQUESTS][32];
bool      g_bConnecting[MAX_ACTIVE_REQUESTS];
bool      g_bConnected[MAX_ACTIVE_REQUESTS];
bool      g_bReady[MAX_ACTIVE_REQUESTS];
bool      g_bDebug[MAX_ACTIVE_REQUESTS];
any       g_aData[MAX_ACTIVE_REQUESTS];

methodmap HTTPRequest
{
    public HTTPRequest(const char[] method, const char[] sURL, const char[] sResponseCallback, any data=0)
    {
        for (int i = 0; i < MAX_ACTIVE_REQUESTS; ++i) {
            if (g_hSockets[i] == null) {
                g_hSockets[i] = SocketCreate(SOCKET_TCP, HTTPRequest_OnError);
                SocketSetArg(g_hSockets[i], i);

                g_tHeaders[i] = CreateTrie();
                g_tParams[i] = CreateTrie();

                strcopy(g_sMethod[i], sizeof(g_sMethod[]), method);

                // hope it's http but ignore protocol regardless
                char sURLParts[2][MAX_URL_LENGTH];
                g_sURL[i] = (ExplodeString(sURL, "://", sURLParts, sizeof(sURLParts), sizeof(sURLParts[]), true) == 2) ? sURLParts[1] : sURLParts[0];

                strcopy(g_sResponseCallback[i], sizeof(g_sResponseCallback[]), sResponseCallback);
                g_bConnecting[i] = false;
                g_bConnected[i] = false;
                g_bReady[i] = false;
                g_bDebug[i] = false;
                g_aData[i] = data;

                return view_as<HTTPRequest>(i);
            }
        }

        PrintToServer("httpreq::HTTPRequest could not be created, max active requests (%d) reached", MAX_ACTIVE_REQUESTS);
        return view_as<HTTPRequest>(0);
    }

    property int index 
    { 
        public get()
        {
            return view_as<int>(this);
        }
    }

    property Handle socket
    {
        public get()
        {
            return g_hSockets[this.index];
        }
    }

    property StringMap headers
    {
        public get()
        {
            return g_tHeaders[this.index];
        }
    }

    property StringMap params
    {
        public get()
        {
            return g_tParams[this.index];
        }
    }

    property bool connecting
    {
        public get()
        {
            return g_bConnecting[this.index];
        }

        public set(bool value)
        {
            g_bConnecting[this.index] = value;
        }
    }

    property bool connected
    {
        public get()
        {
            return g_bConnected[this.index];
        }

        public set(bool value)
        {
            g_bConnected[this.index] = value;
        }
    }

    property bool ready
    {
        public get()
        {
            return g_bReady[this.index];
        }

        public set(bool value)
        {
            g_bReady[this.index] = value;
        }
    }

    property bool debug
    {
        public get()
        {
            return g_bDebug[this.index];
        }

        public set(bool value)
        {
            g_bDebug[this.index] = value;
        }
    }

    property any data
    {
        public get()
        {
            return g_aData[this.index];
        }

        public set(any value)
        {
            g_aData[this.index] = value;
        }
    }

    public void Connect()
    {
        this.connecting = true;
        char sFullHost[1][MAX_URL_LENGTH];
        ExplodeString(g_sURL[this.index], "/", sFullHost, sizeof(sFullHost), sizeof(sFullHost[]), false);

        char sHostParts[2][MAX_URL_LENGTH];
        if (ExplodeString(sFullHost[0], ":", sHostParts, sizeof(sHostParts), sizeof(sHostParts[]), false) != 2) {
            strcopy(sHostParts[1], sizeof(sHostParts[]), "80");
        }

        char sHost[MAX_URL_LENGTH];
        strcopy(sHost, sizeof(sHost), sHostParts[0]);

        int iPort = StringToInt(sHostParts[1]);
        if (iPort <= 0) {
            iPort = 80;
        }

        if (this.debug) {
            PrintToServer("httpreq::HTTPRequest %d socket connecting to %s:%d", this.index, sHost, iPort);
        }

        SocketConnect(this.socket, HTTPRequest_OnConnect, HTTPRequest_OnReceive, HTTPRequest_OnDisconnect, sHost, iPort);
    }

    public bool SendRequest()
    {
        if (!this.connected) { // not ready to request yet
            this.ready = true; // request as soon as connected
            if (!this.connecting) {
                this.Connect(); // auto connect if not already trying to
            }
            return false;
        }

        // split URL between host and path
        char sURLParts[2][MAX_URL_LENGTH];
        ExplodeString(g_sURL[this.index], "/", sURLParts, sizeof(sURLParts), sizeof(sURLParts[]), true);
        
        char sHost[MAX_URL_LENGTH], sPath[MAX_URL_LENGTH];
        strcopy(sHost, sizeof(sHost), sURLParts[0]);
        strcopy(sPath, sizeof(sPath), "/");
        StrCat(sPath, sizeof(sPath), sURLParts[1]);

        // use this for tmp string storage
        char sBuilder[MAX_BODY_SIZE], sKey[MAX_BODY_SIZE], sValue[MAX_BODY_SIZE];

        // build query string
        char sQueryString[MAX_BODY_SIZE];
        StringMapSnapshot tParamsSnapshot = this.params.Snapshot();
        for (int i = 0; i < tParamsSnapshot.Length; ++i) {
            tParamsSnapshot.GetKey(i, sKey, sizeof(sKey));
            this.params.GetString(sKey, sValue, sizeof(sValue));
            Format(sBuilder, sizeof(sBuilder), "%s=%s", sKey, sValue);
            if (i + 1 < tParamsSnapshot.Length) { // append & for all except final param
                StrCat(sBuilder, sizeof(sBuilder), "&");
            }
            StrCat(sQueryString, sizeof(sQueryString), sBuilder);
        }

        // add query string to path if GET
        if (StrEqual(g_sMethod[this.index], "GET", false) && tParamsSnapshot.Length > 0) {
            if (StrContains(g_sMethod[this.index], "?") == -1) {
                StrCat(sPath, sizeof(sPath), "?");
            } else {
                StrCat(sPath, sizeof(sPath), "&");
            }
            StrCat(sPath, sizeof(sPath), sQueryString);
        }

        // build base HTTP request
        char sRequest[MAX_BODY_SIZE];
        Format(sBuilder, sizeof(sBuilder), "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n", g_sMethod[this.index], sPath, sHost);
        StrCat(sRequest, sizeof(sRequest), sBuilder);

        // add content headers if POST
        if (!StrEqual(g_sMethod[this.index], "GET", false)) {
            char sQuerySize[128];
            IntToString(strlen(sQueryString), sQuerySize, sizeof(sQuerySize));

            this.headers.SetString("Content-Type", "application/x-www-form-urlencoded");
            this.headers.SetString("Content-Length", sQuerySize);
        }

        // send out headers
        StringMapSnapshot tHeaderSnapshot = this.headers.Snapshot();
        for (int i = 0; i < tHeaderSnapshot.Length; ++i) {
            tHeaderSnapshot.GetKey(i, sKey, sizeof(sKey));
            this.headers.GetString(sKey, sValue, sizeof(sValue));
            Format(sBuilder, sizeof(sBuilder), "%s: %s\r\n", sKey, sValue);
            StrCat(sRequest, sizeof(sRequest), sBuilder);
        }
        StrCat(sRequest, sizeof(sRequest), "\r\n");

        // add query string to body if not GET
        if (!StrEqual(g_sMethod[this.index], "GET", false)) {
            StrCat(sRequest, sizeof(sRequest), sQueryString);
        }
        StrCat(sRequest, sizeof(sRequest), "\r\n");

        if (this.debug) {
            PrintToServer("httpreq::HTTPRequest %d socket making request:\n%s", this.index, sRequest);
        }

        SocketSend(this.socket, sRequest);
        return true;
    }
}

public int HTTPRequest_OnError(Handle socket, const int errorType, const int errorNum, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        PrintToServer("httpreq::HTTPRequest %d socket error: type %d, number %d", arg, errorType, errorNum);
    }

    // callback
    Call_StartFunction(INVALID_HANDLE, GetFunctionByName(INVALID_HANDLE, g_sResponseCallback[arg]));
    Call_PushCell(false); // bool bSuccess
    Call_PushCell(-1); // int iStatusCode
    Call_PushCell(INVALID_HANDLE); // StringMap tHeaders
    Call_PushString(""); // const char[] sBody
    Call_PushCell(errorType); // int iErrorType
    Call_PushCell(errorNum); // int iErrorNum
    Call_PushCell(req.data); // any data
    Call_Finish();

    // cleanup
    HTTPRequest_OnDisconnect(socket, arg);
}

public int HTTPRequest_OnConnect(Handle socket, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        PrintToServer("httpreq::HTTPRequest %d socket connected", arg);
    }

    req.connected = true;
    if (req.ready) {
        req.SendRequest();
    }
}

public int HTTPRequest_OnReceive(Handle socket, const char[] receiveData, const int dataSize, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        PrintToServer("httpreq::HTTPRequest %d socket received %d bytes:\n%s", arg, dataSize, receiveData);
    }

    char[][] sResponseStatus = new char[2][dataSize];
    ExplodeString(receiveData, "\r\n", sResponseStatus, 2, dataSize, true); // strip status line from response body

    char sResponseStatusLine[2][128];
    ExplodeString(sResponseStatus[0], " ", sResponseStatusLine, sizeof(sResponseStatusLine), sizeof(sResponseStatusLine[])); // strip status code from rest of line
    int iStatusCode = StringToInt(sResponseStatusLine[1]);
    

    char[][] sResponseParts = new char[2][dataSize];
    ExplodeString(sResponseStatus[1], "\r\n\r\n", sResponseParts, 2, dataSize, true); // strip status line from response body

    // get header count
    int iHeaderAmount = 1;
    for (int i = 0; i < strlen(sResponseParts[0]); ++i) {
        if (sResponseParts[0][i] == '\r' && sResponseParts[0][i + 1] == '\n') {
            iHeaderAmount++;
            ++i;
        }
    }

    StringMap tResponseHeaders = CreateTrie();
    char[][] sHeaderLines = new char[iHeaderAmount][dataSize];
    char[][] sHeaderLine = new char[2][dataSize];
    ExplodeString(sResponseParts[0], "\r\n", sHeaderLines, iHeaderAmount, dataSize, false); // split headers by line
    for (int i = 0; i < iHeaderAmount; ++i) {
        ExplodeString(sHeaderLines[i], ":", sHeaderLine, 2, dataSize, true); // split on first colon
        TrimString(sHeaderLine[0]);
        TrimString(sHeaderLine[1]);
        tResponseHeaders.SetString(sHeaderLine[0], sHeaderLine[1]);
    }

    // callback
    Call_StartFunction(INVALID_HANDLE, GetFunctionByName(INVALID_HANDLE, g_sResponseCallback[arg]));
    Call_PushCell(true); // bool bSuccess
    Call_PushCell(iStatusCode); // int iStatusCode
    Call_PushCell(tResponseHeaders); // StringMap tHeaders
    Call_PushString(sResponseParts[1]); // const char[] sBody
    Call_PushCell(0); // int iErrorType
    Call_PushCell(0); // int iErrorNum
    Call_PushCell(req.data); // any data

    return Call_Finish();
}

public int HTTPRequest_OnDisconnect(Handle socket, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        PrintToServer("httpreq::HTTPRequest %d socket disconnected", arg);
    }

    CloseHandle(socket);
    g_hSockets[arg] = null;
    req.headers.Clear();
    CloseHandle(req.headers);
    req.params.Clear();
    CloseHandle(req.params);
    strcopy(g_sMethod[arg], sizeof(g_sMethod[]), "");
    strcopy(g_sURL[arg], sizeof(g_sURL[]), "");
    strcopy(g_sResponseCallback[arg], sizeof(g_sResponseCallback[]), "");
    req.connecting = false;
    req.connected = false;
    req.ready = false;
    req.debug = false;
}
