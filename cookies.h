#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <sh_vector.h>
#include "utlvector.h"
#include "ehandle.h"
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "igameevents.h"
#include "vector.h"
#include <deque>
#include <functional>
#include <utlstring.h>
#include <keyvalues.h>
#include "CCSPlayerController.h"
#include "include/menus.h"
#include "include/mysql_mm.h"
#include "include/sqlite_mm.h"
#include "include/cookies.h"

class cookies final : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	void AllPluginsLoaded();
	void* OnMetamodQuery(const char* iface, int* ret);
private:
	const char* GetAuthor();
	const char* GetName();
	const char* GetDescription();
	const char* GetURL();
	const char* GetLicense();
	const char* GetVersion();
	const char* GetDate();
	const char* GetLogTag();
};

class CookiesApi : public ICookiesApi {
private:
	std::map<int, ClientCookieLoadedCallback> m_ClientCookieLoadedCallbacks;
public:
	void SetCookie(int iSlot, const char* sCookieName, const char* sData);
    const char* GetCookie(int iSlot, const char* sCookieName);
    void HookClientCookieLoaded(SourceMM::PluginId id, ClientCookieLoadedCallback callback) {
		m_ClientCookieLoadedCallbacks[id] = callback;
	}

	void CallClientCookieLoaded(int iSlot) {
		for (auto& callback : m_ClientCookieLoadedCallbacks) {
			if (callback.second) {
				callback.second(iSlot);
			}
		}
	}
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
