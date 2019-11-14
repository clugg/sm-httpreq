# sm-httpreq

**I would not recommend this for use in production. It does not support HTTPS and is therefore not secure. I consider this more of a learning/informative project. If you are a looking for a more fully featured HTTP requests library, I would recommend [SteamWorks](https://forums.alliedmods.net/showthread.php?t=229556).**

Provides the ability to make simple HTTP requests via the Socket extension in SourcePawn.

Crafts and sends basic HTTP requests, with support for setting headers and parameters. Parameters are first crafted into a query string (no URL escaping is performed), then either appended to the URI when using GET, or placed in the request body (with `Content-Type: application/x-www-form-urlencoded` and `Content-Length` headers automatically added) otherwise. This library does not fully support HTTP/1.1 specifications.

Thanks to f0oster for helping with some concepts.

## Requirements
* SourceMod 1.7 or later
* [Socket 3.0.1](https://forums.alliedmods.net/showthread.php?t=67640) - a transitional syntax version of the include can be found [here](https://github.com/nefarius/sm-ext-socket/blob/master/socket.inc)

## Example Usage
```c++
#include <httpreq>

public void OnPluginStart()
{
    HTTPRequest req = new HTTPRequest("GET", "http://example.com/api", OnRequestComplete);
    req.debug = true;
    req.headers.SetString("User-Agent", "sm-httpreq by clugg");
    req.params.SetString("test", "param");
    req.SendRequest();
}

void OnRequestComplete(HTTPRequest req, bool success, int statusCode, FancyStringMap headers, const char[] body, int errorType, int errorNum, any data)
{
    if (success) {
        PrintToServer("finished request with status code %d", statusCode);
        PrintToServer("headers:");

        StringMapSnapshot snap = headers.Snapshot();
        for (int i = 0, keyLength = 0, valueLength = 0; i < snap.Length; ++i) {
            // get header name
            keyLength = snap.KeyBufferSize(i);
            char[] key = new char[keyLength];
            snap.GetKey(i, key, keyLength);

            if (headers.IsBufferKey(key)) {
                continue;
            }

            // get header value
            valueLength = headers.StringBufferSize(key);
            char[] value = new char[valueLength];
            headers.GetString(key, value, valueLength);

            // output header
            PrintToServer("%s: %s", key, value);
        }
        delete snap;

        PrintToServer("response: %s", body);
    } else {
        PrintToServer("request failed with error type %d, error num %d", errorType, errorNum);
    }

    req.Cleanup();
    delete req;
    delete headers;
}
```
