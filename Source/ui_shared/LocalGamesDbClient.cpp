#include <cassert>
#include "LocalGamesDbClient.h"
#include "sqlite/SqliteStatement.h"
#include "PathUtils.h"

using namespace LocalGamesDb;

static const char* g_dbFileName = "games.db";

CClient::CClient()
{
	auto dbPath = Framework::PathUtils::GetAppResourcesPath() / g_dbFileName;
	try
	{
		m_db = Framework::CSqliteDb(Framework::PathUtils::GetNativeStringFromPath(dbPath).c_str(),
			SQLITE_OPEN_READONLY);
	}
	catch(...)
	{
		//Failed to open database, act as if it was not available
	}
}

Game CClient::GetGame(const char* serial)
{
	if(m_db.IsEmpty())
	{
		throw std::runtime_error("Local Games database unavailable.");
	}

	Framework::CSqliteStatement statement(m_db, "SELECT GameID, GameTitle FROM games WHERE serial = ?");
	statement.BindText(1, serial, true);
	statement.StepWithResult();

	Game game;
	game.theGamesDbId = sqlite3_column_int(statement, 0);
	game.title        = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
	return game;
}