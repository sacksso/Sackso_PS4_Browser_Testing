#pragma once
#include <cstdint>
#include <cstddef>
#define ORBIS_HTTP_VERSION_1_1 1
#define ORBIS_HTTP_CONTENTLEN_EXIST 0
enum { ORBIS_METHOD_GET = 0 };
extern "C" {
int sceHttpInit(int, int, int); int sceHttpTerm(int);
int sceHttpCreateTemplate(int, const char*, int, int);
int sceHttpSetResolveTimeOut(int, uint32_t); int sceHttpSetConnectTimeOut(int, uint32_t);
void sceHttpSetResponseHeaderMaxSize();
int sceHttpsSetSslCallback(int, int (*)(int, unsigned, void* const*, int, void*), void*);
int sceHttpCreateConnectionWithURL(int, const char*, int);
int32_t sceHttpCreateRequestWithURL(int32_t, int32_t, const char*, uint64_t);
int sceHttpAddRequestHeader(int, const char*, const char*, uint32_t);
int32_t sceHttpSendRequest(int32_t, const void*, size_t);
int sceHttpGetStatusCode(int, int32_t*); int sceHttpGetAllResponseHeaders(int, char**, size_t*);
int sceHttpGetResponseContentLength(int, int32_t*, size_t*);
int sceHttpReadData(int, void*, uint32_t);
int sceHttpDeleteRequest(int); int sceHttpDeleteConnection(int); int sceHttpDeleteTemplate(int);
}
