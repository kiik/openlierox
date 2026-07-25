#ifndef DEDICATED_ONLY
#include "player_input.h"

#include "gusgame.h"
#include "CWormHuman.h"
#include "gconsole.h"
#include "LuaCallbacks.h"
#include "luaapi/context.h"
#include "util/text.h"
#include "util/log.h"
#include "util/stringbuild.h"
#include "game/Game.h"
#include "CClientNetEngine.h"

#include <string>
#include <list>
#include <vector>
#include <assert.h>

static_assert( C_LocalPlayer_ActionCount == CWormHumanInputHandler::ACTION_COUNT, "actioncount_mismatch" );

std::string _event(size_t index, CWormHumanInputHandler::Actions action, bool start) {
	assert((int)action >= 0 && (int)action < CWormHumanInputHandler::ACTION_COUNT);

	if ( index < game.localPlayers.size() )
	{
		CWormHumanInputHandler& player = *game.localPlayers[index];

		bool ignore = false;

		if(!player.worm()->bWeaponsReady)
			ignore = true;

		LuaCallbackProxy::PostHandler f =
				[&ignore](LuaContext& ctx, int n){ ignore |= (n > 0 && ctx.tobool(-1)); };

		LUACALLBACK(localplayerEvent+action).call(1, f)(player.getLuaReference())(start)();
		LUACALLBACK(localplayerEventAny).call(1, f)(player.getLuaReference())((int)action)(start)();

		if(!ignore) {
			if(start)
				player.actionStart(action);
			else
				player.actionStop(action);
		}
	}
	return "";
}

std::string eventStart(size_t index, CWormHumanInputHandler::Actions action, std::list<std::string> const&)
{
	return _event(index, action, true);
}

std::string eventStop(size_t index, CWormHumanInputHandler::Actions action, std::list<std::string> const&)
{
	return _event(index, action, false);
}

void registerPlayerInput()
{
	for ( size_t i = 0; i < GusGame::MAX_LOCAL_PLAYERS; ++i)
	{
		static char const* actionNames[] =
		{
			"_LEFT", "_RIGHT", "_UP", "_DOWN", "_FIRE", "_JUMP", "_CHANGE", "_NINJAROPE"
		};
		static_assert( sizeof(actionNames)/sizeof(char*) == CWormHumanInputHandler::ACTION_COUNT, "actioncount_mismatch" );
		
		for(int action = CWormHumanInputHandler::LEFT; action < CWormHumanInputHandler::ACTION_COUNT; ++action)
		{
			console.registerCommands()
				((S_("+P") << i << actionNames[action]), [i, action](std::list<std::string> const& args){ return eventStart(i, (CWormHumanInputHandler::Actions)action, args); })
				((S_("-P") << i << actionNames[action]), [i, action](std::list<std::string> const& args){ return eventStop(i, (CWormHumanInputHandler::Actions)action, args); })
			;
		}
	}
	console.registerCommands()
		("SAY", say);
}

std::string say( const std::list<std::string> &args )
{
	
	if ( !args.empty() )
	{
		if ( !game.localPlayers.empty() )
		{
			CWorm* w = game.localPlayers[0]->worm();
			if(w)
				cClient->getNetEngine()->SendText(*(args.begin()), w->getName());
		}
	}
	else
	{
		return "SAY <MESSAGE> : SENDS A MESSAGE TO THE OTHER PLAYERS ON THE SERVER";
	}
	return "";
}

#endif
