#ifndef EVENTS_H
#define EVENTS_H

//#include "base_action.h"

#include <string>
#include <vector>
#include <memory>

class Weapon;
class CWorm;
class CGameObject;
class BaseAction;

struct GameEvent
{
public:
	typedef std::vector< std::shared_ptr<BaseAction> > Actions;

	GameEvent();
	GameEvent(Actions&);
	virtual ~GameEvent();

	bool addAction( const std::string& name, const std::vector<std::string>& params );
	//void swapActionList(std::vector<BaseAction*>& b); //TODO
	void run( CGameObject *object, CGameObject *object2 = NULL, CWorm *worm = NULL, Weapon *weapon = NULL );
	
	//private:
	
	Actions actions;
};

#endif  // _PART_EVENTS_H_
