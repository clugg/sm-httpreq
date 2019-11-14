#if defined _httpreq_included
    #endinput
#endif
#define _httpreq_included

#include <socket>

#define MAX_BODY_SIZE 8192

methodmap FancyStringMap < StringMap
{
    public FancyStringMap() {
        return view_as<FancyStringMap>(CreateTrie());
    }

    public bool SetString(const char[] key, const char[] value, bool replace = true)
    {
        int metaKeyLength = strlen(key) + 8;
        char[] metaKey = new char[metaKeyLength];
        Format(metaKey, metaKeyLength, "%s:buffer", key);

        StringMap self = view_as<StringMap>(this);

        return self.SetString(key, value, replace)
            && self.SetValue(metaKey, strlen(value) + 1, replace);
    }

    public bool Remove(const char[] key)
    {
        int metaKeyLength = strlen(key) + 8;
        char[] metaKey = new char[metaKeyLength];
        Format(metaKey, metaKeyLength, "%s:buffer", key);

        StringMap self = view_as<StringMap>(this);
        self.Remove(metaKey);

        return self.Remove(key);
    }

    public bool IsBufferKey(const char[] key)
    {
        int length = strlen(key);
        if (length < 7) {
            return false;
        }

        return StrEqual(key[length - 7], ":buffer", false);
    }

    public int StringBufferSize(const char[] key)
    {
        int metaKeyLength = strlen(key) + 8;
        char[] metaKey = new char[metaKeyLength];
        Format(metaKey, metaKeyLength, "%s:buffer", key);

        int value;
        return this.GetValue(metaKey, value) ? value : -1;
    }
}

typedef HTTPRequestFinished = function void(HTTPRequest req, bool success, int statusCode, FancyStringMap headers, const char[] body, int errorType, int errorNum, any data);

methodmap HTTPRequest < FancyStringMap
{
    /** @section Properties **/

    property Handle socket
    {
        public get()
        {
            Handle value;
            return this.GetValue("socket", value) ? value : null;
        }
    }

    property FancyStringMap headers
    {
        public get()
        {
            FancyStringMap value;
            return this.GetValue("headers", value) ? value : null;
        }
    }

    property FancyStringMap params
    {
        public get()
        {
            FancyStringMap value;
            return this.GetValue("params", value) ? value : null;
        }
    }

    property bool connecting
    {
        public get()
        {
            bool value;
            return this.GetValue("connecting", value) ? value : false;
        }
    }

    property bool connected
    {
        public get()
        {
            bool value;
            return this.GetValue("connected", value) ? value : false;
        }
    }

    property DataPack callbacks
    {
        public get() {
            DataPack value;
            return this.GetValue("callbacks", value) ? value : null;
        }
    }

    property bool ready
    {
        public get()
        {
            bool value;
            return this.GetValue("ready", value) ? value : false;
        }
    }

    property bool debug
    {
        public get()
        {
            bool value;
            return this.GetValue("debug", value) ? value : false;
        }

        public set(bool value)
        {
            this.SetValue("debug", value);
        }
    }

    property any data
    {
        public get()
        {
            any value;
            return this.GetValue("data", value) ? value : -1;
        }

        public set(any value)
        {
            this.SetValue("data", value);
        }
    }

    /** @section Methods **/

    public void Connect()
    {
        this.SetValue("connecting", true);

        int urlLength = this.StringBufferSize("url");
        char[] url = new char[urlLength];
        this.GetString("url", url, urlLength);

        // determine full host from URL
        char[][] fullHost = new char[1][urlLength];
        ExplodeString(url, "/", fullHost, 1, urlLength, false);

        // split host from port
        char[][] hostParts = new char[2][urlLength];
        if (ExplodeString(fullHost[0], ":", hostParts, 2, urlLength, false) != 2) {
            strcopy(hostParts[1], urlLength, "80");
        }

        int hostLength = strlen(hostParts[0]) + 1;
        char[] host = new char[hostLength];
        strcopy(host, hostLength, hostParts[0]);

        int port = StringToInt(hostParts[1]);
        if (port <= 0 || port > 65535) {
            LogError("httpreq::HTTPRequest attempted with invalid port %d", port);
        }

        if (this.debug) {
            LogMessage("httpreq::HTTPRequest socket connecting to %s:%d", host, port);
        }

        SocketConnect(this.socket, HTTPRequest_OnConnect, HTTPRequest_OnReceive, HTTPRequest_OnDisconnect, host, port);
    }

    public bool SendRequest()
    {
        // if we're not connected/ready to send a request yet
        if (! this.connected) {
            // note that we are ready to SendRequest as soon as connected
            this.SetValue("ready", true);

            // if we're not already connecting, connect
            if (! this.connecting) {
                this.Connect();
            }

            return false;
        }

        // get HTTP method and determine whether we're using GET
        int methodLength = this.StringBufferSize("method");
        char[] method = new char[methodLength];
        this.GetString("method", method, methodLength);
        bool bIsGET = StrEqual(method, "GET", false);

        int urlLength = this.StringBufferSize("url");
        char[] url = new char[urlLength];
        this.GetString("url", url, urlLength);

        // split URL between host and path
        char[][] urlParts = new char[2][urlLength];
        if (ExplodeString(url, "/", urlParts, 2, urlLength, false)) {
            strcopy(urlParts[1], urlLength, "");
        }

        int hostLength = strlen(urlParts[0]) + 1;
        char[] host = new char[hostLength];
        strcopy(host, hostLength, urlParts[0]);

        int pathLength = strlen(urlParts[0]) + 2;
        char[] path = new char[pathLength];
        Format(path, pathLength, "/%s", urlParts[1]);

        // calculate query string length
        StringMapSnapshot snap = this.params.Snapshot();
        int queryLength = 1;
        int paramCount = 0;
        for (int i = 0, keyLength = 0, valueLength = 0; i < snap.Length; ++i) {
            // get key
            keyLength = snap.KeyBufferSize(i);
            char[] key = new char[keyLength];
            snap.GetKey(i, key, keyLength);

            if (this.params.IsBufferKey(key)) {
                continue;
            }

            // get value length
            valueLength = this.params.StringBufferSize(key);

            // +1 for & for all except first param
            if (paramCount != 0) {
                queryLength += 1;
            }

            // calculate length for key=value, -1 to remove NULL padding
            queryLength += keyLength + valueLength - 1;

            ++paramCount;
        }

        // build query string
        char[] query = new char[queryLength];
        for (int i = 0, count = 0, keyLength = 0, valueLength = 0; i < snap.Length; ++i) {
            // get key
            keyLength = snap.KeyBufferSize(i);
            char[] key = new char[keyLength];
            snap.GetKey(i, key, keyLength);

            if (this.params.IsBufferKey(key)) {
                continue;
            }

            // get value
            valueLength = this.params.StringBufferSize(key);
            char[] value = new char[valueLength];
            this.params.GetString(key, value, valueLength);

            // prepend & for all except first param
            if (count != 0) {
                StrCat(query, queryLength, "&");
            }

            // format param as key=value
            StrCat(query, queryLength, key);
            StrCat(query, queryLength, "=");
            StrCat(query, queryLength, value);

            ++count;
        }

        // clean up snapshot
        delete snap;

        // calculate full path length (including query string for GET)
        bool pathHasQueryString = bIsGET && paramCount > 0;
        int fullPathLength = strlen(path) + 1;
        if (pathHasQueryString) {
            // +1 for ? or &
            fullPathLength += queryLength + 1;
        }

        // build full path
        char[] fullPath = new char[fullPathLength];
        strcopy(fullPath, fullPathLength, path);
        if (pathHasQueryString) {
            StrCat(fullPath, fullPathLength, StrContains(fullPath, "?") == -1 ? "?" : "&");
            StrCat(fullPath, fullPathLength, query);
        }

        // add basic headers
        this.headers.SetString("Host", host);
        this.headers.SetString("Connection", "close");

        // add content headers if not using GET
        if (! bIsGET) {
            char contentLength[8];
            IntToString(queryLength, contentLength, sizeof(contentLength));

            this.headers.SetString("Content-Type", "application/x-www-form-urlencoded");
            this.headers.SetString("Content-Length", contentLength);
        }

        // calculate header length
        snap = this.headers.Snapshot();
        int headersLength = 1;
        for (int i = 0, keyLength = 0, valueLength = 0; i < snap.Length; ++i) {
            // get key
            keyLength = snap.KeyBufferSize(i);
            char[] key = new char[keyLength];
            snap.GetKey(i, key, keyLength);

            if (this.headers.IsBufferKey(key)) {
                continue;
            }

            // get value length
            valueLength = this.headers.StringBufferSize(key);

            // calculate length for key: value\r\n
            headersLength += keyLength + valueLength + 2;
        }

        // build headers
        char[] headers = new char[headersLength];
        for (int i = 0, keyLength = 0, valueLength = 0; i < snap.Length; ++i) {
            // get key
            keyLength = snap.KeyBufferSize(i);
            char[] key = new char[keyLength];
            snap.GetKey(i, key, keyLength);

            if (this.headers.IsBufferKey(key)) {
                continue;
            }

            // get value
            valueLength = this.headers.StringBufferSize(key);
            char[] value = new char[valueLength];
            this.headers.GetString(key, value, valueLength);

            // format header as Name: Value
            StrCat(headers, headersLength, key);
            StrCat(headers, headersLength, ": ");
            StrCat(headers, headersLength, value);
            StrCat(headers, headersLength, "\r\n");
        }

        // clean up snapshot
        delete snap;

        // calculate request length for {method} {fullPath} HTTP/1.1\r\n{headers}\r\n
        int requestLength = methodLength + 1 + fullPathLength + 11 + headersLength + 2;
        if (! bIsGET) {
            requestLength += queryLength + 2;
        }

        // build request
        char[] request = new char[requestLength];
        Format(request, requestLength, "%s %s HTTP/1.1\r\n", method, fullPath);
        StrCat(request, requestLength, headers);
        StrCat(request, requestLength, "\r\n");
        // add query to body if not using GET
        if (! bIsGET) {
            StrCat(request, requestLength, query);
            StrCat(request, requestLength, "\r\n");
        }

        if (this.debug) {
            LogMessage("httpreq::HTTPRequest socket making request:\n%s", request);
        }

        SocketSend(this.socket, request);

        return true;
    }

    /** @section Constructor/Destructor **/

    public HTTPRequest(const char[] method, const char[] url, HTTPRequestFinished callback, any data = 0)
    {
        HTTPRequest self = view_as<HTTPRequest>(new FancyStringMap());

        Handle socket = SocketCreate(SOCKET_TCP, HTTPRequest_OnError);
        SocketSetArg(socket, self);
        self.SetValue("socket", socket);

        self.SetValue("headers", new FancyStringMap());
        self.SetValue("params", new FancyStringMap());
        self.SetString("method", method);
        self.SetValue("connecting", false);
        self.SetValue("connected", false);
        self.SetValue("ready", false);

        self.debug = false;
        self.data = data;

        // prepare pack to store callback
        DataPack callbacks = new DataPack();
        callbacks.WriteFunction(callback);
        self.SetValue("callbacks", callbacks);

        // split protocol from URL
        int urlLength = strlen(url) + 1;
        char[][] urlParts = new char[2][urlLength];
        if (ExplodeString(url, "://", urlParts, 2, urlLength, true) == 2) {
            if (! StrEqual(urlParts[0], "http")) {
                LogError("httpreq::HTTPRequest attempted with invalid protocol '%s'", urlParts[0]);
            }

            self.SetString("url", urlParts[1]);
        } else {
            self.SetString("url", urlParts[0]);
        }

        return self;
    }

    public void Cleanup()
    {
        if (this.socket != null) {
            delete this.socket;
            this.Remove("socket");
        }

        if (this.headers != null) {
            delete this.headers;
            this.Remove("headers");
        }

        if (this.params != null) {
            delete this.params;
            this.Remove("params");
        }

        if (this.callbacks != null) {
            delete this.callbacks;
            this.Remove("callbacks");
        }
    }
}

stock int HTTPRequest_OnError(Handle socket, const int errorType, const int errorNum, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        LogMessage("httpreq::HTTPRequest %d socket error: type %d, number %d", arg, errorType, errorNum);
    }

    req.callbacks.Reset();
    Call_StartFunction(null, req.callbacks.ReadFunction());
    Call_PushCell(req);
    Call_PushCell(false);
    Call_PushCell(-1);
    Call_PushCell(new FancyStringMap());
    Call_PushString("");
    Call_PushCell(errorType);
    Call_PushCell(errorNum);
    Call_PushCell(req.data);
    Call_Finish();
}

stock int HTTPRequest_OnConnect(Handle socket, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        LogMessage("httpreq::HTTPRequest socket connected");
    }

    req.SetValue("connected", true);
    if (req.ready) {
        req.SendRequest();
    }
}

stock int HTTPRequest_OnReceive(Handle socket, const char[] receiveData, const int dataSize, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        LogMessage("httpreq::HTTPRequest socket received %d bytes:\n%s", dataSize, receiveData);
    }

    // strip status line from response body
    char[][] responseStatus = new char[2][dataSize];
    ExplodeString(receiveData, "\r\n", responseStatus, 2, dataSize, true);

    // strip status code from rest of line
    char responseStatusLine[2][128];
    ExplodeString(responseStatus[0], " ", responseStatusLine, sizeof(responseStatusLine), sizeof(responseStatusLine[]));
    int statusCode = StringToInt(responseStatusLine[1]);

    // split headers from body
    char[][] responseParts = new char[2][dataSize];
    ExplodeString(responseStatus[1], "\r\n\r\n", responseParts, 2, dataSize, true);

    // get header count
    int headerCount = 1;
    for (int i = 0; i < strlen(responseParts[0]); ++i) {
        if (responseParts[0][i] == '\r' && responseParts[0][i + 1] == '\n') {
            headerCount++;
            ++i;
        }
    }

    // get headers
    FancyStringMap headers = new FancyStringMap();
    char[][] headerLines = new char[headerCount][dataSize];
    char[][] headerLine = new char[2][dataSize];
    ExplodeString(responseParts[0], "\r\n", headerLines, headerCount, dataSize, false);

    for (int i = 0; i < headerCount; ++i) {
        ExplodeString(headerLines[i], ": ", headerLine, 2, dataSize, true);
        TrimString(headerLine[0]);
        TrimString(headerLine[1]);
        headers.SetString(headerLine[0], headerLine[1]);
    }

    req.callbacks.Reset();
    Call_StartFunction(null, req.callbacks.ReadFunction());
    Call_PushCell(req);
    Call_PushCell(true);
    Call_PushCell(statusCode);
    Call_PushCell(headers);
    Call_PushString(responseParts[1]);
    Call_PushCell(0);
    Call_PushCell(0);
    Call_PushCell(req.data);

    return Call_Finish();
}

stock int HTTPRequest_OnDisconnect(Handle socket, any arg)
{
    HTTPRequest req = view_as<HTTPRequest>(arg);
    if (req.debug) {
        LogMessage("httpreq::HTTPRequest %d socket disconnected", arg);
    }
}
