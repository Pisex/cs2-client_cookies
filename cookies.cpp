#include <stdio.h>
#include <string>
#include <cstring>
#include "cookies.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

cookies g_cookies;
PLUGIN_EXPOSE(cookies, g_cookies);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IMySQLClient *g_pMysqlClient;
ISQLiteClient *g_pSqliteClient;
IMySQLConnection* g_pConnectionMysql;
ISQLConnection* g_pConnectionSQLite;
bool g_bSQLite = false;

IPlayersApi* g_pPlayers;
IUtilsApi* g_pUtils;

CookiesApi* g_pCookiesApi = nullptr;
ICookiesApi* g_pCookiesCore = nullptr;

std::map<std::string, std::string> g_mapCookies[64];

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool cookies::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	g_pCookiesApi = new CookiesApi();
	g_pCookiesCore = g_pCookiesApi;

	return true;
}

void* cookies::OnMetamodQuery(const char* iface, int* ret)
{
	if (!strcmp(iface, COOKIES_INTERFACE))
	{
		*ret = META_IFACE_OK;
		return g_pCookiesCore;
	}

	*ret = META_IFACE_FAILED;
	return nullptr;
}

bool cookies::Unload(char *error, size_t maxlen)
{
	ConVar_Unregister();
	
	return true;
}

void LoadConfig() {
	KeyValues* pKVConfig = new KeyValues("Databases");
	if (!pKVConfig->LoadFromFile(g_pFullFileSystem, "addons/configs/databases.cfg")) {
		g_pUtils->ErrorLog("[%s] Failed to load databases config 'addons/config/databases.cfg'", g_PLAPI->GetLogTag());
		return;
	}
	g_bSQLite = false;
	const char* szDatabase = "cookies";
	pKVConfig = pKVConfig->FindKey("cookies", false);
	char szPath[256];
	if (pKVConfig) {
		const char* szDriver = pKVConfig->GetString("driver", "mysql");
		szDatabase = pKVConfig->GetString("database", "cookies");
		if(!strcmp(szDriver, "mysql")) {
			MySQLConnectionInfo info;
			info.host = pKVConfig->GetString("host", nullptr);
			info.user = pKVConfig->GetString("user", nullptr);
			info.pass = pKVConfig->GetString("pass", nullptr);
			info.database = pKVConfig->GetString("database", nullptr);
			info.port = pKVConfig->GetInt("port");
			g_pConnectionMysql = g_pMysqlClient->CreateMySQLConnection(info);
			g_pConnectionMysql->Connect([](bool connect) {
				if (!connect) {
					META_CONPRINT("Failed to connect the mysql database\n");
				} else {
					g_pConnectionMysql->Query("CREATE TABLE IF NOT EXISTS `client_cookies` ( \
						`id` INT PRIMARY KEY AUTO_INCREMENT, \
						`steamid` BIGINT NOT NULL, \
						`cookie_name` VARCHAR(255) NOT NULL, \
						`cookie_data` TEXT NOT NULL, \
						UNIQUE KEY `steamid_cookie_name` (`steamid`, `cookie_name`)\
					);", [](ISQLQuery* test){});
				}
			});
			return;
		}
	}
	g_bSQLite = true;
	g_SMAPI->Format(szPath, sizeof(szPath), "addons/data/%s.sqlite3", szDatabase);
	SQLiteConnectionInfo info;
	info.database = szPath;
	g_pConnectionSQLite = g_pSqliteClient->CreateSQLiteConnection(info);
	g_pConnectionSQLite->Connect([](bool connect) {
		if (!connect) {
			META_CONPRINT("Failed to connect the sqlite database\n");
		} else {
			g_pConnectionSQLite->Query("CREATE TABLE IF NOT EXISTS `client_cookies` ( \
				`id` INTEGER PRIMARY KEY AUTOINCREMENT, \
				`steamid` INTEGER NOT NULL, \
				`cookie_name` TEXT NOT NULL, \
				`cookie_data` TEXT NOT NULL, \
				UNIQUE(`steamid`, `cookie_name`)\
			);", [](ISQLQuery* test){});
		}
	});
}

void ProcessQueryResult(int iSlot, ISQLQuery* pQuery)
{
    ISQLResult* pResult = pQuery->GetResultSet();
    if (pResult->GetRowCount() > 0)
    {
        while (pResult->MoreRows())
        {
            if(pResult->FetchRow())
            {
                std::string sCookieName = pResult->GetString(2);
                std::string sCookieData = pResult->GetString(3);
                g_mapCookies[iSlot][sCookieName] = sCookieData;
            }
        }
    }
	g_pCookiesApi->CallClientCookieLoaded(iSlot);
}

void GetClientCookies(int iSlot, uint64 iSteamID64)
{
    char szBuffer[256];
    g_SMAPI->Format(szBuffer, sizeof(szBuffer), "SELECT * FROM client_cookies WHERE steamid = %lld", iSteamID64);
    if(g_bSQLite) {
        g_pConnectionSQLite->Query(szBuffer, [iSlot](ISQLQuery* pQuery) {
            ProcessQueryResult(iSlot, pQuery);
        });
    } else {
        g_pConnectionMysql->Query(szBuffer, [iSlot](ISQLQuery* pQuery) {
            ProcessQueryResult(iSlot, pQuery);
        });
    }
}

void OnClientAuthorized(int iSlot, uint64 iSteamID64)
{
	g_mapCookies[iSlot].clear();
	GetClientCookies(iSlot, iSteamID64);
}

void cookies::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_pUtils->ErrorLog("[%s] Missing Players system plugin", g_PLAPI->GetLogTag());
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	ISQLInterface* g_SqlInterface = (ISQLInterface *)g_SMAPI->MetaFactory(SQLMM_INTERFACE, &ret, nullptr);
	if (ret == META_IFACE_FAILED) {
		g_pUtils->ErrorLog("[%s] Missing SQL plugin", g_PLAPI->GetLogTag());
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pPlayers->HookOnClientAuthorized(g_PLID, OnClientAuthorized);
	g_pMysqlClient = g_SqlInterface->GetMySQLClient();
	g_pSqliteClient = g_SqlInterface->GetSQLiteClient();
	LoadConfig();
	g_pUtils->StartupServer(g_PLID, StartupServer);
}

void CookiesApi::SetCookie(int iSlot, const char* sCookieName, const char* sData)
{
	if(!sCookieName) sCookieName = "";
	if(!sData) sData = "";
	ISQLConnection* pConn = g_bSQLite ? g_pConnectionSQLite : (ISQLConnection*)g_pConnectionMysql;
	if(pConn) {
		std::string sName = pConn->Escape(sCookieName);
		std::string sValue = pConn->Escape(sData);
		sName.resize(strlen(sName.c_str()));
		sValue.resize(strlen(sValue.c_str()));
		std::string sQuery = "INSERT ";
		if(g_bSQLite)
			sQuery = "INSERT OR REPLACE INTO client_cookies (steamid, cookie_name, cookie_data) VALUES (" + std::to_string(engine->GetClientXUID(iSlot)) + ", '" + sName + "', '" + sValue + "')";
		else
			sQuery = "INSERT INTO client_cookies (steamid, cookie_name, cookie_data) VALUES (" + std::to_string(engine->GetClientXUID(iSlot)) + ", '" + sName + "', '" + sValue + "') ON DUPLICATE KEY UPDATE cookie_data = '" + sValue + "'";
		pConn->Query((char*)sQuery.c_str(), [](ISQLQuery* pQuery){});
	}
	g_mapCookies[iSlot][sCookieName] = sData;
}

const char* CookiesApi::GetCookie(int iSlot, const char* sCookieName)
{
	auto it = g_mapCookies[iSlot].find(sCookieName);
	if(it != g_mapCookies[iSlot].end())
	{
		return it->second.c_str();
	}
	return "";
}

///////////////////////////////////////
const char* cookies::GetLicense()
{
	return "GPL";
}

const char* cookies::GetVersion()
{
	return "1.1";
}

const char* cookies::GetDate()
{
	return __DATE__;
}

const char *cookies::GetLogTag()
{
	return "cookies";
}

const char* cookies::GetAuthor()
{
	return "Pisex";
}

const char* cookies::GetDescription()
{
	return "cookies";
}

const char* cookies::GetName()
{
	return "Client Cookies";
}

const char* cookies::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}
