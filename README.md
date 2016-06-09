# sm-httpreq
Provides the ability to make simple HTTP requests via the socket extension in SourcePawn.

Currently only supports GET and POST requests (officially, anyway), and probably doesn't follow the HTTP/1.1 specifications as closely as it should, but it seems to work with the limited testing I've done so far.

The code is not nice to look at, so I would recommend you didn't. Furthermore, due to my limited understanding of SourcePawn, by default you may only have 15 requests active at any one time. A request is considered inactive once the socket disconnects. This limit can be *changed* by modifying `httpreq.inc`'s defined value for the `MAX_ACTIVE_REQUESTS` constant. However, it cannot be removed.

Thanks to f0oster for helping with some concepts.

## Requirements
* SourceMod 1.7+
* [Socket 3.0.1](https://forums.alliedmods.net/showthread.php?t=67640)

## Example Usage
```c++
public void OnPluginStart()
{
    HTTPRequest req = HTTPRequest("POST", "http://example.com/api/", "OnRequestComplete");
    req.debug = true;
    req.headers.SetString("User-Agent", "HTTPRequests for SourceMod");
    req.params.SetString("test", "1");
    req.SendRequest();
}

public void OnRequestComplete(bool bSuccess, int iStatusCode, StringMap tHeaders, const char[] sBody, int iErrorType, int iErrorNum, any data)
{
    if (bSuccess) {
        PrintToServer("finished request with status code %d", iStatusCode);

        PrintToServer("headers:");

        char sKey[128], sValue[512];
        StringMapSnapshot tHeadersSnapshot = tHeaders.Snapshot();
        for (int i = 0; i < tHeadersSnapshot.Length; ++i) {
            tHeadersSnapshot.GetKey(i, sKey, sizeof(sKey));
            tHeaders.GetString(sKey, sValue, sizeof(sValue));
            PrintToServer("%s => %s", sKey, sValue);
        }

        PrintToServer("response: %s", sBody);
    } else {
        PrintToServer("failed request with error type %d, error num %d", iErrorType, iErrorNum);
    }
}
```